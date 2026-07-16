// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/detail/_components.hh>
// [stdc++]
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace TNG_NAMESPACE;

// =============================================================================
// RAII-Helfer fuer temporaere Dateien und Verzeichnisse
// =============================================================================

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        path = std::filesystem::temp_directory_path()
             / ("iso8583_pp_" + std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    // Schreibt eine Datei relativ zum TempDir
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p.string();
    }

    std::string operator/(const std::string& name) const {
        return (path / name).string();
    }
};

// =============================================================================
// !template Direktive
// =============================================================================

TEST_CASE("Preprocessor - !template standalone without !merge", "[preprocessor][template]") {
    // !template gibt eine vollstaendige Map zurueck (type, format, length).
    // !merge wird nur gebraucht wenn zusaetzliche Keys wie description
    // oder encoding ergaenzt werden sollen.
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Template Standalone Test"
encoding: ebcdic
fields:
  "000":
    type: scalar
    format: numeric
    length: 4
    description: "MTI"
  "001":
    type: scalar
    format: bitmap
    length: 8
    description: "Bitmap"
  "002": !template LL(CHAR, 19)
  "036": !template LLL(CHAR, 104)
  "055": !template LLL(BINARY, 255)
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

TEST_CASE("Preprocessor - !template with !merge to add description", "[preprocessor][template]") {
    // !merge erganzt description, encoding oder type zum Template-Ergebnis.
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Template Merge Test"
encoding: ebcdic
fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "002":
    !merge
    - !template LL(CHAR, 19)
    - description: "Primary Account Number"
  "055":
    !merge
    - !template LLL(BINARY, 255)
    - description: "ICC / EMV Data"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

// =============================================================================
// Rueckwaertskompatibilitaet: !include wird noch akzeptiert (mit Warnung)
// =============================================================================

TEST_CASE("Preprocessor - !include still works as backward compat for !use", "[preprocessor][compat]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Compat Test"
encoding: ebcdic

definitions:
  stan_field:
    type: scalar
    format: numeric
    length: 6
    description: "STAN"

fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "011": !include stan_field
)");

    // !include verhält sich identisch zu !use - nur mit Deprecation-Warnung im Log
    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

// =============================================================================
// !use Direktive (Definition-Referenz innerhalb derselben Datei)
// =============================================================================

TEST_CASE("Preprocessor - !use definition reference", "[preprocessor][include]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Include Test"
encoding: ebcdic

definitions:
  amount:
    type: scalar
    format: numeric
    length: 12

fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "004":
    !merge
    - !use amount
    - description: "Amount, Transaction"
)");

    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
}

// =============================================================================
// !merge Direktive
// =============================================================================

TEST_CASE("Preprocessor - !merge combines type and template", "[preprocessor][merge]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Merge Test"
encoding: ebcdic
fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "002":
    !merge
    - type: scalar
    - !template LL(CHAR, 19)
    - description: "Primary Account Number"
  "004":
    !merge
    - type: scalar
    - format: numeric
    - length: 12
    - description: "Amount"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

// =============================================================================
// !include_files Direktive (externe YAML-Dateien einbinden)
// =============================================================================

TEST_CASE("Preprocessor - !include_files loads external file", "[preprocessor][include_files]") {
    TempDir dir;

    // Externe Datei mit gemeinsamen Feldern
    dir.write("common.yml", R"(
definitions:
  mti_field:
    type: scalar
    format: numeric
    length: 4
    description: "MTI"
  bitmap_field:
    type: scalar
    format: bitmap
    length: 8
    description: "Bitmap"
)");

    auto spec_path = dir.write("spec.yml", R"(
!include_files
- common.yml

spec: "Include Files Test"
encoding: ebcdic

fields:
  "000": !use mti_field
  "001": !use bitmap_field
  "011":
    type: scalar
    format: numeric
    length: 6
    description: "STAN"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

TEST_CASE("Preprocessor - !include_files with multiple files", "[preprocessor][include_files]") {
    TempDir dir;

    dir.write("fields_a.yml", R"(
definitions:
  field_mti:
    type: scalar
    format: numeric
    length: 4
)");

    dir.write("fields_b.yml", R"(
definitions:
  field_bitmap:
    type: scalar
    format: bitmap
    length: 8
)");

    auto spec_path = dir.write("spec.yml", R"(
!include_files
- fields_a.yml
- fields_b.yml

spec: "Multi-Include Test"
encoding: ebcdic

fields:
  "000": !use field_mti
  "001": !use field_bitmap
)");

    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
}

// =============================================================================
// Fehlerbehandlung
// =============================================================================

TEST_CASE("Preprocessor - missing !include_files target throws", "[preprocessor][error]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
!include_files
- nonexistent_file.yml

spec: "Error Test"
fields: {}
)");

    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml(spec_path));
}

TEST_CASE("Preprocessor - unknown !use key throws or warns", "[preprocessor][error]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Unknown Include Test"
encoding: ebcdic
definitions:
  known_field:
    type: scalar
    format: numeric
    length: 4
fields:
  "000": !use unknown_key
  "001":
    type: scalar
    format: bitmap
    length: 8
)");

    // Unbekannter !include-Key sollte entweder Exception werfen
    // oder einen Fallback-Wert produzieren – kein Crash
    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml(spec_path));
}

TEST_CASE("Preprocessor - invalid !template format throws", "[preprocessor][error]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Invalid Template Test"
encoding: ebcdic
fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "002":
    !template INVALID_FORMAT
    description: "Bad field"
)");

    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml(spec_path));
}

// =============================================================================
// Encoding-Vererbung via Preprocessor
// =============================================================================

TEST_CASE("Preprocessor - global encoding inherited by included definitions", "[preprocessor][encoding]") {
    TempDir dir;

    dir.write("defs.yml", R"(
definitions:
  numeric_field:
    type: scalar
    format: numeric
    length: 6
    description: "Numeric"
)");

    auto spec_path = dir.write("spec.yml", R"(
!include_files
- defs.yml

spec: "Encoding Inheritance Test"
encoding: ebcdic

fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "011":
    !merge
    - !use numeric_field
    - description: "STAN"
)");

    // Laden ohne Fehler = globales Encoding wird korrekt vererbt
    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
}

// =============================================================================
// End-to-End: Preprocessor + Parser + Unparse
// =============================================================================

TEST_CASE("Preprocessor - end-to-end with !template field", "[preprocessor][e2e]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "E2E Template Test"
encoding: ebcdic
fields:
  "000":
    type: scalar
    format: numeric
    length: 4
    description: "MTI"
  "001":
    type: scalar
    format: bitmap
    length: 8
    description: "Bitmap"
  "002":
    !merge
    - type: scalar
    - !template LL(CHAR, 19)
    - description: "PAN"
  "011":
    type: scalar
    format: numeric
    length: 6
    description: "STAN"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(spec_path);
    REQUIRE(parser != nullptr);

    // MTI: EBCDIC "0100"
    // Bitmap: DE2 + DE11 aktiv
    // DE2:  EBCDIC LL="16" + 16 EBCDIC-Ziffern (PAN)
    // DE11: EBCDIC "000001"
    auto buf = std::vector<uint8_t>({
        0xF0, 0xF1, 0xF0, 0xF0,
        0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xF1, 0xF6,
        0xF4, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == buf.size());
    CHECK(msg->mti() == "0100");

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");

    auto de11 = msg->get<ISOOpaqueField>(11);
    REQUIRE(de11 != nullptr);
    CHECK(de11->value() == "000001");
}
