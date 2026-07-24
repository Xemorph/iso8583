// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/detail/_components.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#include <thread>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path()
             / ("iso8583_rem_" + std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        std::ofstream f(p); f << content;
        return p.string();
    }
};

// =============================================================================
// ISORemainderFieldParser direkt
// =============================================================================

TEST_CASE("IFE_REMAINING - reads all remaining bytes", "[remaining][direct]") {
    // Buffer: 4 feste Bytes + 3 variable Bytes
    auto buf = B({ 0xF1, 0xF2, 0xF3, 0xF4,   // 4 Bytes feste Felder (schon konsumiert)
                   0xC1, 0xC2, 0xC3 });        // 3 Bytes verbleibend

    IFE_REMAINING parser(10, "Trailing Field");
    auto component = std::make_shared< OpaqueField >(14);

    std::size_t consumed = parser.unparse(component, buf, 4);  // Offset = 4

    CHECK(consumed == 3);
    CHECK(component->wire_length() == 3);
    CHECK(component->value() == "ABC");
}

/*
TEST_CASE("IF_REMAINING - empty buffer returns 0 (no crash)", "[remaining][direct]") {
    auto buf = B({ 0xF1, 0xF2 });
    IF_REMAINING parser(0, "Optional Trailing");
    auto component = std::make_shared<ISOOpaqueField>(15);

    // Offset = Buffer-Ende -> nichts zu lesen
    REQUIRE_NOTHROW(parser.unparse(component, buf, 2));
    std::size_t consumed = parser.unparse(component, buf, 2);
    CHECK(consumed == 0);
    CHECK(component->value().empty());
}

TEST_CASE("IF_REMAINING - full buffer (no preceding field)", "[remaining][direct]") {
    auto buf = B({ 0xC1, 0xC2, 0xC3, 0xC4, 0xC5 });
    IF_REMAINING parser(0, "All Bytes");
    auto component = std::make_shared<ISOOpaqueField>(1);

    std::size_t consumed = parser.unparse(component, buf, 0);
    CHECK(consumed == 5);
    CHECK(component->wire_length() == 5);
}
*/

TEST_CASE("IF_REMAINING - type() returns REMAINING", "[remaining][direct]") {
    IF_REMAINING parser(0, "test");
    CHECK(parser.type() == ISOFieldParserType::REMAINING);
}

// =============================================================================
// BMP_061 Simulation: nested mit trailing REMAINING Subfeld
// =============================================================================

// Baut einen nested Parser der BMP_061 simuliert:
//   Subfelder 1-14: fix (16 Bytes total)
//   Subfeld 15:     remaining (0-10 Bytes, kein Prefix)
static std::shared_ptr<ISOBaseParser> make_bmp061_parser() {
    auto sub = std::make_shared<ISOBaseParser>("BMP061 children", 0);

    // Subfeld 0: NOP (0 Bytes)
    sub->add(std::make_shared<IF_NOP>());
    // Subfeld 1: 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "Terminal Attendance"));
    // Subfeld 2: reserved 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "reserved"));
    // Subfelder 3-8: je 1 Byte EBCDIC numeric/char
    for (int i = 0; i < 6; ++i)
        sub->add(std::make_shared<IFE_NUMERIC>(1, "POS SF" + std::to_string(i+2)));
    // Subfeld 9: reserved 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "reserved"));
    // Subfelder 10-11: je 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "CAT Level"));
    sub->add(std::make_shared<IFE_NUMERIC>(1, "Input Cap"));
    // Subfeld 12: 2 Bytes
    sub->add(std::make_shared<IFE_NUMERIC>(2, "Auth Life Cycle"));
    // Subfeld 13: 3 Bytes
    sub->add(std::make_shared<IFE_NUMERIC>(3, "Country Code"));
    // Subfeld 14: REMAINING (0-10 Bytes, kein Prefix)
    sub->add(std::make_shared<IFE_REMAINING>(10, "POS Postal Code"));

    return sub;
}

TEST_CASE("BMP_061 - without postal code (16 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 16 Bytes, Subfeld 15 bekommt 0 Bytes
    auto buf = B({
        // 16 EBCDIC-Bytes fuer Subfelder 1-13
        // SF0(NOP)=0, SF1-8=je 0xF0, SF9=0xF0, SF10=0xF0, SF11=0xF0,
        // SF12=0xF0 0xF0, SF13=0xF0 0xF0 0xF0
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,  // SF1-8
        0xF0,                                              // SF9
        0xF0, 0xF0,                                        // SF10-11
        0xF0, 0xF0,                                        // SF12
        0xF0, 0xF0, 0xF0,                                  // SF13
        // No SF14
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    // SF14 (Postal Code) sollte leer sein - kein Crash
    // (key = 14, 0-basiert in der Subparser-Liste)
    auto sf14 = msg->get< OpaqueField >(14);
    CHECK(sf14 == nullptr);
}

TEST_CASE("BMP_061 - with full postal code (26 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 26 Bytes, Subfeld 15 bekommt 10 Bytes
    auto buf = B({
        // SF2-9 (8 Bytes)
        0xF1, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
        // SF10 reserved
        0xF0,
        // SF11-12
        0xF0, 0xF0,
        // SF13 (2 Bytes)
        0xF0, 0xF0,
        // SF14 (3 Bytes)
        0xF4, 0xF9, 0xF2,   // "492" (PLZ-Vorwahl)
        // SF15 Postal Code (10 Bytes EBCDIC "6900      ")
        0xF6, 0xF9, 0xF0, 0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    // SF13 Country Code
    auto sf13 = msg->get< OpaqueField >(13);
    REQUIRE(sf13 != nullptr);
    CHECK(sf13->value() == "492");

    // SF14 Postal Code: 10 Bytes remaining
    auto sf14 = msg->get< OpaqueField >(14);
    REQUIRE(sf14 != nullptr);
    //CHECK(sf14->wire_length() == 10);
    CHECK(sf14->value() == "6900      ");
}

TEST_CASE("BMP_061 - with short postal code (19 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 19 Bytes, Subfeld 14 has 3 Bytes
    auto buf = B({
        0xF1, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,    // SF1-8
        0xF0,                                              // SF9
        0xF0, 0xF0,                                        // SF10-11
        0xF0, 0xF0,                                        // SF12
        0xF4, 0xF9, 0xF2,                                  // SF13 "492"
        0xF6, 0xF9, 0xF0,                                  // SF14 "690" (3 Bytes)
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    auto sf14 = msg->get< OpaqueField >(14);
    REQUIRE(sf14 != nullptr);
    //CHECK(sf14->wire_length() == 3);
    CHECK(sf14->value() == "690");
}

// =============================================================================
// YAML Spec: format: remaining
// =============================================================================

TEST_CASE("SpecDecoder - format: remaining loads without error", "[remaining][spec]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Remaining Field Test"
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
  "061":
    type: nested
    format: binary
    length: 26
    description: "POS Data"
    children:
      - type: scalar
        format: nop
        length: 0
      - type: scalar
        format: numeric
        length: 1
        description: "POS Terminal Attendance"
      - type: scalar
        format: numeric
        length: 1
        description: "reserved"
      - type: scalar
        format: numeric
        length: 3
        description: "POS Country Code"
      - type: scalar
        format: remaining
        description: "POS Postal Code"
        length: 10
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}
