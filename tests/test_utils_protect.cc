// =============================================================================
// test_utils_protect.cc - Tests für tng::utils::protect<>() (internes Utility)
// =============================================================================
//
// protect<'#'> war im Header als extern template deklariert, in _utils.cc
// aber nie explizit instanziiert (dort stand stattdessen protect<'*'>, das
// nirgends referenziert wird) - ein latenter Linker-Fehler, sobald die
// (noch auskommentierte) PAN-Maskierung in ISOComponent::print() aktiviert
// wird. Dieser Test instanziiert/ruft protect<'#'> direkt auf und beweist
// damit, dass das Symbol tatsächlich linkt.

#include <catch2/catch_test_macros.hpp>

#include "_utils.hh"

using namespace TNG_NAMESPACE;

TEST_CASE("protect<'#'> - short strings (<=4 chars) are fully masked", "[utils][protect]") {
    CHECK(utils::protect<'#'>("1234") == "####");
    CHECK(utils::protect<'#'>("1") == "#");
    CHECK(utils::protect<'#'>("") == "");
}

TEST_CASE("protect<'#'> - PAN: BIN (6) and last 4 digits remain visible", "[utils][protect]") {
    // 16-stellige PAN: erste 6 (BIN) + letzte 4 sichtbar, Rest maskiert
    const auto masked = utils::protect<'#'>("4111111111111111");
    CHECK(masked == "411111######1111");
}

TEST_CASE("protect<'#'> - Track2 data: separator '=' preserved, remainder masked", "[utils][protect]") {
    const auto masked = utils::protect<'#'>("4111111111111111=29012011234567890");
    // BIN (6) sichtbar, Mittelteil bis 4 vor '=' maskiert, '=' erhalten,
    // alles danach maskiert.
    CHECK(masked.substr(0, 6) == "411111");
    CHECK(masked.find('=') != std::string::npos);
    CHECK(masked.back() == '#');
}

TEST_CASE("protect<'#'> - short separator index (<6) returns the string unchanged", "[utils][protect]") {
    // sepPos < 6 -> per Implementierung bewusst keine Maskierung (siehe
    // protect() in _utils.cc)
    CHECK(utils::protect<'#'>("12=34567") == "12=34567");
}

TEST_CASE("protect<'_'> - default mask character works identically", "[utils][protect]") {
    CHECK(utils::protect<'_'>("1234") == "____");
    CHECK(utils::protect("1234") == "____"); // Default-Templateparameter '_'
}
