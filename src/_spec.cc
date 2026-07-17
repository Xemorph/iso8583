#include "_spec.hh"

// [stdc++]
#include <algorithm>
#include <set>
#include <stdexcept>
#include <sstream>
#include <unordered_set>
#include <vector>
// [yaml-cpp]
#include <yaml-cpp/yaml.h>
// [tng]
#include "_preprocessor.hh"
#include "_parser.hh"
#include "fmt_types.hh"
#include "_logger.hh"
// [stdc++ string case conversion – ersetzt boost::to_lower / boost::to_upper]
#include <algorithm>
#include <cctype>

namespace TNG_NAMESPACE::spec {

    class SpecValidationError : public std::runtime_error {
    public:
        SpecValidationError(const std::string& msg, const YAML::Mark& mark)
            : std::runtime_error(buildMessage(msg, mark)) {}

    private:
        static std::string buildMessage(const std::string& msg, const YAML::Mark& mark) {
            std::ostringstream oss;
            oss << "Fehler in YAML bei Zeile " << (mark.line + 1)
                << ", Spalte " << (mark.column + 1) << ": " << msg;
            return oss.str();
        }
    };

    enum class SpecFieldType {
        UNKNOWN,
        SCALAR,
        NESTED,
    };
    // String switch paridgam   
    struct SpecFieldTypeMap : public std::map<std::string, SpecFieldType> {
        SpecFieldTypeMap() {
            this->operator[]("scalar") = SpecFieldType::SCALAR;
            this->operator[]("nested") = SpecFieldType::NESTED;
            this->operator[]("") = SpecFieldType::UNKNOWN;
        };
        ~SpecFieldTypeMap() {}
    };

    struct SpecField {
        SpecFieldType type;
        std::string format;
        std::string encoding; // "ebcdic" | "ascii" | "bcd" | "binary" | "" (= default)
        int length = 0;
        std::string description{ "<dummy>" };

        std::vector<SpecField> children;
    };

    // === PRIVATE ============================================================

    static SpecFieldType specFieldTypeFromString(const std::string& s) {
        static const std::unordered_map<std::string, SpecFieldType> map = {
            {"scalar", SpecFieldType::SCALAR},
            {"nested", SpecFieldType::NESTED},
            {"",       SpecFieldType::UNKNOWN},
        };
        auto it = map.find(s);
        return it != map.end() ? it->second : SpecFieldType::UNKNOWN;
    }

    static void validateFieldKeys(const YAML::Node& node, const std::string& tag) {
        static const std::set<std::string> allowedKeys = {
            "type", "format", "encoding", "length", "description", "children"
        };

        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            if (allowedKeys.find(key) == allowedKeys.end()) {
                throw SpecValidationError("Unbekannter Schlüssel '" + key + "' im Feld " + tag, it->first.Mark());
            }
        }
    }

    static void validateSpecYaml(const YAML::Node& root) {
        // Validierung läuft auf dem BEREITS PREPROCESSIERTEN YAML.
        // !template, !merge, !use wurden bereits expandiert – daher:
        //   - 'type' kann durch !template fehlen (produziert kein type-Feld)
        //   - 'length' kann bei NOP fehlen (gültig)
        //   - children-Kinder können NOP ohne length sein

        if (!root["fields"])
            throw std::runtime_error("Fehlender Abschnitt \'fields\' in YAML.");

        const YAML::Node& fields = root["fields"];
        for (const auto& fieldEntry : fields) {
            const std::string key = fieldEntry.first.as<std::string>();
            const YAML::Mark  mark = fieldEntry.first.Mark();

            if (!std::all_of(key.begin(), key.end(), ::isdigit))
                throw SpecValidationError("Feldschlüssel \'" + key + "\' ist nicht numerisch", mark);

            const YAML::Node& field = fieldEntry.second;
            if (!field.IsMap()) continue;  // z.B. Bitmap-Wert als Scalar

            // format ist das einzig zwingend nötige Feld nach dem Preprocessing
            if (field["format"] && field["format"].IsScalar()) {
                std::string fmt = field["format"].as<std::string>();
                std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                // Nur bei nicht-NOP/nicht-BITMAP Feldern ist length erforderlich
                const bool needsLength = (fmt != "nop" && fmt != "bitmap" &&
                    fmt != "unused" && fmt != "remaining");
                if (needsLength && !field["length"]) {
                    TNG_LOG_WARN("[SpecDecoder] Feld {} hat format='{}' aber kein 'length'", key, fmt);
                }
            }

            // Nested: children muss eine Sequence sein
            if (field["type"] && field["type"].as<std::string>() == "nested") {
                if (field["children"] && !field["children"].IsSequence())
                    throw SpecValidationError(
                        "\'children\' im nested-Feld " + key + " muss eine Liste sein",
                        field.Mark());
            }
        }
    }

    // Formate die kein Encoding kennen – ignorieren globales und Feld-Encoding
    static bool isEncodingNeutral(const std::string& format) {
        static const std::unordered_set<std::string> neutral = {
            "BINARY",
            "BITMAP", "NOP", "UNUSED",
            "REMAINING",  // kein Encoding – liest rohe Bytes
        };
        return neutral.count(format) > 0;
    }

    static SpecField parseSpecField(const YAML::Node& node,
        const std::string& defaultEncoding,
        const std::string& tag = "") {
        SpecField f;
        std::string type = node["type"].as<std::string>("");
        std::transform(type.begin(), type.end(), type.begin(),
            [](unsigned char c) { return std::tolower(c); });
        f.type = specFieldTypeFromString(type);

        f.format = node["format"].as<std::string>("");
        std::transform(f.format.begin(), f.format.end(), f.format.begin(),
            [](unsigned char c) { return std::toupper(c); });

        // Encoding-Auflösung: Feld-Encoding > globales Encoding > leer (für neutrale Formate)
        if (isEncodingNeutral(f.format)) {
            f.encoding = "";  // encoding-neutrale Formate ignorieren jede Vorgabe
        }
        else {
            f.encoding = node["encoding"].as<std::string>(defaultEncoding);
            std::transform(f.encoding.begin(), f.encoding.end(), f.encoding.begin(),
                [](unsigned char c) { return std::toupper(c); });
        }

        f.length = node["length"] ? node["length"].as<int>() : 0;
        f.description = node["description"]
            ? node["description"].as<std::string>()
            : (f.length == 0 ? "<dummy>" : "?");

        if (node["children"]) {
            // Encoding das an Kinder vererbt wird:
            //   - Wenn das Elternfeld selbst encoding-neutral ist (z.B. format: binary
            //     bei einem nested-Container), darf sein leeres f.encoding NICHT an
            //     die Kinder weitergereicht werden – sonst erben Kinder fälschlicherweise
            //     ein leeres Encoding und das globale defaultEncoding geht verloren.
            //   - Nur wenn das Elternfeld selbst ein echtes Encoding trägt
            //     (z.B. format: numeric mit encoding: bcd), erben Kinder dieses.
            const std::string& inheritedEncoding =
                isEncodingNeutral(f.format) ? defaultEncoding : f.encoding;

            for (const auto& child : node["children"]) {
                SpecField childSpec;
                childSpec.format = child["format"].as<std::string>("");
                std::transform(childSpec.format.begin(), childSpec.format.end(), childSpec.format.begin(),
                    [](unsigned char c) { return std::toupper(c); });

                if (isEncodingNeutral(childSpec.format)) {
                    childSpec.encoding = "";
                }
                else {
                    // Kinder erben das Encoding des Elternfeldes – oder, falls das
                    // Elternfeld encoding-neutral ist, direkt das globale Encoding.
                    childSpec.encoding = child["encoding"].as<std::string>(inheritedEncoding);
                    std::transform(childSpec.encoding.begin(), childSpec.encoding.end(), childSpec.encoding.begin(),
                        [](unsigned char c) { return std::toupper(c); });
                }

                childSpec.length = child["length"] ? child["length"].as<int>() : 0;
                childSpec.description = child["description"]
                    ? child["description"].as<std::string>()
                    : (childSpec.length == 0 ? "<dummy>" : "?");
                f.children.push_back(childSpec);
            }
        }

        return f;
    }

    // ── Parser-Fabrik ────────────────────────────────────────────────────────
    //
    // Einzige Lookup-Tabelle für alle Format/Encoding-Kombinationen.
    // Schlüssel: "<FORMAT>|<ENCODING>"  (beide uppercase, Encoding darf leer sein)
    //
    // Wenn eine YAML-Spec kein `encoding` angibt, gilt folgende Konvention:
    //   BITMAP / BINARY / NOP / UNUSED → encoding-unabhängig
    //   Alle anderen                   → Fehler (encoding muss gesetzt sein)
    //
    // Neue Typen aus fmt_types.hh ergänzen: eine Zeile in der Tabelle genügt.
    // Die if-else-Kaskade entfällt vollständig.

    using ParserFactory = std::function<
        ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr(int len, const std::string& desc)
    >;

    static const std::unordered_map<std::string, ParserFactory>& parserTable() {
        using F = ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr;

        // Lokale Makros um die Factory-Lambda-Redundanz zu reduzieren.
        // #undef direkt nach der Tabelle – kein Einfluss auf andere TUs.
#define MAKE(T) [](int len, const std::string& desc) -> F \
            { return std::make_shared<T>(len, desc); }
#define MAKE_NOP() [](int, const std::string&) -> F \
            { return std::make_shared<IF_NOP>(); }

        static const std::unordered_map<std::string, ParserFactory> table = {
            // ── Encoding-unabhängige Sondertypen ──────────────────────────────
            { "BITMAP|",          MAKE(IFB_BITMAP)      },
            { "NOP|",             MAKE_NOP()             },
            { "UNUSED|",          MAKE_NOP()             },
            // Liest alle verbleibenden Bytes des Parent-Buffers.
            // Fuer trailing variable-length Felder ohne eigenen Laengen-Prefix.
            // Trailing variable-length Felder ohne Prefix.
            // Encoding-neutral fuer binary, EBCDIC-Variante fuer EBCDIC-Specs.
            { "REMAINING|",          MAKE(IF_REMAINING)   },
            { "REMAINING|BINARY",    MAKE(IF_REMAINING)   },
            { "REMAINING|EBCDIC",    MAKE(IFE_REMAINING)  },

            // ── BINARY (rohe Bytes) ───────────────────────────────────────────
            { "BINARY|",          MAKE(IF_BINARY)        },
            { "LBINARY|",         MAKE(IF_LBINARY)       },
            { "LLBINARY|",        MAKE(IF_LLBINARY)      },
            { "LLLBINARY|",       MAKE(IF_LLLBINARY)     },
            { "BINARY|BINARY",    MAKE(IF_BINARY)        },
            { "LBINARY|BINARY",   MAKE(IF_LBINARY)       },
            { "LLBINARY|BINARY",  MAKE(IF_LLBINARY)      },
            { "LLLBINARY|BINARY", MAKE(IF_LLLBINARY)     },

            // ── ASCII ─────────────────────────────────────────────────────────
            { "NUMERIC|ASCII",    MAKE(IFA_NUMERIC)      },
            { "CHAR|ASCII",       MAKE(IFA_CHAR)         },
            { "NOPAD_CHAR|ASCII", MAKE(IFA_NOPAD_CHAR)   },
            { "LCHAR|ASCII",      MAKE(IFA_LCHAR)        },
            { "LLCHAR|ASCII",     MAKE(IFA_LLCHAR)       },
            { "LLLCHAR|ASCII",    MAKE(IFA_LLLCHAR)      },
            { "LLLLCHAR|ASCII",   MAKE(IFA_LLLLCHAR)     },
            { "LNUM|ASCII",       MAKE(IFA_LNUM)         },
            { "LLNUM|ASCII",      MAKE(IFA_LLNUM)        },
            { "LBINARY|ASCII",    MAKE(IFA_LBINARY)      },
            { "LLBINARY|ASCII",   MAKE(IFA_LLBINARY)     },
            { "LLLBINARY|ASCII",  MAKE(IFA_LLLBINARY)    },

            // ── BCD ───────────────────────────────────────────────────────────
            { "NUMERIC|BCD",      MAKE(IFB_NUMERIC)      },
            { "LCHAR|BCD",        MAKE(IFB_LCHAR)        },
            { "LLCHAR|BCD",       MAKE(IFB_LLCHAR)       },
            { "LLLCHAR|BCD",      MAKE(IFB_LLLCHAR)      },
            { "LBINARY|BCD",      MAKE(IFB_LBINARY)      },
            { "LLBINARY|BCD",     MAKE(IFB_LLBINARY)     },
            { "LLLBINARY|BCD",    MAKE(IFB_LLLBINARY)    },

            // ── EBCDIC ────────────────────────────────────────────────────────
            { "BINARY|EBCDIC",       MAKE(IFE_BINARY)      },
            { "LBINARY|EBCDIC",      MAKE(IFE_LBINARY)     },
            { "LLBINARY|EBCDIC",     MAKE(IFE_LLBINARY)    },
            { "LLLBINARY|EBCDIC",    MAKE(IFE_LLLBINARY)   },
            { "LLLLBINARY|EBCDIC",   MAKE(IFE_LLLLBINARY)  },
            { "NUMERIC|EBCDIC",      MAKE(IFE_NUMERIC)     },
            { "LNUM|EBCDIC",         MAKE(IFE_LNUM)        },
            { "CHAR|EBCDIC",         MAKE(IFE_CHAR)        },
            { "NOPAD_CHAR|EBCDIC",   MAKE(IFE_NOPAD_CHAR)  },
            { "LCHAR|EBCDIC",        MAKE(IFE_LCHAR)       },
            { "LLCHAR|EBCDIC",       MAKE(IFE_LLCHAR)      },
            { "LLLCHAR|EBCDIC",      MAKE(IFE_LLLCHAR)     },
        };

#undef MAKE
#undef MAKE_NOP
        return table;
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr
        createScalarParser(const SpecField& f) {
        const std::string key = f.format + "|" + f.encoding;
        const auto& table = parserTable();

        auto it = table.find(key);
        if (it != table.end())
            return it->second(f.length, f.description);

        // Fallback: encoding weggelassen, Format-only-Lookup (für BITMAP, NOP, BINARY)
        auto it2 = table.find(f.format + "|");
        if (it2 != table.end())
            return it2->second(f.length, f.description);

        // Unbekannte Kombination – Exception mit klarer Fehlermeldung.
        // Häufige Ursache: Tippfehler im encoding-Wert
        throw std::runtime_error(
            "Unbekannte Format/Encoding-Kombination in der Spec:\n"
            "  format:   '" + f.format + "'\n"
            "  encoding: '" + f.encoding + "'\n"
            "  description: '" + f.description + "'\n"
            "  Erlaubte Encodings: ASCII | BCD | BINARY | EBCDIC\n"
            "  Prüfe auf Tippfehler im globalen 'encoding'-Schlüssel oder im Feld selbst."
        );
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr buildFieldParser(const SpecField& f) {
        if (f.type == SpecFieldType::SCALAR) {
            return createScalarParser(f);
        }
        else if (f.type == SpecFieldType::NESTED) {
            auto base = createScalarParser(f);

            auto nestedParser = std::make_shared<::TNG_NAMESPACE::ISONestedFieldParser<::TNG_NAMESPACE::ISOBaseParser>>(base, f.description);
            auto subparser = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(f.description);
            for (const auto& child : f.children) {
                subparser->add(createScalarParser(child));
            }
            nestedParser->subParser(subparser);
            return nestedParser;
        }
        else {
            return std::make_shared<IF_NOP>();
        }
    }

    //=== PUBLIC ==============================================================
    // ── SpecFieldFormat helper ────────────────────────────────────────────────
    // Splits "LLCHAR" -> {type="CHAR", prefix_digits=2, max_length=N}
    static SpecFieldFormat makeSpecFieldFormat(const SpecField& f) {
        SpecFieldFormat fmt;
        fmt.max_length = f.length;

        // Count leading L's (format string is already uppercase)
        const std::string& s = f.format;
        std::size_t prefix = 0;
        while (prefix < s.size() && s[prefix] == 'L')
            ++prefix;

        fmt.prefix_digits = static_cast<int>(prefix);
        fmt.type = (prefix > 0) ? s.substr(prefix) : s;

        // These formats carry no meaningful length
        if (fmt.type == "NOP" || fmt.type == "UNUSED" || fmt.type == "REMAINING")
            fmt.max_length = 0;

        return fmt;
    }

    // Recursively converts internal SpecField -> public SpecFieldInfo
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

    // ── Shared YAML loading ───────────────────────────────────────────────────
    // Preprocesses and validates once, returns the parsed SpecField map plus
    // metadata so both loadFromYaml and loadBothFromYaml share the same logic.
    struct LoadedSpec {
        std::string              desc;
        std::string              defaultEncoding;
        std::size_t              hdr_sz;
        std::map<int, SpecField> fields;
    };

    static LoadedSpec loadAndParse(const std::string& path) {
        auto processedYaml = SpecPreProcessor::preprocessFile(path);
        validateSpecYaml(processedYaml);

        LoadedSpec result;
        result.desc = processedYaml["spec"].as<std::string>("<unnamed>");
        result.hdr_sz = processedYaml["header"]
            ? processedYaml["header"].as<std::size_t>() : 0;
        result.defaultEncoding = processedYaml["encoding"].as<std::string>("");
        std::transform(result.defaultEncoding.begin(), result.defaultEncoding.end(),
            result.defaultEncoding.begin(),
            [](unsigned char c) { return std::toupper(c); });

        for (const auto& fieldEntry : processedYaml["fields"]) {
            std::string de = fieldEntry.first.as<std::string>();
            int         deNum = std::stoi(de);
            result.fields[deNum] = parseSpecField(
                fieldEntry.second, result.defaultEncoding, de);
        }
        return result;
    }

    // ── ISOSpec public methods

    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        SpecDecoder::loadFromYaml(const std::string& path) {
        try {
            auto loaded = loadAndParse(path);
            auto parser = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(
                loaded.desc, loaded.hdr_sz);
            int hf = loaded.fields.rbegin()->first;
            for (int i = 0; i <= hf; ++i) {
                parser->add(loaded.fields.count(i)
                    ? buildFieldParser(loaded.fields[i])
                    : std::make_shared<IF_NOP>());
            }
            TNG_LOG_INFO("[SpecDecoder] Loaded '{}' - {} fields, header={}B",
                loaded.desc, loaded.fields.size(), loaded.hdr_sz);
            return parser;
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadFromYaml failed for '{}': {}", path, e.what());
            throw;
        }
    }

    std::optional<SpecFieldInfo> ISOSpec::field(TNG_KEY_TYPE key) const {
        for (const auto& f : fields_)
            if (f.key == key)
                return f;
        return std::nullopt;
    }

    bool ISOSpec::has(TNG_KEY_TYPE key) const noexcept {
        for (const auto& f : fields_)
            if (f.key == key)
                return true;
        return false;
    }

    // ── SpecDecoder::loadBothFromYaml ─────────────────────────────────────────

    std::pair<
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
        ISOSpec::SmartPtr>
        SpecDecoder::loadBothFromYaml(const std::string& path) {
        try {
            auto loaded = loadAndParse(path);

            // Build parser (same logic as loadFromYaml)
            auto parser = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(
                loaded.desc, loaded.hdr_sz);
            int hf = loaded.fields.rbegin()->first;
            for (int i = 0; i <= hf; ++i) {
                parser->add(loaded.fields.count(i)
                    ? buildFieldParser(loaded.fields[i])
                    : std::make_shared<IF_NOP>());
            }

            // Build ISOSpec
            std::vector<SpecFieldInfo> fieldInfos;
            fieldInfos.reserve(loaded.fields.size());
            for (const auto& [key, f] : loaded.fields)
                fieldInfos.push_back(makeSpecFieldInfo(
                    static_cast<TNG_KEY_TYPE>(key), f));

            auto spec = std::make_shared<ISOSpec>(
                loaded.desc, loaded.defaultEncoding, std::move(fieldInfos));

            TNG_LOG_INFO("[SpecDecoder] loadBothFromYaml '{}' - {} fields",
                loaded.desc, loaded.fields.size());

            return { parser, spec };
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] loadBothFromYaml failed for '{}': {}",
                path, e.what());
            throw;
        }
    }

}