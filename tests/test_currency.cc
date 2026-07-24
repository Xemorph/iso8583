// =============================================================================
// test_currency.cc - Tests für currency::Currency
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso8583/Currency.hh>
#include <type_traits>

using namespace TNG_NAMESPACE::currency;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Currency - all Currency objects share the same type (fixes the old template-parameter design)", "[currency]") {
    // Die Vorgängerversion (_currency.LEGACY) hatte `decimals` als
    // Template-Parameter - Currency<2> (EUR) und Currency<0> (JPY) waren
    // dadurch unterschiedliche C++-Typen und konnten nicht in einer
    // gemeinsamen Laufzeit-Tabelle stehen. Hier sind beide derselbe Typ.
    auto* eur = findByNumeric(978);
    auto* jpy = findByAlpha("JPY");
    REQUIRE(eur != nullptr);
    REQUIRE(jpy != nullptr);
    STATIC_REQUIRE(std::is_same_v<decltype(eur), decltype(jpy)>);
}

TEST_CASE("Currency - findByNumeric/findByAlpha find known currencies", "[currency]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);
    CHECK(std::string(eur->alphaCode()) == "EUR");
    CHECK(eur->isoCode() == 978);
    CHECK(eur->decimals() == 2);

    auto* jpy = findByAlpha("JPY");
    REQUIRE(jpy != nullptr);
    CHECK(jpy->isoCode() == 392);
    CHECK(jpy->decimals() == 0);

    auto* jod = findByAlpha("JOD");
    REQUIRE(jod != nullptr);
    CHECK(jod->decimals() == 3); // Jordanian Dinar - einer der 3-Dezimalstellen-Sonderfälle
}

TEST_CASE("Currency - unknown codes return nullptr", "[currency]") {
    CHECK(findByNumeric(1) == nullptr);
    CHECK(findByAlpha("ZZZ") == nullptr);
    CHECK(findByAlpha("") == nullptr);
}

TEST_CASE("Currency - formatAmountForISOMessage/parse roundtrip (integer, no floating-point imprecision)", "[currency]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);

    CHECK(eur->formatAmountForISOMessage(1999LL) == "000000001999");
    CHECK(eur->parseAmountMinorUnitsFromISOMessage("000000001999") == 1999);

    // Roundtrip über viele Werte
    for (long long v : { 0LL, 1LL, 100LL, 999999999999LL }) {
        const auto formatted = eur->formatAmountForISOMessage(v);
        CHECK(formatted.size() == 12);
        CHECK(eur->parseAmountMinorUnitsFromISOMessage(formatted) == v);
    }
}

TEST_CASE("Currency - formatAmountForISOMessage/parse roundtrip (double comfort version)", "[currency]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);

    CHECK(eur->formatAmountForISOMessage(19.99) == "000000001999");
    CHECK_THAT(eur->parseAmountFromISOMessage("000000001999"), 
        Catch::Matchers::WithinAbs(19.99, 0.001));
}

TEST_CASE("Currency - JPY has 0 decimal places (no cent scale)", "[currency]") {
    auto* jpy = findByAlpha("JPY");
    REQUIRE(jpy != nullptr);
    CHECK(jpy->formatAmountForISOMessage(500.0) == "000000000500");
    CHECK_THAT(jpy->parseAmountFromISOMessage("000000000500"),
        Catch::Matchers::WithinAbs(500.0, 0.001));
}

TEST_CASE("Currency - throws at negative amounts (ISO-8583 amount fields are unsigned)", "[currency][error]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);
    CHECK_THROWS_AS(eur->formatAmountForISOMessage(-5LL), std::invalid_argument);
    CHECK_THROWS_AS(eur->formatAmountForISOMessage(-5.0), std::invalid_argument);
}

TEST_CASE("Currency - if amount is too large, throws instead of cutting", "[currency][error]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);
    CHECK_THROWS_WITH(eur->formatAmountForISOMessage(1234567890123LL),
        ContainsSubstring("passt nicht"));
}

TEST_CASE("Currency - parseAmount throws if input is empty or invalid", "[currency][error]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);
    CHECK_THROWS_AS(eur->parseAmountMinorUnitsFromISOMessage(""), std::invalid_argument);
    CHECK_THROWS_AS(eur->parseAmountMinorUnitsFromISOMessage("12a4"), std::invalid_argument);
}

TEST_CASE("Currency - operator<< returns readable text", "[currency]") {
    auto* eur = findByNumeric(978);
    REQUIRE(eur != nullptr);
    std::ostringstream oss;
    oss << *eur;
    CHECK_THAT(oss.str(), ContainsSubstring("EUR"));
    CHECK_THAT(oss.str(), ContainsSubstring("978"));
}

TEST_CASE("Currency - allCurrencies() contains a plausible number of entries", "[currency]") {
    const auto& all = allCurrencies();
    CHECK(all.size() > 150);

    // Jeder Eintrag muss über beide Lookups wiederfindbar sein
    for (const auto& c : all) {
        CHECK(findByAlpha(c.alphaCode()) == &c);
        CHECK(findByNumeric(c.isoCode()) == &c);
    }
}

TEST_CASE("Currency - Custom currencies without ISO-defined decimal places are set to 0", "[currency]") {
    // XAU (Gold), XXX (keine Waehrung) etc. haben in der Quelle "-" als
    // MinorUnit - der Generator setzt dafuer 0 Dezimalstellen.
    auto* xau = findByAlpha("XAU");
    REQUIRE(xau != nullptr);
    CHECK(xau->decimals() == 0);

    auto* xxx = findByAlpha("XXX");
    REQUIRE(xxx != nullptr);
    CHECK(xxx->isoCode() == 999);
    CHECK(xxx->decimals() == 0);
}
