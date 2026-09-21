// (0.6.0, FE-1) Field-only-Specs: Validierung (WP1) + decodeField (WP4).
//
// WP1 (hier): Fehler-Cases der Validierung:
//   - fields: in Field-only-Dokument,
//   - fehlendes field: (leeres Dokument bzw. nur Root-Keys),
//   - header: in Field-only-Dokument (Entscheidung c),
//   - ungültiger Feld-Key im field:-Block (validateFieldKeys),
//   - field: in Message-Dokument (erwischt über die Message-Validierung,
//     über die öffentliche loadFromYaml).
//
// Die Field-only-Loader-Eintritte (loadField*) existieren erst ab WP3,
// daher fahren die Field-only-Cases die Validierung über die interne
// Testnaht spec::validateFieldSpecYamlFile() (src/_spec.hh) – dieselbe
// Preprocessor-/SourceMap-Pipeline wie der Loader.

// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
// [tng/internal]
#include "_spec.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <process.h> // getpid()
#else
#include <unistd.h>  // getpid()
#endif
#include <atomic>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers (TempYaml: Identisch mit test_spec_loader.cc – PID+Zaehler, da
// ctest -j mehrere Test-Prozesse parallel fährt)
// =============================================================================

namespace {

struct TempYaml {
    std::filesystem::path path;

    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }

    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.string(); }
};

// Field-only-Validierung über die Testnaht (wirft ab → what() zurückgeben).
static std::string field_error(const std::string& yaml_text) {
    TempYaml y(yaml_text);
    try {
        spec::validateFieldSpecYamlFile(y.str());
        return "";
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

// Message-Validierung über die öffentliche API (Message-Seiten-Konflikt).
static std::string msg_error(const std::string& yaml_text) {
    TempYaml y(yaml_text);
    try {
        spec::SpecDecoder::loadFromYaml(y.str());
        return "";
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

} // namespace

// =============================================================================
// WP1 – Validierungs-Fehler (validateFieldSpecYaml / validateSpecYaml)
// =============================================================================

TEST_CASE("FE-1 Error - fields: in Field-only-Dokument", "[fe1][error][validation]") {
    const auto msg = field_error(R"(
spec: "ICC"
field:
  format: lllbinary
  tlv: {tag_bytes: 2, len_bytes: 2}
  children:
    "71": {format: lllchar}
fields:
  "000": {format: bitmap, length: 8}
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Konflikt") != std::string::npos);
    CHECK(msg.find("fields") != std::string::npos);
}

TEST_CASE("FE-1 Error - fehlendes field: (leeres Dokument)", "[fe1][error][validation]") {
    // Eine 0-Byte-Datei wird schon im Preprocessor fail-closed abgewiesen
    // ("Leere YAML-Datei", src/_preprocessor.cc), noch bevor
    // validateFieldSpecYaml greift. (Eine Datei, die nur aus
    // Leerzeichen/Zeilenumbrüchen besteht, ist für den Preprocessor
    // nicht "leer" – sie läuft in den Validator, s. Nachbar-Test.)
    const auto msg = field_error("");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Leere YAML-Datei") != std::string::npos);
}

TEST_CASE("FE-1 Error - fehlendes field: (nur Root-Keys)", "[fe1][error][validation]") {
    const auto msg = field_error(R"(
spec: "ICC"
encoding: ebcdic
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("field") != std::string::npos);
}

TEST_CASE("FE-1 Error - header: in Field-only-Dokument (Entscheidung c)",
    "[fe1][error][validation]") {
    const auto msg = field_error(R"(
spec: "ICC"
field:
  format: lllbinary
header: 4
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("header") != std::string::npos);
    CHECK(msg.find("Konflikt") != std::string::npos);
}

TEST_CASE("FE-1 Error - ungültiger Key im field:-Block (validateFieldKeys)",
    "[fe1][error][validation]") {
    const auto msg = field_error(R"(
spec: "ICC"
field:
  format: lllbinary
  blubb: 1
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Unbekannter Schlüssel") != std::string::npos);
    CHECK(msg.find("blubb") != std::string::npos);
}

TEST_CASE("FE-1 Error - field: in Message-Dokument", "[fe1][error][validation]") {
    const auto msg = msg_error(R"(
spec: "Test"
field:
  format: lllbinary
fields:
  "000": {format: bitmap, length: 8}
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Konflikt") != std::string::npos);
    CHECK(msg.find("fields") != std::string::npos);
}