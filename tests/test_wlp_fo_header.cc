// =============================================================================
// test_wlp_fo_header.cc - Tests für WLP_FOHeader::pack()/unpack()
// =============================================================================
//
// Deckt die Umstellung von toEbcdic()/fromEbcdic() (entfernte, unbedingt von
// iconv abhängige Wrapper) auf codec::to<Encoder::EBCDIC>()/
// codec::as<std::string, Encoder::EBCDIC>() ab - dieselben Funktionen, die
// der Rest der Bibliothek für EBCDIC-Felder verwendet und die ENABLE_ICONV
// bereits korrekt respektieren.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iso8583/ISOMessage.hh>

using namespace TNG_NAMESPACE;

TEST_CASE("WLP_FOHeader - pack()/unpack() Roundtrip", "[header][wlp_fo]") {
    WLP_FOHeader hdr("REC0000001"_sv, "0100"_sv, 1, "UUID1234567890123456"_sv,
        "REF0000000000000"_sv, 42);

    const auto packed = hdr.pack();
    REQUIRE_FALSE(packed.empty());

    // Frisch aus den gepackten (EBCDIC-)Bytes dekodieren ...
    WLP_FOHeader decoded(packed);
    // Rückgabewert = Anzahl konsumierter Bytes aus dem Eingabepuffer `b`
    // (nicht header.size() - der interne header-Puffer ist wegen des
    // separat verwalteten 4-Byte-Längen-Prefix 4 Bytes größer als `packed`).
    CHECK(decoded.unpack(packed) == packed.size());

    // ... und erneut packen muss exakt dieselben Bytes reproduzieren -
    // beweist, dass codec::to/codec::as tatsächlich zueinander inverse
    // EBCDIC-Konvertierungen liefern (das genaue Gegenstück zum vorherigen
    // toEbcdic()/fromEbcdic()-Duo, jetzt ohne feste iconv-Abhängigkeit).
    CHECK(decoded.pack() == packed);
}

TEST_CASE("WLP_FOHeader - pack() returns EBCDIC-encoded bytes", "[header][wlp_fo]") {
    // "0100" ist im MTI-Feld enthalten - ASCII '0'=0x30, EBCDIC '0'=0xF0.
    WLP_FOHeader hdr("REC0000001"_sv, "0100"_sv, 1, "UUID1234567890123456"_sv,
        "REF0000000000000"_sv, 42);

    const auto packed = hdr.pack();
    // Byte 0x30 (ASCII '0') darf in einem EBCDIC-kodierten Puffer nicht mehr
    // vorkommen (0xF0 ist die EBCDIC-Entsprechung von '0').
    CHECK(std::find(packed.begin(), packed.end(), uint8_t{ 0x30 }) == packed.end());
    CHECK(std::find(packed.begin(), packed.end(), uint8_t{ 0xF0 }) != packed.end());
}

TEST_CASE("WLP_FOHeader - default construction and length()/version()", "[header][wlp_fo]") {
    WLP_FOHeader hdr;
    CHECK(hdr.length() == static_cast<int>(WLP_FOHeader::LENGTH));

    hdr.version(2);
    CHECK(hdr.version() == 2);
}

TEST_CASE("WLP_FOHeader - clone() returns an independent copy", "[header][wlp_fo]") {
    WLP_FOHeader hdr("REC0000001"_sv, "0200"_sv, 2, "UUID1234567890123456"_sv,
        "REF0000000000000"_sv, 42);

    auto clone = hdr.clone();
    REQUIRE(clone != nullptr);

    auto* cloned_wlp = dynamic_cast<WLP_FOHeader*>(clone.get());
    REQUIRE(cloned_wlp != nullptr);
    CHECK(cloned_wlp->pack() == hdr.pack());
}
