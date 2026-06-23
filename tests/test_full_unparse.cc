// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/iso8583.h>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// Baut einen minimalen ISOBaseParser fuer Tests ohne YAML-Datei.
// Felder werden direkt per push_back eingefuegt.
//
// Layout der Hilfs-Funktion:
//   slot 0 = MTI (IFE_NUMERIC, fix 4)
//   slot 1 = Bitmap (IFB_BITMAP, fix 8)
//   slot 2..N = Datenfelder
static std::shared_ptr<ISOBaseParser> make_parser(
    std::vector<std::shared_ptr<ISOFieldParserPtrBase>> field_parsers)
{
    auto parser = std::make_shared<ISOBaseParser>("TestParser", 0);
    // Slot 0: MTI
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));
    // Slot 1: Primary Bitmap
    parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
    // Restliche Felder (Index = DE-Nummer)
    for (auto& fp : field_parsers)
        parser->add(fp);
    return parser;
}

// =============================================================================
// Full unparse: MTI only (kein Bitmap, keine Felder)
// =============================================================================

TEST_CASE("Full unparse - MTI only, no bitmap", "[unparse][mti]") {
    // Parser ohne Bitmap (kein Bitmap-Slot)
    auto parser = std::make_shared<ISOBaseParser>("MTIOnly", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));

    // EBCDIC "0200" = 0xF0 0xF2 0xF0 0xF0
    auto buf = B({ 0xF0, 0xF2, 0xF0, 0xF0 });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == 4);
    CHECK(msg->hasMTI());
    CHECK(msg->mti() == "0200");
}

// =============================================================================
// Full unparse: MTI + Bitmap + single field
// =============================================================================

TEST_CASE("Full unparse - MTI + bitmap + DE2 (PAN)", "[unparse][field]") {
    // Parser: MTI(4) + Bitmap(8) + DE2=IFB_NUMERIC(19)
    auto parser = std::make_shared<ISOBaseParser>("PAN-Parser", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));
    parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
    parser->add(std::make_shared<IFB_NUMERIC>(16, "PAN"));  // DE2: 16-stellige PAN

    // MTI:    EBCDIC "0100" = F0 F1 F0 F0
    // Bitmap: Bit 2 gesetzt = 0x40 00 00 00 00 00 00 00
    // DE2:    BCD  "4111111111111111" = 41 11 11 11 11 11 11 11
    auto buf = B({
        0xF0, 0xF1, 0xF0, 0xF0,                          // MTI: 0100
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Bitmap: DE2 aktiv
        0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11   // DE2: PAN
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == 20);
    CHECK(msg->mti() == "0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");
    CHECK(de2->wire_offset() == 12);  // 4 (MTI) + 8 (Bitmap)
    CHECK(de2->wire_length() == 8);   // 16 BCD-Ziffern = 8 Bytes
}

// =============================================================================
// Full unparse: MTI + Bitmap + multiple fields
// =============================================================================

TEST_CASE("Full unparse - authorization request with amount and STAN", "[unparse][multi-field]") {
    // DE2=PAN(16), DE3=ProcessingCode(6), DE4=Amount(12), DE11=STAN(6)
    auto parser = std::make_shared<ISOBaseParser>("AuthParser", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4,  "MTI"));
    parser->add(std::make_shared<IFB_BITMAP>(8,   "Bitmap"));
    parser->add(std::make_shared<IFB_NUMERIC>(16, "PAN"));               // DE2
    parser->add(std::make_shared<IFE_NUMERIC>(6,  "Processing Code"));   // DE3
    parser->add(std::make_shared<IFB_NUMERIC>(12, "Amount"));            // DE4
    // DE5-DE10: UNUSED
    for (int i = 5; i <= 10; ++i)
        parser->add(std::make_shared<IF_NOP>());
    parser->add(std::make_shared<IFE_NUMERIC>(6,  "STAN"));              // DE11

    // Bitmap: DE2, DE3, DE4, DE11 aktiv
    // Bit  2 = 0x40 (Byte 0)
    // Bit  3 = 0x20 (Byte 0)
    // Bit  4 = 0x10 (Byte 0)
    // Bit 11 = 0x20 (Byte 1)
    // => Byte 0: 0x70, Byte 1: 0x20
    auto buf = B({
        // MTI: EBCDIC "0100"
        0xF0, 0xF1, 0xF0, 0xF0,
        // Bitmap: DE2+DE3+DE4 aktiv (Byte0=0x70), DE11 aktiv (Byte1=0x20)
        0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // DE2: BCD "4111111111111111" (8 Bytes)
        0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        // DE3: EBCDIC "000000" (6 Bytes)
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
        // DE4: BCD "000000010000" (6 Bytes)
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        // DE11: EBCDIC "000001" (6 Bytes)
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == buf.size());
    CHECK(msg->mti() == "0100");

    // DE2: PAN
    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");
    CHECK(de2->wire_offset() == 12);

    // DE3: Processing Code
    auto de3 = msg->get<ISOOpaqueField>(3);
    REQUIRE(de3 != nullptr);
    CHECK(de3->value() == "000000");
    CHECK(de3->wire_offset() == 20);  // 4+8+8

    // DE4: Amount
    auto de4 = msg->get<ISOOpaqueField>(4);
    REQUIRE(de4 != nullptr);
    CHECK(de4->value() == "000000010000");
    CHECK(de4->wire_offset() == 26);  // 4+8+8+6

    // DE11: STAN
    auto de11 = msg->get<ISOOpaqueField>(11);
    REQUIRE(de11 != nullptr);
    CHECK(de11->value() == "000001");
    CHECK(de11->wire_offset() == 32); // 4+8+8+6+6
}

// =============================================================================
// Full unparse: variable-length field (LLCHAR)
// =============================================================================

TEST_CASE("Full unparse - variable length field LLCHAR", "[unparse][variable]") {
    auto parser = std::make_shared<ISOBaseParser>("VarParser", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4,  "MTI"));
    parser->add(std::make_shared<IFB_BITMAP>(8,   "Bitmap"));
    parser->add(std::make_shared<IF_NOP>());                              // DE2: unused
    parser->add(std::make_shared<IF_NOP>());                              // DE3: unused
    parser->add(std::make_shared<IF_NOP>());                              // DE4: unused
    // DE5-DE34: unused
    for (int i = 5; i <= 34; ++i)
        parser->add(std::make_shared<IF_NOP>());
    // DE35: Track 2 Data (LLCHAR, max 37, EBCDIC)
    parser->add(std::make_shared<IFE_LLCHAR>(37, "Track2"));

    // Bitmap: DE35 aktiv = Bit 35
    // Bit 35 = Byte 4 (0-basiert), Bit 5 von links = 0x08
    auto buf = B({
        // MTI: EBCDIC "0100"
        0xF0, 0xF1, 0xF0, 0xF0,
        // Bitmap: nur DE35 aktiv (Byte 4 = 0x08)
        0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        // DE35: EBCDIC LL-Prefix "06" + 6 EBCDIC-Zeichen "123456"
        0xF0, 0xF6,
        0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == buf.size());

    auto de35 = msg->get<ISOOpaqueField>(35);
    REQUIRE(de35 != nullptr);
    CHECK(de35->value() == "123456");
    CHECK(de35->wire_offset() == 12);
    CHECK(de35->wire_length() == 8);  // 2 (prefix) + 6 (data)
}

// =============================================================================
// Full unparse: wire_offset accumulates correctly across fields
// =============================================================================

TEST_CASE("Full unparse - wire offsets accumulate correctly", "[unparse][wire]") {
    auto parser = std::make_shared<ISOBaseParser>("OffsetParser", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4,  "MTI"));
    parser->add(std::make_shared<IFB_BITMAP>(8,   "Bitmap"));
    parser->add(std::make_shared<IFE_NUMERIC>(6,  "DE2"));  // fix 6 Bytes
    parser->add(std::make_shared<IFE_NUMERIC>(4,  "DE3"));  // fix 4 Bytes
    parser->add(std::make_shared<IFE_NUMERIC>(12, "DE4"));  // fix 12 Bytes

    // Bitmap: DE2+DE3+DE4 aktiv
    auto buf = B({
        0xF0, 0xF2, 0xF0, 0xF0,                          // MTI: 0200
        0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Bitmap
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1,              // DE2: "000001" (6 Bytes)
        0xF0, 0xF0, 0xF0, 0xF2,                           // DE3: "0002" (4 Bytes)
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1, 0xF0, 0xF0, 0xF0, 0xF0,  // DE4 (12 Bytes)
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);
    msg->unparse(msg, buf);

    auto de2 = msg->get<ISOOpaqueField>(2);
    auto de3 = msg->get<ISOOpaqueField>(3);
    auto de4 = msg->get<ISOOpaqueField>(4);

    REQUIRE(de2); REQUIRE(de3); REQUIRE(de4);

    CHECK(de2->wire_offset() == 12);   // 4 (MTI) + 8 (Bitmap)
    CHECK(de2->wire_length() == 6);

    CHECK(de3->wire_offset() == 18);   // 12 + 6
    CHECK(de3->wire_length() == 4);

    CHECK(de4->wire_offset() == 22);   // 18 + 4
    CHECK(de4->wire_length() == 12);
}

// =============================================================================
// Full unparse: empty buffer handling
// =============================================================================

TEST_CASE("Full unparse - empty buffer returns 0", "[unparse][error]") {
    auto parser = std::make_shared<ISOBaseParser>("EmptyParser", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    std::vector<uint8_t> empty;
    REQUIRE_NOTHROW(msg->unparse(msg, empty));
}

TEST_CASE("Full unparse - no parser set returns SIZE_MAX", "[unparse][error]") {
    auto msg = std::make_shared<ISOMessage>();
    auto buf = std::vector<uint8_t>({ 0xF0, 0xF1, 0xF0, 0xF0 });

    // Kein Parser gesetzt -> SIZE_MAX
    auto result = msg->unparse(msg, buf);
    CHECK(result == SIZE_MAX);
}
