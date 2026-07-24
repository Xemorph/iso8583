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
// [yaml-cpp]
#include <yaml-cpp/yaml.h>
// [tng/internal]
#include "_logger.hh"
#include "_parser.hh"
#include "_preprocessor.hh"
#include "_sourcemap.hh"
#include "_tlv.hh"
#include "fmt_types.hh"

namespace TNG_NAMESPACE::spec {

    // =============================================================================
    // Interne Typen
    // =============================================================================

    class SpecValidationError : public std::runtime_error {
    public:
        /// Ohne SourceMap: zeigt prozessierte Zeile/Spalte
        SpecValidationError(const std::string& msg, const YAML::Mark& mark)
            : std::runtime_error(format(msg, mark, nullptr))
        {
        }

        /// Mit SourceMap: schlägt Original-Position nach
        SpecValidationError(const std::string& msg, const YAML::Mark& mark,
            const SourceMap* smap)
            : std::runtime_error(format(msg, mark, smap))
        {
        }

    private:
        static std::string format(const std::string& msg,
            const YAML::Mark& mark, const SourceMap* smap)
        {
            const int line = mark.line + 1;
            const int col = mark.column + 1;
            if (smap) {
                // Alle Positionsinfos aus der SourceMap holen – nicht mischen
                if (auto loc = smap->lookup(line))
                    return loc->to_string() + ": " + msg;
                if (auto loc = smap->lookup_nearest(line))
                    return loc->to_string() + ": " + msg;
            }
            return "Zeile " + std::to_string(line) +
                ", Spalte " + std::to_string(col) + ": " + msg;
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
        std::vector<SpecField>   children;             // Sequence-Kinder (non-TLV)
        std::map<int, SpecField> tlv_children;         // Map-Kinder (TLV, key = SE-Nummer)
        std::optional<TLVOptions> tlv;
    };

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
    static std::string resolveEncoding(const YAML::Node& node,
        const std::string& fmt,
        const std::string& defaultEncoding)
    {
        if (isEncodingNeutral(fmt)) return "";
        return toUpper(node["encoding"].as<std::string>(defaultEncoding));
    }

    // =============================================================================
    // Validierung
    // =============================================================================

    static void validateFieldKeys(const YAML::Node& node, const std::string& de,
        const SourceMap* smap) {
        static const std::set<std::string> allowed = {
            "type", "format", "encoding", "length", "description", "children", "tlv"
        };
        for (auto it = node.begin(); it != node.end(); ++it) {
            const auto key = it->first.as<std::string>();
            if (!allowed.count(key))
                throw SpecValidationError(
                    "Unbekannter Schlüssel '" + key + "' im Feld " + de,
                    it->first.Mark(), smap);
        }
    }

    static void validateSpecYaml(const YAML::Node& root, const SourceMap* smap = nullptr) {
        // Läuft auf dem BEREITS PREPROCESSIERTEN YAML – !template, !merge, !use
        // wurden bereits expandiert.

        if (!root["fields"])
            throw std::runtime_error("Fehlender Abschnitt 'fields' in YAML.");  // no mark available

        for (const auto& entry : root["fields"]) {
            const auto key = entry.first.as<std::string>();
            const auto mark = entry.first.Mark();

            if (!std::all_of(key.begin(), key.end(), ::isdigit))
                throw SpecValidationError(
                    "Feldschlüssel '" + key + "' ist nicht numerisch", mark, smap);

            const YAML::Node& field = entry.second;
            if (!field.IsMap()) continue;

            // Warnung wenn length für nicht-triviale Formate fehlt
            if (field["format"] && field["format"].IsScalar()) {
                const auto fmt = toLower(field["format"].as<std::string>());
                const bool needsLength = (fmt != "nop" && fmt != "bitmap" &&
                    fmt != "unused" && fmt != "remaining");
                if (needsLength && !field["length"])
                    TNG_LOG_WARN("[SpecDecoder] Feld {} hat format='{}' aber kein 'length'",
                        key, fmt);
            }

            // 'format: ...bertlv' ist eine reine Kurzschreibweise für scalare
            // Felder (siehe parseSpecField) - BER-TLV-Tags sind dynamisch, eine
            // vorab deklarierte Kinderliste ergibt keinen Sinn. Explizites
            // 'type: nested', 'children' oder ein eigener 'tlv:'-Block wären
            // daher widersprüchlich und werden hier abgelehnt.
            if (field["format"] && field["format"].IsScalar()) {
                const auto fmtUpper = toUpper(field["format"].as<std::string>());
                std::size_t p = 0;
                while (p < fmtUpper.size() && fmtUpper[p] == 'L') ++p;
                if (fmtUpper.substr(p) == "BERTLV") {
                    const bool explicitNested = field["type"] &&
                        toLower(field["type"].as<std::string>()) == "nested";
                    if (field["children"] || field["tlv"] || explicitNested)
                        throw SpecValidationError(
                            "Feld " + key + ": 'format: ...bertlv' ist nur bei "
                            "scalaren Feldern gültig - 'children', 'tlv' und "
                            "'type: nested' dürfen nicht zusätzlich gesetzt sein",
                            field.Mark(), smap);
                }
            }

            // Nested: erkennbar durch 'children' (oder optionales type: nested)
            const bool isNested = field["children"] ||
                (field["type"] && field["type"].as<std::string>() == "nested");
            if (isNested) {
                if (field["children"] &&
                    !field["children"].IsSequence() &&
                    !field["children"].IsMap())
                    throw SpecValidationError(
                        "'children' im nested-Feld " + key +
                        " muss eine Liste (normal) oder Map (TLV) sein",
                        field.Mark(), smap);

                if (field["tlv"]) {
                    const auto& tlv = field["tlv"];
                    const bool isBer = tlv["ber"] && tlv["ber"].as<bool>(false);
                    if (!isBer && (!tlv["tag_bytes"] || !tlv["len_bytes"]))
                        throw SpecValidationError(
                            "TLV-Block im Feld " + key +
                            " benötigt 'tag_bytes' und 'len_bytes' "
                            "(oder 'ber: true' für BER-TLV mit variabler Länge)",
                            tlv.Mark(), smap);
                }
            }
        }
    }

    // =============================================================================
    // YAML → SpecField
    // =============================================================================

    // Forward-Deklaration für rekursiven Aufruf
    static SpecField parseSpecField(const YAML::Node& node,
        const std::string& defaultEncoding,
        const std::string& tag = "",
        const SourceMap* smap = nullptr);

    static SpecField parseSpecField(const YAML::Node& node,
        const std::string& defaultEncoding,
        const std::string& tag,
        const SourceMap* smap)
    {
        SpecField f;
        f.format = toUpper(node["format"].as<std::string>(""));

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
        else if (node["type"]) {
            f.type = fieldTypeFromString(toLower(node["type"].as<std::string>("")));
        }
        else if (node["children"]) {
            f.type = SpecFieldType::NESTED;
        }
        else {
            f.type = SpecFieldType::SCALAR;
        }

        // NOP-Felder: nur ein Index-Placeholder wegen des +1-Offsets im Parser.
        // length und description sind bedeutungslos und müssen nicht angegeben werden.
        if (f.format == "NOP" || f.format == "UNUSED") {
            f.length = 0;
            f.description = node["description"].as<std::string>("<nop>");
            f.encoding = "";
            return f;
        }

        f.encoding = resolveEncoding(node, f.format, defaultEncoding);

        if (node["length"]) {
            const int raw_length = node["length"].as<int>();
            if (raw_length < 0) {
                // node["length"] hat noch den originalen YAML::Mark aus der Quelldatei
                // (weil processNode keine deepClone macht für Scalar-Nodes).
                // Damit zeigt die Fehlermeldung direkt auf die richtige Datei + Zeile.
                const YAML::Mark value_mark = node["length"].Mark();
                throw SpecValidationError(
                    "Feld '" + node["description"].as<std::string>("<unnamed>") +
                    "' hat ungültige length=" + std::to_string(raw_length) +
                    " (muss >= 0 sein)",
                    value_mark, smap);
            }
            f.length = static_cast<std::size_t>(raw_length);
        }

        f.description = node["description"]
            ? node["description"].as<std::string>()
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
        else if (node["tlv"]) {
            const YAML::Node& t = node["tlv"];
            TLVOptions opts;
            opts.ber = t["ber"].as<bool>(false);
            if (!opts.ber) {
                opts.tag_bytes = t["tag_bytes"].as<int>(2);
                opts.len_bytes = t["len_bytes"].as<int>(2);
            }
            opts.tcc = t["tcc"].as<bool>(false);
            opts.encoding = toUpper(t["encoding"].as<std::string>(childEnc));
            if (opts.ber && opts.tcc)
                TNG_LOG_WARN("[SpecDecoder] Feld '{}': 'tcc' wird bei BER-TLV "
                    "ignoriert (BER-TLV kennt kein TCC-Feld)", f.description);
            f.tlv = opts;
        }

        // ── Children ─────────────────────────────────────────────────────────────
        if (node["children"]) {
            const std::string& seEnc = f.tlv ? f.tlv->encoding : childEnc;

            if (node["children"].IsMap()) {
                // TLV-Modus: Key = SE-Nummer, Wert = SpecField
                for (const auto& entry : node["children"]) {
                    const auto seKey = entry.first.as<std::string>();
                    const int  seNum = std::stoi(seKey);
                    f.tlv_children[seNum] = parseSpecField(entry.second, seEnc, seKey);
                }
            }
            else {
                // Normal-Modus: Sequence mit Index-Feldern
                // Kinder rekursiv parsen – parseSpecField löst Encoding korrekt auf
                for (const auto& child : node["children"])
                    f.children.push_back(parseSpecField(child, childEnc));
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
        makeTlvParser(int tag_bytes, int len_bytes, bool tcc, codec::Encoder enc, bool ber = false)
    {
        using namespace ::TNG_NAMESPACE;

        // ── BER-TLV: variable Tag-/Length-Länge, kein TCC ─────────────────────
        if (ber)
            return std::make_shared<BERTLVParser>();

        // ── Feste Byte-Anzahl (bisheriges Verhalten, jetzt über
        //    FixedNumericTag/FixedNumericLength statt der ursprünglichen
        //    4 Template-Parameter TAG_BYTES/LEN_BYTES/TAG_ENC/LEN_ENC) ────────
#define MAKE_FIXED_TLV(TB, LB, HAS_TCC, ENC) \
        std::make_shared<ISOTLVParser< \
            FixedNumericTag<TB, codec::Encoder::ENC>, \
            FixedNumericLength<LB, codec::Encoder::ENC>, \
            HAS_TCC, codec::Encoder::ENC>>()

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
            false, codec::Encoder::EBCDIC>>();
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
                nested->subParser(makeTlvParser(opts.tag_bytes, opts.len_bytes, opts.tcc, enc, opts.ber));
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
        auto [yaml, smap] = SpecPreProcessor::preprocessWithSourceMap(path, trackSourceMap);
        validateSpecYaml(yaml, &smap);

        LoadedSpec result;
        result.desc = yaml["spec"].as<std::string>("<unnamed>");
        result.hdr_sz = yaml["header"] ? yaml["header"].as<std::size_t>() : 0;
        result.defaultEncoding = toUpper(yaml["encoding"].as<std::string>(""));

        for (const auto& entry : yaml["fields"]) {
            const auto de = entry.first.as<std::string>();
            const int  deNum = std::stoi(de);
            result.fields[deNum] = parseSpecField(
                entry.second, result.defaultEncoding, de, &smap);
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
