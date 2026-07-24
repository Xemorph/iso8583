// =============================================================================
// test_iso_utils.cc - Unit-Tests für <iso8583/ISOUtils.hh>
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOUtils.hh>

using namespace TNG_NAMESPACE;
using TNG_NAMESPACE::utils::makeBitmap;

// =============================================================================
// makeBitmap() - Primary-only (DE <= 64)
// =============================================================================

TEST_CASE("makeBitmap - primary bitmap (DE2,3,4,11,49)", "[utils][bitmap]") {
    const auto bmp = makeBitmap({ 2, 3, 4, 11, 49 });
    REQUIRE(bmp.size() == 8);
    // DE2,3,4 -> Byte0 Bits 6,5,4 (0x40|0x20|0x10 = 0x70)
    CHECK(bmp[0] == 0x70);
    // DE11 -> p=10, byte=1, bit=7-2=5 -> 0x20
    CHECK(bmp[1] == 0x20);
    CHECK(bmp[2] == 0x00);
    CHECK(bmp[3] == 0x00);
    CHECK(bmp[4] == 0x00);
    // DE49 -> p=48, byte=6, bit=7-0=7 -> 0x80
    CHECK(bmp[5] == 0x00);
    CHECK(bmp[6] == 0x80);
    CHECK(bmp[7] == 0x00);
}

TEST_CASE("makeBitmap - empty DE list yields an empty 8-byte primary bitmap", "[utils][bitmap]") {
    const auto bmp = makeBitmap({});
    CHECK(bmp == std::vector<uint8_t>(8, 0x00));
}

TEST_CASE("makeBitmap - single DE64 stays within the primary bitmap", "[utils][bitmap]") {
    // DE64 ist die letzte Position der Primary-Bitmap - darf NICHT bereits
    // eine Secondary-Bitmap erzwingen.
    const auto bmp = makeBitmap({ 64 });
    REQUIRE(bmp.size() == 8);
    CHECK(bmp[7] == 0x01); // p=63, byte=7, bit=0
}

// =============================================================================
// makeBitmap() - Secondary (65 < DE <= 128)
// =============================================================================

TEST_CASE("makeBitmap - secondary bitmap is activated automatically (DE100)", "[utils][bitmap]") {
    const auto bmp = makeBitmap({ 2, 100 });
    REQUIRE(bmp.size() == 16);
    // Secondary Bitmap Present (Byte0 Bit7) automatisch gesetzt, zusätzlich zu DE2 (Bit6)
    CHECK(bmp[0] == 0xC0);
    // DE100 -> p=99, byte=99/8=12, bit=7-(99%8)=7-3=4 -> 0x10
    CHECK(bmp[12] == 0x10);
}

TEST_CASE("makeBitmap - DE65 as the secondary bitmap boundary (last valid DE is 128)", "[utils][bitmap]") {
    const auto bmp = makeBitmap({ 128 });
    REQUIRE(bmp.size() == 16);
    CHECK((bmp[0] & 0x80) == 0x80); // Secondary Present
    CHECK(bmp[15] == 0x01);          // DE128 -> p=127, byte=15, bit=0
}

// =============================================================================
// makeBitmap() - Tertiary (128 < DE <= 192)
// =============================================================================

TEST_CASE("makeBitmap - tertiary bitmap is activated automatically (DE150)", "[utils][bitmap]") {
    const auto bmp = makeBitmap({ 2, 150 });
    REQUIRE(bmp.size() == 24);
    CHECK((bmp[0] & 0x80) == 0x80); // Secondary Present
    CHECK((bmp[8] & 0x80) == 0x80); // Tertiary Present
    // DE150 -> p=149, byte=149/8=18, bit=7-(149%8)=7-5=2 -> 0x04
    CHECK(bmp[18] == 0x04);
}

TEST_CASE("makeBitmap - DE192 as the highest valid boundary", "[utils][bitmap]") {
    const auto bmp = makeBitmap({ 192 });
    REQUIRE(bmp.size() == 24);
    CHECK(bmp[23] == 0x01); // p=191, byte=23, bit=0
}

// =============================================================================
// makeBitmap() - Fehlerfälle
// =============================================================================

TEST_CASE("makeBitmap - reserved indicator positions are rejected", "[utils][bitmap][error]") {
    CHECK_THROWS_AS(makeBitmap({ 1 }), std::invalid_argument);
    CHECK_THROWS_AS(makeBitmap({ 65 }), std::invalid_argument);
    CHECK_THROWS_AS(makeBitmap({ 129 }), std::invalid_argument);
    CHECK_THROWS_WITH(makeBitmap({ 65 }),
        Catch::Matchers::ContainsSubstring("reservierte"));
}

TEST_CASE("makeBitmap - out of range [2, 192] is rejected", "[utils][bitmap][error]") {
    CHECK_THROWS_AS(makeBitmap({ 0 }), std::invalid_argument);
    CHECK_THROWS_AS(makeBitmap({ 193 }), std::invalid_argument);
    CHECK_THROWS_WITH(makeBitmap({ 193 }),
        Catch::Matchers::ContainsSubstring("außerhalb"));
}

// =============================================================================
// ISOMessage::keys() + makeBitmap(ISOMessage&)
// =============================================================================

TEST_CASE("ISOMessage::keys() - sorted, without internal sentinels -1/-2", "[utils][keys]") {
    auto msg = std::make_shared<Message>();
    // Bewusst außer der Reihe gesetzt, um die Sortierung zu prüfen.
    msg->set(TNG_KEY_TYPE(11), std::string("000001"));
    msg->set(TNG_KEY_TYPE(2), std::string("4111111111111111"));
    msg->set(TNG_KEY_TYPE(0), std::string("0100"));

    CHECK(msg->keys() == std::vector<TNG_KEY_TYPE>{ 0, 2, 11 });
}

TEST_CASE("makeBitmap(ISOMessage&) - derives the bitmap directly from the set fields", "[utils][keys][bitmap]") {
    auto msg = std::make_shared<Message>();
    msg->set(TNG_KEY_TYPE(2), std::string("PAN"));
    msg->set(TNG_KEY_TYPE(3), std::string("000000"));
    msg->set(TNG_KEY_TYPE(11), std::string("000001"));

    CHECK(makeBitmap(*msg) == makeBitmap({ 2, 3, 11 }));
}

TEST_CASE("makeBitmap(ISOMessage&) - DE0 (MTI) is NOT included in the bitmap", "[utils][keys][bitmap]") {
    // MTI liegt unter Key 0 in der ISOMessage - wird von makeBitmap(ISOMessage&)
    // automatisch herausgefiltert (siehe Doku in ISOUtils.hh), da die MTI
    // niemals Teil der Bitmap ist. msg->keys() selbst schließt nur die
    // internen Sentinels -1/-2 aus, NICHT 0 - daher der eigene Test hier.
    auto msg = std::make_shared<Message>();
    msg->set(TNG_KEY_TYPE(0), std::string("0100"));
    msg->set(TNG_KEY_TYPE(2), std::string("PAN"));

    CHECK(msg->keys() == std::vector<TNG_KEY_TYPE>{ 0, 2 });
    CHECK(makeBitmap(*msg) == makeBitmap({ 2 }));
}
