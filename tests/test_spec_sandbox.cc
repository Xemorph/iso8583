// [ISO8583] 3.1 (Sicherheits-Audit): Spec-Load-Sandbox, Ressourcenlimits,
// E3-Guards (leere 'fields'), Sidecar-Gating und Legacy-Kompatibilitaet.
//
// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
// [stdc++]
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#if defined(_WIN32)
#include <process.h> // getpid()
#else
#include <unistd.h>  // getpid()
#endif

using namespace TNG_NAMESPACE;
using namespace TNG_NAMESPACE::spec;
namespace fs = std::filesystem;

// =============================================================================
// RAII-Helfer fuer temporaere Dateien und Verzeichnisse (gleiches Muster wie
// in test_preprocessor.cc)
// =============================================================================

namespace {

struct TempDir {
    fs::path path;

    TempDir() {
        // Pro Instanz eindeutig (PID + Zaehler): ctest -j faehrt mehrere
        // Test-Prozesse parallel; die alte Thread-ID-Hash-Namenskennung
        // kollidierte ueber Prozessgrenzen (gleiche Haupt-Thread-IDs) und
        // liess parallele Tests dasselbe Temp-Verzeichnis teilen (Reste,
        // exists()-Rennbedingungen).
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = fs::temp_directory_path()
             / ("iso8583_sbx_" + std::to_string(pid) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    // Schreibt eine Datei relativ zum TempDir (Zwischenverzeichnisse werden
    // angelegt). Gibt den absoluten Pfad zurueck.
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p.string();
    }

    std::string dir(const std::string& name = "") const {
        return (path / name).string();
    }
};
} // namespace

// Minimale, gueltige ASCII-Spec (MTI + Bitmap + DE2)
static std::string makeMiniSpec(const std::string& name) {
    return "spec: \"" + name + "\"\n"
           "encoding: ascii\n"
           "fields:\n"
           "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
           "  \"001\": { type: scalar, format: bitmap, length: 8 }\n"
           "  \"002\": { type: scalar, format: llchar, length: 19 }\n";
}

// Faengt die Fehlermeldung eines Load-Versuchs auf (leer = kein Fehler)
static std::string loadErrorText(const std::string& path, const SpecLoadOptions& opts) {
    try {
        SpecDecoder::loadFromYaml(path, opts);
        return {};
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

// =============================================================================
// Sandbox: includes innerhalb der Wurzel funktionieren weiter
// =============================================================================

TEST_CASE("Sandbox - include inside root loads normally", "[sandbox][include_files][spec]") {
    TempDir dir;
    dir.write("root/inc.yml",
        "definitions:\n"
        "  pan_field: { type: scalar, format: llchar, length: 19 }\n");
    auto top = dir.write("root/top.yml",
        "!include_files\n"
        "- inc.yml\n"
        "---\n"
        "spec: \"Sandbox Inner\"\n"
        "encoding: ascii\n"
        "fields:\n"
        "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
        "  \"001\": { type: scalar, format: bitmap, length: 8 }\n"
        "  \"002\": !use pan_field\n");

    const SpecLoadOptions opts; // Default: sandbox an, Wurzel = Verzeichnis von top.yml
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, opts));
}

// =============================================================================
// Sandbox: ../-Traversals werden abgelehnt (fail-closed)
// =============================================================================

TEST_CASE("Sandbox - parent-dir escape via .. is rejected", "[sandbox][spec]") {
    TempDir base;
    base.write("outer.yml", makeMiniSpec("Outer"));
    auto top = base.write("root/top.yml",
        "!include_files\n"
        "- ../outer.yml\n"
        "---\n"
        + makeMiniSpec("Sandbox DotDot"));

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Sandbox") != std::string::npos);
    CHECK(err.find("../outer.yml") != std::string::npos);
}

TEST_CASE("Sandbox - absolute path outside roots is rejected", "[sandbox][spec]") {
    TempDir base;
    auto outer = base.write("outer.yml", makeMiniSpec("Outer"));
    auto top = base.write("root/top.yml",
        std::string("!include_files\n")
        + "- " + outer + "\n"
        + "---\n"
        + makeMiniSpec("Sandbox Abs"));

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Sandbox") != std::string::npos);
}

TEST_CASE("Sandbox - sandbox=false allows outside include (legacy behavior)", "[sandbox][spec]") {
    TempDir base;
    base.write("outer.yml", makeMiniSpec("Outer"));
    auto top = base.write("root/top.yml",
        std::string("!include_files\n")
        + "- " + base.dir("outer.yml") + "\n"
        + "---\n"
        + makeMiniSpec("Sandbox Off"));

    SpecLoadOptions opts;
    opts.sandbox = false;
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, opts));
}

TEST_CASE("Sandbox - symbolic link pointing outside roots is rejected", "[sandbox][spec]") {
    TempDir base;
    base.write("outer.yml", makeMiniSpec("Outer"));
    const fs::path link = fs::path(base.dir("root")) / "link.yml";
    std::error_code ec;
    fs::create_directories(link.parent_path(), ec);
    fs::create_symlink(fs::path(base.dir("outer.yml")), link, ec);
    if (ec) {
        // Symlinks koennen auf dem Host-System nicht angelegt werden
        // (z.B. Windows ohne Developer-Mode/Privilege) - Test wird
        // bewusst nicht durchgefallen.
        INFO("Symlink-Erstellung nicht moeglich (" << ec.message() << ") - Test wird übersprungen.");
        return;
    }

    auto top = base.write("root/top.yml",
        "!include_files\n"
        "- link.yml\n"
        "---\n"
        + makeMiniSpec("Sandbox Symlink"));

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Sandbox") != std::string::npos);
}

TEST_CASE("Sandbox - explicit roots override the default parent dir", "[sandbox][spec]") {
    // Das Include liegt im (Default-)Wurzelverzeichnis der Top-Level-Spec,
    // die explizit gesetzte Wurzel zeigt aber woanders hin -> muss abgelehnt
    // werden, weil explizite Roots den Default ERSETZEN.
    TempDir base;
    base.write("b/placeholder.yml", "# nur damit das Verzeichnis existiert\n");
    auto top = base.write("a/top.yml",
        "!include_files\n"
        "- inc.yml\n"
        "---\n"
        + makeMiniSpec("Sandbox Roots"));
    base.write("a/inc.yml",
        "definitions:\n"
        "  pan_field: { type: scalar, format: llchar, length: 19 }\n");

    SpecLoadOptions opts;
    opts.roots = { base.dir("b") };
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Sandbox") != std::string::npos);
}

// =============================================================================
// Ressourcenlimits: maxIncludeFiles / maxSpecBytes
// =============================================================================

TEST_CASE("Caps - include chain beyond maxIncludeFiles is rejected", "[sandbox][caps][spec]") {
    TempDir dir;
    // Kette: top (1) -> a.yml (2) -> b.yml (3)
    dir.write("a.yml",
        "!include_files\n"
        "- b.yml\n"
        "---\n"
        "definitions:\n"
        "  f3: { type: scalar, format: llchar, length: 5 }\n");
    dir.write("b.yml",
        "definitions:\n"
        "  f4: { type: scalar, format: numeric, length: 4 }\n");
    auto top = dir.write("top.yml",
        "!include_files\n"
        "- a.yml\n"
        "---\n"
        + makeMiniSpec("Caps Top"));

    // Default-Limit (1024): Kette laedt ohne Probleme
    const SpecLoadOptions def;
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, def));

    // Limit 2: top (1) + a (2) passen, b (3) wird abgelehnt
    SpecLoadOptions opts;
    opts.maxIncludeFiles = 2;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Ressourcenlimit") != std::string::npos);
    CHECK(err.find("maxIncludeFiles") != std::string::npos);
}

TEST_CASE("Caps - spec file beyond maxSpecBytes is rejected while streaming", "[sandbox][caps][spec]") {
    TempDir dir;
    // Gueltige Spec mit grossem Kommentar-Padding (weit ueber das Limit von 1 KiB)
    std::string content = makeMiniSpec("Caps Size");
    while (content.size() < 5000)
        content += "# padding\n";
    auto top = dir.write("top.yml", content);

    SpecLoadOptions opts;
    opts.maxSpecBytes = 1024;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("Ressourcenlimit") != std::string::npos);
    const bool sizeHit = (err.find("zu gross") != std::string::npos)
                      || (err.find("zu groß") != std::string::npos);
    CHECK(sizeHit);
}

// =============================================================================
// E3: 'fields'-Guards (leere Map, Sequenz, Ziffern-Overflow)
// =============================================================================

TEST_CASE("E3 - empty fields map is rejected with positioned error", "[sandbox][e3][spec][error]") {
    TempDir dir;
    auto top = dir.write("top.yml",
        "spec: \"E3 Empty\"\n"
        "encoding: ascii\n"
        "fields: {}\n");

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("fields") != std::string::npos);
    CHECK(err.find("nicht-leere Map") != std::string::npos);
    // Lokalisiert: die Meldung tragt eine file:line:col-Position
    CHECK(err.find(top) != std::string::npos);
}

TEST_CASE("E3 - fields as sequence is rejected (no raw stoi escape)", "[sandbox][e3][spec][error]") {
    TempDir dir;
    auto top = dir.write("top.yml",
        "spec: \"E3 Seq\"\n"
        "encoding: ascii\n"
        "fields:\n"
        "  - something\n"
        "  - else\n");

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    REQUIRE(err.find("nicht-leere Map") != std::string::npos);
}

TEST_CASE("E3 - DE key digit overflow is rejected (no raw out_of_range)", "[sandbox][e3][spec][error]") {
    TempDir dir;
    auto top = dir.write("top.yml",
        "spec: \"E3 Overflow\"\n"
        "encoding: ascii\n"
        "fields:\n"
        "  \"99999999999\": { type: scalar, format: numeric, length: 1 }\n");

    const SpecLoadOptions opts;
    const std::string err = loadErrorText(top, opts);
    const bool hit = (err.find("DE-Nummer zu groess") != std::string::npos)
                  || (err.find("DE-Nummer zu groß") != std::string::npos);
    REQUIRE(hit);
}

// =============================================================================
// Sidecar-Gating: .smap-Schreiben ist opt-in-able und sandboxbewusst
// =============================================================================

TEST_CASE("Sidecar - allowSmapWrite=false suppresses sidecar creation", "[sandbox][sidecar][spec]") {
    TempDir dir;
    auto top = dir.write("top.yml", makeMiniSpec("Sidecar Off"));
    const auto smapPath = top + ".smap";

    SpecLoadOptions opts;
    opts.allowSmapWrite = false;
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, opts));
    CHECK_FALSE(fs::exists(smapPath));

    // Default (allowSmapWrite=true, Wurzel = Verzeichnis der Spec):
    // die Sidecar wird geschrieben
    const SpecLoadOptions def;
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, def));
    CHECK(fs::exists(smapPath));
}

// =============================================================================
// Legacy-Kompatibilitaet: bool-Ueberladungen bleiben unveraendert
// =============================================================================

TEST_CASE("Compat - legacy bool overloads still work unchanged", "[sandbox][compat][spec]") {
    TempDir dir;
    auto top = dir.write("top.yml", makeMiniSpec("Legacy"));

    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top));
    REQUIRE_NOTHROW(SpecDecoder::loadFromYaml(top, true));
    REQUIRE_NOTHROW(SpecDecoder::loadBothFromYaml(top));
    REQUIRE_NOTHROW(SpecDecoder::loadBothFromYaml(top, true));
}