#include "_preprocessor.hh"
// [stdc++]
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
// [tng]
#include "_logger.hh"

// =============================================================================
// Interner Preprocessor
//
// Unterstützte YAML-Direktiven:
//
//  !include_files        – Externe YAML-Dateien laden und deren `definitions`
//                          in den globalen Kontext mergen (Root-Level)
//
//  !use <name>           – Eine benannte Definition aus `definitions` einfügen.
//                          Ersatz für das missverständliche `!include`.
//                          Beispiel:  "002": !use pan_field
//
//  !template <expr>      – Feld-Kurzschreibweise für variable Längen.
//                          Syntax: <PREFIX>(<FORMAT>, <MAX_LENGTH>[, <DESCRIPTION>])
//                          Beispiel: !template LL(CHAR, 19)
//                          Erlaubte Prefixe: L, LL, LLL, LLLL
//                          Produziert: { type: scalar, format: LLCHAR, length: 19 }
//
//  !merge [...]          – Mehrere Maps zu einer zusammenführen.
//                          Spätere Einträge überschreiben frühere.
//                          Typischer Einsatz: !template-Ergebnis + description
//
// Rückwärtskompatibilität:
//   `!include` wird noch akzeptiert, verhält sich identisch zu `!use` und
//   gibt eine Deprecation-Warnung aus.
// =============================================================================

namespace TNG_NAMESPACE::spec {

    // -- Forward-Deklaration ---------------------------------------------------
    static YAML::Node processNode(
        const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited);

    // -- mergeInto: fügt alle Keys aus source in dest ein ---------------------
    // definitions werden tief gemergt (Quelle überschreibt Ziel-Keys).
    // Alle anderen Top-Level-Keys werden flach überschrieben.
    static void mergeInto(YAML::Node& dest, const YAML::Node& source) {
        if (!source.IsMap()) return;

        for (auto it : source) {
            const std::string key = it.first.as<std::string>();

            if (key == "definitions") {
                // Definitions tief mergen: Quelle überschreibt Ziel
                if (!dest["definitions"] || !dest["definitions"].IsMap())
                    dest["definitions"] = YAML::Node(YAML::NodeType::Map);
                if (it.second.IsMap())
                    for (auto def : it.second)
                        dest["definitions"][def.first] = def.second;
            } else {
                dest[it.first] = it.second;
            }
        }
    }

    // -- !use / !include: Definition aus `definitions` einfügen ---------------
    static YAML::Node processUse(
        const YAML::Node& node,
        const YAML::Node& context,
        bool is_legacy_include = false)
    {
        if (!node.IsScalar())
            throw std::runtime_error(
                is_legacy_include
                    ? "!include erwartet einen Schlüsselstring (Hinweis: !include wurde durch !use ersetzt)"
                    : "!use erwartet einen Schlüsselstring");

        const std::string refKey = node.as<std::string>();

        if (is_legacy_include)
            TNG_LOG_WARN("[Preprocessor] '!include {}' ist veraltet – bitte '!use {}' verwenden",
                         refKey, refKey);

        if (!context["definitions"] || !context["definitions"][refKey]) {
            throw std::runtime_error(
                "!use verweist auf unbekannte Definition '" + refKey + "' – "
                "ist die Definition in 'definitions' oder via '!include_files' geladen?");
        }

        return context["definitions"][refKey];
    }

    // -- !template: Kurzschreibweise für variable Feldlängen ------------------
    // Syntax: PREFIX(FORMAT, MAX_LENGTH[, DESCRIPTION])
    // Beispiele:
    //   LL(CHAR, 19)            → { type: scalar, format: LLCHAR,   length: 19  }
    //   LLL(BINARY, 255)        → { type: scalar, format: LLLBINARY, length: 255 }
    //   LL(CHAR, 19, PAN)       → zusätzlich description: "PAN"
    static YAML::Node processTemplate(const YAML::Node& node) {
        if (!node.IsScalar())
            throw std::runtime_error(
                "!template erwartet einen Ausdruck wie 'LL(CHAR, 19)' oder 'LLL(BINARY, 255)'");

        const std::string expr = node.as<std::string>();

        // Regex: PREFIX(FORMAT, LENGTH[, DESCRIPTION])
        static const std::regex pattern(
            R"(^(L{1,4})\((\w+),\s*(\d{1,4})\s*(?:,\s*(.*))?\)$)",
            std::regex::icase);

        std::smatch m;
        if (!std::regex_match(expr, m, pattern))
            throw std::runtime_error(
                "Ungültiger !template-Ausdruck: '" + expr + "'\n"
                "  Erwartetes Format: PREFIX(FORMAT, MAX_LENGTH)\n"
                "  Beispiele:  LL(CHAR, 19)  |  LLL(BINARY, 255)  |  LLLL(CHAR, 9999)");

        const std::string prefix = m[1].str();
        const std::string fmt    = m[2].str();
        const int         length = std::stoi(m[3].str());

        // ISO 8583 Längen-Constraints
        static const std::vector<std::pair<std::string,int>> limits = {
            {"L", 9}, {"LL", 99}, {"LLL", 999}, {"LLLL", 9999}
        };
        for (auto& [p, max] : limits) {
            if (prefix == p && length > max)
                throw std::runtime_error(
                    "!template: Länge " + std::to_string(length) +
                    " überschreitet Maximum " + std::to_string(max) +
                    " für Prefix '" + prefix + "' (ISO 8583: L=9, LL=99, LLL=999, LLLL=9999)");
        }

        YAML::Node result(YAML::NodeType::Map);
        result["type"]   = "scalar";
        result["format"] = prefix + fmt;
        result["length"] = length;
        if (m[4].matched && !m[4].str().empty())
            result["description"] = m[4].str();

        return result;
    }

    // -- !merge: mehrere Maps zusammenführen -----------------------------------
    static YAML::Node processMerge(
        const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited)
    {
        if (!node.IsSequence())
            throw std::runtime_error(
                "!merge erwartet eine Liste von Maps:\n"
                "  Beispiel:\n"
                "    !merge\n"
                "    - !template LL(CHAR, 19)\n"
                "    - description: \"PAN\"");

        YAML::Node result(YAML::NodeType::Map);
        for (const auto& part : node) {
            YAML::Node evaluated = processNode(part, context, basePath, visited);
            if (!evaluated.IsMap())
                throw std::runtime_error(
                    "Alle Elemente in !merge müssen Maps sein – "
                    "erhalten: " + std::to_string(static_cast<int>(evaluated.Type())));
            for (auto it : evaluated)
                result[it.first] = it.second;
        }
        return result;
    }

    // -- Rekursiver Node-Prozessor ---------------------------------------------
    static YAML::Node processNode(
        const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited)
    {
        const std::string tag = node.Tag();

        if (tag == "!use")
            return processUse(node, context, false);

        if (tag == "!include")                          // Rückwärtskompatibilität
            return processUse(node, context, true);

        if (tag == "!template")
            return processTemplate(node);

        if (tag == "!merge")
            return processMerge(node, context, basePath, visited);

        if (node.IsMap()) {
            YAML::Node out(YAML::NodeType::Map);
            for (auto it : node)
                out[it.first] = processNode(it.second, context, basePath, visited);
            return out;
        }

        if (node.IsSequence()) {
            YAML::Node out(YAML::NodeType::Sequence);
            for (const auto& elem : node)
                out.push_back(processNode(elem, context, basePath, visited));
            return out;
        }

        return node;  // Scalar oder Null: unverändert zurückgeben
    }

} // namespace TNG_NAMESPACE::spec

// =============================================================================
// Öffentliche API
// =============================================================================
YAML::Node TNG_NAMESPACE::spec::SpecPreProcessor::preprocessFile(const std::string& path) {
    namespace fs = std::filesystem;

    const fs::path absPath  = fs::absolute(path);
    const fs::path basePath = absPath.parent_path();

    std::unordered_set<std::string> visitedFiles;
    std::vector<YAML::Node> docs = YAML::LoadAllFromFile(path);

    if (docs.empty())
        throw std::runtime_error("Leere YAML-Datei: " + path);

    YAML::Node root(YAML::NodeType::Map);

    // -- !include_files: externe Dateien laden ---------------------------------
    const YAML::Node& first = docs[0];
    if (first.Tag() == "!include_files" && first.IsSequence()) {
        TNG_LOG_DEBUG("[Preprocessor] !include_files in '{}'", path);

        for (const auto& entry : first) {
            if (!entry.IsScalar())
                throw std::runtime_error("!include_files: Einträge müssen Dateinamen sein");

            const fs::path fullPath = fs::absolute(basePath / entry.as<std::string>());
            if (!fs::exists(fullPath))
                throw std::runtime_error(
                    "!include_files: Datei nicht gefunden: " + fullPath.string() +
                    "\n  Referenziert von: " + path);

            const std::string absStr = fullPath.string();
            if (visitedFiles.count(absStr)) {
                TNG_LOG_DEBUG("[Preprocessor] Zirkulaeren Include übersprungen: {}", absStr);
                continue;
            }
            visitedFiles.insert(absStr);

            TNG_LOG_DEBUG("[Preprocessor] Lade '{}'", absStr);
            YAML::Node ext = preprocessFile(absStr);   // rekursiv
            if (!ext.IsMap())
                throw std::runtime_error(
                    "!include_files: '" + fullPath.string() + "' muss eine YAML-Map sein");

            YAML::Node processed = processNode(ext, ext, fullPath.parent_path(), visitedFiles);
            mergeInto(root, processed);
        }

        // Haupt-Dokument(e) ab docs[1] zusammenführen
        for (std::size_t i = 1; i < docs.size(); ++i)
            mergeInto(root, docs[i]);

    } else {
        // Keine !include_files Sektion – Datei direkt verarbeiten
        for (auto& doc : docs)
            mergeInto(root, doc);
    }

    return processNode(root, root, basePath, visitedFiles);
}
