// =============================================================================
// test_pos_data_code.cc - Tests für pos::POSDataCode
// =============================================================================

#include <cstdint>
#include <sstream>
#include <vector>

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

TEST_CASE("POSDataCode - full set of flag operators (|, &, ^, ~, |=, &=, ^=)", "[pos]") {
    const auto r = ReadingMethod::CONTACTLESS | ReadingMethod::ICC;

    // & : Durchschnitt (nicht gesetzter Flag -> Wert 0)
    CHECK((r & ReadingMethod::ICC) == ReadingMethod::ICC);
    CHECK(static_cast<unsigned int>(r & ReadingMethod::MAGNETIC_STRIPE) == 0u);

    // ^ : symmetrische Differenz entfernt/ergänzt einen einzelnen Flag
    auto x = r ^ ReadingMethod::CONTACTLESS;
    CHECK(static_cast<unsigned int>(x) == static_cast<unsigned int>(ReadingMethod::ICC));

    // ^= : toggeln (an -> aus -> an); x ist ein ReadingMethod-Wert, daher
    // direkt per Bit-Check (has* existiert nur auf POSDataCode).
    x ^= ReadingMethod::TRACK1_PRESENT;
    CHECK((x & ReadingMethod::TRACK1_PRESENT) == ReadingMethod::TRACK1_PRESENT);
    x ^= ReadingMethod::TRACK1_PRESENT;
    CHECK(static_cast<unsigned int>(x & ReadingMethod::TRACK1_PRESENT) == 0u);
    CHECK(static_cast<unsigned int>(x) == static_cast<unsigned int>(ReadingMethod::ICC));

    // &= : Flag-Maske verkleinern
    auto y = ReadingMethod::ICC | ReadingMethod::FALLBACK;
    y &= ReadingMethod::ICC;
    CHECK(static_cast<unsigned int>(y) == static_cast<unsigned int>(ReadingMethod::ICC));

    // |= : Flag ergänzen
    auto z = VerificationMethod::NONE;
    z |= VerificationMethod::MANUAL_SIGNATURE;
    CHECK(static_cast<unsigned int>(z) ==
          static_cast<unsigned int>(VerificationMethod::NONE | VerificationMethod::MANUAL_SIGNATURE));

    // ~ : Bit-Negation; UNKNOWN (Bit 0) ist das einzige Bit, das ~UNKNOWN löscht
    CHECK(static_cast<unsigned int>(~ReadingMethod::UNKNOWN & ReadingMethod::ICC) ==
          static_cast<unsigned int>(ReadingMethod::ICC));

    // has* mit Kombinationen und mit dem UNKNOWN-Pseudoflag:
    // ein Objekt ohne gesetzte Bits meldet keins der Flags - auch nicht UNKNOWN.
    const POSDataCode empty(std::vector<uint8_t>(POSDataCode::LENGTH, 0x00));
    CHECK_FALSE(empty.hasReadingMethod(ReadingMethod::UNKNOWN));
    CHECK_FALSE(empty.hasReadingMethod(ReadingMethod::ICC));
    // Leere Flag-Maske (Wert 0): vacuously true - es fehlt kein gesetztes Bit.
    CHECK(empty.hasReadingMethod(static_cast<ReadingMethod>(0u)));
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

TEST_CASE("POSDataCode - describe()/operator<< return readable, deterministic text", "[pos]") {
    POSDataCode pdc(
        ReadingMethod::ICC | ReadingMethod::TRACK2_PRESENT,
        VerificationMethod::ONLINE_PIN,
        POSEnvironment::ATTENDED,
        SecurityCharacteristic::END_TO_END_ENCRYPTION);

    // Exakte Zeichenfolge: jede Kategorie immer gelabelt, Flags in
    // deterministischer (steigender) Bit-Reihenfolge.
    CHECK(pdc.describe() ==
          "Reading: ICC, Track 2 present; Verification: Online PIN; "
          "Environment: Attended; Security: End-to-end encryption");

    CHECK_THAT(pdc.describe(), ContainsSubstring("ICC"));
    CHECK_THAT(pdc.describe(), ContainsSubstring("Online PIN"));
    CHECK_THAT(pdc.describe(), ContainsSubstring("Attended"));
    CHECK_THAT(pdc.describe(), ContainsSubstring("End-to-end encryption"));

    std::ostringstream oss;
    oss << pdc;
    CHECK(oss.str() == pdc.describe());

    // Leere Kategorien liefern "<label>: none" ...
    const POSDataCode empty(std::vector<uint8_t>(POSDataCode::LENGTH, 0x00));
    CHECK(empty.describe() ==
          "Reading: none; Verification: none; Environment: none; Security: none");

    // ... und standardkonstruierte Objekte melden UNKNOWN in allen Kategorien.
    const POSDataCode allUnknown;
    CHECK(allUnknown.describe() ==
          "Reading: Unknown; Verification: Unknown; Environment: Unknown; Security: Unknown");

    // Deterministische Reihenfolge unabhängig von der Set-Reihenfolge:
    const POSDataCode shuffled(
        ReadingMethod::ICC | ReadingMethod::CONTACTLESS,
        VerificationMethod::UNKNOWN,
        POSEnvironment::UNKNOWN,
        SecurityCharacteristic::UNKNOWN);
    CHECK(shuffled.describe() ==
          "Reading: Contactless, ICC; Verification: Unknown; Environment: Unknown; Security: Unknown");
}

TEST_CASE("POSDataCode - renamed SecurityCharacteristic::PRIVATE_ALG_ENCRYPTION (vormals PRIVAT_ALG_*)", "[pos]") {
    const POSDataCode pdc(ReadingMethod::UNKNOWN, VerificationMethod::UNKNOWN, POSEnvironment::UNKNOWN,
        SecurityCharacteristic::PRIVATE_ALG_ENCRYPTION);

    CHECK(pdc.hasSecurityCharacteristic(SecurityCharacteristic::PRIVATE_ALG_ENCRYPTION));
    CHECK_THAT(pdc.describe(), ContainsSubstring("Private-alg encryption"));

    // Bit-Position (1 << 7) bleibt unverändert -> Round-Trip bleibt kompatibel.
    const auto raw = pdc.pack();
    const POSDataCode decoded(raw);
    CHECK(decoded.hasSecurityCharacteristic(SecurityCharacteristic::PRIVATE_ALG_ENCRYPTION));
}

TEST_CASE("POSDataCode - default arguments on the flag constructor", "[pos]") {
    const POSDataCode zeroArgs; // alle vier Kategorien = UNKNOWN
    CHECK(zeroArgs.hasReadingMethod(ReadingMethod::UNKNOWN));
    CHECK(zeroArgs.hasSecurityCharacteristic(SecurityCharacteristic::UNKNOWN));

    const POSDataCode onlyReading(ReadingMethod::ICC);
    CHECK(onlyReading.hasReadingMethod(ReadingMethod::ICC));
    CHECK_FALSE(onlyReading.hasVerificationMethod(VerificationMethod::ONLINE_PIN));
    CHECK(onlyReading.hasVerificationMethod(VerificationMethod::UNKNOWN));
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