// =============================================================================
// test_pos_data_code.cc - Tests für pos::POSDataCode
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <iso8583/ISOMessage.hh>
#include <iso8583/POSDataCode.hh>

using namespace TNG_NAMESPACE;
using namespace TNG_NAMESPACE::pos;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("POSDataCode - combining flags via '|' (core bug of the previous version)", "[pos]") {
    // Mit enum class kompiliert das nur, wenn operator| überladen ist -
    // genau das fehlte in der alten _pos.bak/_pos.h.bak-Version.
    POSDataCode pdc(
        ReadingMethod::ICC | ReadingMethod::TRACK2_PRESENT,
        VerificationMethod::ONLINE_PIN,
        POSEnvironment::ATTENDED,
        SecurityCharacteristic::END_TO_END_ENCRYPTION);

    CHECK(pdc.hasReadingMethod(ReadingMethod::ICC));
    CHECK(pdc.hasReadingMethod(ReadingMethod::TRACK2_PRESENT));
    CHECK(pdc.hasReadingMethod(ReadingMethod::ICC | ReadingMethod::TRACK2_PRESENT));
    CHECK_FALSE(pdc.hasReadingMethod(ReadingMethod::MAGNETIC_STRIPE));
}

TEST_CASE("POSDataCode - pack()/construction-from-bytes is an exact roundtrip", "[pos]") {
    POSDataCode original(
        ReadingMethod::ICC,
        VerificationMethod::OFFLINE_PIN_ENCRYPTED,
        POSEnvironment::UNATTENDED | POSEnvironment::CAT,
        SecurityCharacteristic::CHANNEL_ENCRYPTION);

    const auto raw = original.pack();
    REQUIRE(raw.size() == POSDataCode::LENGTH);

    POSDataCode decoded(raw);
    CHECK(decoded.pack() == raw);
    CHECK(decoded.hasReadingMethod(ReadingMethod::ICC));
    CHECK(decoded.hasVerificationMethod(VerificationMethod::OFFLINE_PIN_ENCRYPTED));
    CHECK(decoded.hasPOSEnvironment(POSEnvironment::UNATTENDED));
    CHECK(decoded.hasPOSEnvironment(POSEnvironment::CAT));
    CHECK(decoded.hasSecurityCharacteristic(SecurityCharacteristic::CHANNEL_ENCRYPTION));
}

TEST_CASE("POSDataCode - wrong buffer size throws instead of silently truncating/padding", "[pos][error]") {
    CHECK_THROWS_AS(POSDataCode(std::vector<uint8_t>{ 0x01, 0x02, 0x03 }), std::invalid_argument);
    CHECK_THROWS_AS(POSDataCode(std::vector<uint8_t>(17, 0x00)), std::invalid_argument);
    CHECK_NOTHROW(POSDataCode(std::vector<uint8_t>(POSDataCode::LENGTH, 0x00)));
}

TEST_CASE("POSDataCode - isEMV/isCardNotPresent/isSwiped convenience methods", "[pos]") {
    POSDataCode emv(ReadingMethod::ICC, VerificationMethod::ONLINE_PIN,
        POSEnvironment::ATTENDED, SecurityCharacteristic::END_TO_END_ENCRYPTION);
    CHECK(emv.isEMV());
    CHECK_FALSE(emv.isCardNotPresent());
    CHECK_FALSE(emv.isSwiped());

    POSDataCode swiped(ReadingMethod::MAGNETIC_STRIPE, VerificationMethod::MANUAL_SIGNATURE,
        POSEnvironment::ATTENDED, SecurityCharacteristic::CHANNEL_MACING);
    CHECK(swiped.isSwiped());
    CHECK_FALSE(swiped.isEMV());

    POSDataCode ecom(ReadingMethod::DATA_ON_FILE, VerificationMethod::NONE,
        POSEnvironment::E_COMMERCE, SecurityCharacteristic::END_TO_END_ENCRYPTION);
    CHECK(ecom.isCardNotPresent());
    CHECK(ecom.isECommerce());

    POSDataCode recurring(ReadingMethod::DATA_ON_FILE, VerificationMethod::NONE,
        POSEnvironment::RECURRING, SecurityCharacteristic::END_TO_END_ENCRYPTION);
    CHECK(recurring.isCardNotPresent());
    CHECK(recurring.isRecurring());
}

TEST_CASE("POSDataCode - describe()/operator<< return readable text", "[pos]") {
    POSDataCode pdc(ReadingMethod::ICC, VerificationMethod::ONLINE_PIN,
        POSEnvironment::ATTENDED, SecurityCharacteristic::END_TO_END_ENCRYPTION);

    const auto desc = pdc.describe();
    CHECK_THAT(desc, ContainsSubstring("ICC"));
    CHECK_THAT(desc, ContainsSubstring("Online PIN"));
    CHECK_THAT(desc, ContainsSubstring("Attended"));
    CHECK_THAT(desc, ContainsSubstring("End-to-end encryption"));

    std::ostringstream oss;
    oss << pdc;
    CHECK(oss.str() == desc);
}

TEST_CASE("POSDataCode - integration with a real ISOMessage (BinaryField)", "[pos][integration]") {
    // Der eigentliche Verwendungszweck: ein DE, das POS-Fähigkeiten als
    // Binärfeld trägt, ganz normal über die bestehende Feld-API lesen/
    // schreiben und mit POSDataCode interpretieren/aufbauen - ohne jede
    // Änderung am Parser/Spec-System.
    auto msg = std::make_shared<Message>();

    POSDataCode built(
        ReadingMethod::ICC | ReadingMethod::TRACK1_PRESENT,
        VerificationMethod::ONLINE_PIN,
        POSEnvironment::ATTENDED,
        SecurityCharacteristic::END_TO_END_ENCRYPTION);

    msg->set(std::make_shared<BinaryField>(61, built.pack()));

    auto se = msg->get<BinaryField>(61);
    REQUIRE(se != nullptr);

    POSDataCode read(se->value());
    CHECK(read.isEMV());
    CHECK(read.hasReadingMethod(ReadingMethod::TRACK1_PRESENT));
    CHECK(read.hasVerificationMethod(VerificationMethod::ONLINE_PIN));
}
