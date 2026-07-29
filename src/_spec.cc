#include "_spec.hh"

// [stdc++]
#include <algorithm>
#include <cctype>
#include <filesystem>
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
    static bool isEncodingNeutral(const std::string& fmt) {
        static const std::unordered_set<std::string> neutral = {
            "BINARY", "BITMAP", "NOP", "UNUSED", "REMAINING"
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
            "type", "format", "encoding", "length", "description", "children", "tlv"
        };
        for (ryml::ConstNodeRef child : node.children()) {
            const auto key = toStdString(child.key());
            if (!allowed.count(key))
                throw SpecValidationError(
                    "Unbekannter Schlüssel '" + key + "' im Feld " + de,
                    child.id(), smap);
        }
    }

    static void validateSpecYaml(ryml::ConstNodeRef root, const SourceMap* smap = nullptr) {
        // Läuft auf dem BEREITS PREPROCESSIERTEN YAML – !template, !merge, !use
        // wurden bereits expandiert.

        if (!hasKey(root, "fields"))
            throw std::runtime_error("Fehlender Abschnitt 'fields' in YAML.");  // keine Position verfügbar

        for (ryml::ConstNodeRef entry : root["fields"].children()) {
            const auto key = toStdString(entry.key());

            if (!std::all_of(key.begin(), key.end(), ::isdigit))
                throw SpecValidationError(
                    "Feldschlüssel '" + key + "' ist nicht numerisch", entry.id(), smap);

            ryml::ConstNodeRef field = entry;
            if (!field.is_map()) continue;

            // Warnung wenn length für nicht-triviale Formate fehlt
            if (hasKey(field, "format")) {
                const auto fmt = toLower(getStr(field, "format"));
                const bool needsLength = (fmt != "nop" && fmt != "bitmap" &&
                    fmt != "unused" && fmt != "remaining");
                if (needsLength && !hasKey(field, "length"))
                    TNG_LOG_WARN("[SpecDecoder] Feld {} hat format='{}' aber kein 'length'",
                        key, fmt);
            }

            // 'format: ...bertlv' ist eine reine Kurzschreibweise für scalare
            // Felder (siehe parseSpecField) - BER-TLV-Tags sind dynamisch, eine
            // vorab deklarierte Kinderliste ergibt keinen Sinn. Explizites
            // 'type: nested', 'children' oder ein eigener 'tlv:'-Block wären
            // daher widersprüchlich und werden hier abgelehnt.
            if (hasKey(field, "format")) {
                const auto fmtUpper = toUpper(getStr(field, "format"));
                std::size_t p = 0;
                while (p < fmtUpper.size() && fmtUpper[p] == 'L') ++p;
                if (fmtUpper.substr(p) == "BERTLV") {
                    const bool explicitNested = hasKey(field, "type") &&
                        toLower(getStr(field, "type")) == "nested";
                    if (hasKey(field, "children") || hasKey(field, "tlv") || explicitNested)
                        throw SpecValidationError(
                            "Feld " + key + ": 'format: ...bertlv' ist nur bei "
                            "scalaren Feldern gültig - 'children', 'tlv' und "
                            "'type: nested' dürfen nicht zusätzlich gesetzt sein",
                            field.id(), smap);
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
        // Nur bei scalaren Feldern gültig (siehe validateSpecYaml für die
        // entsprechende Exklusivitätsprüfung gegen type/children/tlv). Anders
        // als bei Mastercard/Visa-TLV (fixe, vorab bekannte SE-Liste über
        // 'children') sind BER-TLV/EMV-Tags dynamisch - eine Kinderliste ergibt
        // hier keinen Sinn. Es genügt also z.B.:
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
                    f.tlv_children[seNum] = parseSpecField(entry, seEnc, seKey, smap, depth + 1);
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

        static const std::unordered_map<std::string, ParserFactory> table = {
            // ── Encoding-unabhängig ──────────────────────────────────────────────
            { "BITMAP|",           MAKE(IFB_BITMAP)     },
            { "NOP|",              MAKE_NOP()            },
            { "UNUSED|",           MAKE_NOP()            },
            { "REMAINING|",        MAKE(IF_REMAINING)    },
            { "REMAINING|BINARY",  MAKE(IF_REMAINING)    },
            { "REMAINING|EBCDIC",  MAKE(IFE_REMAINING)   },
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
            // ── BCD ──────────────────────────────────────────────────────────────
            { "NUMERIC|BCD",       MAKE(IFB_NUMERIC)     },
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
            { "LNUM|EBCDIC",       MAKE(IFE_LNUM)        },
            { "CHAR|EBCDIC",       MAKE(IFE_CHAR)        },
            { "NOPAD_CHAR|EBCDIC", MAKE(IFE_NOPAD_CHAR)  },
            { "LCHAR|EBCDIC",      MAKE(IFE_LCHAR)       },
            { "LLCHAR|EBCDIC",     MAKE(IFE_LLCHAR)      },
            { "LLLCHAR|EBCDIC",    MAKE(IFE_LLLCHAR)     },
        };

#undef MAKE
#undef MAKE_NOP
        return table;
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
            const std::unordered_map<std::size_t, std::string>& descriptionMap)
    {
        using namespace ::TNG_NAMESPACE;

        // ── BER-TLV: variable Tag-/Length-Länge, kein TCC ─────────────────────
        if (ber)
            return std::make_shared<BERTLVParser>(BERTLVParser::DataEncodingMap{}, descriptionMap);

        // ── Feste Byte-Anzahl (bisheriges Verhalten, jetzt über
        //    FixedNumericTag/FixedNumericLength statt der ursprünglichen
        //    4 Template-Parameter TAG_BYTES/LEN_BYTES/TAG_ENC/LEN_ENC) ────────
#define MAKE_FIXED_TLV(TB, LB, HAS_TCC, ENC) \
        std::make_shared<ISOTLVParser< \
            FixedNumericTag<TB, codec::Encoder::ENC>, \
            FixedNumericLength<LB, codec::Encoder::ENC>, \
            HAS_TCC, codec::Encoder::ENC>>( \
                ISOTLVParser< \
                    FixedNumericTag<TB, codec::Encoder::ENC>, \
                    FixedNumericLength<LB, codec::Encoder::ENC>, \
                    HAS_TCC, codec::Encoder::ENC>::DataEncodingMap{}, \
                descriptionMap)

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
            false, codec::Encoder::EBCDIC>>(
                ISOTLVParser<
                    FixedNumericTag<2, codec::Encoder::EBCDIC>,
                    FixedNumericLength<2, codec::Encoder::EBCDIC>,
                    false, codec::Encoder::EBCDIC>::DataEncodingMap{},
                descriptionMap);
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr
        buildFieldParser(const SpecField& f)
    {
        switch (f.type) {
        case SpecFieldType::SCALAR:
            return createScalarParser(f);

        case SpecFieldType::NESTED: {
            auto base = createScalarParser(f);
            auto nested = std::make_shared<
                ::TNG_NAMESPACE::ISONestedFieldParser<::TNG_NAMESPACE::ISOBaseParser>>(
                    base, f.description);

            if (f.tlv) {
                const auto& opts = *f.tlv;
                const auto enc = [&] {
                    if (opts.encoding == "BCD")   return codec::Encoder::BCD;
                    if (opts.encoding == "ASCII")  return codec::Encoder::ASCII;
                    return codec::Encoder::EBCDIC;
                    }();

                // Aus 'children' deklarierte Beschreibungen an den Laufzeit-
                // Parser weiterreichen (siehe description_for() in
                // ISOTLVParser) - macht 'children: <tag>: {description: ...}'
                // erstmals tatsächlich wirksam, statt rein dokumentarisch zu
                // sein. Format/Typisierung pro Tag bleiben bewusst
                // zurückgestellt (siehe Konversation) - jedes SE/Tag wird
                // weiterhin als BinaryField dekodiert, nur die Beschreibung
                // wird aus der Spec übernommen.
                std::unordered_map<std::size_t, std::string> descriptionMap;
                for (const auto& [tag, child] : f.tlv_children)
                    if (child.has_explicit_description)
                        descriptionMap[static_cast<std::size_t>(tag)] = child.description;

                nested->subParser(makeTlvParser(opts.tag_bytes, opts.len_bytes, opts.tcc, enc, opts.ber, descriptionMap));
            }
            else {
                auto sub = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(f.description);
                for (const auto& child : f.children)
                    sub->add(createScalarParser(child));
                nested->subParser(sub);
            }
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

        if (fmt.type == "NOP" || fmt.type == "UNUSED" || fmt.type == "REMAINING")
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

        return info;
    }

    // =============================================================================
    // YAML laden und vorverarbeiten
    // =============================================================================

    struct LoadedSpec {
        std::string              desc;
        std::string              defaultEncoding;
        std::size_t              hdr_sz = 0;
        std::map<int, SpecField> fields;
    };

    static LoadedSpec loadAndParse(const std::string& path, bool trackSourceMap) {
        // Preprocessor läuft und baut gleichzeitig die SourceMap auf (sofern
        // trackSourceMap - siehe Kommentar bei preprocessWithSourceMap()).
        // Die Sidecar (.smap) wird automatisch geschrieben/validiert.
        auto [tree, smap] = SpecPreProcessor::preprocessWithSourceMap(path, trackSourceMap);
        ryml::ConstNodeRef yaml = tree.crootref();
        validateSpecYaml(yaml, &smap);

        LoadedSpec result;
        result.desc = getStr(yaml, "spec", "<unnamed>");
        result.hdr_sz = getSizeT(yaml, "header", 0);
        result.defaultEncoding = toUpper(getStr(yaml, "encoding", ""));

        for (ryml::ConstNodeRef entry : yaml["fields"].children()) {
            const auto de = toStdString(entry.key());
            const int  deNum = std::stoi(de);
            result.fields[deNum] = parseSpecField(
                entry, result.defaultEncoding, de, &smap);
        }
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
        return parser;
    }

    // =============================================================================
    // In-Prozess-Cache für loadFromYamlCached()/loadBothFromYamlCached()
    // =============================================================================
    // Gecached wird das FERTIGE Ergebnis (Parser bzw. Parser+ISOSpec), nicht nur
    // die YAML-Zwischenrepräsentation - ein Cache-Treffer kostet dadurch nur
    // noch einen mutex-geschützten Map-Lookup + einen last_write_time()-Aufruf,
    // statt jedes Mal neu zu parsen/prozessieren/aufzubauen. Invalidierung über
    // die Datei-Modifikationszeit (kein Datei-Inhalts-Hash - last_write_time()
    // ist praktisch kostenlos, ein Hash würde die Datei erneut lesen).
    //
    // Zwei getrennte Caches (statt einem gemeinsamen): vermeidet den Sonderfall
    // "für denselben Pfad wurde vorher nur der Parser gecacht, jetzt wird aber
    // auch das ISOSpec gebraucht" - beide Cache-Varianten sind unabhängig
    // voneinander bef üllt, auf Kosten von im Edge-Case doppelt gecachten Daten
    // für Pfade, die über BEIDE Funktionen geladen werden.
    struct ParserCacheEntry {
        std::filesystem::file_time_type mtime;
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser;
    };
    struct BothCacheEntry {
        std::filesystem::file_time_type mtime;
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser;
        ISOSpec::SmartPtr spec;
    };

    static std::shared_mutex& parserCacheMutex() {
        static std::shared_mutex m;
        return m;
    }
    static std::unordered_map<std::string, ParserCacheEntry>& parserCache() {
        static std::unordered_map<std::string, ParserCacheEntry> cache;
        return cache;
    }
    static std::shared_mutex& bothCacheMutex() {
        static std::shared_mutex m;
        return m;
    }
    static std::unordered_map<std::string, BothCacheEntry>& bothCache() {
        static std::unordered_map<std::string, BothCacheEntry> cache;
        return cache;
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
        try {
            const auto loaded = loadAndParse(path, trackSourceMap);
            auto parser = buildParser(loaded);
            TNG_LOG_INFO("[SpecDecoder] loadFromYaml '{}' – {} Felder, header={}B",
                loaded.desc, loaded.fields.size(), loaded.hdr_sz);
            return parser;
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
        const auto absPath = std::filesystem::absolute(path).string();

        if (validation == CacheValidation::TrustUntilInvalidated) {
            // Kein last_write_time()-Aufruf (Systemaufruf, ~0.9 us gemessen) -
            // ein Cache-Treffer ist hier nur noch Map-Lookup + shared_ptr-Kopie
            // (~25 ns). Erkennt Dateiänderungen NICHT automatisch - siehe
            // Doku bei CacheValidation/invalidateCache().
            std::shared_lock lock(parserCacheMutex());
            auto it = parserCache().find(absPath);
            if (it != parserCache().end()) {
                TNG_LOG_DEBUG("[SpecDecoder] loadFromYamlCached '{}' – Cache-Treffer (ungeprüft)", absPath);
                return it->second.parser;
            }
        }
        else {
            const auto mtime = tryGetMTime(absPath);
            std::shared_lock lock(parserCacheMutex());
            auto it = parserCache().find(absPath);
            if (it != parserCache().end() && it->second.mtime == mtime) {
                TNG_LOG_DEBUG("[SpecDecoder] loadFromYamlCached '{}' – Cache-Treffer", absPath);
                return it->second.parser;
            }
        }

        auto parser = loadFromYaml(path, trackSourceMap);
        const auto mtime = tryGetMTime(absPath);

        std::unique_lock lock(parserCacheMutex());
        parserCache()[absPath] = ParserCacheEntry{ mtime, parser };
        return parser;
    }

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYaml(const std::string& path, bool trackSourceMap)
    {
        try {
            const auto loaded = loadAndParse(path, trackSourceMap);
            auto parser = buildParser(loaded);

            std::vector<SpecFieldInfo> infos;
            infos.reserve(loaded.fields.size());
            for (const auto& [key, f] : loaded.fields)
                infos.push_back(makeSpecFieldInfo(static_cast<TNG_KEY_TYPE>(key), f));

            auto spec = std::make_shared<ISOSpec>(
                loaded.desc, loaded.defaultEncoding, std::move(infos));

            TNG_LOG_INFO("[SpecDecoder] loadBothFromYaml '{}' – {} Felder",
                loaded.desc, loaded.fields.size());
            return { parser, spec };
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
        const auto absPath = std::filesystem::absolute(path).string();

        if (validation == CacheValidation::TrustUntilInvalidated) {
            std::shared_lock lock(bothCacheMutex());
            auto it = bothCache().find(absPath);
            if (it != bothCache().end()) {
                TNG_LOG_DEBUG("[SpecDecoder] loadBothFromYamlCached '{}' – Cache-Treffer (ungeprüft)", absPath);
                return { it->second.parser, it->second.spec };
            }
        }
        else {
            const auto mtime = tryGetMTime(absPath);
            std::shared_lock lock(bothCacheMutex());
            auto it = bothCache().find(absPath);
            if (it != bothCache().end() && it->second.mtime == mtime) {
                TNG_LOG_DEBUG("[SpecDecoder] loadBothFromYamlCached '{}' – Cache-Treffer", absPath);
                return { it->second.parser, it->second.spec };
            }
        }

        auto [parser, spec] = loadBothFromYaml(path, trackSourceMap);
        const auto mtime = tryGetMTime(absPath);

        std::unique_lock lock(bothCacheMutex());
        bothCache()[absPath] = BothCacheEntry{ mtime, parser, spec };
        return { parser, spec };
    }

    void SpecDecoder::invalidateCache(const std::string& path) {
        const auto absPath = std::filesystem::absolute(path).string();
        {
            std::unique_lock lock(parserCacheMutex());
            parserCache().erase(absPath);
        }
        {
            std::unique_lock lock(bothCacheMutex());
            bothCache().erase(absPath);
        }
    }

    void SpecDecoder::clearCache() {
        {
            std::unique_lock lock(parserCacheMutex());
            parserCache().clear();
        }
        {
            std::unique_lock lock(bothCacheMutex());
            bothCache().clear();
        }
    }

} // namespace TNG_NAMESPACE::spec
