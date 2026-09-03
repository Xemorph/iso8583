#include "_preprocessor.hh"
// [stdc++]
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
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
    // Architektur (yaml-cpp -> ryml, Phase 3+4 der Migration)
    // =============================================================================
    //
    // ALLES (akkumulierte Definitionen, Zwischenergebnisse) lebt in EINEM
    // einzigen `ryml::Tree` ("workspace"). Jede Datei wird zunächst in einen
    // EIGENEN, TEMPORÄREN `ryml::Tree` ("filetree") geparst - notwendig, weil
    // nur ein frisch per parse_in_arena() erzeugter STANDALONE-Tree YAML-
    // STREAMS (mehrere "---"-getrennte Dokumente pro Datei) korrekt erkennt.
    // Der Datei-Inhalt darf dabei NICHT über eine ANDERE, gleichzeitig aktive
    // Tree-Arena an parse_in_arena() übergeben werden (Aliasing-Eigenheit
    // zwischen zwei gleichzeitig existierenden Arenen, korrumpiert einzelne
    // Dokument-Knoten). Beides empirisch geprüft (ryml v0.15.2).
    //
    // Da filetree seine EIGENE Arena hat, muss filetree für die gesamte
    // restliche Verarbeitung am Leben bleiben (merge_with() kopiert nur
    // Baumstruktur, keine String-Bytes) - siehe fileTreesKeepAlive unten.
    //
    // Das ENDERGEBNIS lebt in einem SEPARATEN Tree (result.tree, siehe
    // _preprocessor.hh) - nicht im workspace selbst. So bleibt result.tree
    // absolut sauber (keine __scratch__/__definitions__-Bereiche) und die
    // Knoten-IDs, die die SourceMap referenziert, sind exakt die IDs, die
    // _spec.cc später sieht - keine weitere Kopie mehr nötig.
    //
    // KRITISCHE REGEL für den gesamten Code unten: set_key() IMMER ALS
    // LETZTEN Schritt aufrufen, NACHDEM set_type()/append_child()/
    // merge_with() für denselben Knoten bereits gelaufen sind - nicht davor!
    // Empirisch geprüft (ryml v0.15.2): set_type() (und merge_with() intern)
    // überschreibt die Knoten-Flags komplett, inklusive des "hat einen Key"-
    // Bits. Ein vorher gesetzter Key geht dadurch stillschweigend verloren.
    //
    // Positions-Tracking (SourceMap): ryml erlaubt Location-Abfragen nur für
    // den ZULETZT mit einem Parser-Objekt geparsten Baum, und Knoten-IDs
    // ändern sich bei JEDEM merge_with()-Kopiervorgang (Quell- und
    // Ziel-Knoten haben unterschiedliche IDs, auch innerhalb desselben
    // Trees). Es gibt deshalb KEINE stabile "prozessierte Zeile" mehr, wie
    // es sie mit yaml-cpp gab. Stattdessen: für jede Datei wird EINMALIG,
    // solange ihr Parser noch lebt, eine Origin-Map (Knoten-ID -> wahre
    // Quellposition) aufgebaut. Bei JEDEM merge_with()-Kopiervorgang läuft
    // anschließend propagateOrigins() im Gleichschritt über Quelle und Kopie
    // und überträgt die bekannte Herkunft auf die NEUEN Knoten-IDs der
    // Kopie. So wandert die Herkunfts-Info über alle Kopierschritte hinweg
    // mit, bis sie beim finalen Schreiben nach result.tree in die
    // eigentliche (öffentliche) SourceMap übernommen wird - jetzt mit
    // result.tree's IDs als Schlüssel, exakt was _spec.cc sehen wird.
    // =============================================================================

    /// Herkunfts-Map: Knoten-ID (in EINEM bestimmten Baum) -> wahre Quellposition.
    /// Rein intern/transient - nicht zu verwechseln mit der öffentlichen SourceMap
    /// (die am Ende, mit result.tree's IDs als Schlüssel, befüllt wird).
    using OriginMap = std::unordered_map<ryml::id_type, SourceLocation>;

    /// Trackt welche Definition aus welcher Datei stammt.
    using DefOriginMap = std::unordered_map<std::string, std::string>;

    // Verhindert Stack-Overflow bei böswillig oder versehentlich extrem tief
    // verschachtelten YAML-Strukturen - wandelt einen harten Absturz in eine
    // saubere, fangbare Exception um. 200 ist für jede realistische Spec
    // (typischerweise 3-5 Ebenen: fields -> field -> children -> child -> tlv)
    // weit mehr Spielraum als je gebraucht wird.
    static constexpr int MAX_RECURSION_DEPTH = 200;

    static void checkDepth(int depth) {
        if (depth > MAX_RECURSION_DEPTH)
            throw std::runtime_error(
                "YAML-Struktur zu tief verschachtelt (> " + std::to_string(MAX_RECURSION_DEPTH) +
                " Ebenen) - möglicherweise eine zirkuläre !use-Referenz oder eine "
                "fehlerhafte/böswillige Spec-Datei.");
    }

    // =============================================================================
    // [ISO8583] 3.1 (Sicherheits-Audit): Include-Sandbox
    // =============================================================================
    // Prüft, ob der Pfad `p` mit `root` identisch ist oder innerhalb (der
    // kanonisierten) Wurzel `root` liegt. Auf Windows ist der Vergleich
    // case-insensitiv: das Dateisystem ist es auch, und fs::canonical() kann
    // eine andere Groß-/Kleinschreibung liefern als fs::absolute() - ein
    // case-sensitiver Vergleich würde dann gültige Includes verwerfen.
    //
    // Alias-Fallback: 8.3-Kurznamen (GitHub-Windows-Runner setzt
    // TEMP=C:\Users\RUNNER~1\AppData\Local\Temp, fs::canonical() liefert
    // C:\Users\runneradmin\...) oder Symlinks/Junctions (macOS /
    // tmp->/private/tmp) sind lexikalisch verschieden, aber physikalisch
    // identisch. Nach einem negativen lexikalischen Ergebnis wird `p` mit
    // fs::weakly_canonical() aufgelöst (löst das existierende Präfix auf,
    // funktioniert auch wenn die Endkomponente noch fehlt) und erneut
    // gegen die kanonisierte Wurzel geprüft. Fail-closed bleibt erhalten:
    // ein wirklich außerhalb liegender Pfad kanonisiert auf eine
    // außerhalb liegende Form und wird weiterhin abgelehnt.
    static bool isWithinRoot(const std::filesystem::path& p, const std::filesystem::path& root) {
        auto cmp = [](const std::filesystem::path& a, const std::filesystem::path& b) {
#if defined(_WIN32)
            auto la = a.string();
            auto lb = b.string();
            std::transform(la.begin(), la.end(), la.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lb.begin(), lb.end(), lb.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return la == lb;
#else
            return a == b;
#endif
        };
        auto withinLexical = [&cmp, &root](const std::filesystem::path& a) {
            auto itP = a.begin();
            for (auto itR = root.begin(); itR != root.end(); ++itR) {
                if (itP == a.end() || !cmp(*itP, *itR))
                    return false;
                ++itP;
            }
            return true; // alle Root-Komponenten abgedeckt, `a` darf tiefer liegen
        };
        if (withinLexical(p))
            return true;
        std::error_code ec;
        const auto alias = std::filesystem::weakly_canonical(p, ec);
        if (!ec)
            return withinLexical(alias);
        return false;
    }

    static std::string rootsToString(const std::vector<std::filesystem::path>& roots) {
        std::string s;
        for (const auto& r : roots) {
            if (!s.empty()) s += ", ";
            s += r.string();
        }
        return s;
    }

    struct ProcessContext {
        ryml::Tree* ws;                     // Workspace - besitzt Scratch/Definitionen
        ryml::id_type defs_id;              // workspace["__definitions__"]
        ryml::id_type scratch_id;           // workspace["__scratch__"]
        std::filesystem::path base_path;
        std::unordered_set<std::string>& visited;
        DefOriginMap* def_origins;
        OriginMap* origins;                 // Herkunft, keyed by *ws*-Knoten-ID
        // Namen der Definitionen, die GERADE (in der aktuellen !use-
        // Aufrufkette) aufgelöst werden - erkennt zirkuläre !use-Referenzen
        // (A !use B, B !use A) mit einer präzisen Fehlermeldung, statt sich
        // auf den generischen Tiefenlimit-Schutz (checkDepth) zu verlassen.
        std::unordered_set<std::string> activeUseChain;
    };

    // =============================================================================
    // Forward-Deklarationen
    // =============================================================================
    static void processNode(ryml::ConstNodeRef src, ryml::NodeRef dst, ProcessContext& ctx, int depth = 0);

    // =============================================================================
    // Hilfsfunktionen
    // =============================================================================

    static ryml::NodeRef scratchNode(ProcessContext& ctx) {
        return ctx.ws->ref(ctx.scratch_id).append_child();
    }

    static std::string toStdString(ryml::csubstr s) {
        return std::string(s.str, s.len);
    }

    // Läuft `src` und `dst` STRUKTURELL PARALLEL (dst ist eine merge_with-Kopie
    // von src - gleiche Keys, gleiche Reihenfolge) und überträgt für jeden
    // Knoten die bekannte Herkunft aus `originsIn[src.id()]` nach
    // `originsOut[dst.id()]`. originsIn und originsOut dürfen dieselbe Map
    // sein (Same-Tree-Merge) - sicher, da dst immer NEUE, bei diesem Aufruf
    // noch nicht existierende IDs hat.
    static void propagateOrigins(ryml::ConstNodeRef src, ryml::ConstNodeRef dst,
        const OriginMap& originsIn, OriginMap& originsOut, int depth = 0)
    {
        checkDepth(depth);
        auto it = originsIn.find(src.id());
        if (it != originsIn.end())
            originsOut.emplace(dst.id(), it->second);

        if ((src.is_map() && dst.is_map()) || (src.is_seq() && dst.is_seq())) {
            auto sit = src.children().begin();
            auto dit = dst.children().begin();
            for (; sit != src.children().end() && dit != dst.children().end(); ++sit, ++dit)
                propagateOrigins(*sit, *dit, originsIn, originsOut, depth + 1);
        }
    }

    // Läuft rekursiv über `tree` und kopiert JEDEN Key/Value über
    // tree.to_arena() in TREE'S EIGENE Arena um. Notwendig, weil
    // merge_with() nur die Baumstruktur kopiert, NICHT die zugrundeliegenden
    // String-Bytes - Keys/Values eines per merge_with() kopierten Knotens
    // zeigen deshalb weiterhin in die Arena des URSPRÜNGLICHEN Baums (hier:
    // ws bzw. die einzelnen filetree-Instanzen). Da diese Bäume nach dem
    // Ende von preprocessWithSourceMap() zerstört werden, MUSS result.tree
    // vor der Rückgabe vollständig eigenständig gemacht werden - sonst
    // zeigen alle Keys/Values ins Leere (dangling), sobald der Aufrufer das
    // Ergebnis benutzt.
    static void makeSelfContained(ryml::Tree& tree, ryml::NodeRef node, int depth = 0) {
        checkDepth(depth);
        if (node.has_key())
            node.set_key(tree.to_arena(node.key()));
        if (node.has_val())
            node.set_val(tree.to_arena(node.val()));
        if (node.is_map() || node.is_seq())
            for (ryml::NodeRef child : node.children())
                makeSelfContained(tree, child, depth + 1);
    }

    // =============================================================================
    // ryml-Fehlerbehandlung: Exceptions statt abort()
    // =============================================================================
    // KRITISCH: ryml ruft standardmäßig std::abort() bei JEDEM Parse-/Basic-/
    // Visit-Fehler auf - nicht etwa eine C++-Exception. Ohne diese explizite
    // Umstellung würde jede fehlerhafte YAML-Spec-Datei den GESAMTEN Prozess
    // abschießen, statt (wie von SpecDecoder::loadFromYaml() dokumentiert und
    // vom Rest dieser Bibliothek erwartet) einen fangbaren std::runtime_error
    // zu werfen. Wird einmalig beim ersten Preprocessing-Aufruf installiert.
    // =============================================================================
    [[noreturn]] static void rymlErrorBasic(ryml::csubstr msg, ryml::ErrorDataBasic const&, void*) {
        throw std::runtime_error("YAML-Fehler: " + std::string(msg.str, msg.len));
    }
    [[noreturn]] static void rymlErrorParse(ryml::csubstr msg, ryml::ErrorDataParse const&, void*) {
        throw std::runtime_error("YAML-Parse-Fehler: " + std::string(msg.str, msg.len));
    }
    [[noreturn]] static void rymlErrorVisit(ryml::csubstr msg, ryml::ErrorDataVisit const&, void*) {
        throw std::runtime_error("YAML-Fehler: " + std::string(msg.str, msg.len));
    }

    static void ensureRymlThrowsExceptions() {
        static const bool installed = [] {
            ryml::Callbacks cb = ryml::get_callbacks();
            cb.m_error_basic = &rymlErrorBasic;
            cb.m_error_parse = &rymlErrorParse;
            cb.m_error_visit = &rymlErrorVisit;
            ryml::set_callbacks(cb);
            return true;
        }();
        (void)installed;
    }

    // =============================================================================
    // hasDirective – rein lesende, rekursive Prüfung ob `node` oder ein
    // Nachfahre einen der vier Preprocessor-Tags trägt. Deutlich günstiger
    // als eine volle Knoten-für-Knoten-Rekursion, da nur Tags gelesen werden
    // - für Teilbäume ohne jede Direktive reicht ein Bulk-merge_with().
    // =============================================================================
    static bool hasDirective(ryml::ConstNodeRef node, int depth = 0) {
        checkDepth(depth);
        if (node.has_val_tag()) {
            ryml::csubstr t = node.val_tag();
            if (t == "!use" || t == "!include" || t == "!template" || t == "!merge")
                return true;
        }
        if (node.is_map() || node.is_seq()) {
            for (ryml::ConstNodeRef child : node.children())
                if (hasDirective(child, depth + 1)) return true;
        }
        return false;
    }

    // =============================================================================
    // Direktiv-Handler
    //
    // Alle Handler setzen NIE einen Key auf `dst` - das ist immer Sache des
    // AUFRUFERS, und zwar NACH dem Handler-Aufruf (siehe KRITISCHE REGEL oben).
    // =============================================================================

    static void processUse(ryml::ConstNodeRef src, ryml::NodeRef dst,
        ProcessContext& ctx, bool is_legacy, int depth)
    {
        checkDepth(depth);
        if (src.is_map() || src.is_seq() || !src.has_val())
            throw std::runtime_error(
                is_legacy ? "!include erwartet einen Schlüsselstring"
                : "!use erwartet einen Schlüsselstring");

        const std::string refKey = toStdString(src.val());
        if (is_legacy)
            TNG_LOG_WARN("[Preprocessor] '!include {}' veraltet – bitte '!use {}' verwenden",
                refKey, refKey);

        ryml::ConstNodeRef defs = ctx.ws->cref(ctx.defs_id);
        ryml::csubstr refKeyC = ryml::to_csubstr(refKey);
        if (!defs.has_child(refKeyC))
            throw std::runtime_error(
                "!use verweist auf unbekannte Definition '" + refKey + "' – "
                "ist die Definition in 'definitions' oder via '!include_files' geladen?");

        // Zirkuläre !use-Referenzen (A !use B, B !use A, oder längere Ketten)
        // erkennen, BEVOR sie den generischen Tiefenlimit-Schutz auslösen -
        // deutlich präzisere Fehlermeldung für den in der Praxis häufigsten
        // Auslöser einer Endlos-Rekursion (ein Autorenfehler, keine böswillige
        // Datei).
        if (ctx.activeUseChain.count(refKey)) {
            std::string chain;
            for (const auto& k : ctx.activeUseChain) chain += k + " -> ";
            throw std::runtime_error(
                "Zirkuläre !use-Referenz erkannt: " + chain + refKey);
        }
        ctx.activeUseChain.insert(refKey);

        // Definitionen wurden beim Akkumulieren bereits vollständig getrackt.
        try {
            processNode(defs[refKeyC], dst, ctx, depth + 1);
        }
        catch (...) {
            ctx.activeUseChain.erase(refKey);
            throw;
        }
        ctx.activeUseChain.erase(refKey);
    }

    static void processTemplate(ryml::ConstNodeRef src, ryml::NodeRef dst, ProcessContext& ctx, int depth) {
        checkDepth(depth);
        if (src.is_map() || src.is_seq() || !src.has_val())
            throw std::runtime_error("!template erwartet einen Ausdruck wie 'LL(CHAR, 19)'");

        const std::string expr = toStdString(src.val());
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

        dst.set_type(ryml::MAP);

        auto fmt_child = dst.append_child();
        fmt_child.set_type(ryml::VAL);
        fmt_child.set_val(ctx.ws->to_arena(prefix + fmt));
        fmt_child.set_key("format");

        auto len_child = dst.append_child();
        len_child.set_type(ryml::VAL);
        len_child.set_val(ctx.ws->to_arena(length));
        len_child.set_key("length");

        ryml::id_type desc_id = ryml::NONE;
        if (m[4].matched && !m[4].str().empty()) {
            auto desc_child = dst.append_child();
            desc_child.set_type(ryml::VAL);
            desc_child.set_val(ctx.ws->to_arena(m[4].str()));
            desc_child.set_key("description");
            desc_id = desc_child.id();
        }

        // Herkunft: synthetisierte Felder (format/length/description werden
        // berechnet, nicht aus der Quelldatei kopiert) bekommen die Position
        // des "!template ..."-Ausdrucks selbst zugewiesen - präziser geht's
        // hier nicht.
        auto it = ctx.origins->find(src.id());
        if (it != ctx.origins->end()) {
            ctx.origins->emplace(dst.id(), it->second);
            ctx.origins->emplace(fmt_child.id(), it->second);
            ctx.origins->emplace(len_child.id(), it->second);
            if (desc_id != ryml::NONE)
                ctx.origins->emplace(desc_id, it->second);
        }
    }

    static void processMerge(ryml::ConstNodeRef src, ryml::NodeRef dst, ProcessContext& ctx, int depth) {
        checkDepth(depth);
        if (!src.is_seq())
            throw std::runtime_error("!merge erwartet eine Liste von Maps");

        dst.set_type(ryml::MAP);
        for (ryml::ConstNodeRef part : src.children()) {
            ryml::NodeRef scratch = scratchNode(ctx);
            processNode(part, scratch, ctx, depth + 1);

            if (!scratch.is_map())
                throw std::runtime_error("Alle Elemente in !merge müssen Maps sein");

            for (ryml::ConstNodeRef child : scratch.children()) {
                // Vorhandenen Eintrag (falls von einem früheren !merge-Teil)
                // komplett entfernen, bevor der neue geschrieben wird - sonst
                // würde merge_with() weiter unten rekursiv in den ALTEN
                // Inhalt hineinmergen statt ihn (wie im Original) komplett zu
                // ersetzen. "Spätere Teile überschreiben frühere" gilt hier
                // bewusst nur auf oberster Ebene (flach), nicht rekursiv -
                // exakt das Verhalten der ursprünglichen yaml-cpp-Version.
                if (dst.has_child(child.key()))
                    ctx.ws->remove(dst.find_child(child.key()).id());
                ryml::NodeRef out = dst.append_child();
                out.set_type(child.type());
                ctx.ws->merge_with(ctx.ws, child.id(), out.id());
                propagateOrigins(child, out, *ctx.origins, *ctx.origins);
                out.set_key(child.key());  // ZULETZT (siehe KRITISCHE REGEL)
            }
        }
    }

    // =============================================================================
    // processNode – arbeitet ausschließlich auf bereits workspace-residenten
    // Knoten. Setzt NIE selbst den Key von `dst` (Sache des Aufrufers, NACH
    // diesem Aufruf - siehe KRITISCHE REGEL oben).
    //
    // PERFORMANCE: Für einen Teilbaum ganz ohne !use/!include/!template/
    // !merge reicht ein Bulk-merge_with() (intern optimiert) statt Knoten-
    // für-Knoten-Rekursion - siehe hasDirective() oben.
    // =============================================================================
    static void processNode(ryml::ConstNodeRef src, ryml::NodeRef dst, ProcessContext& ctx, int depth) {
        checkDepth(depth);
        if (src.has_val_tag()) {
            ryml::csubstr tag = src.val_tag();
            if (tag == "!use")      { processUse(src, dst, ctx, false, depth + 1); return; }
            if (tag == "!include")  { processUse(src, dst, ctx, true,  depth + 1); return; }
            if (tag == "!template") { processTemplate(src, dst, ctx, depth + 1);   return; }
            if (tag == "!merge")    { processMerge(src, dst, ctx, depth + 1);      return; }
        }

        if (src.is_map()) {
            dst.set_type(ryml::MAP);
            if (!hasDirective(src)) {
                ctx.ws->merge_with(ctx.ws, src.id(), dst.id());
                propagateOrigins(src, dst, *ctx.origins, *ctx.origins);
                return;
            }
            for (ryml::ConstNodeRef child : src.children()) {
                ryml::NodeRef out_child = dst.append_child();
                processNode(child, out_child, ctx, depth + 1);
                out_child.set_key(child.key());  // ZULETZT (siehe KRITISCHE REGEL)
                auto it = ctx.origins->find(child.id());
                if (it != ctx.origins->end())
                    ctx.origins->emplace(out_child.id(), it->second);
            }
            return;
        }

        if (src.is_seq()) {
            dst.set_type(ryml::SEQ);
            if (!hasDirective(src)) {
                ctx.ws->merge_with(ctx.ws, src.id(), dst.id());
                propagateOrigins(src, dst, *ctx.origins, *ctx.origins);
                return;
            }
            for (ryml::ConstNodeRef child : src.children()) {
                ryml::NodeRef out_child = dst.append_child();
                processNode(child, out_child, ctx, depth + 1);
                auto it = ctx.origins->find(child.id());
                if (it != ctx.origins->end())
                    ctx.origins->emplace(out_child.id(), it->second);
            }
            return;
        }

        // Scalar: `src` und `dst` liegen im selben Baum - der Wert zeigt
        // bereits in workspace's eigene Arena, kein to_arena() nötig (nur
        // für SYNTHETISIERTE Werte wie in !template gebraucht).
        dst.set_type(ryml::VAL);
        dst.set_val(src.val());
        auto it = ctx.origins->find(src.id());
        if (it != ctx.origins->end())
            ctx.origins->emplace(dst.id(), it->second);
    }

} // namespace TNG_NAMESPACE::spec

// =============================================================================
// Öffentliche API
// =============================================================================

TNG_NAMESPACE::spec::PreprocessResult
TNG_NAMESPACE::spec::SpecPreProcessor::preprocessWithSourceMap(
    const std::string& path, const SpecLoadOptions& opts)
{
    namespace fs = std::filesystem;
    using namespace TNG_NAMESPACE::spec;

    const bool trackSourceMap = opts.trackSourceMap;

    ensureRymlThrowsExceptions();

    const fs::path    absPath = fs::absolute(path);
    const std::string smapPath = absPath.string() + ".smap";

    // [ISO8583] 3.1 (Sicherheits-Audit): Sandbox-Wurzeln vorbereiten.
    // Default (leeres `opts.roots`): das Verzeichnis der Top-Level-Spec.
    // Wurzel-Pfade werden kanonisiert (Symlinks aufgelöst, echte Groß/
    // Kleinschreibung) - existiert eine Wurzel nicht, ist das ein Load-Fehler
    // (fail-closed, statt still zu degradieren).
    // Die Top-Level-Datei selbst ist Wahl der Anwendung (kein Include) und
    // wird NICHT gegen die Wurzeln geprüft; ihr Vorhandensein wird bei
    // aktivierter Sandbox vorab geprüft, damit die (sonst) präzisere
    // "Datei nicht lesbar"-Meldung vor eventuellen Wurzel-Fehlern kommt.
    std::vector<fs::path> sandboxRoots;
    if (opts.sandbox) {
        std::error_code ec;
        if (!fs::exists(absPath, ec))
            throw std::runtime_error("Datei nicht lesbar: " + absPath.string());

        const std::vector<std::string> rootList = opts.roots.empty()
            ? std::vector<std::string>{ absPath.parent_path().string() }
            : opts.roots;
        sandboxRoots.reserve(rootList.size());
        for (const auto& r : rootList) {
            const auto canon = fs::canonical(fs::path(r), ec);
            if (ec || canon.empty())
                throw std::runtime_error(
                    "[ISO8583] Sandbox: Wurzel ungültig (existiert nicht oder ist nicht "
                    "auflösbar): '" + r + "'");
            sandboxRoots.push_back(std::move(canon));
        }
    }

    std::unordered_set<std::string> visitedFiles;
    std::vector<std::string>        allFiles;
    DefOriginMap                    defOrigins;

    PreprocessResult result;
    result.tree.rootref().set_type(ryml::MAP);
    SourceMap& smap = result.source_map;

    ryml::Tree ws;
    ws.rootref().set_type(ryml::MAP);
    ryml::id_type scratch_id = ws.rootref().append_child().id();
    ws.ref(scratch_id).set_type(ryml::MAP);
    ws.ref(scratch_id).set_key("__scratch__");
    ryml::id_type defs_id = ws.rootref().append_child().id();
    ws.ref(defs_id).set_type(ryml::MAP);
    ws.ref(defs_id).set_key("__definitions__");
    ryml::id_type raw_docs_id = ws.rootref().append_child().id();
    ws.ref(raw_docs_id).set_type(ryml::SEQ);
    ws.ref(raw_docs_id).set_key("__raw_docs__");

    OriginMap wsOrigins;    // Herkunft, keyed by ws-Knoten-ID (transient)
    wsOrigins.reserve(512); // grobe Vorab-Dimensionierung, vermeidet Rehashing
                            // während des Wachstums bei den meisten Specs

    // Jede Datei wird in einen EIGENEN, temporären Tree geparst (siehe
    // Architektur-Kommentar oben). merge_with() kopiert beim Übernehmen nach
    // ws aber NUR die Baumstruktur, nicht die zugrundeliegenden String-
    // Bytes - die müssen weiterleben, solange ws sie referenziert. Deshalb
    // werden ALLE filetree-Instanzen hier bis zum Ende dieser Funktion (nach
    // dem finalen Zusammenbau von result.tree) am Leben gehalten.
    // unique_ptr statt direktem vector<Tree>, damit eine Reallocation des
    // Vektors nicht die Adresse eines bereits geparsten Trees verschiebt.
    std::vector<std::unique_ptr<ryml::Tree>> fileTreesKeepAlive;

    ProcessContext ctx{ &ws, defs_id, scratch_id, {}, visitedFiles, &defOrigins, &wsOrigins };

    // ── Rekursive Ladefunktion ────────────────────────────────────────────────
    // Jede Datei wird SEPARAT geladen. Definitionen werden global akkumuliert
    // (für !use-Lookups), der Rest wird flach (Top-Level-Replace) in
    // result.tree gemergt - identisch zum ursprünglichen mergeInto()-Verhalten.
    std::function<void(const std::string&)> loadAndProcess =
        [&](const std::string& filePath)
        {
            const fs::path    abs = fs::absolute(filePath);
            const std::string absStr = abs.string();

            allFiles.push_back(absStr);
            visitedFiles.insert(absStr);
            ctx.base_path = abs.parent_path();

            std::ifstream f(absStr, std::ios::binary);
            if (!f)
                throw std::runtime_error("Datei nicht lesbar: " + absStr);

            // [ISO8583] 3.1 (Sicherheits-Audit): per-Datei-Größenlimit wird
            // SCHON WÄHREND des Einlesens erzwungen (streaming, 64 KiB-Blöcke) -
            // eine überdimensionierte Spec-Datei wird dadurch nie vollständig
            // in den Speicher geladen, sondern der Load bricht ab, sobald das
            // Limit überschritten wird (DoS-Schutz bei untrusted Specs).
            std::string content;
            {
                std::array<char, 65536> buf{};
                bool any = false;
                for (;;) {
                    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                    const std::streamsize got = f.gcount();
                    if (got > 0) {
                        any = true;
                        content.append(buf.data(), static_cast<std::size_t>(got));
                        if (content.size() > opts.maxSpecBytes)
                            throw std::runtime_error(
                                "[ISO8583] Ressourcenlimit: Spec-Datei zu groß: " + absStr +
                                " (" + std::to_string(content.size()) + " Bytes, Limit " +
                                std::to_string(opts.maxSpecBytes) + " Bytes)");
                        if (f.bad())
                            throw std::runtime_error("Datei nicht lesbar: " + absStr);
                        continue;
                    }
                    if (f.bad())
                        throw std::runtime_error("Datei nicht lesbar: " + absStr);
                    break; // EOF
                }
                if (!any)
                    throw std::runtime_error("Leere YAML-Datei: " + absStr);
            }

            // WICHTIG (per AddressSanitizer gefunden): ryml ruft intern
            // strlen() auf dem Dateinamen auf, behandelt ihn also wie einen
            // klassischen C-String - ws.to_arena(std::string) kopiert aber
            // NUR die exakte Länge OHNE Null-Terminator. Deshalb hier
            // manuell ein Byte mehr allozieren und explizit auf '\0' setzen.
            ryml::substr filenameBuf = ws.alloc_arena(absStr.size() + 1);
            std::memcpy(filenameBuf.str, absStr.data(), absStr.size());
            filenameBuf.str[absStr.size()] = '\0';
            ryml::csubstr filenameArena(filenameBuf.str, absStr.size());

            ryml::ParserOptions popts = {};
            popts.locations(trackSourceMap);
            ryml::EventHandlerTree evt_handler = {};
            ryml::Parser parser(&evt_handler, popts);
            if (trackSourceMap)
                parser.reserve_locations(256u);

            // Frischer, eigenständiger Tree (siehe Architektur-Kommentar) -
            // Inhalt kommt direkt aus `content`, NICHT aus ws' Arena.
            auto filetreePtr = std::make_unique<ryml::Tree>(
                ryml::parse_in_arena(&parser, filenameArena, ryml::to_csubstr(content)));
            ryml::Tree& filetree = *filetreePtr;
            fileTreesKeepAlive.push_back(std::move(filetreePtr));

            ryml::ConstNodeRef streamRoot = filetree.crootref();

            std::vector<ryml::ConstNodeRef> docs;
            if (streamRoot.is_stream()) {
                for (ryml::ConstNodeRef d : streamRoot.children())
                    docs.push_back(d);
            } else {
                docs.push_back(streamRoot);
            }

            for (ryml::ConstNodeRef doc : docs) {
                // 1) Positions-Tracking: EIN vollständiger Durchlauf über das
                //    Original-Dokument in filetree, solange parser noch
                //    gültig ist. Ergebnis landet in einer TEMPORÄREN, nach
                //    filetree-Knoten-IDs geordneten Map - erst nach dem
                //    gleich folgenden merge_with() wird das per
                //    propagateOrigins() auf ws-Knoten-IDs übertragen.
                OriginMap fileOrigins;
                fileOrigins.reserve(256);
                std::function<void(ryml::ConstNodeRef, int)> trackAll =
                    [&](ryml::ConstNodeRef node, int depth) {
                        if (!trackSourceMap) return;
                        checkDepth(depth);
                        ryml::Location loc = node.location(parser);
                        fileOrigins.emplace(node.id(),
                            SourceLocation{ absStr, (int)loc.line + 1, (int)loc.col + 1 });
                        if (node.is_map() || node.is_seq())
                            for (ryml::ConstNodeRef child : node.children())
                                trackAll(child, depth + 1);
                    };
                if (doc.is_map()) {
                    for (ryml::ConstNodeRef child : doc.children())
                        trackAll(child, 0);
                } else {
                    trackAll(doc, 0);
                }

                // !include_files: WICHTIG - Tag-Prüfung auf `doc` (dem
                // Original in filetree), NICHT auf einer per merge_with()
                // erzeugten Kopie: merge_with() setzt zwar has_val_tag(),
                // liefert aber einen LEEREN val_tag() zurück (empirisch
                // geprüfte ryml-Eigenheit in v0.15.2) - eine Tag-Prüfung
                // NACH dem Kopieren würde hier stillschweigend fehlschlagen.
                // !include_files-Dokumente selbst werden nie nach ws kopiert
                // - ihr Inhalt (eine Liste von Dateipfaden) wird nur
                // transient gebraucht.
                if (doc.has_val_tag() && doc.val_tag() == "!include_files" && doc.is_seq()) {
                    TNG_LOG_DEBUG("[Preprocessor] !include_files in '{}'", absStr);

                    for (ryml::ConstNodeRef entry : doc.children()) {
                        // [ISO8583] 3.1: Einträge müssen Pfad-Strings sein -
                        // Maps/Sequences/leere Einträge sind ein Spec-Fehler.
                        if (entry.is_map() || entry.is_seq() || !entry.has_val())
                            throw std::runtime_error(
                                "!include_files: Eintrag muss ein Pfad-String sein"
                                "\n  Referenziert von: " + absStr);

                        const std::string refStr = toStdString(entry.val());
                        if (refStr.empty())
                            throw std::runtime_error(
                                "!include_files: Eintrag muss ein Pfad-String sein"
                                "\n  Referenziert von: " + absStr);
                        fs::path fullPath = fs::absolute(abs.parent_path() / refStr);

                        // [ISO8583] 3.1 (Sicherheits-Audit): Include-Sandbox -
                        // jeder Referenzpfad wird aufgelöst und ABGELEHNT, wenn
                        // er außerhalb der erlaubten Wurzeln liegt (fail-closed):
                        //   1) lexikalische Prüfung gegen die kanonisierten
                        //      Wurzeln - fängt ../-Traversals, absolute Pfade
                        //      und UNC-Pfade; und
                        //   2) existiert die Datei, zusätzlich auf dem
                        //      vollständig kanonisierten (symlink-auflösenden)
                        //      Pfad - ein Symlink innerhalb der Wurzel kann
                        //      sonst nach außen zeigen.
                        if (opts.sandbox) {
                            bool inside = false;
                            for (const auto& root : sandboxRoots)
                                if (isWithinRoot(fullPath, root)) { inside = true; break; }
                            if (!inside)
                                throw std::runtime_error(
                                    "[ISO8583] Sandbox: !include_files-Eintrag '" + refStr +
                                    "' löst außerhalb der erlaubten Wurzel"
                                    + (sandboxRoots.size() == 1 ? "" : "n") +
                                    " (" + rootsToString(sandboxRoots) + ") auf"
                                    "\n  Referenziert von: " + absStr);

                            std::error_code ec;
                            if (fs::exists(fullPath, ec)) {
                                const auto canon = fs::canonical(fullPath, ec);
                                if (!ec) {
                                    bool ok = false;
                                    for (const auto& root : sandboxRoots)
                                        if (isWithinRoot(canon, root)) { ok = true; break; }
                                    if (!ok)
                                        throw std::runtime_error(
                                            "[ISO8583] Sandbox: !include_files-Eintrag '" + refStr +
                                            "' führt (via symbolischem Link) außerhalb der "
                                            "erlaubten Wurzel"
                                            + (sandboxRoots.size() == 1 ? "" : "n") +
                                            " (" + rootsToString(sandboxRoots) + ")"
                                            "\n  Referenziert von: " + absStr);
                                }
                            }
                        }

                        if (!fs::exists(fullPath))
                            throw std::runtime_error(
                                "!include_files: Datei nicht gefunden: " + fullPath.string() +
                                "\n  Referenziert von: " + absStr);

                        const std::string fullStr = fullPath.string();
                        if (visitedFiles.count(fullStr)) {
                            TNG_LOG_DEBUG("[Preprocessor] Duplikat übersprungen: {}", fullStr);
                            continue;
                        }

                        // [ISO8583] 3.1 (Sicherheits-Audit): globales Limit für
                        // die Gesamtzahl geladener, distinkter Dateien
                        // (Top-Level-Datei + alle Includes) - verhindert
                        // exponentielle Explosion durch verschachtelte Include-
                        // Graphen. `visitedFiles` enthält zu diesem Punkt die
                        // Top-Level-Datei und alle bereits geladenen Includes.
                        if (visitedFiles.size() >= opts.maxIncludeFiles)
                            throw std::runtime_error(
                                "[ISO8583] Ressourcenlimit: maxIncludeFiles=" +
                                std::to_string(opts.maxIncludeFiles) +
                                " erreicht, nicht ladbar: " + fullStr);

                        loadAndProcess(fullStr);
                    }
                    continue; // nichts weiter zu tun für dieses Dokument
                }

                // 2) EINMALIGE Cross-Tree-Kopie nach ws - danach läuft alles
                //    Weitere ausschließlich innerhalb von ws.
                ryml::id_type raw_id = ws.ref(raw_docs_id).append_child().id();
                ws.merge_with(&filetree, doc.id(), raw_id);
                ryml::ConstNodeRef fileRoot = ws.cref(raw_id);
                propagateOrigins(doc, fileRoot, fileOrigins, wsOrigins);

                if (!fileRoot.is_map())
                    continue; // Dokumente ohne Map-Struktur tragen keine fields/definitions

                // "definitions" extrahieren
                if (fileRoot.has_child("definitions")) {
                    ryml::ConstNodeRef fileDefs = fileRoot["definitions"];
                    if (fileDefs.is_map()) {
                        for (ryml::ConstNodeRef def : fileDefs.children()) {
                            const std::string defName = toStdString(def.key());

                            if (ws.ref(defs_id).has_child(def.key()))
                                ws.remove(ws.ref(defs_id).find_child(def.key()).id());
                            ryml::NodeRef out = ws.ref(defs_id).append_child();
                            ws.merge_with(&ws, def.id(), out.id());
                            propagateOrigins(def, out, wsOrigins, wsOrigins);
                            out.set_key(def.key());  // ZULETZT (siehe KRITISCHE REGEL)

                            // ryml-Quirk (empirisch in v0.15.2 verifiziert): Eine
                            // SAME-TREE merge_with() (src und dst beide in ws)
                            // entwertet das val_tag eines SEQ-Wertes - nach der
                            // Kopie bleibt has_val_tag() true, liefert aber ein
                            // LEERES val_tag() zurueck. Eine Definition, deren
                            // Wert eine !merge-fuehrende Seq ist (klassische
                            // "Track 2 Data"-Form: `d: !merge / - type: scalar`),
                            // verliert so ihr Tag; processUse() erkennt es nicht
                            // mehr, und das Feld landet als kaputte Seq mit
                            // leeren First-Child (format: '' / <dummy>). Der
                            // fields-Pfad ist unbeeindruckt (er schreibt via
                            // processNode() neu); hier wird das Tag aus dem
                            // weiterhin lesbaren Quelldokument wiederhergestellt.
                            if (def.has_val_tag() && !def.val_tag().empty())
                                out.set_val_tag(def.val_tag());

                            defOrigins[defName] = absStr;
                        }
                    }
                }

                // Restliche Top-Level-Keys verarbeiten und flach in
                // result.tree mergen (Top-Level-Replace, siehe processMerge-
                // Kommentar zum selben Prinzip).
                for (ryml::ConstNodeRef field : fileRoot.children()) {
                    const std::string key = toStdString(field.key());
                    if (key == "definitions") continue;

                    ryml::NodeRef scratch = scratchNode(ctx);
                    processNode(field, scratch, ctx);
                    scratch.set_key(field.key());  // ZULETZT (siehe KRITISCHE REGEL)
                    {
                        auto it = wsOrigins.find(field.id());
                        if (it != wsOrigins.end())
                            wsOrigins.emplace(scratch.id(), it->second);
                    }

                    ryml::NodeRef outRoot = result.tree.rootref();
                    if (outRoot.has_child(field.key()))
                        result.tree.remove(outRoot.find_child(field.key()).id());
                    ryml::NodeRef out = outRoot.append_child();
                    result.tree.merge_with(&ws, scratch.id(), out.id());
                    // Finale Übertragung: von ws-Knoten-IDs auf result.tree-
                    // Knoten-IDs - HIER, und erst hier, wird die öffentliche
                    // SourceMap (smap) tatsächlich befüllt.
                    {
                        // [ISO8583] C2: finalize() rekursiert über die gesamte
                        // (ggf. künstlich extrem tiefe) Knotenstruktur - derselbe
                        // Tiefenlimit-Schutz wie propagateOrigins()/processNode().
                        std::function<void(ryml::ConstNodeRef, ryml::ConstNodeRef, int)> finalize =
                            [&](ryml::ConstNodeRef s, ryml::ConstNodeRef d, int depth) {
                                checkDepth(depth);
                                auto it = wsOrigins.find(s.id());
                                if (it != wsOrigins.end())
                                    smap.record(static_cast<int>(d.id()), it->second);
                                if ((s.is_map() && d.is_map()) || (s.is_seq() && d.is_seq())) {
                                    auto sit = s.children().begin();
                                    auto dit = d.children().begin();
                                    for (; sit != s.children().end() && dit != d.children().end(); ++sit, ++dit)
                                        finalize(*sit, *dit, depth + 1);
                                }
                            };
                        finalize(scratch, out, 0);
                    }
                    out.set_key(field.key());  // ZULETZT (siehe KRITISCHE REGEL)
                }
            }
        };

    // ── Verarbeitung starten ──────────────────────────────────────────────────
    loadAndProcess(absPath.string());

    // result.tree eigenständig machen (siehe Kommentar bei makeSelfContained) -
    // MUSS vor der Rückgabe passieren, sonst zeigen alle Keys/Values in die
    // gleich zerstörten Bäume ws/filetree.
    makeSelfContained(result.tree, result.tree.rootref());

    smap.finalise(allFiles);

    // [ISO8583] 3.2: Dateimenge des Loads fuer den Cache-Content-Snapshot
    // (Hash + TOCTOU-Verifikation) weiterreichen.
    result.sourceFiles = std::move(allFiles);

    // ── Sidecar prüfen / schreiben ────────────────────────────────────────────
    const std::string currentHash = smap.hash();
    // [ISO8583] 3.1 (Sicherheits-Audit): Sidecar-Schreiben wird zusätzlich
    // durch allowSmapWrite + Sandbox-Wurzeln begrenzt (keine Datei-
    // Erzeugungs-Nebenwirkungen bei sandboxisierten Loads aus
    // nutzerbestimmten Verzeichnissen); der Lesezugriff ist durch
    // maxSmapBytes begrenzt (überdimensionierte Sidecars werden verworfen
    // und neu erzeugt). Der Load selbst ist von alledem unberührt.
    if (auto loaded = SourceMap::load(smapPath, currentHash, opts.maxSmapBytes)) {
        result.source_map = std::move(*loaded);
        TNG_LOG_DEBUG("[Preprocessor] SourceMap aus Sidecar geladen: {}", smapPath);
    }
    else if (trackSourceMap) {
        bool withinRoots = true;
        if (opts.sandbox) {
            withinRoots = false;
            const fs::path smapFile = fs::path(smapPath);
            for (const auto& root : sandboxRoots)
                if (isWithinRoot(smapFile, root)) { withinRoots = true; break; }
        }
        if (opts.allowSmapWrite && withinRoots) {
            smap.save(smapPath);
            TNG_LOG_DEBUG("[Preprocessor] SourceMap neu geschrieben: {}", smapPath);
        }
        else {
            TNG_LOG_DEBUG("[Preprocessor] SourceMap-Sidecar nicht geschrieben "
                "(allowSmapWrite={}, innerhalb Sandbox-Wurzeln={})",
                opts.allowSmapWrite, withinRoots);
        }
    }

    return result;
}
