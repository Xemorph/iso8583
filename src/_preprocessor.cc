#include "_preprocessor.hh"
// [stdc++]
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// [tng/internal]
#include "_logger.hh"

namespace TNG_NAMESPACE::spec {

    // =============================================================================
    // Typen
    // =============================================================================

    /// Trackt welche Definition aus welcher Datei stammt.
    using DefOriginMap = std::unordered_map<std::string, std::string>;

    struct ProcessContext {
        const YAML::Node& root_context; // für !use-Lookups
        const std::filesystem::path& base_path;
        std::unordered_set<std::string>& visited;
        SourceMap* smap;
        std::string                      current_file;
        const DefOriginMap* def_origins;
    };

    // =============================================================================
    // Forward-Deklaration
    // =============================================================================
    static YAML::Node processNode(const YAML::Node& node, ProcessContext& ctx);

    // =============================================================================
    // deepClone – nur für Stellen wo yaml-cpp Referenz-Aliasing vermieden werden muss
    // =============================================================================
    static YAML::Node deepClone(const YAML::Node& node) {
        switch (node.Type()) {
        case YAML::NodeType::Map: {
            YAML::Node out(YAML::NodeType::Map);
            for (auto it : node)
                out[it.first.as<std::string>()] = deepClone(it.second);
            return out;
        }
        case YAML::NodeType::Sequence: {
            YAML::Node out(YAML::NodeType::Sequence);
            for (const auto& e : node)
                out.push_back(deepClone(e));
            return out;
        }
        default:
            return YAML::Node(node.Scalar());
        }
    }

    // =============================================================================
    // mergeInto – Definitionen OHNE Clone damit YAML::Mark erhalten bleibt
    // =============================================================================
    static void mergeInto(YAML::Node& dest,
        const YAML::Node& source,
        DefOriginMap& def_origins,
        const std::string& source_file)
    {
        if (!source.IsMap()) return;
        for (auto it : source) {
            const std::string key = it.first.as<std::string>();
            if (key == "definitions") {
                if (!dest["definitions"] || !dest["definitions"].IsMap())
                    dest["definitions"] = YAML::Node(YAML::NodeType::Map);
                if (it.second.IsMap()) {
                    for (auto def : it.second) {
                        const std::string defName = def.first.as<std::string>();
                        // Originaler Node ohne Clone: YAML::Mark bleibt erhalten
                        dest["definitions"][defName] = def.second;
                        def_origins[defName] = source_file;
                    }
                }
            }
            else {
                dest[it.first] = it.second;
            }
        }
    }

    // =============================================================================
    // SourceMap-Tracking
    // =============================================================================
    static void track(SourceMap* smap,
        int                proc_line,
        const std::string& origin_file,
        int                origin_line,
        int                origin_col)
    {
        if (!smap || proc_line <= 0) return;
        smap->record(proc_line, SourceLocation{ origin_file, origin_line, origin_col });
    }

    // =============================================================================
    // Direktiv-Handler
    // =============================================================================

    static YAML::Node processUse(const YAML::Node& node,
        ProcessContext& ctx,
        bool               is_legacy = false)
    {
        if (!node.IsScalar())
            throw std::runtime_error(
                is_legacy ? "!include erwartet einen Schlüsselstring"
                : "!use erwartet einen Schlüsselstring");

        const std::string refKey = node.as<std::string>();
        if (is_legacy)
            TNG_LOG_WARN("[Preprocessor] '!include {}' veraltet – bitte '!use {}' verwenden",
                refKey, refKey);

        if (!ctx.root_context["definitions"] ||
            !ctx.root_context["definitions"][refKey])
            throw std::runtime_error(
                "!use verweist auf unbekannte Definition '" + refKey + "' – "
                "ist die Definition in 'definitions' oder via '!include_files' geladen?");

        // Ursprungs-Datei der Definition nachschlagen
        std::string def_file = ctx.current_file;
        if (ctx.def_origins) {
            auto it = ctx.def_origins->find(refKey);
            if (it != ctx.def_origins->end())
                def_file = it->second;
        }

        // Track: !use-Stelle in der aufrufenden Datei
        track(ctx.smap,
            node.Mark().line + 1,
            ctx.current_file,
            node.Mark().line + 1,
            node.Mark().column + 1);

        // Expansion mit Definitions-Datei als current_file –
        // damit werden die Kinder-Nodes mit der richtigen Datei getrackt
        ProcessContext defCtx = ctx;
        defCtx.current_file = def_file;
        return processNode(ctx.root_context["definitions"][refKey], defCtx);
    }

    static YAML::Node processTemplate(const YAML::Node& node) {
        if (!node.IsScalar())
            throw std::runtime_error("!template erwartet einen Ausdruck wie 'LL(CHAR, 19)'");

        const std::string expr = node.as<std::string>();
        static const std::regex pattern(
            R"(^(L{1,4})\((\w+),\s*(\d{1,4})\s*(?:,\s*(.*))?\)$)",
            std::regex::icase);

        std::smatch m;
        if (!std::regex_match(expr, m, pattern))
            throw std::runtime_error(
                "Ungültiger !template-Ausdruck: '" + expr + "'\n"
                "  Erwartetes Format: PREFIX(FORMAT, MAX_LENGTH)\n"
                "  Beispiele:  LL(CHAR, 19)  |  LLL(BINARY, 255)");

        const std::string prefix = m[1].str();
        const std::string fmt = m[2].str();
        const int         length = std::stoi(m[3].str());

        static const std::vector<std::pair<std::string, int>> limits = {
            {"L",9},{"LL",99},{"LLL",999},{"LLLL",9999}
        };
        for (auto& [p, max] : limits)
            if (prefix == p && length > max)
                throw std::runtime_error(
                    "!template: Länge " + std::to_string(length) +
                    " überschreitet Maximum " + std::to_string(max) +
                    " für Prefix '" + prefix + "'");

        YAML::Node result(YAML::NodeType::Map);
        result["format"] = prefix + fmt;
        result["length"] = length;
        if (m[4].matched && !m[4].str().empty())
            result["description"] = m[4].str();
        return result;
    }

    static YAML::Node processMerge(const YAML::Node& node, ProcessContext& ctx) {
        if (!node.IsSequence())
            throw std::runtime_error("!merge erwartet eine Liste von Maps");

        YAML::Node result(YAML::NodeType::Map);
        for (const auto& part : node) {
            YAML::Node evaluated = processNode(part, ctx);
            if (!evaluated.IsMap())
                throw std::runtime_error("Alle Elemente in !merge müssen Maps sein");
            for (auto it : evaluated)
                result[it.first] = it.second;
        }
        return result;
    }

    // =============================================================================
    // processNode
    // Gibt wo möglich originale Nodes zurück damit YAML::Mark erhalten bleibt.
    // Trackt jeden Node in der SourceMap mit current_file.
    // =============================================================================
    static YAML::Node processNode(const YAML::Node& node, ProcessContext& ctx) {
        const std::string tag = node.Tag();

        if (tag == "!use")      return processUse(node, ctx, false);
        if (tag == "!include")  return processUse(node, ctx, true);
        if (tag == "!template") return processTemplate(node);
        if (tag == "!merge")    return processMerge(node, ctx);

        if (node.IsMap()) {
            YAML::Node out(YAML::NodeType::Map);
            for (auto it : node) {
                const YAML::Mark key_mark = it.first.Mark();
                YAML::Node val = processNode(it.second, ctx);

                track(ctx.smap,
                    key_mark.line + 1,
                    ctx.current_file,
                    key_mark.line + 1,
                    key_mark.column + 1);

                out[it.first] = val;
            }
            return out;
        }

        if (node.IsSequence()) {
            YAML::Node out(YAML::NodeType::Sequence);
            for (const auto& elem : node)
                out.push_back(processNode(elem, ctx));
            return out;
        }

        // Scalar: Original-Node zurückgeben damit Mark erhalten bleibt.
        // pos > 0: Zeile 1 (line=0, col=0, pos=0) würde sonst herausgefiltert.
        if (node.IsScalar() && node.Mark().pos > 0) {
            track(ctx.smap,
                node.Mark().line + 1,
                ctx.current_file,
                node.Mark().line + 1,
                node.Mark().column + 1);
        }
        return node;
    }

} // namespace TNG_NAMESPACE::spec

// =============================================================================
// Öffentliche API
// =============================================================================

YAML::Node TNG_NAMESPACE::spec::SpecPreProcessor::preprocessFile(
    const std::string& path)
{
    return preprocessWithSourceMap(path).node;
}

TNG_NAMESPACE::spec::PreprocessResult
TNG_NAMESPACE::spec::SpecPreProcessor::preprocessWithSourceMap(
    const std::string& path)
{
    namespace fs = std::filesystem;
    using namespace TNG_NAMESPACE::spec;

    const fs::path    absPath = fs::absolute(path);
    const std::string smapPath = absPath.string() + ".smap";

    std::unordered_set<std::string> visitedFiles;
    std::vector<std::string>        allFiles;
    DefOriginMap                    defOrigins;

    PreprocessResult result;
    SourceMap& smap = result.source_map;

    // ── Akkumulierter Definitions-Node ────────────────────────────────────────
    // Enthält alle geladenen Definitionen mit ihren originalen YAML::Mark.
    // Wird als { "definitions": { "name": node, ... } } gewrappt damit
    // processUse via ctx.root_context["definitions"][refKey] zugreifen kann.
    YAML::Node defsWrapper(YAML::NodeType::Map);
    defsWrapper["definitions"] = YAML::Node(YAML::NodeType::Map);
    YAML::Node& globalDefs = defsWrapper["definitions"];

    // ── Rekursive Ladefunktion ────────────────────────────────────────────────
    // Jede Datei wird SEPARAT geladen und prozessiert.
    // Nur Definitions werden global akkumuliert (für !use-Lookups).
    // Der Rest (fields, spec, encoding) wird direkt in root gemergt.
    std::function<void(const std::string&, YAML::Node&)> loadAndProcess =
        [&](const std::string& filePath, YAML::Node& root)
        {
            const fs::path    abs = fs::absolute(filePath);
            const std::string absStr = abs.string();

            allFiles.push_back(absStr);
            visitedFiles.insert(absStr);

            std::vector<YAML::Node> docs = YAML::LoadAllFromFile(absStr);
            if (docs.empty())
                throw std::runtime_error("Leere YAML-Datei: " + absStr);

            // !include_files: erst Abhängigkeiten laden
            const YAML::Node& first = docs[0];
            if (first.Tag() == "!include_files" && first.IsSequence()) {
                TNG_LOG_DEBUG("[Preprocessor] !include_files in '{}'", absStr);

                for (const auto& entry : first) {
                    const fs::path fullPath = fs::absolute(
                        abs.parent_path() / entry.as<std::string>());

                    if (!fs::exists(fullPath))
                        throw std::runtime_error(
                            "!include_files: Datei nicht gefunden: " + fullPath.string() +
                            "\n  Referenziert von: " + absStr);

                    const std::string fullStr = fullPath.string();
                    if (visitedFiles.count(fullStr)) {
                        TNG_LOG_DEBUG("[Preprocessor] Duplikat übersprungen: {}", fullStr);
                        continue;
                    }

                    // Rekursiv laden: Definitions landen in globalDefs,
                    // andere Felder in root – jeweils mit korrekter current_file
                    loadAndProcess(fullStr, root);
                }

                // Restliche docs der Haupt-Sequenz-Datei prozessieren
                for (std::size_t i = 1; i < docs.size(); ++i) {
                    // Definitions separat sammeln (mit korrekter Datei)
                    if (docs[i]["definitions"] && docs[i]["definitions"].IsMap()) {
                        for (auto def : docs[i]["definitions"]) {
                            const std::string defName = def.first.as<std::string>();
                            globalDefs[defName] = def.second;
                            defOrigins[defName] = absStr;
                        }
                    }
                    // Felder prozessieren
                    ProcessContext ctx{ defsWrapper, abs.parent_path(),
                                       visitedFiles, &smap, absStr, &defOrigins };
                    YAML::Node processed = processNode(docs[i], ctx);
                    mergeInto(root, processed, defOrigins, absStr);
                }

            }
            else {
                // Keine !include_files: alle docs direkt prozessieren
                for (auto& doc : docs) {
                    // Definitions zuerst in globalDefs aufnehmen (mit korrekter Datei)
                    if (doc["definitions"] && doc["definitions"].IsMap()) {
                        for (auto def : doc["definitions"]) {
                            const std::string defName = def.first.as<std::string>();
                            globalDefs[defName] = def.second;
                            defOrigins[defName] = absStr;
                        }
                    }
                    // Felder prozessieren – globalDefs als root_context für !use
                    ProcessContext ctx{ defsWrapper, abs.parent_path(),
                                       visitedFiles, &smap, absStr, &defOrigins };
                    YAML::Node processed = processNode(doc, ctx);
                    mergeInto(root, processed, defOrigins, absStr);
                }
            }
        };

    // ── Verarbeitung starten ──────────────────────────────────────────────────
    YAML::Node root(YAML::NodeType::Map);
    loadAndProcess(absPath.string(), root);
    result.node = root;

    smap.finalise(allFiles);

    // ── Sidecar prüfen / schreiben ────────────────────────────────────────────
    const std::string currentHash = smap.hash();
    if (auto loaded = SourceMap::load(smapPath, currentHash)) {
        result.source_map = std::move(*loaded);
        TNG_LOG_DEBUG("[Preprocessor] SourceMap aus Sidecar geladen: {}", smapPath);
    }
    else {
        smap.save(smapPath);
        TNG_LOG_DEBUG("[Preprocessor] SourceMap neu geschrieben: {}", smapPath);
    }

    return result;
}
