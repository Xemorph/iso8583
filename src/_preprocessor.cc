#include "_preprocessor.hh"
// [stdc++]
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <sstream>
#include <unordered_set>
#include <vector>
// [tng]
#include "_logger.hh"

// === PRIVATE ================================================================
namespace TNG_NAMESPACE::spec {
    // Forward decleration for 'processMerge'
    static YAML::Node processNode(const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited);
    
    static void mergeDefinitions(YAML::Node& dest, const YAML::Node& source) {
        if (!source.IsMap()) return;

        for (auto it : source) {
            const auto& key = it.first.as<std::string>();
            const auto& value = it.second;

            // Überschreibe die Definition im Ziel-Knoten mit der Quelle, wenn sie bereits existiert
            dest[key] = value;
        }
    }

    static void mergeInto(YAML::Node& dest, const YAML::Node& source) {
        if (!source.IsMap()) return;

        // Spezielles Merging der "definitions"-Sektion:
        // Neueste Definition überschreibt die vorherige

        // Überprüfe, ob source["definitions"] existiert und vom Typ Map ist
        if (source["definitions"] && source["definitions"].IsMap()) {
            // Stelle sicher, dass dest["definitions"] existiert und vom Typ Map ist
            if (!dest["definitions"] || !dest["definitions"].IsMap()) {
                dest["definitions"] = YAML::Node(YAML::NodeType::Map);  // Initialisiere als leere Map
            }
        }

        // Führe Definitions-Zusammenführung durch
        YAML::Node shadow = dest["definitions"];  // Speichern der Ziel-Definitions
        mergeDefinitions(shadow, source["definitions"]);  // Merging durchführen
        dest["definitions"] = shadow;  // Ersetze das Ziel mit der gemergten Version

        // Standardmäßiges Merging der anderen Knoten
        for (auto it : source) {
            const auto& key = it.first;
            const auto& value = it.second;

            if (dest[key]) {
                if (dest[key].IsMap() && value.IsMap()) {
                    YAML::Node merged = dest[key];
                    mergeInto(merged, value);
                    dest[key] = merged;
                    continue;
                }
            }

            dest[key] = value;
        }
    }

    /*
    static YAML::Node processInclude(const YAML::Node& node, const YAML::Node& context) {
        if (!node.IsScalar()) {
            throw std::runtime_error("!include erwartet einen Schlüsselstring");
        }

        std::string refKey = node.as<std::string>();
        if (!context["definitions"] || !context["definitions"][refKey]) {
            std::ostringstream oss;
            oss << "Fehler: !include verweist auf unbekannten Schlüssel '" << refKey << "'";
            throw std::runtime_error(oss.str());
        }

        return context["definitions"][refKey];
    }
    */
    static YAML::Node processInclude(const YAML::Node& node, const YAML::Node& context) {
        if (!node.IsScalar()) {
            throw std::runtime_error("!include erwartet einen Schlüsselstring");
        }

        std::string refKey = node.as<std::string>();
        if (!context["definitions"] || !context["definitions"][refKey]) {
            std::ostringstream oss;
            oss << "Fehler: !include verweist auf unbekannten Schlüssel '" << refKey << "'";
            throw std::runtime_error(oss.str());
        }

        // Hier holen wir uns die Definitionen, die vom include referenziert werden
        return context["definitions"][refKey];
    }

    static YAML::Node processMerge(const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited) {
        if (!node.IsSequence()) {
            throw std::runtime_error("!merge erwartet eine Liste von Maps");
        }

        YAML::Node result(YAML::NodeType::Map);
        for (const auto& part : node) {
            YAML::Node evaluated = processNode(part, context, basePath, visited);
            if (!evaluated.IsMap()) {
                throw std::runtime_error("Alle Elemente in !merge müssen Maps sein");
            }

            for (auto it : evaluated) {
                result[it.first] = it.second;
            }
        }

        return result;
    }

    static YAML::Node processTemplate(const YAML::Node& node, const YAML::Node&) {
        if (!node.IsScalar()) {
            throw std::runtime_error("Makro !template erwartet einen String wie LL(CHAR, 19)");
        }

        const std::string expr = node.as<std::string>();  // z.B. "LL(CHAR, 19)"

        // Verschärfte "Eval"-Parsersimulation
        std::regex pattern(R"(^([L]{1,4})\((\w+),\s*(\d{1,4})\s*(?:,\s*(.*))?\)$)", std::regex::icase);
        std::smatch match;
        //std::regex pattern(R"(^([A-Z]+)\s*\(\s*([A-Z]+)\s*,\s*(\d+)\s*\)$)", std::regex::icase);
        //std::smatch match;

        if (!std::regex_match(expr, match, pattern)) {
            throw std::runtime_error("Ungültiger Makro-Ausdruck: " + expr + " (erwartet z.B. LL(CHAR, 19))");
        }

        std::string macro = match[1].str();
        std::string fmt = match[2];
        int length = std::stoi(match[3].str());

        static const std::set<std::string> allowed = { "L", "LL", "LLL", "LLLL" };
        if (!allowed.count(macro)) {
            throw std::runtime_error("Unbekannter Makro: " + macro);
        }
        if (macro == "L" && length > 9) throw std::runtime_error("Unzulässige Länge: " + length);
        if (macro == "LL" && length > 99) throw std::runtime_error("Unzulässige Länge: " + length);
        if (macro == "LLL" && length > 999) throw std::runtime_error("Unzulässige Länge: " + length);
        if (macro == "LLLL" && length > 9999) throw std::runtime_error("Unzulässige Länge: " + length);


        YAML::Node result(YAML::NodeType::Map);
        result["type"] = "scalar";
        result["format"] = macro + fmt;
        result["length"] = length;
        if (match[4].matched) {
            std::string description = match[4];
            result["description"] = description;
        }

        return result;
    }

    /*
    static bool startsWith(const std::string& s, const std::string& prefix) {
        return s.compare(0, prefix.size(), prefix) == 0;
    }
    */

    static std::string getPlaceholderValue(const std::string& placeholder, const YAML::Node& context) {
        // Hier gehst du davon aus, dass die Platzhalter im Kontext (z.B. in definitions) zu finden sind
        if (context[placeholder]) {
            return context[placeholder].as<std::string>();
        }

        // Falls der Platzhalter nicht gefunden wurde, kannst du einen Default-Wert zurückgeben oder eine Ausnahme werfen
        return "default_value";  // Hier kannst du auch einen Default-Wert zurückgeben oder eine Exception werfen.
    }

    static std::string resolvePlaceholders(const std::string& value, const YAML::Node& context) {
        std::string resolvedValue = value;

        // Hier suchst du nach Platzhaltern, die mit {{ und }} umschlossen sind
        std::regex placeholderPattern(R"(\{\{(\w+)\}\})");
        std::smatch matches;

        // Suche nach Platzhaltern im Text
        while (std::regex_search(resolvedValue, matches, placeholderPattern)) {
            for (size_t i = 1; i < matches.size(); ++i) {
                std::string placeholder = matches[i].str();
                // Versuche, den Wert des Platzhalters aus dem Kontext zu extrahieren
                std::string replacement = getPlaceholderValue(placeholder, context);
                resolvedValue.replace(matches.position(i), matches.length(i), replacement);
            }
        }

        return resolvedValue;
    }

    YAML::Node processNode(const YAML::Node& node,
        const YAML::Node& context,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& visited) {

        // **Neue Funktionalität**: Falls der Node ein Scalar ist, versuche, Platzhalter aufzulösen
        /*
        if (node.IsScalar()) {
            std::string value = node.as<std::string>();
            value = resolvePlaceholders(value, context);  // Hier Platzhalter auflösen
            return YAML::Node(value);  // Ersetze den Node durch den neuen Wert
        }
        */

        // Deine bestehende Behandlung der Tags (z.B. !template, !include, !merge)
        if (node.Tag() == "!template") {
            return processTemplate(node, context);
        }

        if (node.Tag() == "!include") {
            return processInclude(node, context);
        }

        if (node.Tag() == "!merge") {
            return processMerge(node, context, basePath, visited);
        }

        // Falls der Node eine Map ist, gehe rekursiv durch alle Keys und Werte
        if (node.IsMap()) {
            YAML::Node out(YAML::NodeType::Map);
            for (auto it : node) {
                out[it.first] = processNode(it.second, context, basePath, visited);
            }
            return out;
        }

        // Falls der Node eine Sequence ist, gehe rekursiv durch alle Elemente
        if (node.IsSequence()) {
            YAML::Node out(YAML::NodeType::Sequence);
            for (const auto& elem : node) {
                out.push_back(processNode(elem, context, basePath, visited));
            }
            return out;
        }

        // Rückgabe des unveränderten Nodes, wenn keine weiteren Operationen nötig sind
        return node;
    }
}

// === PUBLIC =================================================================
YAML::Node TNG_NAMESPACE::spec::SpecPreProcessor::preprocessFile(const std::string& path) {
    YAML::Node root;
    std::unordered_set<std::string> visitedFiles;

    std::filesystem::path basePath = std::filesystem::absolute(path).parent_path();
    std::vector<YAML::Node> docs = YAML::LoadAllFromFile(path);

    if (docs.empty()) {
        throw std::runtime_error("Leere YAML-Datei: " + path);
    }

    YAML::Node includeList = docs[0];
    // Wenn die Datei eine !include_files Sektion hat, dann verarbeite sie
    if (includeList.Tag() == "!include_files" && includeList.IsSequence()) {
        //std::cout << "Abhängigkeiten:\n" << includeList << '\n';
        YAML::Emitter emitter;
        emitter << includeList;
        TNG_LOG_TRACE("Abhängigkeiten:");
        TNG_LOG_TRACE("{}", emitter.c_str());

        YAML::Node mergedRoot(YAML::NodeType::Map);

        // 1. Include-Dateien laden
        for (const auto& includePathNode : includeList) {
            if (!includePathNode.IsScalar()) {
                throw std::runtime_error("!include_files enthält nicht-skalare Elemente");
            }

            std::string relPath = includePathNode.as<std::string>();
            std::filesystem::path fullPath = std::filesystem::absolute(basePath / relPath);

            if (!std::filesystem::exists(fullPath)) {
                throw std::runtime_error("Include-Datei nicht gefunden: " + fullPath.string());
            }

            std::string absPath = fullPath.string();
            if (visitedFiles.count(absPath)) {
                continue;
            }
            visitedFiles.insert(absPath);

            // 1.1 Abhängigkeiten von Include-Datei verarbeiten
            YAML::Node ext = preprocessFile(fullPath.string());  // Rekursive Verarbeitung
            if (!ext.IsMap()) {
                throw std::runtime_error("Include-Datei '" + fullPath.string() + "' muss ein YAML-Mapping sein");
            }

            YAML::Node processed = ::TNG_NAMESPACE::spec::processNode(ext, ext, fullPath.parent_path(), visitedFiles);
            ::TNG_NAMESPACE::spec::mergeInto(mergedRoot, processed);
        }

        // 2. Weitere Dokumentteile (ab docs[1]) hinzufügen
        for (size_t i = 1; i < docs.size(); ++i) {
            const auto& extra = docs[i];
            if (!extra.IsMap()) continue;

            // Merge "definitions" aus dem Hauptdokument
            if (extra["definitions"] && extra["definitions"].IsMap()) {
                if (!mergedRoot["definitions"] || !mergedRoot["definitions"].IsMap()) {
                    mergedRoot["definitions"] = YAML::Node(YAML::NodeType::Map);
                }
                YAML::Node shadow = mergedRoot["definitions"];
                ::TNG_NAMESPACE::spec::mergeDefinitions(shadow, extra["definitions"]);
                mergedRoot["definitions"] = shadow;
            }

            for (const auto& it : extra) {
                if (it.first.as<std::string>() != "definitions") {
                    mergedRoot[it.first] = it.second;
                }
            }
        }

        root = mergedRoot;
        return ::TNG_NAMESPACE::spec::processNode(root, root, basePath, visitedFiles);
    }
    // Wenn keine !include_files Sektion vorhanden ist, verarbeite die Datei direkt
    else {
        YAML::Node mergedRoot(YAML::NodeType::Map);

        if (docs[0].IsMap())
            root = docs[0];
        return ::TNG_NAMESPACE::spec::processNode(root, root, basePath, visitedFiles);
        /*
        // Hier fügst du die Daten aus der Datei direkt ohne das include_files Merging hinzu
        const auto& doc = docs[0];  // Die erste YAML-Node (Hauptdokument)
        if (doc.IsMap()) {
            for (const auto& it : doc) {
                mergedRoot[it.first] = it.second;

            }
        }

        root = mergedRoot;
        return processNode(root, root, basePath, visitedFiles);
        */
    }
}