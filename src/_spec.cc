#include "_spec.hh"

// [stdc++]
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// [ryml]
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
// [tng/internal]
#include "_logger.hh"
#include "_parser.hh"
#include "_preprocessor.hh"
#include "_sourcemap.hh"
#include "_tlv.hh"
#include "fmt_types.hh"

namespace TNG_NAMESPACE::spec {

    // =============================================================================
    // Hilfsfunktionen für ryml::ConstNodeRef (Ersatz für yaml-cpp's .as<T>())
    // =============================================================================
    // ryml::ConstNodeRef hat kein direktes Äquivalent zu yaml-cpp's bequemem
    // node["key"].as<T>(default) - diese Helfer bilden genau dieses Muster nach.
    // =============================================================================

    static std::string toStdString(ryml::csubstr s) {
        return std::string(s.str, s.len);
    }

    static bool hasKey(ryml::ConstNodeRef node, ryml::csubstr key) {
        return !node.invalid() && node.is_map() && node.has_child(key);
    }

    static std::string getStr(ryml::ConstNodeRef node, ryml::csubstr key,
        const std::string& def = "")
    {
        if (!hasKey(node, key)) return def;
        ryml::ConstNodeRef c = node[key];
        if (!c.has_val()) return def;
        return toStdString(c.val());
    }

    static int getInt(ryml::ConstNodeRef node, ryml::csubstr key, int def) {
        if (!hasKey(node, key)) return def;
        ryml::ConstNodeRef c = node[key];
        int v = def;
        if (c.has_val()) c4::atoi(c.val(), &v);
        return v;
    }

    static std::size_t getSizeT(ryml::ConstNodeRef node, ryml::csubstr key, std::size_t def) {
        if (!hasKey(node, key)) return def;
        ryml::ConstNodeRef c = node[key];
        std::size_t v = def;
        if (c.has_val()) c4::atou(c.val(), &v);
        return v;
    }

    static bool getBool(ryml::ConstNodeRef node, ryml::csubstr key, bool def) {
        if (!hasKey(node, key)) return def;
        ryml::ConstNodeRef c = node[key];
        if (!c.has_val()) return def;
        return c.val() == "true" || c.val() == "1" || c.val() == "yes";
    }

    // =============================================================================
    // Interne Typen
    // =============================================================================

    class SpecValidationError : public std::runtime_error {
    public:
        /// Ohne SourceMap: generischer Fallback (Knoten-ID sagt einem Menschen
        /// nichts, aber ohne SourceMap gibt es keine bessere Positionsangabe -
        /// siehe Kommentar in _preprocessor.hh zum Positions-Tracking-Modell).
        SpecValidationError(const std::string& msg, ryml::id_type nodeId)
            : std::runtime_error(format(msg, nodeId, nullptr))
        {
        }

        /// Mit SourceMap: schlägt Original-Position nach
        SpecValidationError(const std::string& msg, ryml::id_type nodeId,
            const SourceMap* smap)
            : std::runtime_error(format(msg, nodeId, smap))
        {
        }

    private:
        static std::string format(const std::string& msg,
            ryml::id_type nodeId, const SourceMap* smap)
        {
            const int key = static_cast<int>(nodeId);
            if (smap) {
                if (auto loc = smap->lookup(key))
                    return loc->to_string() + ": " + msg;
                if (auto loc = smap->lookup_nearest(key))
                    return loc->to_string() + ": " + msg;
            }
            return "(Position unbekannt): " + msg;
        }
    };

    enum class SpecFieldType { UNKNOWN, SCALAR, NESTED };

    struct TLVOptions {
        int tag_bytes = 2;
        int len_bytes = 2;
        bool tcc = false;
        bool ber = false;    // true = BER-TLV (ISO/IEC 8825-1): variable Tag-/
                             // Length-Länge, tag_bytes/len_bytes werden ignoriert
        std::string encoding; // leer = erbt von Elternfeld / globalem Encoding
    };

    struct SpecField {
        SpecFieldType            type = SpecFieldType::UNKNOWN;
        std::string              format;
        std::string              encoding;
        std::size_t              length = 0;
        std::string              description = "<dummy>";
        bool                     has_explicit_description = false; // s. parseSpecField
        // [ISO8583] 3.4 (PCI): 'sensitive: true' — dump()/Logs maskieren den
        // Wert mit "***". Bei NESTED-Containern vererbt sich der Satz auf
        // alle Kinder (s. buildFieldParser).
        bool                     sensitive = false;
        std::vector<SpecField>   children;             // Sequence-Kinder (non-TLV)
        std::map<int, SpecField> tlv_children;         // Map-Kinder (TLV, key = SE-Nummer/Tag)
        std::optional<TLVOptions> tlv;
    };

    // Parst einen TLV-'children'-Schlüssel als SE-Nummer (Mastercard/Visa-
    // Fix-Format-TLV, z.B. DE48-Subelemente: "26" = dezimal 26) oder als
    // EMV/BER-TLV-Tag (z.B. "9F26" = hex 0x9F26, "1A" = hex 0x1A = dez. 26).
    //
    // Standard: dezimal für Fix-Format-TLV, hexadezimal für BER-TLV
    // (`tlv: {ber: true}`) - das entspricht jeweils der in der Praxis
    // etablierten Schreibweise (Mastercard-Handbücher nennen SE-Nummern
    // dezimal, EMV Book 3 / ISO 7816 nennen Tags hexadezimal). Ein
    // explizites '0x'-Präfix (z.B. "0x1A") erzwingt hexadezimal UNABHÄNGIG
    // vom TLV-Modus - ein Escape-Hatch für den seltenen Fall, dass eine
    // Fix-Format-Spec trotzdem hexadezimale SE-Nummern bräuchte.
    static int parseTlvChildKey(const std::string& key, bool defaultHex,
        ryml::ConstNodeRef node, const SourceMap* smap)
    {
        std::string toParse = key;
        int base = defaultHex ? 16 : 10;

        if (key.size() > 2 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
            base = 16;
            toParse = key.substr(2);
        }

        try {
            std::size_t consumed = 0;
            const int value = std::stoi(toParse, &consumed, base);
            if (consumed != toParse.size() || toParse.empty())
                throw std::invalid_argument("trailing/leere Zeichenfolge");
            if (value < 0)
                throw std::invalid_argument("negativer Wert");
            return value;
        }
        catch (const std::exception&) {
            throw SpecValidationError(
                "Ungültiger TLV-Kindschlüssel '" + key + "' (erwartet " +
                (base == 16 ? "hexadezimal, z.B. '9F26' oder '1A'"
                            : "dezimal, z.B. '26' - für hexadezimal explizit "
                              "mit '0x'-Präfix schreiben, z.B. '0x1A'") + ")",
                node.id(), smap);
        }
    }

    // =============================================================================
    // Kleine Hilfsfunktionen
    // =============================================================================

    static std::string toUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    static SpecFieldType fieldTypeFromString(const std::string& s) {
        if (s == "scalar") return SpecFieldType::SCALAR;
        if (s == "nested") return SpecFieldType::NESTED;
        return SpecFieldType::UNKNOWN;
    }

    /// Formate ohne Encoding-Konzept – ignorieren globales und Feld-Encoding.
    ///
    /// (0.6.0) 'REMAINING' ist NICHT mehr neutral: es folgt der Encoding-
    /// Auflösung wie jedes andere Format (Feld > global > ""). ""/binary
    /// bleiben roh (BinaryField), Text-Encodings dekodieren OpaqueField.
    static bool isEncodingNeutral(const std::string& fmt) {
        static const std::unordered_set<std::string> neutral = {
            "BINARY", "BITMAP", "NOP", "UNUSED"
        };
        return neutral.count(fmt) > 0;
    }

    /// Löst das Encoding für ein Feld auf (Feld-Override > Default > leer).
    static std::string resolveEncoding(ryml::ConstNodeRef node,
        const std::string& fmt,
        const std::string& defaultEncoding)
    {
        if (isEncodingNeutral(fmt)) return "";
        return toUpper(getStr(node, "encoding", defaultEncoding));
    }

    // =============================================================================
    // Validierung
    // =============================================================================

    static void validateFieldKeys(ryml::ConstNodeRef node, const std::string& de,
        const SourceMap* smap) {
        static const std::set<std::string> allowed = {
            "type", "format", "encoding", "length", "description", "children",
            "tlv", "sensitive"
        };
        for (ryml::ConstNodeRef child : node.children()) {
            const auto key = toStdString(child.key());
            if (!allowed.count(key))
                throw SpecValidationError(
                    "Unbekannter Schlüssel '" + key + "' im Feld " + de,
                    child.id(), smap);
        }
    }

    // (0.6.0, FE-1) Validierung für Field-only-Dokumente: ein einzelner
    // 'field:'-Block (eine einzelne Felddefinition, DE-Key synthetisch '0')
    // statt der Message-'fields:'-Map. Läuft wie validateSpecYaml auf dem
    // BEREITS PREPROZESSIERTEN YAML. Die sonstigen Root-Keys (spec/encoding/
    // strict/definitions/Direktiven) sind identisch erlaubt; 'fields:' und
    // 'header:' werden abgelehnt (explizite Dokument-Form; 'header:' ist für
    // ein isoliertes Feld widersprüchlich – Entscheidung c). Die Feld-Level-
    // Checks (validateFieldKeys, 'remaining' → 'length' Fail-closed) laufen
    // auf dem Block und seinen Kindern.
    static void validateFieldSpecYaml(ryml::ConstNodeRef root,
        const SourceMap* smap = nullptr) {
        if (!hasKey(root, "field"))
            throw std::runtime_error("Fehlender Abschnitt 'field' in YAML.");  // keine Position verfügbar

        // (Analog zur E3-Regel für 'fields' in validateSpecYaml): 'field'
        // muss eine nicht-leere Map sein, sonst baut der Field-only-Loader
        // einen Parser ohne Kind bzw. entkommen rohe Exceptions aus
        // parseSpecField.
        if (!root["field"].is_map() || !root["field"].has_children())
            throw SpecValidationError(
                "Abschnitt 'field' muss eine nicht-leere Map sein "
                "(eine einzelne Felddefinition, z.B. 'format: lllbinary')",
                root["field"].id(), smap);

        if (hasKey(root, "fields"))
            throw SpecValidationError(
                "Konflikt: field-only-Dokumente dürfen keine fields:-Map "
                "enthalten (fields: gehört in Message-Specs, field: hier)",
                root["fields"].id(), smap);

        // (Entscheidung c): 'header:' (MTI/Bitmap-Größe) ist für ein
        // isoliertes Feld widersprüchlich – Field-only-Parsen hat keinen
        // Header. Fail-closed statt stiller Ignorierung.
        if (hasKey(root, "header"))
            throw SpecValidationError(
                "Konflikt: header: ist in Field-only-Dokumenten unzulässig "
                "(ein isoliertes Feld hat keinen MTI/Bitmap-Header)",
                root["header"].id(), smap);

        const ryml::ConstNodeRef block = root["field"];

        // (4) Feld-Level-Key-Whitelist auf dem Block (gleiche Regeln wie
        // Message-Specs; der DE-Key ist hier synthetisch '0').
        validateFieldKeys(block, "0", smap);

        // (5) Fail-closed: 'remaining' ohne 'length' dekodiert andernfalls
        // still 0 Bytes (gleiche Regel wie in validateSpecYaml) – auf dem
        // Block und dessen Kindern (nested-Sequenz oder TLV-Map).
        auto checkRemaining =
            [&](ryml::ConstNodeRef f, const std::string& label) {
                if (!hasKey(f, "format")) return;
                const auto fmt = toLower(getStr(f, "format"));
                if (fmt == "remaining" && !hasKey(f, "length"))
                    throw SpecValidationError(
                        label + ": 'format: remaining' benötigt 'length' "
                        "(Maximum der verbleibenden Bytes) – ohne 'length' "
                        "würden 0 Bytes dekodiert",
                        f.id(), smap);
            };
        checkRemaining(block, "Feld");
        if (hasKey(block, "children")) {
            int idx = 0;
            for (ryml::ConstNodeRef c : block["children"].children())
                checkRemaining(c, "Kind " + std::to_string(idx++));
        }
    }

    // (0.6.0, FE-1) Testnaht (Deklaration: _spec.hh, nur für Tests, keine
    // Produktions-API): Preprocessor + SourceMap + Field-only-Validierung
    // für die Datei unter `path` – dieselbe Pipeline wie loadAndParse,
    // ohne Parser-Bau (die Field-only-Loader-Eintritte loadField* existieren
    // erst ab WP3).
    void validateFieldSpecYamlFile(const std::string& path) {
        SpecLoadOptions opts;
        opts.trackSourceMap = true; // SourceMap unbedingt aufbauen (Positionen)
        const auto pr = SpecPreProcessor::preprocessWithSourceMap(path, opts);
        validateFieldSpecYaml(pr.tree.crootref(), &pr.source_map);
    }

    static void validateSpecYaml(ryml::ConstNodeRef root, const SourceMap* smap = nullptr) {
        // Läuft auf dem BEREITS PREPROCESSIERTEN YAML – !template, !merge, !use
        // wurden bereits expandiert.

        if (!hasKey(root, "fields"))
            throw std::runtime_error("Fehlender Abschnitt 'fields' in YAML.");  // keine Position verfügbar

        // (0.6.0, FE-1): 'field:' ist nur in Field-only-Dokumenten erlaubt –
        // in einer Message-Spec widersprüchlich, Fail-closed.
        if (hasKey(root, "field"))
            throw SpecValidationError(
                "Konflikt: Message-Specs verwenden fields:, field: ist nur "
                "in Field-only-Dokumenten erlaubt",
                root["field"].id(), smap);

        // [ISO8583] E3 (Sicherheits-Audit): Leeres 'fields' verwerfen -
        // sonst baut buildParser() aus einem leeren Feld-Map einen Parser
        // (rbegin() auf leerem std::map ist UB), und die Preprocessor-Warnung
        // "Feld '000' fehlt" würde das Problem verschleiern. Beide Sonderformen
        // (fehlender Abschnitt + leere Map) enden hier mit einer präzisen,
        // lokalisierten Fehlermeldung.
        // (leere Map = kein einziger Key im ryml-Tree)
        // [ISO8583] E3 (Sicherheits-Audit): 'fields' muss eine nicht-leere
        // Map sein, sonst baut buildParser() einen kaputten Parser (rbegin()
        // auf leerem std::map ist UB) oder es entkommen rohe Standard-
        // Exceptions (z.B. std::invalid_argument aus std::stoi("") bei
        // sequenz- oder skalaren Werten). Beide Sonderformen enden hier mit
        // einer präzisen, lokalisierten Fehlermeldung.
        if (!root["fields"].is_map() || !root["fields"].has_children())
            throw SpecValidationError(
                "Abschnitt 'fields' muss eine nicht-leere Map sein "
                "(DE-Nummer → Felddefinition, mind. '000' und '001')",
                root["fields"].id(), smap);

        for (ryml::ConstNodeRef entry : root["fields"].children()) {
            const auto key = toStdString(entry.key());

            if (key.empty() || !std::all_of(key.begin(), key.end(), ::isdigit))
                throw SpecValidationError(
                    "Feldschlüssel '" + key + "' ist nicht numerisch", entry.id(), smap);

            // [ISO8583] E3: Ziffern-Overflow von std::stoi() verhindern -
            // sonst entkommt hier eine rohe std::out_of_range.
            if (key.size() > 9)
                throw SpecValidationError(
                    "DE-Nummer zu groß: '" + key + "'", entry.id(), smap);

            ryml::ConstNodeRef field = entry;
            if (!field.is_map()) continue;

            // Warnung wenn length für nicht-triviale Formate fehlt.
            // 'remaining' ist hier ausgenommen: es hat eine eigene,
            // härtere Regel (Fail-closed, siehe unten).
            if (hasKey(field, "format")) {
                const auto fmt = toLower(getStr(field, "format"));
                const bool needsLength = (fmt != "nop" && fmt != "bitmap" &&
                    fmt != "unused" && fmt != "remaining");
                if (needsLength && !hasKey(field, "length"))
                    TNG_LOG_WARN("[SpecDecoder] Feld {} hat format='{}' aber kein 'length'",
                        key, fmt);

                // Fail-closed (0.6.0): 'remaining' ohne 'length' dekodiert
                // andernfalls still 0 Bytes (Clamp mit de_l_ = 0) – jetzt
                // positionierter Validierungsfehler. 'length' = Maximum.
                if (fmt == "remaining" && !hasKey(field, "length"))
                    throw SpecValidationError(
                        "Feld " + key + ": 'format: remaining' benötigt "
                        "'length' (Maximum der verbleibenden Bytes) – ohne "
                        "'length' würden 0 Bytes dekodiert",
                        field.id(), smap);
            }

            // 'format: ...bertlv' ist eine Kurzschreibweise für ein BER-TLV-
            // Containerfeld (siehe parseSpecField). Seit 0.5.0 (FR-2) dürfen
            // bekannte/erwartete Tags zusätzlich über 'children' als Hex-Map
            // deklariert werden (undeclared Tags bleiben dynamisch); ein
            // eigener 'tlv:'-Block, explizit 'type: nested' oder 'children'
            // als Sequence wären widersprüchlich und bleiben unzulässig.
            if (hasKey(field, "format")) {
                const auto fmtUpper = toUpper(getStr(field, "format"));
                std::size_t p = 0;
                while (p < fmtUpper.size() && fmtUpper[p] == 'L') ++p;
                if (fmtUpper.substr(p) == "BERTLV") {
                    const bool explicitNested = hasKey(field, "type") &&
                        toLower(getStr(field, "type")) == "nested";
                    if (explicitNested)
                        throw SpecValidationError(
                            "Feld " + key + ": 'format: ...bertlv' impliziert "
                            "bereits ein nested BER-TLV-Feld - 'type: nested' "
                            "darf nicht zusätzlich gesetzt werden",
                            field.id(), smap);
                    if (hasKey(field, "tlv"))
                        throw SpecValidationError(
                            "Feld " + key + ": 'format: ...bertlv' impliziert "
                            "BER-TLV (ISO/IEC 8825-1) - ein eigener 'tlv:'-Block "
                            "ist redundant und unzulässig",
                            field.id(), smap);
                    if (hasKey(field, "children") && !field["children"].is_map())
                        throw SpecValidationError(
                            "Feld " + key + ": 'format: ...bertlv' akzeptiert "
                            "'children' nur als Map (Tag → Deklaration, z.B. "
                            "'9F26': { format: binary }) - eine Sequence ist "
                            "unzulässig",
                            field["children"].id(), smap);
                }
            }

            // Nested: erkennbar durch 'children' (oder optionales type: nested)
            const bool isNested = hasKey(field, "children") ||
                (hasKey(field, "type") && getStr(field, "type") == "nested");
            if (isNested) {
                if (hasKey(field, "children")) {
                    ryml::ConstNodeRef ch = field["children"];
                    if (!ch.is_seq() && !ch.is_map())
                        throw SpecValidationError(
                            "'children' im nested-Feld " + key +
                            " muss eine Liste (normal) oder Map (TLV) sein",
                            field.id(), smap);
                }

                if (hasKey(field, "tlv")) {
                    ryml::ConstNodeRef tlv = field["tlv"];
                    const bool isBer = getBool(tlv, "ber", false);
                    if (!isBer && (!hasKey(tlv, "tag_bytes") || !hasKey(tlv, "len_bytes")))
                        throw SpecValidationError(
                            "TLV-Block im Feld " + key +
                            " benötigt 'tag_bytes' und 'len_bytes' "
                            "(oder 'ber: true' für BER-TLV mit variabler Länge)",
                            tlv.id(), smap);
                }

                // FR-1 (0.5.0, D5): TLV-Kind-Whitelist — gilt für beide
                // TLV-Formen (tlv:-Block und ...bertlv). Läuft auf dem
                // gepreprozessierten Baum → sieht die Endform nach
                // !use/!template/!merge-Expansion. Die TLV-Länge liegt auf
                // dem Wire im Length-Feld, daher sind L-präfixierte Formate,
                // 'bitmap', 'remaining' und 'nop' bei TLV-Kindern
                // widersprüchlich (Fail-closed statt stiller "documentation-
                // only"-Semantik, Q2-Präzedenz aus 0.3.0).
                const bool isTlvField = hasKey(field, "tlv")
                    || (hasKey(field, "format") && [&] {
                            const auto fu = toUpper(getStr(field, "format"));
                            std::size_t q = 0;
                            while (q < fu.size() && fu[q] == 'L') ++q;
                            return fu.substr(q) == "BERTLV";
                        }());
                if (isTlvField && hasKey(field, "children")) {
                    const ryml::ConstNodeRef ch = field["children"];
                    if (ch.is_map()) {
                        static const std::set<std::string> textChildFormats = {
                            "CHAR", "NUMERIC", "NOPAD_CHAR", "AMOUNT" };
                        static const std::set<std::string> allChildFormats = {
                            "BINARY", "CHAR", "NUMERIC", "NOPAD_CHAR", "AMOUNT" };
                        static const std::set<std::string> allChildEncodings = {
                            "ASCII", "BCD", "BINARY", "EBCDIC" };
                        for (const ryml::ConstNodeRef c : ch.children()) {
                            const auto seKey = toStdString(c.key());
                            if (!c.is_map())
                                throw SpecValidationError(
                                    "Feld " + key + ", TLV-Kind '" + seKey +
                                    "': Kind-Deklaration muss eine Map sein "
                                    "(z.B. { format: binary, "
                                    "description: ... })",
                                    c.id(), smap);
                            if (hasKey(c, "format")) {
                                const auto cf = toUpper(getStr(c, "format"));
                                if (!allChildFormats.count(cf))
                                    throw SpecValidationError(
                                        "Feld " + key + ", TLV-Kind '" + seKey +
                                        "': Format '" + cf +
                                        "' unzulässig - die TLV-Länge liegt im "
                                        "Length-Feld, L-präfixierte Formate "
                                        "(llchar, ...), 'bitmap', 'remaining' "
                                        "und 'nop' sind bei TLV-Kindern nicht "
                                        "erlaubt (erlaubt: binary, char, "
                                        "numeric, nopad_char)",
                                        c["format"].id(), smap);
                            }
                            if (hasKey(c, "encoding")) {
                                const auto ce = toUpper(getStr(c, "encoding"));
                                if (!allChildEncodings.count(ce))
                                    throw SpecValidationError(
                                        "Feld " + key + ", TLV-Kind '" + seKey +
                                        "': Encoding '" + ce +
                                        "' unzulässig (erlaubt: ascii, "
                                        "ebcdic, bcd, binary)",
                                        c["encoding"].id(), smap);
                                if (hasKey(c, "format")) {
                                    const auto cf = toUpper(getStr(c, "format"));
                                    if (textChildFormats.count(cf) &&
                                        ce != "ASCII" && ce != "BCD" && ce != "EBCDIC")
                                        throw SpecValidationError(
                                            "Feld " + key + ", TLV-Kind '" + seKey +
                                            "': Text-Format '" + cf +
                                            "' benötigt ein Encoding ascii, "
                                            "ebcdic oder bcd ('" + ce +
                                            "' ist für Text-Kinder nicht "
                                            "verwendbar)",
                                            c["encoding"].id(), smap);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // =============================================================================
    // YAML → SpecField
    // =============================================================================

    // Verhindert Stack-Overflow bei extrem tief verschachtelten 'children'-
    // Strukturen (siehe analoge Begründung/Konstante in _preprocessor.cc -
    // eigene Konstante hier, da beide Übersetzungseinheiten `static`/interne
    // Bindung nutzen und sich nichts teilen).
    static constexpr int MAX_RECURSION_DEPTH = 200;

    static void checkDepth(int depth) {
        if (depth > MAX_RECURSION_DEPTH)
            throw std::runtime_error(
                "Feld-Verschachtelung zu tief (> " + std::to_string(MAX_RECURSION_DEPTH) +
                " Ebenen) - vermutlich eine fehlerhafte 'children'-Struktur in der Spec.");
    }

    // Forward-Deklaration für rekursiven Aufruf
    static SpecField parseSpecField(ryml::ConstNodeRef node,
        const std::string& defaultEncoding,
        const std::string& tag = "",
        const SourceMap* smap = nullptr,
        int depth = 0);

    static SpecField parseSpecField(ryml::ConstNodeRef node,
        const std::string& defaultEncoding,
        const std::string& tag,
        const SourceMap* smap,
        int depth)
    {
        checkDepth(depth);
        SpecField f;
        f.format = toUpper(getStr(node, "format"));

        // ── format: ...BERTLV - Kurzschreibweise für ein BER-TLV-Feld ─────────────
        // Seit 0.5.0 (FR-2) dürfen bekannte/erwartete Tags zusätzlich über
        // 'children' als Hex-Map deklariert werden (z.B. "9F26": { format: binary });
        // undeclaried Tags bleiben dynamisch (BinaryField + "SE<n>"-Fallback).
        // Ein eigener 'tlv:'-Block, explizit 'type: nested' oder 'children' als
        // Sequence bleiben unzulässig (siehe validateSpecYaml). Es genügt also
        // z.B.
        //   "055": { format: lllbertlv, length: 999, description: "ICC Data" }
        // ohne 'type: nested', 'children:' oder 'tlv:'.
        bool isBerTlvShorthand = false;
        {
            std::size_t p = 0;
            while (p < f.format.size() && f.format[p] == 'L') ++p;
            if (f.format.substr(p) == "BERTLV") {
                isBerTlvShorthand = true;
                // Auf dem Wire ist das Feld identisch zu einem L(L(L(L)))BINARY-
                // Container (Längen-Prefix + Binärdaten) - die BER-TLV-Dekodierung
                // des extrahierten Payloads übernimmt anschließend BERTLVParser.
                f.format = f.format.substr(0, p) + "BINARY";
            }
        }

        // 'type' wird aus dem Kontext abgeleitet – kein explizites Pflichtfeld:
        //   format: ...bertlv  → NESTED (BER-TLV, siehe oben)
        //   children vorhanden → NESTED
        //   alles andere       → SCALAR
        // Ein explizites 'type:' wird akzeptiert wenn vorhanden, aber nie gefordert.
        if (isBerTlvShorthand) {
            f.type = SpecFieldType::NESTED;
        }
        else if (hasKey(node, "type")) {
            f.type = fieldTypeFromString(toLower(getStr(node, "type")));
        }
        else if (hasKey(node, "children")) {
            f.type = SpecFieldType::NESTED;
        }
        else {
            f.type = SpecFieldType::SCALAR;
        }

        // NOP-Felder: nur ein Index-Placeholder wegen des +1-Offsets im Parser.
        // length und description sind bedeutungslos und müssen nicht angegeben werden.
        if (f.format == "NOP" || f.format == "UNUSED") {
            f.length = 0;
            f.description = getStr(node, "description", "<nop>");
            f.encoding = "";
            return f;
        }

        f.encoding = resolveEncoding(node, f.format, defaultEncoding);

        if (hasKey(node, "length")) {
            const int raw_length = getInt(node, "length", 0);
            if (raw_length < 0) {
                // node["length"]s Knoten-ID trägt die ursprüngliche Herkunft
                // aus der Quelldatei (siehe _preprocessor.hh) - damit zeigt
                // die Fehlermeldung direkt auf die richtige Datei + Zeile.
                const ryml::id_type value_id = node["length"].id();
                throw SpecValidationError(
                    "Feld '" + getStr(node, "description", "<unnamed>") +
                    "' hat ungültige length=" + std::to_string(raw_length) +
                    " (muss >= 0 sein)",
                    value_id, smap);
            }
            f.length = static_cast<std::size_t>(raw_length);
        }

        f.has_explicit_description = hasKey(node, "description");
        f.description = f.has_explicit_description
            ? getStr(node, "description")
            : (f.length == 0 ? "<dummy>" : "?");

        // [ISO8583] 3.4 (PCI-Logging-Hygiene): 'sensitive: true' → der
        // Feld-Wert wird in dump()/Log-Ausgaben als "***" maskiert.
        f.sensitive = getBool(node, "sensitive", false);

        // Warnung wenn length == 0 bei einem Feld das Daten erwartet
        const bool expectsData = (f.format != "NOP" && f.format != "UNUSED" &&
            f.format != "BITMAP" && f.format != "REMAINING" &&
            f.type == SpecFieldType::SCALAR);
        const bool hasVariablePrefix = (f.format.find('L') == 0); // LL, LLL etc.
        if (expectsData && !hasVariablePrefix && f.length == 0)
            TNG_LOG_WARN("[SpecDecoder] Feld '{}' (format={}) hat length=0",
                f.description, f.format);

        // Encoding das an Kinder vererbt wird: neutrale Formate geben global-Encoding weiter
        const std::string& childEnc = isEncodingNeutral(f.format) ? defaultEncoding : f.encoding;

        // ── TLV-Block ────────────────────────────────────────────────────────────
        if (isBerTlvShorthand) {
            // Kein 'tlv:'-Knoten im YAML nötig - 'format: ...bertlv' impliziert
            // bereits BER-TLV ohne TCC (siehe Kommentar bei isBerTlvShorthand oben).
            TLVOptions opts;
            opts.ber = true;
            f.tlv = opts;
        }
        else if (hasKey(node, "tlv")) {
            ryml::ConstNodeRef t = node["tlv"];
            TLVOptions opts;
            opts.ber = getBool(t, "ber", false);
            if (!opts.ber) {
                opts.tag_bytes = getInt(t, "tag_bytes", 2);
                opts.len_bytes = getInt(t, "len_bytes", 2);
            }
            opts.tcc = getBool(t, "tcc", false);
            opts.encoding = toUpper(getStr(t, "encoding", childEnc));
            if (opts.ber && opts.tcc)
                TNG_LOG_WARN("[SpecDecoder] Feld '{}': 'tcc' wird bei BER-TLV "
                    "ignoriert (BER-TLV kennt kein TCC-Feld)", f.description);
            f.tlv = opts;
        }

        // ── Children ─────────────────────────────────────────────────────────────
        if (hasKey(node, "children")) {
            const std::string& seEnc = f.tlv ? f.tlv->encoding : childEnc;
            ryml::ConstNodeRef children = node["children"];

            if (children.is_map()) {
                // TLV-Modus: Key = SE-Nummer (dezimal) oder EMV-Tag (hex, bei
                // ber:true) - siehe parseTlvChildKey().
                const bool asHex = f.tlv && f.tlv->ber;
                for (ryml::ConstNodeRef entry : children.children()) {
                    const auto seKey = toStdString(entry.key());
                    const int  seNum = parseTlvChildKey(seKey, asHex, entry, smap);
                    SpecField child = parseSpecField(entry, seEnc, seKey, smap, depth + 1);
                    // FR-1 (0.5.0, D5): Text-Kinder (char/numeric/nopad_char)
                    // brauchen ein erlaubtes Encoding (explizit deklariert ODER
                    // vererbt) - ansonsten wäre die Codec-Konversion beim
                    // Decode nicht definiert. Fail-closed mit Position (zeigt
                    // auf den Encoding-Knoten, falls vorhanden, sonst auf den
                    // Kind-Knoten); baut auf der Whitelist-Prüfung in
                    // validateSpecYaml auf (dort nur die deklarierten Werte).
                    if (child.format == "CHAR" || child.format == "NUMERIC" ||
                        child.format == "NOPAD_CHAR" || child.format == "AMOUNT")
                        if (child.encoding != "ASCII" && child.encoding != "EBCDIC" &&
                            child.encoding != "BCD") {
                            const bool hasEncKey = hasKey(entry, "encoding");
                            const auto pos = hasEncKey ? entry["encoding"].id() : entry.id();
                            throw SpecValidationError(hasEncKey
                                ? "TLV-Kind '" + seKey + "' (Format " + child.format +
                                  "') hat das unzulässige Encoding '" + child.encoding +
                                  "' - Text-Kinder benötigen ascii, ebcdic oder bcd"
                                : "TLV-Kind '" + seKey + "' (Format " + child.format +
                                  "') hat kein erlaubtes Encoding (kein 'encoding:' "
                                  "deklariert, vererbt: '" + seEnc + "') - erlaubt: "
                                  "ascii, ebcdic, bcd",
                                pos, smap);
                        }
                    f.tlv_children[seNum] = std::move(child);
                }
            }
            else {
                // Normal-Modus: Sequence mit Index-Feldern
                // Kinder rekursiv parsen – parseSpecField löst Encoding korrekt auf
                for (ryml::ConstNodeRef child : children.children())
                    f.children.push_back(parseSpecField(child, childEnc, "", smap, depth + 1));
            }
        }

        return f;
    }

    // =============================================================================
    // SpecField → ISOFieldParser (Parser-Fabrik)
    // =============================================================================

    using ParserFactory = std::function<
        ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr(
            int len, const std::string& desc)>;

    static const std::unordered_map<std::string, ParserFactory>& parserTable() {
        using F = ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr;
#define MAKE(T)     [](int len, const std::string& d) -> F { return std::make_shared<T>(len, d); }
#define MAKE_NOP()  [](int,     const std::string&  ) -> F { return std::make_shared<IF_NOP>(); }

        // Prozess-lebenslange, unveraenderliche Dispatch-Tabelle
        // (Key = "Format|Encoding"). Bewusst als LEAKY SINGLETON: wird NIE
        // dealloziert. Grund: der STL-Container-Destruktor (unordered_map ->
        // _Container_base12::_Orphan_all) wuerde sonst bei Prozess-Exit via
        // atexit aufgerufen und faellt unter MSVC-ASan (statische CRT) mit
        // einer Access-Violation ab — bekanntes MSVC-ASan/STL-Teardown-
        // Artefakt, KEIN Library-Bug (gleiche Tests laufen im plain-Debug-
        // Build 333/333 gruen). Die Tabelle ist immutabel; der Prozess-Exit
        // raeumt den Speicher ohnehin zurueck.
        static const std::unordered_map<std::string, ParserFactory>* table =
            [] {
                auto* t = new const std::unordered_map<std::string, ParserFactory>{
            // ── Encoding-unabhängig ──────────────────────────────────────────────
            { "BITMAP|",           MAKE(IFB_BITMAP)     },
            { "NOP|",              MAKE_NOP()            },
            { "UNUSED|",           MAKE_NOP()            },
            { "REMAINING|",        MAKE(IF_REMAINING)    },
            { "REMAINING|BINARY",  MAKE(IF_REMAINING)    },
            { "REMAINING|ASCII",   MAKE(IFA_REMAINING) },
            { "REMAINING|EBCDIC",  MAKE(IFE_REMAINING)   },
            { "REMAINING|BCD",     MAKE(IFB_REMAINING) },
            // ── BINARY ──────────────────────────────────────────────────────────
            { "BINARY|",           MAKE(IF_BINARY)       },
            { "LBINARY|",          MAKE(IF_LBINARY)      },
            { "LLBINARY|",         MAKE(IF_LLBINARY)     },
            { "LLLBINARY|",        MAKE(IF_LLLBINARY)    },
            { "BINARY|BINARY",     MAKE(IF_BINARY)       },
            { "LBINARY|BINARY",    MAKE(IF_LBINARY)      },
            { "LLBINARY|BINARY",   MAKE(IF_LLBINARY)     },
            { "LLLBINARY|BINARY",  MAKE(IF_LLLBINARY)    },
            // ── ASCII ────────────────────────────────────────────────────────────
            { "NUMERIC|ASCII",     MAKE(IFA_NUMERIC)     },
            { "AMOUNT|ASCII",      MAKE(IFA_AMOUNT)      },
            { "CHAR|ASCII",        MAKE(IFA_CHAR)        },
            { "NOPAD_CHAR|ASCII",  MAKE(IFA_NOPAD_CHAR)  },
            { "LCHAR|ASCII",       MAKE(IFA_LCHAR)       },
            { "LLCHAR|ASCII",      MAKE(IFA_LLCHAR)      },
            { "LLLCHAR|ASCII",     MAKE(IFA_LLLCHAR)     },
            { "LLLLCHAR|ASCII",    MAKE(IFA_LLLLCHAR)    },
            { "LNUM|ASCII",        MAKE(IFA_LNUM)        },
            { "LLNUM|ASCII",       MAKE(IFA_LLNUM)       },
            { "LBINARY|ASCII",     MAKE(IFA_LBINARY)     },
            { "LLBINARY|ASCII",    MAKE(IFA_LLBINARY)    },
            { "LLLBINARY|ASCII",   MAKE(IFA_LLLBINARY)   },
            // (0.6.0, FR-3) Zwilling für llllchar|ascii-Container-Normalisierung
            { "LLLLBINARY|ASCII",  MAKE(IFA_LLLLBINARY)  },
            // ── BCD ──────────────────────────────────────────────────────────────
            { "NUMERIC|BCD",       MAKE(IFB_NUMERIC)     },
            { "AMOUNT|BCD",        MAKE(IFB_AMOUNT)      },
            { "LCHAR|BCD",         MAKE(IFB_LCHAR)       },
            { "LLCHAR|BCD",        MAKE(IFB_LLCHAR)      },
            { "LLLCHAR|BCD",       MAKE(IFB_LLLCHAR)     },
            { "LBINARY|BCD",       MAKE(IFB_LBINARY)     },
            { "LLBINARY|BCD",      MAKE(IFB_LLBINARY)    },
            { "LLLBINARY|BCD",     MAKE(IFB_LLLBINARY)   },
            // ── EBCDIC ───────────────────────────────────────────────────────────
            { "BINARY|EBCDIC",     MAKE(IFE_BINARY)      },
            { "LBINARY|EBCDIC",    MAKE(IFE_LBINARY)     },
            { "LLBINARY|EBCDIC",   MAKE(IFE_LLBINARY)    },
            { "LLLBINARY|EBCDIC",  MAKE(IFE_LLLBINARY)   },
            { "LLLLBINARY|EBCDIC", MAKE(IFE_LLLLBINARY)  },
            { "NUMERIC|EBCDIC",    MAKE(IFE_NUMERIC)     },
            { "AMOUNT|EBCDIC",     MAKE(IFE_AMOUNT)      },
            { "LNUM|EBCDIC",       MAKE(IFE_LNUM)        },
            { "CHAR|EBCDIC",       MAKE(IFE_CHAR)        },
            { "NOPAD_CHAR|EBCDIC", MAKE(IFE_NOPAD_CHAR)  },
            { "LCHAR|EBCDIC",      MAKE(IFE_LCHAR)       },
            { "LLCHAR|EBCDIC",     MAKE(IFE_LLCHAR)      },
            { "LLLCHAR|EBCDIC",    MAKE(IFE_LLLCHAR)     },
                };
                return t;
            }();
#undef MAKE
#undef MAKE_NOP
        return *table;
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr
        createScalarParser(const SpecField& f)
    {
        const auto& table = parserTable();
        const std::string key = f.format + "|" + f.encoding;

        auto it = table.find(key);
        if (it != table.end())
            return it->second(static_cast<int>(f.length), f.description);

        // Fallback: ohne Encoding (für BITMAP, NOP, BINARY)
        auto it2 = table.find(f.format + "|");
        if (it2 != table.end())
            return it2->second(static_cast<int>(f.length), f.description);

        throw std::runtime_error(
            "Unbekannte Format/Encoding-Kombination in der Spec:\n"
            "  format:      '" + f.format + "'\n"
            "  encoding:    '" + f.encoding + "'\n"
            "  description: '" + f.description + "'\n"
            "  Erlaubte Encodings: ASCII | BCD | BINARY | EBCDIC\n"
            "  Prüfe auf Tippfehler im globalen 'encoding'-Schlüssel oder im Feld selbst.");
    }

    static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        makeTlvParser(int tag_bytes, int len_bytes, bool tcc, codec::Encoder enc, bool ber,
            const tlv_detail::TlvChildMap& childMap,
            bool sensitiveAll = false)
    {
        using namespace ::TNG_NAMESPACE;

        // ── BER-TLV: variable Tag-/Length-Länge, kein TCC ─────────────────────
        if (ber)
            return std::make_shared<BERTLVParser>(childMap, sensitiveAll);

        // ── Feste Byte-Anzahl (bisheriges Verhalten, jetzt über
        //    FixedNumericTag/FixedNumericLength statt der ursprünglichen
        //    4 Template-Parameter TAG_BYTES/LEN_BYTES/TAG_ENC/LEN_ENC) ────────
#define MAKE_FIXED_TLV(TB, LB, HAS_TCC, ENC) \
        std::make_shared<ISOTLVParser< \
            FixedNumericTag<TB, codec::Encoder::ENC>, \
            FixedNumericLength<LB, codec::Encoder::ENC>, \
            HAS_TCC, codec::Encoder::ENC>>(childMap, sensitiveAll)

        // tag_bytes == 2, len_bytes == 2
        if (tag_bytes == 2 && len_bytes == 2 && tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(2, 2, true, EBCDIC);
        if (tag_bytes == 2 && len_bytes == 2 && !tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(2, 2, false, EBCDIC);
        if (tag_bytes == 2 && len_bytes == 2 && tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(2, 2, true, BCD);
        if (tag_bytes == 2 && len_bytes == 2 && !tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(2, 2, false, BCD);
        if (tag_bytes == 2 && len_bytes == 2 && tcc && enc == codec::Encoder::ASCII)  return MAKE_FIXED_TLV(2, 2, true, ASCII);
        if (tag_bytes == 2 && len_bytes == 2 && !tcc && enc == codec::Encoder::ASCII)  return MAKE_FIXED_TLV(2, 2, false, ASCII);
        // tag_bytes == 2, len_bytes == 1
        if (tag_bytes == 2 && len_bytes == 1 && tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(2, 1, true, EBCDIC);
        if (tag_bytes == 2 && len_bytes == 1 && !tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(2, 1, false, EBCDIC);
        if (tag_bytes == 2 && len_bytes == 1 && tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(2, 1, true, BCD);
        if (tag_bytes == 2 && len_bytes == 1 && !tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(2, 1, false, BCD);
        // tag_bytes == 1, len_bytes == 1
        if (tag_bytes == 1 && len_bytes == 1 && tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(1, 1, true, EBCDIC);
        if (tag_bytes == 1 && len_bytes == 1 && !tcc && enc == codec::Encoder::EBCDIC) return MAKE_FIXED_TLV(1, 1, false, EBCDIC);
        if (tag_bytes == 1 && len_bytes == 1 && tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(1, 1, true, BCD);
        if (tag_bytes == 1 && len_bytes == 1 && !tcc && enc == codec::Encoder::BCD)    return MAKE_FIXED_TLV(1, 1, false, BCD);

#undef MAKE_FIXED_TLV

        // Fallback
        TNG_LOG_WARN("[SpecDecoder] TLV tag_bytes={} len_bytes={} nicht unterstützt – "
            "Mastercard-Default (2,2,false,EBCDIC)", tag_bytes, len_bytes);
        return std::make_shared<ISOTLVParser<
            FixedNumericTag<2, codec::Encoder::EBCDIC>,
            FixedNumericLength<2, codec::Encoder::EBCDIC>,
            false, codec::Encoder::EBCDIC>>(childMap, sensitiveAll);
    }

    // =============================================================================
    // (0.6.0, FR-3) Container-Basis-Parser normalisieren
    // =============================================================================
    //
    // Text-basierte Containerformate (CHAR/NUMERIC/NOPAD_CHAR/AMOUNT, mit oder
    // ohne L-Prefix; REMAINING + Text-Encoding) erzeugen via createScalarParser
    // einen string-basierten Basis-Parser. In den T=parser-Zweigen von
    // ISOFieldParser (BinaryField-Scratch bei unparse / BinaryField-Wrapper
    // bei parse, s. src/_parser.hh) crasht das mit einer
    // Null-Pointer-Dereferenz: der Zweige castet den BinaryField auf
    // OpaqueField -> nullptr -> SIGSEGV. Latent seit 0.3.0 (Thread-
    // Sicherheits-Scratch-Pattern); betraf u. a. die AGENTS.md-Beispiele
    // (DE48 lllchar + tlv).
    //
    // Die Wire-Präfix-Semantik des binären Zwillings ist identisch (gleicher
    // L-Zähler + Prefix-Encoder), und Container-Nutzdaten sind immer rohe
    // Bytes, die an die Kinder weitergereicht werden (jedes Kind löst sein
    // eigenes Encoding auf) -> die Normalisierung ist wire-neutral:
    //   L* + Text           -> L*BINARY  (Prefix-Encoding bleibt erhalten)
    //   FIX + Text          -> "BINARY|" (IF_BINARY, Roh-Bytes; bewusst NICHT
    //                              "BINARY|EBCDIC" — das wäre IFE_BINARY mit
    //                              HEX_EBCDIC-Data-Encoder)
    //   REMAINING + Text    -> "REMAINING|" (IF_REMAINING, Roh-Bytes)
    //
    // Wichtig: nur der lokale Parser-Bau wird normalisiert (Kopie); das
    // SpecField selbst bleibt unverändert, damit die Introspektion
    // (ISOSpec::field) das deklarierte Format meldet. Skalare (nicht
    // nested) Textfelder bleiben string-basiert (Verhalten unverändert).
    static SpecField containerBaseField(const SpecField& f) {
        SpecField cf = f;
        std::size_t ls = 0;
        while (ls < cf.format.size() && cf.format[ls] == 'L')
            ++ls;
        const std::string rest = cf.format.substr(ls);
        if (rest == "CHAR" || rest == "NUMERIC" || rest == "NOPAD_CHAR" ||
            rest == "AMOUNT") {
            if (ls == 0) {
                cf.format = "BINARY";
                cf.encoding = "";
            }
            else
                cf.format = cf.format.substr(0, ls) + "BINARY";
        }
        else if (cf.format == "REMAINING" &&
                 (cf.encoding == "ASCII" || cf.encoding == "EBCDIC" || cf.encoding == "BCD"))
            cf.encoding = "";
        return cf;
    }

    // =============================================================================
    // (0.6.0, FE-1) Field-only-Block-Parser-Aufbau
    // =============================================================================
    //
    // Aus buildFieldParser extrahierte Helper: sie liefern den PAYLOAD-Parser
    // eines Felds, also genau das, was ein Container-Parser seinem Kind
    // weiterreicht. Der Message-Pfad (buildFieldParser) ruft dieselben
    // Helper – Verhalten byte-identisch, Regression über die unveränderten
    // Message-/TLV-/E2E-Tests.
    //
    // buildFieldBlockParser (FE-1): für ein einzelnes Field-only-Feld der
    // Block-Parser OHNE den äußeren Längenpräfix-Frame des DEs selbst:
    //   tlv/bertlv  → buildTlvFieldParser  (TLV-Frames = Payload)
    //   nested      → buildNestedSubParser (Sequenz-Kinder)
    //   skalar      → createScalarParser   (einzelnes Kind-Parser)
    // Der Top-Parser ist eine ISOBaseParser mit container(true), die diesen
    // Block-Parser als (einziges) Kind hält → unparse/parse überspringen
    // MTI + Bitmap (src/_parser.cc) und der Datenloop decodiert ab Slot 0.
    // checkContainerBase-Guard gilt automatisch (gleicher Pfad).

    // TLV/BERTLV-Sub-Parser (FR-1/FR-2-Logik unverändert): TlvChildMap-Aufbau
    // aus 'children' (Typisierung Text vs. binär + Encoding + Beschreibung +
    // PCI-Sensitivität), Encoding-Auflösung des Containers und
    // Mastercard-Default-Fallback in makeTlvParser.
    static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        buildTlvFieldParser(const SpecField& f)
    {
        const auto& opts = *f.tlv;
        const auto enc = [&] {
            if (opts.encoding == "BCD")   return codec::Encoder::BCD;
            if (opts.encoding == "ASCII")  return codec::Encoder::ASCII;
            return codec::Encoder::EBCDIC;
            }();

        tlv_detail::TlvChildMap childMap;
        for (const auto& [tag, child] : f.tlv_children) {
            tlv_detail::TlvChildInfo info;
            const auto cf = child.format; // bereits Uppercase (parseSpecField)
            info.text = (cf == "CHAR" || cf == "NUMERIC" || cf == "NOPAD_CHAR" ||
                         cf == "AMOUNT");
            if (info.text) {
                if (child.encoding == "BCD")
                    info.enc = codec::Encoder::BCD;
                else if (child.encoding == "ASCII")
                    info.enc = codec::Encoder::ASCII;
                else if (child.encoding == "EBCDIC")
                    info.enc = codec::Encoder::EBCDIC;
                else
                    // Defensive: wird primär von validateSpecYaml abgefangen
                    // (positioniert). Hier nur als Fail-closed-Doppelcheck.
                    throw std::runtime_error(
                        "[SpecDecoder] TLV-Kind " + child.description +
                        " (Format " + cf + ") benötigt ein Encoding (ascii/ebcdic/bcd), "
                        "erbt aber '" + child.encoding + "'");
            }
            else
                info.enc = codec::Encoder::BINARY; // rohe Bytes, Encoding ignorieren
            info.description = child.has_explicit_description ? child.description : "";
            // [ISO8583] 3.4 (PCI): pro-Tag Sensitivität (Tag-Deklaration
            // 'sensitive: true' oder Erbgang von einem sensitive Container).
            info.sensitive = child.sensitive || f.sensitive;
            childMap[static_cast<std::size_t>(tag)] = std::move(info);
        }

        return makeTlvParser(opts.tag_bytes, opts.len_bytes, opts.tcc, enc, opts.ber,
            childMap, f.sensitive);
    }

    // Sequenz-Sub-Parser für nested Container (ohne TLV): ISOBaseParser im
    // Container-Modus (kein MTI/Bitmap, Slot 0 = erstes Kind), je ein
    // Skalaren-Parser pro deklariertem Kind. Der Container-Basis-Parser
    // selbst (Längenpräfix-Frame) gehört NICHT dazu – s. buildFieldBlockParser.
    static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        buildNestedSubParser(const SpecField& f)
    {
        auto sub = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(f.description);
        // FR-3 (0.6.0): Container-Modus – der Sub-Payload hat
        // kein MTI/Bitmap (Slot 0 = erstes Kind-Feld). Verhindert
        // die Doppel-Serialisierung eines einzelnen Kind-Felds
        // durch die Slot-0/MTI-Semantik von ISOBaseParser.
        sub->container(true);
        for (const auto& child : f.children) {
            auto childP = createScalarParser(child);
            // [ISO8583] 3.4 (PCI): eigene Deklaration ODER Erbgang
            // von einem sensitive Container.
            if (child.sensitive || f.sensitive)
                if (auto fp = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOFieldParserPtrBase>(childP))
                    fp->sensitive(true);
            sub->add(childP);
        }
        return sub;
    }

    // (0.6.0, FE-1) Block-Parser eines einzelnen Field-only-Felds (s.
    // Section-Header oben). `defaultEncoding` ist hier nur Signaturstabilität:
    // die per-Feld-Encoding-Auflösung erfolgt bereits in parseSpecField.
    static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        buildFieldBlockParser(const SpecField& f, const std::string& /*defaultEncoding*/)
    {
        if (f.type == SpecFieldType::NESTED) {
            if (f.tlv)
                return buildTlvFieldParser(f);
            return buildNestedSubParser(f);
        }
        // Skalar: als einziges Kind (Slot 0) in einen Container-Top-Parser
        // (ISOBaseParser, container=true) gewickelt – damit liefert
        // buildFieldBlockParser für alle drei Formen den fertigen
        // Top-Parser. Im Container-Modus entfallen MTI/Bitmap und der
        // Daten-Loop startet bei Slot 0 (_parser.cc); der Skalaren-Parser
        // konsumiert dort Präfix + Payload (komplettes Frame-Including).
        auto top = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(f.description);
        top->container(true);
        auto p = createScalarParser(f);
        if (f.sensitive)
            if (auto fp = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOFieldParserPtrBase>(p))
                fp->sensitive(true);
        top->add(p);
        return top;
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr
        buildFieldParser(const SpecField& f)
    {
        switch (f.type) {
        case SpecFieldType::SCALAR: {
            auto p = createScalarParser(f);
            // [ISO8583] 3.4 (PCI): 'sensitive: true' aus der Spec auf den
            // Laufzeit-Parser übertragen (dump()/Logs maskieren dann den Wert).
            if (f.sensitive)
                if (auto fp = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOFieldParserPtrBase>(p))
                    fp->sensitive(true);
            return p;
        }

        case SpecFieldType::NESTED: {
            // FR-3 (0.6.0): Container-Basis-Parser auf den binären Zwilling
            // normalisieren (wire-neutral, s. containerBaseField) — sonst
            // SIGSEGV bei text-basierten Formaten (BinaryField-Scratch/-
            // Wrapper vs. string-basierter Basis-Parser).
            auto base = createScalarParser(containerBaseField(f));
            // Container selbst: sensitive Container markieren das komplette
            // Subfeld-Baum (alle Kinder werden entsprechend gesetzt, s. u.).
            if (f.sensitive)
                if (auto fp = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOFieldParserPtrBase>(base))
                    fp->sensitive(true);
            auto nested = std::make_shared<
                ::TNG_NAMESPACE::ISONestedFieldParser<::TNG_NAMESPACE::ISOBaseParser>>(
                    base, f.description);

            if (f.tlv)
                // (0.6.0, FE-1) Extrahiert: buildTlvFieldParser (identische
                // Logik wie zuvor - TlvChildMap-Aufbau, Encoding-Auflösung,
                // Mastercard-Default-Fallback in makeTlvParser).
                nested->subParser(buildTlvFieldParser(f));
            else
                // (0.6.0, FE-1) Extrahiert: buildNestedSubParser (identische
                // Logik wie zuvor - ISOBaseParser im Container-Modus, je ein
                // Skalar-Parser pro deklariertem Kind).
                nested->subParser(buildNestedSubParser(f));
            return nested;
        }

        default:
            return std::make_shared<IF_NOP>();
        }
    }

    // =============================================================================
    // SpecField → SpecFieldInfo (für ISOSpec Introspection)
    // =============================================================================

    static SpecFieldFormat makeSpecFieldFormat(const SpecField& f) {
        SpecFieldFormat fmt;
        fmt.max_length = static_cast<int>(f.length);

        std::size_t prefix = 0;
        while (prefix < f.format.size() && f.format[prefix] == 'L')
            ++prefix;

        fmt.prefix_digits = static_cast<int>(prefix);
        fmt.type = prefix > 0 ? f.format.substr(prefix) : f.format;

        // NOP/UNUSED haben keine Längen-Semantik; REMAINING (0.6.0) meldet
        // sein deklariertes Maximum (length = Pflicht, Fail-closed beim Load).
        if (fmt.type == "NOP" || fmt.type == "UNUSED")
            fmt.max_length = 0;

        return fmt;
    }

    static SpecFieldInfo makeSpecFieldInfo(TNG_KEY_TYPE key, const SpecField& f) {
        SpecFieldInfo info;
        info.key = key;
        info.description = f.description;
        info.format = makeSpecFieldFormat(f);
        info.encoding = f.encoding;
        info.is_nested = (f.type == SpecFieldType::NESTED);
        info.is_bitmap = (f.format == "BITMAP");

        TNG_KEY_TYPE childKey = 0;
        for (const auto& child : f.children)
            info.children.push_back(makeSpecFieldInfo(childKey++, child));

        // FR-2 (0.5.0): deklarierte TLV-Kinder (tlv:-Block- und bertlv-Felder
        // identisch) in die Introspektion übernehmen (schließt die Lücke, dass
        // f.tlv_children bisher nicht in SpecFieldInfo auftauchte). Der Key
        // ist bewusst int (nicht TNG_KEY_TYPE): EMV-2-Byte-Tags wie 0x9F26
        // passen ohne ISO8583_BERTLV nicht in int16_t - in solchen Builds ist
        // das `key`-Mitglied des Kindes nur eine eingekürzte Sicht desselben
        // Wertes (Map-Key trägt den vollen Wert).
        for (const auto& [tag, child] : f.tlv_children)
            info.tlv_children.emplace(tag, makeSpecFieldInfo(static_cast<TNG_KEY_TYPE>(tag), child));

        return info;
    }

    // =============================================================================
    // YAML laden und vorverarbeiten
    // =============================================================================

    struct LoadedSpec {
        std::string              desc;
        std::string              defaultEncoding;
        std::size_t              hdr_sz = 0;
        // Wurde das YAML-Root-Key "header" definiert? (Kann true sein bei
        // hdr_sz == 0 – ISOSpec::hasHeader() meldet die Präsenz des Keys,
        // der Parser behandelt 0 wie "kein Header".)
        bool                     headerKey = false;
        // [ISO8583] strikte Dekodierung (YAML-Root-Key `strict:`, Default: true)
        bool                     strict = true;
        std::map<int, SpecField> fields;
        // [ISO8583] 3.2 (Sicherheits-Audit): Content-Snapshot des Loads -
        // alle gelesenen Quelldateien + aggregierter SHA-256-Hash ueber
        // sie (von SourceMap::finalise() berechnet). Grundlage fuer das
        // Publish-then-Verify-Protokoll des Spec-Caches (E2).
        std::vector<std::string> sourceFiles;
        std::string              contentHash; // "sha256:<hex>"
    };

    static LoadedSpec loadAndParse(const std::string& path, const SpecLoadOptions& opts) {
        // Preprocessor läuft und baut gleichzeitig die SourceMap auf (sofern
        // trackSourceMap - siehe Kommentar bei preprocessWithSourceMap()).
        // Die Sidecar (.smap) wird automatisch geschrieben/validiert (seit
        // 0.3.0 zusätzlich durch Sandbox-Wurzeln + allowSmapWrite begrenzt).
        const auto pr = SpecPreProcessor::preprocessWithSourceMap(path, opts);
        const ryml::ConstNodeRef yaml = pr.tree.crootref();
        validateSpecYaml(yaml, &pr.source_map);

        LoadedSpec result;
        result.desc = getStr(yaml, "spec", "<unnamed>");
        result.hdr_sz = getSizeT(yaml, "header", 0);
        result.headerKey = hasKey(yaml, "header");
        result.strict = getBool(yaml, "strict", true);
        result.defaultEncoding = toUpper(getStr(yaml, "encoding", ""));

        for (ryml::ConstNodeRef entry : yaml["fields"].children()) {
            const auto de = toStdString(entry.key());
            const int  deNum = std::stoi(de);
            result.fields[deNum] = parseSpecField(
                entry, result.defaultEncoding, de, &pr.source_map);
        }
        // Content-Snapshot (Dateimenge + Hash): SourceMap::finalise() lief
        // unbedingt (unabhaengig von trackSourceMap) im Preprocessor.
        result.sourceFiles = std::move(pr.sourceFiles);
        result.contentHash = pr.source_map.hash();
        return result;
    }

    /// (0.6.0, FE-1) Field-only-Load: dieselbe Pipeline wie loadAndParse,
    /// aber validateFieldSpecYaml (exakt ein field:-Block; fields: und
    /// header: werden fail-closed abgewiesen). Das eine Feld wird als
    /// Key 0 geparst (bewusst der MTI-Key, s. spec_schema.md).
    /// hdr_sz/headerKey behalten ihre Default-Werte (header: wird vom
    /// Validator ohnehin abgewiesen).
    static LoadedSpec loadFieldAndParse(const std::string& path, const SpecLoadOptions& opts) {
        const auto pr = SpecPreProcessor::preprocessWithSourceMap(path, opts);
        const ryml::ConstNodeRef yaml = pr.tree.crootref();
        validateFieldSpecYaml(yaml, &pr.source_map);

        LoadedSpec result;
        result.desc = getStr(yaml, "spec", "<unnamed>");
        result.strict = getBool(yaml, "strict", true);
        result.defaultEncoding = toUpper(getStr(yaml, "encoding", ""));
        // Exakt ein Feld: Key 0 (s. Funktionsdokumentation).
        result.fields[0] = parseSpecField(
            yaml["field"], result.defaultEncoding, "0", &pr.source_map);
        result.sourceFiles = std::move(pr.sourceFiles);
        result.contentHash = pr.source_map.hash();
        return result;
    }

    /// Baut den ISOBaseParser aus einem LoadedSpec auf (geteilt von load* Funktionen).
    static std::shared_ptr<::TNG_NAMESPACE::ISOBaseParser>
        buildParser(const LoadedSpec& loaded)
    {
        auto parser = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(
            loaded.desc, loaded.hdr_sz);
        const int hf = loaded.fields.rbegin()->first;
        for (int i = 0; i <= hf; ++i) {
            parser->add(loaded.fields.count(i)
                ? buildFieldParser(loaded.fields.at(i))
                : std::make_shared<IF_NOP>());
        }
        // [ISO8583] strict-Modus (YAML-Key `strict:`, Default true) auf alle
        // Feld-Parserv inkl. verschachtelter Sub-Parserv propagieren.
        parser->strict(loaded.strict);
        return parser;
    }

    // =============================================================================
    // In-Prozess-Cache für loadFromYamlCached()/loadBothFromYamlCached()
    // =============================================================================
    // Gecached wird das FERTIGE Ergebnis (Parser bzw. Parser+ISOSpec), nicht
    // nur die YAML-Zwischenrepräsentation - ein Cache-Treffer kostet dadurch
    // nur noch einen mutex-geschützten Map-Lookup (plus last_write_time() bei
    // CheckEveryCall), statt jedes Mal neu zu parsen/prozessieren/aufzubauen.
    //
    // [ISO8583] 3.2 (Sicherheits-Audit, E2): TOCTOU-Härtung des Caches:
    //   - Jeder Eintrag speichert den CONTENT-SNAPSHOT der Version: mtime der
    //     Top-Datei + aggregierter SHA-256-Hash über ALLE Quelldateien der
    //     Version (Top-Level + alle Includes) + die Dateimenge selbst.
    //   - Publish-then-Verify: Ein neu geladener Parser wird NUR unter
    //     exakt jenem, gehashten Dateisnapshot veröffentlicht, aus dem er
    //     gebaut wurde (unmittelbar nach dem Load wird die Dateimenge
    //     erneut gehasht). Haben sich Dateien während des Loads geändert,
    //     wird der frische Eintrag NICHT gepublished (der nächste Caller
    //     lädt die neue Version) - der aufrufenden Seite wird aber der
    //     konsistente Snapshot zurückgegeben. Ein veröffentlichter Parser
    //     entspricht damit IMMER exakt einer kompletten Inhaltversion; eine
    //     zur Laufzeit ausgetauschte Spec-Datei kann keinen "gemischten"
    //     Parser mehr liefern.
    //   - CheckEveryCall: mtime-Pre-Filter (ein stat()-Systemaufruf, ~0.9 us)
    //     fängt die meisten Änderungen. Weicht die mtime, wird der
    //     Aggregat-Hash über die gecachte Dateimenge neu berechnet, um
    //     "Touch ohne Inhaltsänderung" (mtime neu, Hash identisch -> Eintrag
    //     bleibt gültig) von echten Änderungen (-> komplettes Reload) zu
    //     unterscheiden.
    //   - LRU-Cap: Der GESAMTE Cache hält maximal 64 Einträge (der am
    //     längsten nicht genutzte wird verworfen) - schützt vor unbeschränktem
    //     Wachstum bei vielen verschiedenen Spec-Pfaden.
    //
    // Ein EINZIGER zusammengeführter Cache (Parser UND ISOSpec in einem
    // Eintrag): Die Parser-Variante füllt `spec` mit null; trifft
    // loadBothFromYamlCached auf einen nur-parser-Eintrag, lädt sie neu und
    // UPGRADET den Eintrag (gleicher Content-Snapshot, gleiche Hash-Identität).
    struct SpecCacheEntry {
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser;
        ISOSpec::SmartPtr        spec;    // null, wenn nur der Parser geladen
        std::filesystem::file_time_type mtime;   // Top-Datei bei Publication
        std::vector<std::string>  files;   // Snapshot-Dateimenge (absolut)
        std::string               contentHash; // "sha256:<hex>" (aggregiert)
        std::atomic<uint64_t>     lastUse{0};  // LRU-Rang (lock-freies Touch)
    };

    static constexpr std::size_t kSpecCacheMaxEntries = 64;

    static std::shared_mutex& specCacheMutex() {
        static std::shared_mutex m;
        return m;
    }
    static std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>>& specCache() {
        static std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>> cache;
        return cache;
    }

    // (0.6.0, FE-1) Die Field-only-Pfade haben eine eigene Cache-Instanz
    // (gleiche Shape + TOCTOU-Protokoll wie specCache, eigenes
    // Mutex/Map/LRU-Budget): Eine Datei darf nicht gleichzeitig als
    // Message-Spec und Field-only-Spec ohne Invalidation geladen werden
    // (Dual-Cache, s. spec_schema.md "Field-only-Specs").
    static std::shared_mutex& fieldSpecCacheMutex() {
        static std::shared_mutex m;
        return m;
    }
    static std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>>& fieldSpecCache() {
        static std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>> cache;
        return cache;
    }

    // Monotoner Zähler für die LRU-Reihung - ein relaxed atomic store unter
    // der shared lock ist race-frei (und erlaubt Hits ohne Exklusivlock);
    // für die Eviktionsentscheidung genügt die grobe Nutzungsreihenfolge.
    static std::atomic<uint64_t>& lruCounter() {
        static std::atomic<uint64_t> c{0};
        return c;
    }

    static void touchLru(SpecCacheEntry& e) {
        e.lastUse.store(lruCounter().fetch_add(1, std::memory_order_relaxed),
                        std::memory_order_relaxed);
    }

    /// Verwirft den am längsten nicht genutzten Eintrag, wenn der Cache voll
    /// ist (AUSSCHLIESSLICH unter exklusive Sperre aufrufen).
    static void evictIfFull(
        std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>>& cache)
    {
        if (cache.size() < kSpecCacheMaxEntries)
            return;
        std::string oldestKey;
        uint64_t    oldestUse = std::numeric_limits<uint64_t>::max();
        bool        found = false;
        for (const auto& [k, e] : cache)
            if (e->lastUse.load(std::memory_order_relaxed) < oldestUse) {
                oldestUse = e->lastUse.load(std::memory_order_relaxed);
                oldestKey = k;
                found = true;
            }
        if (found) {
            cache.erase(oldestKey);
            TNG_LOG_DEBUG("[SpecCache] LRU-Eviction: '{}' ({} Einträge übrig)",
                oldestKey, cache.size());
        }
    }

    /// Anführungspfad->last_write_time(); wirft NICHT bei fehlender Datei -
    /// gibt stattdessen einen "immer ungültigen" Zeitstempel zurück, damit der
    /// eigentliche (aussagekräftige) Dateifehler beim normalen Ladepfad auftritt,
    /// statt hier eine zweite, redundante Fehlermeldung zu produzieren.
    static std::filesystem::file_time_type tryGetMTime(const std::string& absPath) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(absPath, ec);
        return ec ? std::filesystem::file_time_type::min() : t;
    }

    // =============================================================================
    // Cache-Logik: Hit-Lookup, Bundle-Load, Publish-then-Verify
    // =============================================================================

    /// Cache-Lookup gemäß Validierungspolicy. Liefert den Treffer-Eintrag
    /// (oder null). `needSpec`: für loadBothFromYamlCached zählt ein
    /// nur-parser-Eintrag als Verfehlung.
    /// (0.6.0, FE-1) `mtx`/`cacheMap` wahlen die Cache-Instanz: der
    /// Message-Pfad (specCache) und der Field-only-Pfad (fieldSpecCache)
    /// teilen dieselbe verifizierte TOCTOU-Logik.
    static std::shared_ptr<SpecCacheEntry> lookupCacheHit(
        const std::string& absPath, CacheValidation validation, bool needSpec,
        std::shared_mutex& mtx,
        std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>>& cacheMap)
    {
        if (validation == CacheValidation::TrustUntilInvalidated) {
            // Kein last_write_time()-Aufruf (Systemaufruf, ~0.9 us gemessen) -
            // ein Cache-Treffer ist hier nur noch Map-Lookup + shared_ptr-Kopie
            // (~25 ns). Erkennt Dateiänderungen NICHT automatisch - siehe
            // Doku bei CacheValidation/invalidateCache().
            std::shared_lock lock(mtx);
            auto it = cacheMap.find(absPath);
            if (it != cacheMap.end() && (!needSpec || it->second->spec)) {
                touchLru(*it->second);
                TNG_LOG_DEBUG("[SpecDecoder] loadCached '{}' – Cache-Treffer (ungeprüft)", absPath);
                return it->second;
            }
            return nullptr;
        }

        // CheckEveryCall: mtime-Pre-Filter (ein stat()-Systemaufruf).
        const auto mtime = tryGetMTime(absPath);

        // Snapshot-Daten des gecachten Eintrags (für den Fall, dass die mtime
        // abweicht und neu verifiziert werden muss) - Füllung unter shared
        // lock, die teure Hash-Nachverifikation selbst AUSSERHALB des Locks.
        std::vector<std::string>        verifyFiles;
        std::string                     verifyHash;
        std::filesystem::file_time_type verifyMtime{};
        std::shared_ptr<SpecCacheEntry> hit;
        bool                            reverify = false;

        {
            std::shared_lock lock(mtx);
            auto it = cacheMap.find(absPath);
            if (it == cacheMap.end())
                return nullptr;
            auto& entry = *it->second;
            if (entry.mtime == mtime) {
                touchLru(entry);
                TNG_LOG_DEBUG("[SpecDecoder] loadCached '{}' – Cache-Treffer", absPath);
                if (!needSpec || entry.spec)
                    return it->second;
                // nur-parser-Eintrag, aber ISOSpec gewünscht: Reload, der den
                // Eintrag upgradet (gleicher Snapshot - s. publishCacheEntry).
                return nullptr;
            }

            // [E2] mtime hat sich geändert (oder die Datei fehlt):
            verifyFiles = entry.files;
            verifyHash  = entry.contentHash;
            verifyMtime = mtime;
            hit         = it->second;
            reverify    = true;
        }

        if (!reverify)
            return nullptr;

        // Distinguierung "Touch ohne Inhaltsänderung" vs. echte Änderung:
        // Aggregat-Hash über die Dateimenge des gecachten Snapshots neu
        // berechnen (liest die Quelldateien erneut - alle sind durch
        // maxSpecBytes begrenzt).
        const std::string rehash = hash_files(verifyFiles);
        if (rehash != verifyHash)
            return nullptr;  // echte Änderung -> Reload

        // Nur ein "Touch": Die EINTRAG-mtime aktualisieren - aber nur, falls
        // der Eintrag nicht zwischenzeitlich durch ein paralleles Load
        // ERSETZT wurde (in dem Fall ist der NEUE Eintrag aktuell).
        std::shared_ptr<SpecCacheEntry> out;
        {
            std::unique_lock ulock(mtx);
            auto cur = cacheMap.find(absPath);
            if (cur == cacheMap.end())
                return nullptr;   // zwischenzeitlich eviziert -> Reload
            if (cur->second == hit) {
                cur->second->mtime = verifyMtime;
                touchLru(*cur->second);
                out = cur->second;
            }
            else if (!needSpec || cur->second->spec) {
                touchLru(*cur->second);
                out = cur->second;
            }
        }
        if (out && (!needSpec || out->spec))
            return out;
        return nullptr;
    }

    // Komplettes Load + Build (ohne Caching) - geteilt von den uncached
    // load*-Funktionen und dem Cache-Verfehlungs-Pfad.
    struct LoadedBundle {
        LoadedSpec loaded;   // inkl. sourceFiles + contentHash
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser;
        ISOSpec::SmartPtr spec;   // null, wenn !wantSpec
        std::filesystem::file_time_type mtime;  // Top-Datei, NACH dem Load
    };

    static LoadedBundle loadBundle(const std::string& path,
        const SpecLoadOptions& opts, bool wantSpec)
    {
        LoadedBundle b;
        b.loaded = loadAndParse(path, opts);
        b.parser = buildParser(b.loaded);
        b.mtime  = tryGetMTime(std::filesystem::absolute(path).string());
        if (wantSpec) {
            std::vector<SpecFieldInfo> infos;
            infos.reserve(b.loaded.fields.size());
            for (const auto& [key, f] : b.loaded.fields)
                infos.push_back(makeSpecFieldInfo(static_cast<TNG_KEY_TYPE>(key), f));
            const std::optional<std::size_t> hdr =
                b.loaded.headerKey ? std::make_optional(b.loaded.hdr_sz) : std::nullopt;
            b.spec = std::make_shared<ISOSpec>(
                b.loaded.desc, b.loaded.defaultEncoding, std::move(infos), std::move(hdr));
        }
        return b;
    }

    // (0.6.0, FE-1) Field-only-Bundle: gleiche Shape wie loadBundle, aber
    // der Parser kommt aus buildFieldBlockParser (das eine Feld als
    // Container-Top-Parser bzw. TLV/NESTED direkt) und die ISOSpec hat
    // exakt ein SpecFieldInfo (Key 0, ohne Header).
    static LoadedBundle loadFieldBundle(const std::string& path,
        const SpecLoadOptions& opts, bool wantSpec)
    {
        LoadedBundle b;
        b.loaded = loadFieldAndParse(path, opts);
        const auto& f = b.loaded.fields.at(0);
        b.parser = buildFieldBlockParser(f, b.loaded.defaultEncoding);
        b.mtime  = tryGetMTime(std::filesystem::absolute(path).string());
        if (wantSpec) {
            std::vector<SpecFieldInfo> infos;
            infos.emplace_back(makeSpecFieldInfo(static_cast<TNG_KEY_TYPE>(0), f));
            b.spec = std::make_shared<ISOSpec>(
                b.loaded.desc, b.loaded.defaultEncoding,
                std::move(infos), std::nullopt);
        }
        return b;
    }

    /// [E2] Publish-then-Verify: Den neuen Eintrag NUR unter exakt jenem
    /// gehashten Snapshot veröffentlichen, aus dem der Parser gebaut wurde.
    /// `consistent` = Ergebnis der Nach-Hashung (die Dateimenge ist zum
    /// Zeitpunkt der Veröffentlichung unverändert geblieben). Bei
    /// Inconsistenz wird NICHT gepublished - der nächste Aufruf lädt die neue
    /// Version; der aktuelle Caller erhält den (konsistenten) Snapshot, den
    /// er geladen hat.
    /// (0.6.0, FE-1) `mtx`/`cacheMap` wahlen die Cache-Instanz (wie oben).
    static void publishCacheEntry(const std::string& absPath,
        const LoadedBundle& b, bool consistent,
        std::shared_mutex& mtx,
        std::unordered_map<std::string, std::shared_ptr<SpecCacheEntry>>& cacheMap)
    {
        std::unique_lock lock(mtx);
        auto& cache = cacheMap;
        auto it = cache.find(absPath);
        if (it != cache.end() && it->second->contentHash == b.loaded.contentHash) {
            // Paralleles Load derselben Inhaltsversion: bereits publizierten
            // Eintrag WIEDERVERWENDEN (De-Duplizierung - stabiles shared_ptr
            // über parallele Loads). Ein nur-parser-Eintrag wird dabei
            // upgraded, falls dieses Load auch das ISOSpec mitbringt.
            if (b.spec && !it->second->spec)
                it->second->spec = b.spec;
            touchLru(*it->second);
            TNG_LOG_DEBUG("[SpecDecoder] loadCached '{}' – paralleles Load mit identischem Snapshot, Eintrag wiederverwendet", absPath);
            return;
        }
        if (!consistent) {
            TNG_LOG_DEBUG("[SpecCache] '{}' geändert während des Loads – frischer Eintrag NICHT gepublished",
                absPath);
            return;
        }
        evictIfFull(cache);
        auto e = std::make_shared<SpecCacheEntry>();
        e->parser      = b.parser;
        e->spec        = b.spec;
        e->mtime       = b.mtime;
        e->files       = b.loaded.sourceFiles;
        e->contentHash = b.loaded.contentHash;
        touchLru(*e);
        cache[absPath] = e;
        TNG_LOG_DEBUG("[SpecCache] '{}' gepublished ({} Felder, {} Dateien)",
            absPath, b.loaded.fields.size(), e->files.size());
    }

    /// Kern der Cached-Varianten: Hit-Lookup (policy-abhängig), sonst
    /// komplettes Load + Build, danach publish-then-verify (bzw.
    /// De-Duplizierung eines identischen Snapshots).
    static std::pair<::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
                     ISOSpec::SmartPtr>
        loadCachedBundle(const std::string& path, const SpecLoadOptions& opts,
                         CacheValidation validation, bool wantSpec,
                         bool fieldOnly)
    {
        const auto absPath = std::filesystem::absolute(path).string();
        // (0.6.0, FE-1) Cache-Selektor: Message-Pfad und Field-only-Pfad
        // teilen dieselbe verifizierte TOCTOU-Logik, nur die
        // Cache-Instanz unterscheidet sich (specCache vs. fieldSpecCache).
        auto& cacheMtx = fieldOnly ? fieldSpecCacheMutex() : specCacheMutex();
        auto& cacheMap = fieldOnly ? fieldSpecCache() : specCache();

        // 1) Cache-Lookup gemäß Policy (ggf. inkl. Hash-Re-Verifikation bei
        //    CheckEveryCall).
        if (auto hit = lookupCacheHit(absPath, validation, wantSpec, cacheMtx, cacheMap))
            return { hit->parser, hit->spec };

        // 2) Verfehlung: komplettes Load + Build (der Read selbst ist die
        //    "Verify"-Phase - ein Parser ist nur aus einem vollständigen
        //    Read buildbar).
        LoadedBundle b;
        try {
            b = fieldOnly
                ? loadFieldBundle(path, opts, wantSpec)
                : loadBundle(path, opts, wantSpec);
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadCached('{}') fehlgeschlagen: {}", path, e.what());
            throw;
        }

        // 3) [E2] Nach-Hashung: Ist der Disk-Zustand noch identisch mit dem
        //    Snapshot, aus dem der Parser gebaut wurde?
        const bool consistent =
            (hash_files(b.loaded.sourceFiles) == b.loaded.contentHash);

        // 4) Veröffentlichen (ggf. De-Duplizierung) - oder verwerfen bei
        //    Inconsistenz.
        publishCacheEntry(absPath, b, consistent, cacheMtx, cacheMap);

        // 5) Hat ein paralleler Caller die selbe Version bereits publiziert,
        //    dessen Eintrag wiederverwenden (stabiles shared_ptr).
        {
            std::shared_lock lock(cacheMtx);
            auto it = cacheMap.find(absPath);
            if (it != cacheMap.end()
                && it->second->contentHash == b.loaded.contentHash
                && (!wantSpec || it->second->spec))
                return { it->second->parser, it->second->spec };
        }
        return { b.parser, b.spec };
    }

    // =============================================================================
    // ISOSpec – öffentliche Methoden
    // =============================================================================

    std::optional<SpecFieldInfo> ISOSpec::field(TNG_KEY_TYPE key) const {
        for (const auto& f : fields_)
            if (f.key == key) return f;
        return std::nullopt;
    }

    bool ISOSpec::has(TNG_KEY_TYPE key) const noexcept {
        for (const auto& f : fields_)
            if (f.key == key) return true;
        return false;
    }

    // =============================================================================
    // SpecDecoder – öffentliche Methoden
    // =============================================================================

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFromYaml(const std::string& path, bool trackSourceMap)
    {
        SpecLoadOptions opts; opts.trackSourceMap = trackSourceMap;
        return loadFromYaml(path, opts);
    }

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFromYaml(const std::string& path, const SpecLoadOptions& opts)
    {
        try {
            const auto b = loadBundle(path, opts, false);
            TNG_LOG_INFO("[SpecDecoder] loadFromYaml '{}' – {} Felder, header={}B",
                b.loaded.desc, b.loaded.fields.size(), b.loaded.hdr_sz);
            return b.parser;
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadFromYaml '{}' fehlgeschlagen: {}", path, e.what());
            throw;
        }
    }

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFromYamlCached(const std::string& path, bool trackSourceMap,
            CacheValidation validation)
    {
        SpecLoadOptions opts; opts.trackSourceMap = trackSourceMap;
        return loadFromYamlCached(path, opts, validation);
    }

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFromYamlCached(const std::string& path,
            const SpecLoadOptions& opts, CacheValidation validation)
    {
        return loadCachedBundle(path, opts, validation,
            /*wantSpec=*/false, /*fieldOnly=*/false).first;
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYaml(const std::string& path, bool trackSourceMap)
    {
        SpecLoadOptions opts; opts.trackSourceMap = trackSourceMap;
        return loadBothFromYaml(path, opts);
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYaml(const std::string& path, const SpecLoadOptions& opts)
    {
        try {
            const auto b = loadBundle(path, opts, true);
            TNG_LOG_INFO("[SpecDecoder] loadBothFromYaml '{}' – {} Felder",
                b.loaded.desc, b.loaded.fields.size());
            return { b.parser, b.spec };
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadBothFromYaml '{}' fehlgeschlagen: {}", path, e.what());
            throw;
        }
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYamlCached(const std::string& path, bool trackSourceMap,
            CacheValidation validation)
    {
        SpecLoadOptions opts; opts.trackSourceMap = trackSourceMap;
        return loadBothFromYamlCached(path, opts, validation);
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYamlCached(const std::string& path,
            const SpecLoadOptions& opts, CacheValidation validation)
    {
        return loadCachedBundle(path, opts, validation,
            /*wantSpec=*/true, /*fieldOnly=*/false);
    }

    void SpecDecoder::invalidateCache(const std::string& path) {
        const auto absPath = std::filesystem::absolute(path).string();
        std::unique_lock lock(specCacheMutex());
        specCache().erase(absPath);
    }

    void SpecDecoder::clearCache() {
        std::unique_lock lock(specCacheMutex());
        specCache().clear();
    }


    // =============================================================================
    // (0.6.0, FE-1) Field-only-Specs: öffentliche loadField*-Eintritte
    // =============================================================================

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFieldFromYaml(const std::filesystem::path& path)
    {
        return loadFieldFromYaml(path, SpecLoadOptions{});
    }

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFieldFromYaml(const std::filesystem::path& path,
            const SpecLoadOptions& opts)
    {
        try {
            const auto b = loadFieldBundle(path.string(), opts, /*wantSpec=*/false);
            TNG_LOG_INFO("[SpecDecoder] loadFieldFromYaml '{}' - {}", path.string(), b.loaded.desc);
            return b.parser;
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadFieldFromYaml '{}' fehlgeschlagen: {}", path.string(), e.what());
            throw;
        }
    }

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFieldFromYamlCached(const std::filesystem::path& path,
            const SpecLoadOptions& opts, CacheValidation validation)
    {
        return loadCachedBundle(path.string(), opts, validation,
            /*wantSpec=*/false, /*fieldOnly=*/true).first;
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadFieldBothFromYaml(const std::filesystem::path& path)
    {
        return loadFieldBothFromYaml(path, SpecLoadOptions{});
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadFieldBothFromYaml(const std::filesystem::path& path,
            const SpecLoadOptions& opts)
    {
        try {
            const auto b = loadFieldBundle(path.string(), opts, /*wantSpec=*/true);
            TNG_LOG_INFO("[SpecDecoder] loadFieldBothFromYaml '{}' - {}", path.string(), b.loaded.desc);
            return { b.parser, b.spec };
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadFieldBothFromYaml '{}' fehlgeschlagen: {}", path.string(), e.what());
            throw;
        }
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadFieldBothFromYamlCached(const std::filesystem::path& path,
            const SpecLoadOptions& opts, CacheValidation validation)
    {
        return loadCachedBundle(path.string(), opts, validation,
            /*wantSpec=*/true, /*fieldOnly=*/true);
    }

    // (0.6.0, FE-1) Decode-Komfort-API: der Field-only-Parser laeuft auf
    // exakt den Bytes, die ein BinaryField haelt (z. B. aus einer
    // Message-Decode entnommen); das Ergebnis ist eine synthetisch
    // leere ISOMessage (kein MTI/Bitmap), die der Parser direkt befuellt.
    ::TNG_NAMESPACE::ISOMessage::ISOMessageSmartPtr
        SpecDecoder::decodeField(const ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p,
            const ::TNG_NAMESPACE::BinaryField& field)
    {
        if (!p)
            throw std::runtime_error("SpecDecoder::decodeField: parser ist nullptr");
        auto msg = std::make_shared<::TNG_NAMESPACE::ISOMessage>();
        msg->parser(p);
        msg->unparse(msg, field.value());
        return msg;
    }

    void SpecDecoder::invalidateFieldCache(const std::filesystem::path& path) {
        const auto absPath = std::filesystem::absolute(path).string();
        std::unique_lock lock(fieldSpecCacheMutex());
        fieldSpecCache().erase(absPath);
    }

    void SpecDecoder::clearFieldCache() {
        std::unique_lock lock(fieldSpecCacheMutex());
        fieldSpecCache().clear();
    }
} // namespace TNG_NAMESPACE::spec
