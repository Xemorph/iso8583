// =============================================================================
// test_base1_header.cc - Tests für BASE1Header (bcd2str()/str2bcd())
// =============================================================================
//
// Deckt den Fund aus der Code-Review ab:
//   - str2bcd() band den string_view-Parameter bei ungerader Eingabelänge an
//     ein sofort zerstörtes Temporary (dangling view, UB). Die Tests mit
//     5-stelligen IDs unten hätten das zuverlässig aufgedeckt (Crash oder
//     Garbage-Output unter ASan/in der Praxis).
//   - bcd2str() war ein Stub (immer ""); source()/destination()/
//     getRejectCode() lieferten dadurch nie echte Werte.

#include <catch2/catch_test_macros.hpp>

#include <iso8583/ISOMessage.hh>

using namespace TNG_NAMESPACE;

TEST_CASE("BASE1Header - default source/destination is \"000000\"", "[header][base1]") {
    BASE1Header hdr;
    auto src = hdr.source();
    auto dst = hdr.destination();
    REQUIRE(src.has_value());
    REQUIRE(dst.has_value());
    CHECK(*src == "000000");
    CHECK(*dst == "000000");
}

TEST_CASE("BASE1Header - source/destination roundtrip with even digit count", "[header][base1]") {
    BASE1Header hdr;
    hdr.source("123456");
    hdr.destination("654321");

    REQUIRE(hdr.source().has_value());
    REQUIRE(hdr.destination().has_value());
    CHECK(*hdr.source() == "123456");
    CHECK(*hdr.destination() == "654321");
}

TEST_CASE("BASE1Header - source/destination roundtrip with ODD digit count", "[header][base1][regression]") {
    // Regressionstest für den Dangling-string_view-Bug in str2bcd(): bei
    // ungerader Länge wird intern links mit '0' aufgefüllt. Vorher band der
    // Code dafür den string_view-Parameter an ein sofort zerstörtes
    // Temporary - mit ASan zuverlässig ein use-after-free, sonst je nach
    // Stack-Zustand stiller Datenmüll.
    BASE1Header hdr;
    hdr.source("12345");       // 5 Ziffern -> intern "012345"
    hdr.destination("1");      // 1 Ziffer  -> intern "000001"

    REQUIRE(hdr.source().has_value());
    REQUIRE(hdr.destination().has_value());
    CHECK(*hdr.source() == "012345");
    CHECK(*hdr.destination() == "000001");
}

TEST_CASE("BASE1Header - multiple consecutive calls return stable values", "[header][base1]") {
    // Prüft indirekt, dass der Rückgabe-string_view auf einen an die Instanz
    // gebundenen Cache zeigt (nicht auf ein Funktions-lokales Temporary):
    // mehrere Aufrufe hintereinander, mit Zwischennutzung des vorherigen
    // Ergebnisses, dürfen sich nicht gegenseitig invalidieren.
    BASE1Header hdr;
    hdr.source("111111");
    hdr.destination("222222");

    auto src1 = hdr.source();
    REQUIRE(src1.has_value());
    std::string src1_copy(*src1);

    auto dst1 = hdr.destination();
    REQUIRE(dst1.has_value());

    // src1 selbst bleibt gültig, solange `hdr` lebt und source() nicht
    // erneut aufgerufen wurde (dann würde der Cache überschrieben).
    CHECK(*src1 == src1_copy);
    CHECK(*dst1 == "222222");
}

TEST_CASE("BASE1Header - constructor with source/destination", "[header][base1]") {
    BASE1Header hdr("54321"_sv, "9"_sv);
    REQUIRE(hdr.source().has_value());
    REQUIRE(hdr.destination().has_value());
    CHECK(*hdr.source() == "054321");
    CHECK(*hdr.destination() == "000009");
}

TEST_CASE("BASE1Header - getRejectCode without reject extension is empty", "[header][base1]") {
    BASE1Header hdr;
    CHECK(hdr.isRejected() == false);
    CHECK(hdr.getRejectCode() == "");
}

TEST_CASE("BASE1Header - source() with 7 digits throws (max. 6 allowed)", "[header][base1][error]") {
    BASE1Header hdr;
    CHECK_THROWS_AS(hdr.source("1234567"), std::invalid_argument);
}

TEST_CASE("BASE1Header - source() with non-digit throws", "[header][base1][error]") {
    BASE1Header hdr;
    CHECK_THROWS_AS(hdr.source("12a456"), std::invalid_argument);
}
