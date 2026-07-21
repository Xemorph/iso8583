// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/_codec.hh>

using namespace TNG_NAMESPACE::codec;

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// =============================================================================
// decode_length
// =============================================================================

TEST_CASE("decode_length - ASCII LL", "[prefixer][ascii]") {
    auto buf = B({ 0x31, 0x39, 0xFF });
    CHECK(decode_length<PrefixEncoder::ASCII, Length::LL>(buf, 0) == 19);
}

TEST_CASE("decode_length - ASCII LLL", "[prefixer][ascii]") {
    auto buf = B({ 0x30, 0x39, 0x39, 0xFF });
    CHECK(decode_length<PrefixEncoder::ASCII, Length::LLL>(buf, 0) == 99);
}

TEST_CASE("decode_length - ASCII LL with offset", "[prefixer][ascii]") {
    auto buf = B({ 0x00, 0x00, 0x00, 0x30, 0x36 });
    CHECK(decode_length<PrefixEncoder::ASCII, Length::LL>(buf, 3) == 6);
}

TEST_CASE("decode_length - ASCII LL zero length", "[prefixer][ascii]") {
    auto buf = B({ 0x30, 0x30 });
    CHECK(decode_length<PrefixEncoder::ASCII, Length::LL>(buf, 0) == 0);
}

TEST_CASE("decode_length - BCD LL", "[prefixer][bcd]") {
    // 0x19 -> 19
    auto buf = B({ 0x19, 0xFF });
    CHECK(decode_length<PrefixEncoder::BCD, Length::LL>(buf, 0) == 19);
}

TEST_CASE("decode_length - BCD LLL", "[prefixer][bcd]") {
    // 0x00, 0x37 -> 037
    auto buf = B({ 0x00, 0x37, 0xFF });
    CHECK(decode_length<PrefixEncoder::BCD, Length::LLL>(buf, 0) == 37);
}

TEST_CASE("decode_length - BINARY 1 byte", "[prefixer][binary]") {
    auto buf = B({ 0x1F, 0xFF });
    CHECK(decode_length<PrefixEncoder::BINARY, Length::L>(buf, 0) == 31);
}

TEST_CASE("decode_length - BINARY 2 bytes big-endian", "[prefixer][binary]") {
    auto buf = B({ 0x00, 0xFF, 0xAB });
    CHECK(decode_length<PrefixEncoder::BINARY, Length::LL>(buf, 0) == 255);
}

TEST_CASE("decode_length - EBCDIC LL", "[prefixer][ebcdic]") {
    // EBCDIC '1'=0xF1, '9'=0xF9 -> 19
    auto buf = B({ 0xF1, 0xF9 });
    CHECK(decode_length<PrefixEncoder::EBCDIC, Length::LL>(buf, 0) == 19);
}

TEST_CASE("decode_length - NONE always 0", "[prefixer]") {
    auto buf = B({ 0x99, 0x99 });
    CHECK(decode_length<PrefixEncoder::NONE, Length::FIX>(buf, 0) == 0);
}

// =============================================================================
// encode/decode roundtrip
// =============================================================================

TEST_CASE("roundtrip - ASCII LL", "[prefixer][roundtrip]") {
    std::vector<uint8_t> buf(2, 0);
    encode_length<PrefixEncoder::ASCII, Length::LL>(37, buf);
    REQUIRE(buf.size() == 2);
    CHECK(decode_length<PrefixEncoder::ASCII, Length::LL>(buf, 0) == 37);
}

TEST_CASE("roundtrip - BCD LL", "[prefixer][roundtrip]") {
    std::vector<uint8_t> buf(1, 0);
    encode_length<PrefixEncoder::BCD, Length::LL>(19, buf);
    REQUIRE(buf.size() == 1);
    CHECK(decode_length<PrefixEncoder::BCD, Length::LL>(buf, 0) == 19);
}

TEST_CASE("roundtrip - BINARY LL", "[prefixer][roundtrip]") {
    std::vector<uint8_t> buf(2, 0);
    encode_length<PrefixEncoder::BINARY, Length::LL>(99, buf);
    REQUIRE(buf.size() == 2);
    CHECK(decode_length<PrefixEncoder::BINARY, Length::LL>(buf, 0) == 99);
}

TEST_CASE("roundtrip - EBCDIC LL", "[prefixer][roundtrip]") {
    std::vector<uint8_t> buf(2, 0);
    encode_length<PrefixEncoder::EBCDIC, Length::LL>(42, buf);
    REQUIRE(buf.size() == 2);
    CHECK(decode_length<PrefixEncoder::EBCDIC, Length::LL>(buf, 0) == 42);
}

// =============================================================================
// as<T, Encoder>
// =============================================================================

TEST_CASE("as<string, ASCII> - basic string", "[encoder][ascii]") {
    SUCCEED("ASCII encoder path not yet implemented - test pending");
}

TEST_CASE("as<string, EBCDIC> - simple bytes", "[encoder][ebcdic]") {
    // EBCDIC 'A'=0xC1, 'B'=0xC2, 'C'=0xC3
    auto buf = B({ 0xC1, 0xC2, 0xC3 });
    CHECK(as<std::string, Encoder::EBCDIC>(buf, 0, 3) == "ABC");
}

TEST_CASE("as<string, EBCDIC> - digits 0-9", "[encoder][ebcdic]") {
    auto buf = B({ 0xF0, 0xF1, 0xF2, 0xF3, 0xF4,
                   0xF5, 0xF6, 0xF7, 0xF8, 0xF9 });
    CHECK(as<std::string, Encoder::EBCDIC>(buf, 0, 10) == "0123456789");
}

TEST_CASE("as<string, EBCDIC> - with offset", "[encoder][ebcdic]") {
    auto buf = B({ 0x00, 0x00, 0xE7, 0xE8 });
    CHECK(as<std::string, Encoder::EBCDIC>(buf, 2, 2) == "XY");
}

TEST_CASE("as<string, BCD> - even digit count", "[encoder][bcd]") {
    auto buf = B({ 0x12, 0x34 });
    CHECK(as<std::string, Encoder::BCD>(buf, 0, 4) == "1234");
}

TEST_CASE("as<string, BCD> - PAN 16 digits", "[encoder][bcd]") {
    auto buf = B({ 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 });
    CHECK(as<std::string, Encoder::BCD>(buf, 0, 16) == "4111111111111111");
}

TEST_CASE("as<string, BCD> - with offset", "[encoder][bcd]") {
    auto buf = B({ 0xFF, 0xFF, 0x56, 0x78 });
    CHECK(as<std::string, Encoder::BCD>(buf, 2, 4) == "5678");
}

TEST_CASE("as<vector, BINARY> - copy raw bytes", "[encoder][binary]") {
    auto buf = B({ 0xDE, 0xAD, 0xBE, 0xEF });
    auto result = as<std::vector<uint8_t>, Encoder::BINARY>(buf, 0, 4);
    CHECK(result == std::vector<uint8_t>({ 0xDE, 0xAD, 0xBE, 0xEF }));
}

TEST_CASE("as<vector, BINARY> - with offset", "[encoder][binary]") {
    auto buf = B({ 0x00, 0x00, 0xCA, 0xFE });
    auto result = as<std::vector<uint8_t>, Encoder::BINARY>(buf, 2, 2);
    CHECK(result == std::vector<uint8_t>({ 0xCA, 0xFE }));
}

TEST_CASE("as<vector, HEX_EBCDIC> - nibbles from EBCDIC", "[encoder][hex_ebcdic]") {
    // EBCDIC '1'=0xF1, 'A'=0xC1 -> byte 0x1A
    auto buf = B({ 0xF1, 0xC1 });
    auto result = as<std::vector<uint8_t>, Encoder::HEX_EBCDIC>(buf, 0, 1);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == 0x1A);
}

TEST_CASE("as<vector, HEX_EBCDIC> - all letter nibbles A-F", "[encoder][hex_ebcdic]") {
    // EBCDIC 'A'=0xC1 .. 'F'=0xC6, paired with '0'=0xF0
    // 0xC1,0xF0 -> 0xA0
    // 0xC2,0xF0 -> 0xB0
    // 0xC3,0xF0 -> 0xC0
    // 0xC4,0xF0 -> 0xD0
    // 0xC5,0xF0 -> 0xE0
    // 0xC6,0xF0 -> 0xF0
    auto buf = B({ 0xC1,0xF0, 0xC2,0xF0, 0xC3,0xF0,
                   0xC4,0xF0, 0xC5,0xF0, 0xC6,0xF0 });
    auto result = as<std::vector<uint8_t>, Encoder::HEX_EBCDIC>(buf, 0, 6);
    REQUIRE(result.size() == 6);
    CHECK(result[0] == 0xA0);
    CHECK(result[1] == 0xB0);
    CHECK(result[2] == 0xC0);
    CHECK(result[3] == 0xD0);
    CHECK(result[4] == 0xE0);
    CHECK(result[5] == 0xF0);
}

TEST_CASE("as<vector, HEX_EBCDIC> - digits only 0-9", "[encoder][hex_ebcdic]") {
    // EBCDIC '9'=0xF9, '5'=0xF5 -> byte 0x95
    auto buf = B({ 0xF9, 0xF5 });
    auto result = as<std::vector<uint8_t>, Encoder::HEX_EBCDIC>(buf, 0, 1);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == 0x95);
}

// =============================================================================
// ISO 8583 Grenzwerte: L=9, LL=99, LLL=999, LLLL=9999
// =============================================================================

TEST_CASE("ISO 8583 max values - ASCII", "[prefixer][iso8583][boundary]") {
    {
        std::vector<uint8_t> b(1, 0); encode_length<PrefixEncoder::ASCII, Length::L>(9, b);
        CHECK(decode_length<PrefixEncoder::ASCII, Length::L>(b, 0) == 9);
    }
    {
        std::vector<uint8_t> b(2, 0); encode_length<PrefixEncoder::ASCII, Length::LL>(99, b);
        CHECK(decode_length<PrefixEncoder::ASCII, Length::LL>(b, 0) == 99);
    }
    {
        std::vector<uint8_t> b(3, 0); encode_length<PrefixEncoder::ASCII, Length::LLL>(999, b);
        CHECK(decode_length<PrefixEncoder::ASCII, Length::LLL>(b, 0) == 999);
    }
}

TEST_CASE("ISO 8583 max values - BCD", "[prefixer][iso8583][boundary]") {
    {
        std::vector<uint8_t> b(1, 0); encode_length<PrefixEncoder::BCD, Length::L>(9, b);
        CHECK(decode_length<PrefixEncoder::BCD, Length::L>(b, 0) == 9);
    }
    {
        std::vector<uint8_t> b(2, 0); encode_length<PrefixEncoder::BCD, Length::LL>(99, b);
        CHECK(decode_length<PrefixEncoder::BCD, Length::LL>(b, 0) == 99);
    }
    {
        std::vector<uint8_t> b(3, 0); encode_length<PrefixEncoder::BCD, Length::LLL>(999, b);
        CHECK(decode_length<PrefixEncoder::BCD, Length::LLL>(b, 0) == 999);
    }
}

TEST_CASE("ISO 8583 max values - EBCDIC", "[prefixer][iso8583][boundary]") {
    {
        std::vector<uint8_t> b(1, 0); encode_length<PrefixEncoder::EBCDIC, Length::L>(9, b);
        CHECK(decode_length<PrefixEncoder::EBCDIC, Length::L>(b, 0) == 9);
    }
    {
        std::vector<uint8_t> b(2, 0); encode_length<PrefixEncoder::EBCDIC, Length::LL>(99, b);
        CHECK(decode_length<PrefixEncoder::EBCDIC, Length::LL>(b, 0) == 99);
    }
    {
        std::vector<uint8_t> b(3, 0); encode_length<PrefixEncoder::EBCDIC, Length::LLL>(999, b);
        CHECK(decode_length<PrefixEncoder::EBCDIC, Length::LLL>(b, 0) == 999);
    }
}

TEST_CASE("required_sz_for_as - BINARY identity", "[encoder][size]") {
    CHECK(required_sz_for_as<Encoder::BINARY>(8) == 8);
    CHECK(required_sz_for_as<Encoder::BINARY>(1) == 1);
}

TEST_CASE("required_sz_for_as - BCD rounds up", "[encoder][size]") {
    CHECK(required_sz_for_as<Encoder::BCD>(4) == 2);
    CHECK(required_sz_for_as<Encoder::BCD>(5) == 3);
    CHECK(required_sz_for_as<Encoder::BCD>(1) == 1);
}

TEST_CASE("required_sz_for_as - EBCDIC identity", "[encoder][size]") {
    CHECK(required_sz_for_as<Encoder::EBCDIC>(6) == 6);
}

// =============================================================================
// parsed_length
// =============================================================================

TEST_CASE("parsed_length - NONE is always 0", "[prefixer][parsed_length]") {
    CHECK(parsed_length<PrefixEncoder::NONE, Length::FIX>() == 0);
}

TEST_CASE("parsed_length - ASCII 1 byte per digit", "[prefixer][parsed_length]") {
    CHECK(parsed_length<PrefixEncoder::ASCII, Length::L>() == 1);
    CHECK(parsed_length<PrefixEncoder::ASCII, Length::LL>() == 2);
    CHECK(parsed_length<PrefixEncoder::ASCII, Length::LLL>() == 3);
    CHECK(parsed_length<PrefixEncoder::ASCII, Length::LLLL>() == 4);
}

TEST_CASE("parsed_length - BCD ceil(digits/2) bytes", "[prefixer][parsed_length]") {
    CHECK(parsed_length<PrefixEncoder::BCD, Length::L>() == 1);
    CHECK(parsed_length<PrefixEncoder::BCD, Length::LL>() == 1);
    CHECK(parsed_length<PrefixEncoder::BCD, Length::LLL>() == 2);
    CHECK(parsed_length<PrefixEncoder::BCD, Length::LLLL>() == 2);
}

TEST_CASE("parsed_length - BINARY 1 byte per digit", "[prefixer][parsed_length]") {
    CHECK(parsed_length<PrefixEncoder::BINARY, Length::L>() == 1);
    CHECK(parsed_length<PrefixEncoder::BINARY, Length::LL>() == 2);
}

TEST_CASE("parsed_length - EBCDIC 1 byte per digit", "[prefixer][parsed_length]") {
    CHECK(parsed_length<PrefixEncoder::EBCDIC, Length::LL>() == 2);
    CHECK(parsed_length<PrefixEncoder::EBCDIC, Length::LLL>() == 3);
}
