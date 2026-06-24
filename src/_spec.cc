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
        std::string description{"<dummy>"};

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
        if (!root["fields"]) {
            throw std::runtime_error("Fehlender Abschnitt 'fields' in YAML.");
        }

        const YAML::Node& fields = root["fields"];
        for (const auto& fieldEntry : fields) {
            std::string key = fieldEntry.first.as<std::string>();
            YAML::Mark mark = fieldEntry.first.Mark();

            if (!std::all_of(key.begin(), key.end(), ::isdigit)) {
                throw SpecValidationError("Feldschlüssel '" + key + "' ist nicht numerisch", mark);
            }

            const YAML::Node& field = fieldEntry.second;
            validateFieldKeys(field, key);

            if (!field["type"]) {
                throw SpecValidationError("Fehlender 'type' im Feld " + key, field.Mark());
            }

            std::string type = field["type"].as<std::string>();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);

            if (type != "scalar" && type != "nested") {
                throw SpecValidationError("Ungültiger 'type' im Feld " + key + ": " + type, field["type"].Mark());
            }

            if (type == "scalar" || type == "nested") {
                if (!field["format"]) {
                    throw SpecValidationError("Fehlendes 'format' im scalar-Feld " + key, field.Mark());
                }
                if (!field["format"].IsScalar()) {
                    throw SpecValidationError("'format' im Feld " + key + " muss ein String sein", field["format"].Mark());
                }
                std::string fmt = field["format"].as<std::string>();
                std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (fmt != "nop") {
                    if (!field["length"]) {
                        throw SpecValidationError("Fehlendes 'length' im scalar-Feld " + key, field.Mark());
                    }

                    if (!field["length"].IsScalar() || !field["length"].IsDefined()) {
                        throw SpecValidationError("'length' im Feld " + key + " muss ein Integer sein", field["length"].Mark());
                    }
                }
            }

            if (type == "nested") {
                if (!field["children"] || !field["children"].IsSequence()) {
                    throw SpecValidationError("'children' im nested-Feld " + key + " fehlt oder ist keine Liste", field.Mark());
                }

                for (const auto& child : field["children"]) {
                    if (!child["format"] || !child["length"]) {
                        throw SpecValidationError("Kind-Feld in nested-Feld " + key + " unvollständig", child.Mark());
                    }
                }
            }
        }
    }

    // Formate die kein Encoding kennen – ignorieren globales und Feld-Encoding
    static bool isEncodingNeutral(const std::string& format) {
        static const std::unordered_set<std::string> neutral = {
            "BINARY", "LBINARY", "LLBINARY", "LLLBINARY", "LLLLBINARY",
            "BITMAP", "NOP", "UNUSED", "REMAINING"
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
        } else {
            f.encoding = node["encoding"].as<std::string>(defaultEncoding);
            std::transform(f.encoding.begin(), f.encoding.end(), f.encoding.begin(),
                [](unsigned char c) { return std::toupper(c); });
        }

        f.length = node["length"] ? node["length"].as<int>() : 0;
        f.description = node["description"]
            ? node["description"].as<std::string>()
            : (f.length == 0 ? "<dummy>" : "?");

        if (node["children"]) {
            for (const auto& child : node["children"]) {
                SpecField childSpec;
                childSpec.format = child["format"].as<std::string>("");
                std::transform(childSpec.format.begin(), childSpec.format.end(), childSpec.format.begin(),
                    [](unsigned char c) { return std::toupper(c); });

                if (isEncodingNeutral(childSpec.format)) {
                    childSpec.encoding = "";
                } else {
                    // Kinder erben zunächst das Encoding des Elternfeldes,
                    // das wiederum schon defaultEncoding berücksichtigt hat
                    childSpec.encoding = child["encoding"].as<std::string>(f.encoding);
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
        // Lambda-Helfer um Tipp-Redundanz zu reduzieren
        #define MAKE(T) [](int len, const std::string& desc) \
            -> ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr \
            { return std::make_shared<T>(len, desc); }
        #define MAKE_NOP() [](int, const std::string&) \
            -> ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr \
            { return std::make_shared<IF_NOP>(); }

        static const std::unordered_map<std::string, ParserFactory> table = {
            // ── Encoding-unabhängige Sondertypen ──────────────────────────────
            { "BITMAP|",          MAKE(IFB_BITMAP)      },
            { "NOP|",             MAKE_NOP()             },
            { "UNUSED|",          MAKE_NOP()             },

            // ── BINARY (rohe Bytes) ───────────────────────────────────────────
            { "BINARY|",          MAKE(IF_BINARY)        },
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
            { "REMAINING|EBCDIC",    MAKE(IFE_REMAINING)   },
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

        TNG_LOG_WARN("[SpecDecoder] Unbekannte Format/Encoding-Kombination: '{}' / '{}' – falle auf NOP zurück",
            f.format, f.encoding);
        return std::make_shared<IF_NOP>();
    }

    static ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr buildFieldParser(const SpecField& f) {
        if (f.type == SpecFieldType::SCALAR) {
            return createScalarParser(f);
        }
        else if (f.type == SpecFieldType::NESTED) {
            auto base = createScalarParser(f);//std::make_shared<IF_BINARY>(f.binary_length, f.description);

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
    ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr SpecDecoder::loadFromYaml(const std::string& path) {
        // Pre-Processing
        YAML::Node processedYaml = ::TNG_NAMESPACE::spec::SpecPreProcessor::preprocessFile(path);
        // Validation
        try {
            validateSpecYaml(processedYaml);
        }
        catch (const std::exception& e) {
            TNG_LOG_ERROR("[SpecDecoder] Validation failed for '{}': {}", path, e.what());
            return std::nullptr_t{};
        }


        std::string desc = processedYaml["spec"].as<std::string>("Unknown spec");
        std::size_t hdr_sz = 0u;
        if (processedYaml["header"])
            hdr_sz = processedYaml["header"].as<std::size_t>();

        // Globales Encoding: gilt für alle Felder ohne eigenes `encoding`.
        // Mastercard/VISA-Specs können so mit einer einzigen Zeile alle Felder setzen.
        // Priorität: Feld-Encoding > globales Encoding > "" (nur für neutrale Formate)
        std::string defaultEncoding = processedYaml["encoding"].as<std::string>("");
        std::transform(defaultEncoding.begin(), defaultEncoding.end(), defaultEncoding.begin(),
            [](unsigned char c) { return std::toupper(c); });

        auto parser = std::make_shared<::TNG_NAMESPACE::ISOBaseParser>(desc, hdr_sz);

        std::map<int, SpecField> fields;
        for (const auto& fieldEntry : processedYaml["fields"]) {
            std::string de = fieldEntry.first.as<std::string>();
            int deNumeric = std::stoi(de);
            fields[deNumeric] = parseSpecField(fieldEntry.second, defaultEncoding, de);
        }

        // Calculate highest field
        int hf = fields.rbegin()->first;

        for (int i = 0; i <= hf; ++i) {
            if (fields.count(i)) {
                parser->add(buildFieldParser(fields[i]));
            }
            else {
                parser->add(std::make_shared<IF_NOP>());
            }
        }

        TNG_LOG_INFO("[SpecDecoder] Loaded '{}' - {} fields, header={}B",
            desc, fields.size(), hdr_sz);
        return parser;
    }
}