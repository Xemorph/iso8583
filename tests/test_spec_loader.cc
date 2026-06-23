#include <catch2/catch_test_macros.hpp>

// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>
// [stdc++]
#include <filesystem>
#include <fstream>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

// Schreibt eine temporaere YAML-Datei und gibt den Pfad zurueck.
// Die Datei wird nach dem Test automatisch geloescht (RAII).
struct TempYaml {
    std::filesystem::path path;

    explicit TempYaml(const std::string& content) {
        path = std::filesystem::temp_directory_path()
             / ("libiso8583_test_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".yml");
        std::ofstream f(path);
        f << content;
    }

    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.string(); }
};

// Minimale YAML-Spec:
//   MTI (EBCDIC, fix 4)
//   Bitmap (binary, fix 8)
//   DE2: LLCHAR EBCDIC (PAN, max 19)
//   DE11: NUMERIC EBCDIC (STAN, fix 6)
static constexpr const char* MINIMAL_SPEC = R"(
spec: "Test Spec"
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
    description: "Primary Bitmap"
  "002":
    type: scalar
    format: llchar
    length: 19
    description: "PAN"
  "011":
    type: scalar
    format: numeric
    length: 6
    description: "STAN"
)";

// =============================================================================
// SpecDecoder: loadFromYaml
// =============================================================================

TEST_CASE("SpecDecoder - loads minimal spec without throwing", "[spec][load]") {
    TempYaml yaml(MINIMAL_SPEC);
    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(yaml.str()));
    REQUIRE(parser != nullptr);
}

TEST_CASE("SpecDecoder - throws on missing file", "[spec][load][error]") {
    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml("/nonexistent/path/test.yml"));
}

TEST_CASE("SpecDecoder - throws on invalid YAML", "[spec][load][error]") {
    TempYaml yaml("this: is: not: valid: yaml: ::::");
    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml(yaml.str()));
}

// =============================================================================
// SpecDecoder: end-to-end unparse
// =============================================================================

TEST_CASE("SpecDecoder - end-to-end unparse with EBCDIC fields", "[spec][unparse]") {
    TempYaml yaml(MINIMAL_SPEC);
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // MTI:    EBCDIC "0100" = F0 F1 F0 F0
    // Bitmap: DE2 (bit 2) + DE11 (bit 11)
    //   Bit  2 = Byte 0, bit 6 = 0x40
    //   Bit 11 = Byte 1, bit 3 = 0x20
    //   => 0x40, 0x20, 0x00 ...
    // DE2:    EBCDIC LL-Prefix "16" + 16 EBCDIC-Ziffern (PAN)
    //   Prefix: F1 F6 (="16")
    //   Daten:  F4 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 (="4111111111111111")
    // DE11:   EBCDIC "000001" = F0 F0 F0 F0 F0 F1
    auto buf = std::vector<uint8_t>({
        // MTI
        0xF0, 0xF1, 0xF0, 0xF0,
        // Bitmap
        0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // DE2: EBCDIC LL="16" + PAN "4111111111111111"
        0xF1, 0xF6,
        0xF4, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        // DE11: EBCDIC "000001"
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == buf.size());
    CHECK(msg->mti() == "0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");

    auto de11 = msg->get<ISOOpaqueField>(11);
    REQUIRE(de11 != nullptr);
    CHECK(de11->value() == "000001");
}

TEST_CASE("SpecDecoder - per-field encoding overrides global", "[spec][encoding]") {
    // Globales Encoding: ebcdic
    // DE2 hat eigenes encoding: bcd
    TempYaml yaml(R"(
spec: "Mixed Encoding Spec"
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
    type: scalar
    format: numeric
    encoding: bcd
    length: 16
    description: "PAN [BCD]"
  "004":
    type: scalar
    format: numeric
    length: 12
    description: "Amount [EBCDIC]"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    auto buf = std::vector<uint8_t>({
        // MTI: EBCDIC "0200"
        0xF0, 0xF2, 0xF0, 0xF0,
        // Bitmap: DE2 + DE4 aktiv
        0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // DE2: BCD "4111111111111111" (8 Bytes)
        0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        // DE4: EBCDIC "000000010000" (12 Bytes)
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1, 0xF0, 0xF0, 0xF0, 0xF0,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    msg->unparse(msg, buf);

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");  // BCD-kodiert

    auto de4 = msg->get<ISOOpaqueField>(4);
    REQUIRE(de4 != nullptr);
    CHECK(de4->value() == "000000010000");  // EBCDIC-kodiert
}

TEST_CASE("SpecDecoder - binary field is encoding-neutral", "[spec][encoding][binary]") {
    TempYaml yaml(R"(
spec: "Binary Field Spec"
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
  "052":
    type: scalar
    format: binary
    length: 8
    description: "PIN Block"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // Bitmap: DE52 aktiv = Bit 52
    // Bit 52 = Byte 6 (0-basiert), Bit 4 von links = 0x10
    auto buf = std::vector<uint8_t>({
        0xF0, 0xF1, 0xF0, 0xF0,                          // MTI: 0100
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,  // Bitmap: DE52
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, // DE52: 8 raw bytes
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    msg->unparse(msg, buf);

    auto de52 = msg->get<ISOBinaryField>(52);
    REQUIRE(de52 != nullptr);
    CHECK(de52->value() == std::vector<uint8_t>({ 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE }));
}
