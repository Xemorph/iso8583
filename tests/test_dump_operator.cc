// =============================================================================
// test_dump_operator.cc - Tests für operator<</dump()
// =============================================================================
//
// Deckt die Ablösung von print(bool) (fest auf stdout via fmt::print) durch
// dump(std::ostream&, bool) + operator<< ab: der Aufrufer bestimmt jetzt,
// wohin die Ausgabe geht (hier: ein std::ostringstream, um sie als String zu
// prüfen - stellvertretend für "Datei", "Log", o.ä.).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <iso8583/ISOMessage.hh>
#include <sstream>

using namespace TNG_NAMESPACE;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("operator<< - empty ISOMessage", "[dump]") {
    auto msg = std::make_shared<Message>();
    std::ostringstream oss;
    oss << *msg;
    CHECK_THAT(oss.str(), ContainsSubstring("ISOMessage"));
}

TEST_CASE("operator<< - message with fields, output lands in the desired stream", "[dump]") {
    auto msg = std::make_shared<Message>("0200");
    msg->set(TNG_KEY_TYPE(2), std::string("4111111111111111"));
    msg->set(TNG_KEY_TYPE(3), std::string("000000"));

    std::ostringstream oss;
    oss << *msg;
    const std::string out = oss.str();

    // DE-Nummern (3-stellig, nullgepolstert) und Werte müssen auftauchen
    CHECK_THAT(out, ContainsSubstring("DE002"));
    CHECK_THAT(out, ContainsSubstring("4111111111111111"));
    CHECK_THAT(out, ContainsSubstring("DE003"));
    CHECK_THAT(out, ContainsSubstring("000000"));
}

TEST_CASE("operator<< - output is freely redirectable (two independent streams)", "[dump]") {
    // Der eigentliche Kern des Umbaus: verschiedene Aufrufer können
    // unabhängig voneinander bestimmen, wohin die Ausgabe geht - anders als
    // die alte print(bool), die immer fest auf stdout schrieb.
    auto msg = std::make_shared<Message>("0100");
    msg->set(TNG_KEY_TYPE(11), std::string("000042"));

    std::ostringstream a, b;
    a << *msg;
    b << *msg;

    CHECK(a.str() == b.str());
    CHECK_FALSE(a.str().empty());
}

TEST_CASE("operator<< - binary field is rendered as 2-digit hex per byte", "[dump]") {
    // Regressionstest für einen dabei behobenen Formatierungsfehler: die
    // alte fmt::print-Variante nutzte {:X} pro Byte OHNE Zero-Padding -
    // {0x01, 0x23} hätte "123" ergeben (mehrdeutig: 0x01,0x23 oder 0x12,0x3?).
    // Jetzt garantiert 2 Hex-Ziffern pro Byte.
    auto se72 = std::make_shared<BinaryField>(72, std::vector<uint8_t>{ 0x01, 0x23, 0xAB });
    std::ostringstream oss;
    oss << *se72;
    CHECK_THAT(oss.str(), ContainsSubstring("0123AB"));
}

TEST_CASE("operator<< - stream state (flags/fill) remains unchanged after the call", "[dump]") {
    // dump() nutzt intern std::hex/std::setfill/std::left für die Formatierung
    // und MUSS das wieder zurücksetzen - sonst würde nachfolgender Output auf
    // demselben Stream unerwartet beeinflusst (z.B. Zahlen plötzlich in Hex).
    auto se = std::make_shared<BinaryField>(1, std::vector<uint8_t>{ 0xAB });
    std::ostringstream oss;

    const auto flags_before = oss.flags();
    const auto fill_before = oss.fill();

    oss << *se;

    CHECK(oss.flags() == flags_before);
    CHECK(oss.fill() == fill_before);

    // Praktische Probe: eine normale Dezimalzahl danach darf nicht in Hex
    // oder mit Fremd-Fill-Zeichen auftauchen.
    oss.str("");
    oss << 42;
    CHECK(oss.str() == "42");
}

TEST_CASE("operator<< - nested message (TLV sub-message) is printed recursively", "[dump]") {
    auto parent = std::make_shared<Message>();
    auto child = std::make_shared<Message>(TNG_KEY_TYPE(48));
    child->set(TNG_KEY_TYPE(1), std::string("child-value"));
    parent->set(child);

    std::ostringstream oss;
    oss << *parent;
    CHECK_THAT(oss.str(), ContainsSubstring("child-value"));
}
