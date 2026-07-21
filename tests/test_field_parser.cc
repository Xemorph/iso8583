// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/iso8583.h>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"

using namespace TNG_NAMESPACE;

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

template <typename ParserType>
static std::shared_ptr< OpaqueField > unparse_opaque(
    const std::vector<uint8_t>& buf, int fieldLen)
{
    ParserType parser(fieldLen, "TestField");
    auto component = std::make_shared< OpaqueField >(2);
    parser.unparse(component, buf, 0);
    return component;
}

template <typename ParserType>
static std::shared_ptr< BinaryField > unparse_binary(
    const std::vector<uint8_t>& buf, int fieldLen)
{
    ParserType parser(fieldLen, "TestField");
    auto component = std::make_shared< BinaryField >(2);
    parser.unparse(component, buf, 0);
    return component;
}

// =============================================================================
// EBCDIC fields (IFE_*)
// =============================================================================

TEST_CASE("IFE_NUMERIC - fixed 6 digits", "[field][ebcdic][numeric]") {
    auto buf = B({ 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5 });
    auto field = unparse_opaque<IFE_NUMERIC>(buf, 6);
    CHECK(field->value() == "012345");
    CHECK(field->wire_offset() == 0);
    CHECK(field->wire_length() == 6);
}

TEST_CASE("IFE_CHAR - fixed 4 chars with space padding", "[field][ebcdic][char]") {
    // EBCDIC 'A'=0xC1, 'B'=0xC2, space=0x40
    auto buf = B({ 0xC1, 0xC2, 0x40, 0x40 });
    auto field = unparse_opaque<IFE_CHAR>(buf, 4);
    CHECK(field->value().substr(0, 2) == "AB");
}

TEST_CASE("IFE_LLCHAR - 2-digit EBCDIC length prefix", "[field][ebcdic][llchar]") {
    // Length: EBCDIC '0','6' -> 6
    // Data:   EBCDIC 'A'-'F'
    auto buf = B({ 0xF0, 0xF6,
                   0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6 });
    auto field = unparse_opaque<IFE_LLCHAR>(buf, 6);
    CHECK(field->value() == "ABCDEF");
    CHECK(field->wire_length() == 8);  // 2 prefix + 6 data
}

TEST_CASE("IFE_LLLCHAR - 3-digit EBCDIC length prefix", "[field][ebcdic][lllchar]") {
    // Length: EBCDIC '0','0','3' -> 3
    // Data:   EBCDIC '1','2','3'
    auto buf = B({ 0xF0, 0xF0, 0xF3,
                   0xF1, 0xF2, 0xF3 });
    auto field = unparse_opaque<IFE_LLLCHAR>(buf, 3);
    CHECK(field->value() == "123");
    CHECK(field->wire_length() == 6);  // 3 prefix + 3 data
}

// =============================================================================
// BCD fields (IFB_*)
// =============================================================================

TEST_CASE("IFB_NUMERIC - fixed 12 digits (amount)", "[field][bcd][numeric]") {
    // 000000010000 -> 6 BCD bytes
    auto buf = B({ 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 });
    auto field = unparse_opaque<IFB_NUMERIC>(buf, 12);
    CHECK(field->value() == "000000010000");
    CHECK(field->wire_length() == 6);
}

TEST_CASE("IFB_NUMERIC - PAN 16 digits", "[field][bcd][numeric]") {
    auto buf = B({ 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 });
    auto field = unparse_opaque<IFB_NUMERIC>(buf, 16);
    CHECK(field->value() == "4111111111111111");
    CHECK(field->wire_length() == 8);
}

TEST_CASE("IFB_LLCHAR - 1-byte BCD length prefix", "[field][bcd][llchar]") {
    // BCD length: 0x04 -> 4 digits -> 2 BCD data bytes
    auto buf = B({ 0x04, 0x12, 0x34 });
    IFB_LLCHAR parser(4, "TestBCDLL");
    auto component = std::make_shared< OpaqueField >(35);
    parser.unparse(component, buf, 0);
    CHECK(component->value() == "1234");
    CHECK(component->wire_length() == 3);  // 1 prefix + 2 data
}

// =============================================================================
// Binary fields (IF_BINARY, IFB_BITMAP)
// =============================================================================

TEST_CASE("IF_BINARY - 8 raw bytes (PIN block)", "[field][binary]") {
    auto buf = B({ 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE });
    auto field = unparse_binary<IF_BINARY>(buf, 8);
    CHECK(field->value() == std::vector<uint8_t>({ 0x04, 0x12, 0x34, 0x56,
                                                    0x78, 0x9A, 0xBC, 0xDE }));
    CHECK(field->wire_length() == 8);
}

TEST_CASE("IF_BINARY - with offset inside buffer", "[field][binary]") {
    auto buf = B({ 0x00, 0x00, 0x00, 0xCA, 0xFE, 0xBA, 0xBE });
    IF_BINARY parser(4, "BinaryField");
    auto component = std::make_shared< BinaryField >(52);
    parser.unparse(component, buf, 3);
    CHECK(component->value() == std::vector<uint8_t>({ 0xCA, 0xFE, 0xBA, 0xBE }));
}

TEST_CASE("IFB_BITMAP - 64-bit primary bitmap", "[field][bitmap]") {
    // Bit 1 and 2 set: 0b11000000 = 0xC0
    auto buf = B({ 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    IFB_BITMAP parser(8, "Bitmap");
    auto component = std::make_shared< Bitmap >(-1);
    std::size_t consumed = parser.unparse(component, buf, 0);

    CHECK(consumed == 8);
    auto bmp = component->value();
    CHECK(bmp[1] == true);   // bit 1 = secondary bitmap present
    CHECK(bmp[2] == true);   // bit 2 = DE2 active
    CHECK(bmp[3] == false);
}

TEST_CASE("IFB_BITMAP - 128-bit dual bitmap", "[field][bitmap]") {
    // Bit 1 set -> secondary bitmap present -> 16 bytes total
    // Bit 65 set (first bit of secondary bitmap)
    auto buf = B({
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    });
    IFB_BITMAP parser(16, "Bitmap");
    auto component = std::make_shared< Bitmap >(-1);
    std::size_t consumed = parser.unparse(component, buf, 0);

    CHECK(consumed == 16);
    auto bmp = component->value();
    CHECK(bmp[1]  == true);
    CHECK(bmp[65] == true);
}

// =============================================================================
// Wire position (wire_offset / wire_length)
// =============================================================================

TEST_CASE("wire_offset and wire_length are set correctly", "[wire][position]") {
    auto buf = B({ 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5 });
    IFE_NUMERIC parser(6, "STAN");
    auto component = std::make_shared< OpaqueField >(11);
    component->wire_offset(42);
    std::size_t bytes = parser.unparse(component, buf, 0);
    component->wire_length(bytes);

    CHECK(component->wire_offset() == 42);
    CHECK(component->wire_length() == 6);
}

TEST_CASE("wire_length for variable field equals prefix + data", "[wire][position]") {
    // IFE_LLCHAR: 2-byte EBCDIC prefix + N data bytes
    auto buf = B({ 0xF0, 0xF3,          // EBCDIC '0','3' -> length 3
                   0xC1, 0xC2, 0xC3 }); // EBCDIC 'A','B','C'
    IFE_LLCHAR parser(3, "TestField");
    auto component = std::make_shared< OpaqueField >(37);
    component->wire_offset(10);
    std::size_t bytes = parser.unparse(component, buf, 0);
    component->wire_length(bytes);

    CHECK(component->value() == "ABC");
    CHECK(component->wire_length() == 5);  // 2 prefix + 3 data
}

// =============================================================================
// Error cases
// =============================================================================

TEST_CASE("Truncated buffer - field shorter than declared", "[error][truncated]") {
    auto buf = B({ 0xF0, 0xF1, 0xF2 });
    IFE_NUMERIC parser(8, "TruncatedField");
    auto component = std::make_shared< OpaqueField >(4);
    REQUIRE_NOTHROW(parser.unparse(component, buf, 0));
    CHECK(component->value().size() <= 3);
}

TEST_CASE("Truncated buffer - prefix ok but data truncated", "[error][truncated]") {
    // Prefix says length=10 but only 3 data bytes available
    auto buf = B({ 0xF1, 0xF0,           // EBCDIC '1','0' -> length 10
                   0xC1, 0xC2, 0xC3 });
    IFE_LLCHAR parser(10, "TruncatedVarField");
    auto component = std::make_shared< OpaqueField >(44);
    REQUIRE_NOTHROW(parser.unparse(component, buf, 0));
    CHECK(component->value().size() <= 3);
}

TEST_CASE("Offset at buffer end - no crash", "[error][offset]") {
    auto buf = B({ 0xF0, 0xF1, 0xF2 });
    IFE_NUMERIC parser(6, "OffsetField");
    auto component = std::make_shared< OpaqueField >(4);
    REQUIRE_NOTHROW(parser.unparse(component, buf, 3));
    CHECK(component->value().empty());
}

TEST_CASE("Empty buffer - IFE_NUMERIC returns empty field", "[error][empty]") {
    std::vector<uint8_t> buf;
    IFE_NUMERIC parser(6, "EmptyField");
    auto component = std::make_shared< OpaqueField >(4);
    REQUIRE_NOTHROW(parser.unparse(component, buf, 0));
}

TEST_CASE("Empty buffer - IF_BINARY returns empty field", "[error][empty]") {
    std::vector<uint8_t> buf;
    IF_BINARY parser(8, "EmptyBinary");
    auto component = std::make_shared< BinaryField >(52);
    REQUIRE_NOTHROW(parser.unparse(component, buf, 0));
    CHECK(component->value().empty());
}
