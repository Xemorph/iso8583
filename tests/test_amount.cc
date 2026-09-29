// =============================================================================
// test_amount.cc - AmountField: AMOUNT-Feldtyp (jPOS-ISOAmount-Konvention)
// =============================================================================
//
// Deckt die neue AmountField-Component (0.6.0) ab:
//   * Konstruktoren (Währungscode + Minor-Units -> jPOS-Wire-String),
//   * typisierte Accessors (Currency, Skala, Minor-Units, Betrag,
//     Legacy-String, readable_value) - Wire-Wert wird on demand geparst,
//   * Reskalierung und "rounding problem" (jPOS-Paritaet),
//   * Fehlerfaelle (Wire < 12 Zeichen, unbekannte Waehrung),
//   * YAML-Spec-Roundtrip (format: amount, encoding ascii / bcd) inkl.
//     AmountField-Komponenten-Erzeugung, Sensitive-Maskierung im dump()
//     und BCD-Packung,
//   * to_json()-Zusatzfelder (currency/amount/minor_units).
//
// Referenz: org.jpos.iso.ISOAmount - Wire-Format
//   zeropad3(Waehrungs-Ziffercode) + Skala (1 Ziffer) + zeropad12(Betrag),
//   z. B. EUR 19.99 -> "9782000000001999".
//
// Testnamen bewusst ASCII-only: ctest-Red auf dieser Maschine ist eine
// bekannte ACP=1252/OEMCP=437-Umlaut-Anomalie (exe-Level ist source of truth).

// [catch2]
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>
#include <iso8583/Currency.hh>
#include <iso8583/ISOUtils.hh>
// [stdc++]
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#include <process.h> // getpid()
#else
#include <unistd.h>  // getpid()
#endif

using namespace TNG_NAMESPACE;
using TNG_NAMESPACE::utils::makeBitmap;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Helpers
// =============================================================================

namespace {

// TempYaml: Identisch mit test_field_only_spec.cc - PID+Zaehler, da
// ctest -j mehrere Test-Prozesse parallel faehrt.
struct TempYaml {
    std::filesystem::path path;

    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }

    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.string(); }
};

std::vector<uint8_t> ascii_b(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// Full-Message-YAML: MTI (DE0) + Bitmap (DE1) + einem amount|<enc>-Feld.
const char* const kAmountYamlPrefix = R"(
spec: "Amount Roundtrip"
encoding: ascii

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "004":
    format: amount)";
const char* const kAmountAsciiSuffix = R"(
    length: 16
    sensitive: true
    description: "Amount"
)";
const char* const kAmountBcdSuffix = R"(
    encoding: bcd
    length: 16
    description: "Amount"
)";

} // namespace

// =============================================================================
// Konstruktoren
// =============================================================================

TEST_CASE("AmountField - constructor (EUR, 2 decimals) builds jPOS wire value", "[amount][constructor]") {
    // EUR = 978 (ISO 4217), 2 Decimalstellen; 1999 Raten = 19.99 EUR.
    AmountField f(TNG_KEY_TYPE(4), 978, 1999);
    CHECK(f.value() == "9782000000001999");
    CHECK(f.key() == TNG_KEY_TYPE(4));
}

TEST_CASE("AmountField - constructor (JPY, 0 decimals) uses scale 0", "[amount][constructor]") {
    // JPY = 392, keine Decimalstellen: Skala 0, Wert ist direkt der Betrag.
    AmountField f(TNG_KEY_TYPE(4), 392, 1999);
    CHECK(f.value() == "3920000000001999");
}

TEST_CASE("AmountField - constructor (JOD, 3 decimals) and unknown code throws", "[amount][constructor][error]") {
    // JOD = 400, 3 Decimalstellen: 199900 Raten = 199.900 JOD.
    AmountField f(TNG_KEY_TYPE(4), 400, 199900);
    CHECK(f.value() == "4003000000199900");

    // 991 ist kein ISO-4217-Ziffercode (unbelegt) -> Konstruktor wirft.
    CHECK_THROWS_AS(AmountField(TNG_KEY_TYPE(4), 991, 100), std::invalid_argument);
}

// =============================================================================
// Accessors (parsen den Wire-Wert on demand)
// =============================================================================

TEST_CASE("AmountField - accessors roundtrip on the raw wire value", "[amount][accessor]") {
    AmountField f(TNG_KEY_TYPE(4), 978, 1999);
    REQUIRE(f.value() == "9782000000001999");

    CHECK(f.currencyCode() == 978);
    CHECK(f.currencyCodeAsString() == "978");
    CHECK(f.scale() == 2);
    CHECK(f.minorUnits() == 1999);
    CHECK_THAT(f.amount(), WithinAbs(19.99, 1e-9));
    CHECK(f.legacyAmountString() == "000000001999");
    CHECK(f.readable_value() == "978/19.99");

    const auto* cur = f.currency();
    REQUIRE(cur != nullptr);
    CHECK(std::string(cur->alphaCode()) == "EUR");
    CHECK(cur->isoCode() == 978);
}

TEST_CASE("AmountField - wire value with scale 0 rescales to currency decimals", "[amount][accessor]") {
    // Skala 0, EUR hat 2 Decimalstellen: 1999 Ziffern -> 199900 Raten.
    AmountField f(TNG_KEY_TYPE(4));
    f.value("9780000000001999");
    CHECK(f.scale() == 0);
    CHECK(f.minorUnits() == 199900);
    CHECK_THAT(f.amount(), WithinAbs(1999.0, 1e-9));
}

TEST_CASE("AmountField - rounding problem throws std::invalid_argument", "[amount][error]") {
    // JPY (0 Decimals) mit Wire-Skala 2 und nicht-100-teilbaren Ziffern:
    // 1999 Ziffern / 100 ist inexact -> jPOS-"rounding problem".
    AmountField f(TNG_KEY_TYPE(4));
    f.value("3922000000001999");
    CHECK(f.scale() == 2);
    CHECK_THROWS_AS(f.minorUnits(), std::invalid_argument);
    CHECK_THROWS_AS(f.amount(), std::invalid_argument);
    CHECK_THROWS_AS(f.readable_value(), std::invalid_argument);
}

TEST_CASE("AmountField - wire value shorter than 12 characters throws", "[amount][error]") {
    AmountField f(TNG_KEY_TYPE(4));
    f.value("12345678901"); // 11 Zeichen
    CHECK_THROWS_AS(f.currencyCode(), std::invalid_argument);
    CHECK_THROWS_AS(f.scale(), std::invalid_argument);
    CHECK_THROWS_AS(f.minorUnits(), std::invalid_argument);
}

TEST_CASE("AmountField - unknown currency code yields nullptr from currency()", "[amount]") {
    // 991 fehlt in der ISO-4217-Tabelle: currency() == nullptr, aber die
    // typisierten Accessors funktionieren mit Decimals = 0 weiter.
    AmountField f(TNG_KEY_TYPE(4));
    f.value("9910000000001999");
    CHECK(f.currency() == nullptr);
    CHECK(f.currencyCode() == 991);
    CHECK(f.minorUnits() == 1999);
    CHECK_THAT(f.amount(), WithinAbs(1999.0, 1e-9));
}

// =============================================================================
// YAML-Spec-Roundtrip (format: amount, encoding ascii / bcd)
// =============================================================================

TEST_CASE("AmountField - YAML spec roundtrip (amount ascii, sensitive)", "[amount][spec][ascii]") {
    TempYaml yaml(std::string(kAmountYamlPrefix) + kAmountAsciiSuffix);
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    const std::string wire = "9782000000001999";

    // --- Builden: set() erzeugt ein AmountField, Serialisierung ---
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0100")));
    REQUIRE(msg->set(TNG_KEY_TYPE(4), wire));

    auto built = msg->get<AmountField>(4);
    REQUIRE(built != nullptr);
    CHECK(built->value() == wire);
    CHECK(built->is_sensitive()); // aus der Spec uebernommen

    const auto bytes = msg->parse(msg);
    REQUIRE(bytes.size() == 4 + 8 + 16); // MTI + Bitmap + 16 ASCII-Zeichen
    CHECK(std::equal(bytes.begin(), bytes.begin() + 4, ascii_b("0100").begin()));
    const auto bmp = makeBitmap({ 4 });
    CHECK(std::equal(bytes.begin() + 4, bytes.begin() + 12, bmp.begin()));
    const auto tail = ascii_b(wire);
    CHECK(std::equal(bytes.end() - 16, bytes.end(), tail.begin()));

    // --- Dekodieren: Komponente ist wieder AmountField, identischer Wire ---
    auto dec = std::make_shared<Message>();
    dec->parser(parser);
    const auto consumed = dec->unparse(dec, bytes);
    REQUIRE(consumed == bytes.size());
    auto af = dec->get<AmountField>(4);
    REQUIRE(af != nullptr);
    CHECK(af->value() == wire);
    CHECK(af->is_sensitive());
    CHECK(af->minorUnits() == 1999);
    CHECK(af->currencyCode() == 978);

    // Sensitive-Maskierung im dump(): Wert als "***", kein Klartext.
    std::ostringstream oss;
    oss << *af;
    CHECK_THAT(oss.str(), ContainsSubstring("***"));
    CHECK_THAT(oss.str(), !ContainsSubstring(wire)); // operator! -> MatchNotOf (stabile Catch2-API)
}

TEST_CASE("AmountField - YAML spec roundtrip (amount bcd)", "[amount][spec][bcd]") {
    TempYaml yaml(std::string(kAmountYamlPrefix) + kAmountBcdSuffix);
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    const std::string wire = "9782000000001999";

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0100")));
    REQUIRE(msg->set(TNG_KEY_TYPE(4), wire));

    const auto bytes = msg->parse(msg);
    REQUIRE(bytes.size() == 4 + 8 + 8); // 16 BCD-Ziffern = 8 gepackte Bytes
    const std::vector<uint8_t> bcd_tail{ 0x97, 0x82, 0x00, 0x00, 0x00, 0x00, 0x19, 0x99 };
    CHECK(std::equal(bytes.end() - 8, bytes.end(), bcd_tail.begin()));

    auto dec = std::make_shared<Message>();
    dec->parser(parser);
    const auto consumed = dec->unparse(dec, bytes);
    REQUIRE(consumed == bytes.size());
    auto af = dec->get<AmountField>(4);
    REQUIRE(af != nullptr);
    CHECK(af->value() == wire);
    CHECK(af->minorUnits() == 1999);
    CHECK(af->currencyCode() == 978);
}

// =============================================================================
// to_json()
// =============================================================================

TEST_CASE("AmountField - to_json adds currency, amount and minor_units", "[amount][json]") {
    AmountField f(TNG_KEY_TYPE(4), 978, 1999);

    const json j = f.to_json();
    CHECK(j.contains("key"));
    CHECK(j.contains("value"));
    CHECK(j.at("value") == "9782000000001999");

    CHECK(j.contains("currency"));
    CHECK(j.at("currency") == "EUR");
    CHECK(j.contains("minor_units"));
    CHECK(j.at("minor_units") == 1999);
    CHECK(j.contains("amount"));
    CHECK_THAT(j.at("amount").get<double>(), WithinAbs(19.99, 1e-9));
}
// =============================================================================
// Plain-Form (Standard-ISO-8583, 'scale:'-Key) - 0.6.0
// =============================================================================

namespace {

std::string plainAmountYaml(const std::string& extra, const std::string& fmt = "amount") {
    return std::string(R"(
spec: "Amount Plain"
encoding: ascii

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "004":
    format: )") + fmt + R"(
    length: 12
)" + extra + R"(    description: "Amount of Transaction"
)";
}

} // namespace

TEST_CASE("AmountField plain - YAML roundtrip ascii", "[amount][spec][ascii][plain]") {
    TempYaml yaml(plainAmountYaml("    scale: 2\n"));
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    const std::string wire = "000000019990";
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(4), wire));

    const auto bytes = msg->parse(msg);
    REQUIRE(bytes.size() == 4 + 8 + 12);
    const auto tail = ascii_b(wire);
    CHECK(std::equal(bytes.end() - 12, bytes.end(), tail.begin()));

    auto dec = std::make_shared<Message>();
    dec->parser(parser);
    REQUIRE(dec->unparse(dec, bytes) == bytes.size());
    auto af = dec->get<AmountField>(4);
    REQUIRE(af != nullptr);
    CHECK(af->value() == wire);
    CHECK(af->scale() == 2);
    CHECK(af->minorUnits() == 19990);
    CHECK_THAT(af->amount(), WithinAbs(199.90, 1e-9));
    CHECK(af->readable_value() == "199.90");
    CHECK(af->legacyAmountString() == wire);
    CHECK(af->currency() == nullptr);
    CHECK(af->currencyCode() == 0);
    CHECK(af->currencyCodeAsString().empty());
}

TEST_CASE("AmountField plain - YAML roundtrip bcd", "[amount][spec][bcd][plain]") {
    TempYaml yaml(plainAmountYaml("    encoding: bcd\n    scale: 2\n"));
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    const std::string wire = "000000019990";
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(4), wire));

    const auto bytes = msg->parse(msg);
    REQUIRE(bytes.size() == 4 + 8 + 6); // 12 BCD-Ziffern = 6 Bytes
    const std::vector<uint8_t> bcd_tail{ 0x00, 0x00, 0x00, 0x01, 0x99, 0x90 };
    CHECK(std::equal(bytes.end() - 6, bytes.end(), bcd_tail.begin()));

    auto dec = std::make_shared<Message>();
    dec->parser(parser);
    REQUIRE(dec->unparse(dec, bytes) == bytes.size());
    auto af = dec->get<AmountField>(4);
    REQUIRE(af != nullptr);
    CHECK(af->minorUnits() == 19990);
    CHECK(af->scale() == 2);
}

TEST_CASE("AmountField plain - scale 0 has no decimals", "[amount][plain]") {
    AmountField f(TNG_KEY_TYPE(4), AmountForm::plain, 0);
    f.value(std::string("000000001234"));
    CHECK(f.minorUnits() == 1234);
    CHECK(f.readable_value() == "1234");
    CHECK_THAT(f.amount(), WithinAbs(1234.0, 1e-9));
}

TEST_CASE("AmountField plain - non-numeric wire value throws", "[amount][plain][error]") {
    AmountField f(TNG_KEY_TYPE(4), AmountForm::plain, 2);
    f.value(std::string("00000001AB90"));
    CHECK_THROWS_AS(f.minorUnits(), std::invalid_argument);
}

TEST_CASE("AmountField plain - to_json has scale and no currency", "[amount][json][plain]") {
    AmountField f(TNG_KEY_TYPE(4), AmountForm::plain, 2);
    f.value(std::string("000000019990"));

    const json j = f.to_json();
    CHECK(j.at("value") == "000000019990");
    CHECK_FALSE(j.contains("currency"));
    CHECK(j.at("scale") == 2);
    CHECK(j.at("minor_units") == 19990);
    CHECK_THAT(j.at("amount").get<double>(), WithinAbs(199.90, 1e-9));
}

TEST_CASE("AmountField plain - scale key validation is fail-closed", "[amount][spec][error][plain]") {
    SECTION("scale on non-amount field") {
        TempYaml yaml(plainAmountYaml("    scale: 2\n", "numeric"));
        CHECK_THROWS_AS(spec::SpecDecoder::loadFromYaml(yaml.str()), std::runtime_error);
    }
    SECTION("negative scale") {
        TempYaml yaml(plainAmountYaml("    scale: -1\n"));
        CHECK_THROWS_AS(spec::SpecDecoder::loadFromYaml(yaml.str()), std::runtime_error);
    }
    SECTION("non-numeric scale") {
        TempYaml yaml(plainAmountYaml("    scale: abc\n"));
        CHECK_THROWS_AS(spec::SpecDecoder::loadFromYaml(yaml.str()), std::runtime_error);
    }
}

TEST_CASE("AmountField plain - introspection reports amount_scale", "[amount][spec][plain]") {
    SECTION("declared") {
        TempYaml yaml(plainAmountYaml("    scale: 3\n"));
        auto [parser, sp] = spec::SpecDecoder::loadBothFromYaml(yaml.str());
        const auto info = sp->field(4).value();
        REQUIRE(info.amount_scale.has_value());
        CHECK(*info.amount_scale == 3);
    }
    SECTION("absent means jPOS form") {
        TempYaml yaml(plainAmountYaml(""));
        auto [parser, sp] = spec::SpecDecoder::loadBothFromYaml(yaml.str());
        CHECK_FALSE(sp->field(4).value().amount_scale.has_value());
    }
}

TEST_CASE("AmountField plain - TLV child honors scale", "[amount][spec][tlv][plain]") {
    TempYaml yaml(R"(
spec: "Amount TLV"
encoding: ascii

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "048":
    type: nested
    format: lllchar
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "01": { format: amount, length: 12, scale: 2 }
      "02": { format: amount, length: 16 }
)");
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // SE 01 (plain, 12 Ziffern) + SE 02 (jPOS, 16 Zeichen): Tag(2)+Len(2)+Daten (ASCII-Ziffern)
    const std::string payload = "01120000000199900216" "9782000000001999";
    std::string body = "0200";
    const auto bmp = makeBitmap({ 48 });
    body.append(bmp.begin(), bmp.end());
    char len[4];
    std::snprintf(len, sizeof(len), "%03zu", payload.size());
    body += len + payload;

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, ascii_b(body)) == body.size());

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto plain = de48->get<AmountField>(1);
    REQUIRE(plain != nullptr);
    CHECK(plain->scale() == 2);
    CHECK(plain->minorUnits() == 19990);
    CHECK(plain->currency() == nullptr);

    auto jpos = de48->get<AmountField>(2);
    REQUIRE(jpos != nullptr);
    CHECK(jpos->currencyCode() == 978);
    CHECK(jpos->minorUnits() == 1999);
}
