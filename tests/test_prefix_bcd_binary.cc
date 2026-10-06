// =============================================================================
// test_prefix_bcd_binary.cc - FR-8 (0.8.0): BINAERES Laengenpraefix (Einheit
// ZIFFERN) vor gepackten BCD-Ziffern
//   lnum|bcd|binary, llnum|bcd|binary, lchar|bcd|binary, llchar|bcd|binary,
//   lllchar|bcd|binary
// =============================================================================
//
// Hintergrund: VISA BASE I schreibt bei ALLEN variablen Feldern ein binaeres
// Laengenbyte; bei BCD-Feldern zaehlt es ZIFFERN (DE2 '10' + 8 Byte = 16
// Ziffern, DE35 '25' = 37 Ziffern -> 19 Byte). Fixtures stammen aus der
// (maskierten) VISA-Teilnachricht der Feature-Request-Notiz.
//
// A) Codec-Roundtrips ueber die privaten Parser-Typen
// B) Message-Roundtrip mit der realen VISA-Struktur (DE2/DE32/DE35)
// C) Introspektion, bcd_pad-Interaktion, Track-2-Nibble D <-> '='
// D) Fehlerpfade (strict) und Fail-closed beim Laden
//
// Testnamen bewusst ASCII-only (Windows-ctest, s. AGENTS.md).

// [catch2]
#include <catch2/catch_test_macros.hpp>
// [catch2/string_matchers]
#include <catch2/matchers/catch_matchers_string.hpp>
// [tng]
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>
// [tng/internal] (Codec-Roundtrips brauchen die privaten Parser-Typen)
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <atomic>
#include <filesystem>
#include <fstream>

using namespace TNG_NAMESPACE;
using Catch::Matchers::ContainsSubstring;

namespace {

struct TempYaml {
    std::filesystem::path path;
    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_pbb_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// Roundtrip: unparse(Wire) -> Wert -> parse(Wert) == Wire (byte-identisch).
template <typename ParserType>
void roundtrip(const std::vector<uint8_t>& wire, const std::string& expect,
    int maxLen)
{
    ParserType parser(maxLen, "TestField");
    auto field = std::make_shared<OpaqueField>(2);
    CHECK(parser.unparse(field, wire, 0) == wire.size());
    CHECK(field->value() == expect);
    CHECK(parser.parse(field) == wire);
}

using L1  = ISOOpaqueFieldParser<codec::Length::L,   codec::PrefixEncoder::BINARY, codec::Encoder::BCD>;
using LL  = ISOOpaqueFieldParser<codec::Length::LL,  codec::PrefixEncoder::BINARY, codec::Encoder::BCD>;
using LLL = ISOOpaqueFieldParser<codec::Length::LLL, codec::PrefixEncoder::BINARY, codec::Encoder::BCD>;

// Minimale Message-Spec: BCD-MTI + Bitmap + variable Felder DE2/DE32/DE35
// in der VISA-Form (binaeres Laengenbyte, Einheit Ziffern).
const char* kVisaSpec =
    "spec: \"FR-8 VISA\"\n"
    "encoding: ebcdic\n"
    "fields:\n"
    "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
    "  \"001\": { format: bitmap,  length: 8 }\n"
    "  \"002\": { format: lnum,  encoding: bcd, prefix_encoding: binary, bcd_pad: left_zero, length: 19, description: \"PAN\" }\n"
    "  \"032\": { format: lnum,  encoding: bcd, prefix_encoding: binary, bcd_pad: left_zero, length: 11, description: \"Acquirer\" }\n"
    "  \"035\": { format: lchar, encoding: bcd, prefix_encoding: binary, bcd_pad: left_zero, length: 37, description: \"Track 2\" }\n";

// DE2 PAN (16 Ziffern), DE32 (6 Ziffern), DE35 (37 Ziffern, ungerade, 'D'-Nibble)
const std::string kPan = "4524720000000000";
const std::string kAcq = "409023";
const std::string kTrk = "452472" "0000000000" "=" "4912" "0000000000000000";

const std::vector<uint8_t> kVisaWire = {
    0x02, 0x00,                                            // MTI (BCD)
    0x40, 0x00, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00,        // Bitmap: DE2, DE32, DE35
    0x10, 0x45, 0x24, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00,  // DE2: 16 Ziffern
    0x06, 0x40, 0x90, 0x23,                                // DE32: 6 Ziffern
    0x25, 0x04, 0x52, 0x47, 0x20, 0x00, 0x00, 0x00, 0x00,  // DE35: 37 Ziffern, 19 Byte
          0x0D, 0x49, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

} // namespace

// =============================================================================
// A) Codec-Roundtrips
// =============================================================================

TEST_CASE("prefix FR-8 - LNUM|BCD|BINARY 16 digits -> 0x10 + 8 bytes", "[prefix][fr8][roundtrip]") {
    roundtrip<L1>(B({ 0x10, 0x45, 0x24, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00 }),
        "4524720000000000", 19);
}

TEST_CASE("prefix FR-8 - LNUM|BCD|BINARY 6 digits -> 0x06 + 3 bytes", "[prefix][fr8][roundtrip]") {
    roundtrip<L1>(B({ 0x06, 0x40, 0x90, 0x23 }), "409023", 11);
}

TEST_CASE("prefix FR-8 - binary prefix is NOT bcd (0x10 = 16 digits, not '10')", "[prefix][fr8][roundtrip]") {
    // 0x10 binaer = 16 Ziffern; als BCD-Praefix waeren das 10 Ziffern.
    ISOOpaqueFieldParser<codec::Length::L, codec::PrefixEncoder::BINARY, codec::Encoder::BCD> p(19, "T");
    auto f = std::make_shared<OpaqueField>(2);
    const auto wire = B({ 0x10, 0x45, 0x24, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00 });
    p.unparse(f, wire, 0);
    CHECK(f->value().size() == 16u);
}

TEST_CASE("prefix FR-8 - LLNUM|BCD|BINARY uses a 2-byte big-endian prefix", "[prefix][fr8][roundtrip]") {
    roundtrip<LL>(B({ 0x00, 0x04, 0x12, 0x34 }), "1234", 19);
}

TEST_CASE("prefix FR-8 - LLLCHAR|BCD|BINARY uses a 3-byte big-endian prefix", "[prefix][fr8][roundtrip]") {
    roundtrip<LLL>(B({ 0x00, 0x00, 0x04, 0x12, 0x34 }), "1234", 19);
}

TEST_CASE("prefix FR-8 - odd digit count default padding right_zero", "[prefix][fr8][roundtrip]") {
    // 15 Ziffern: Praefix 0x0F, 8 Byte, letztes Nibble 0 (Legacy-Default).
    roundtrip<L1>(B({ 0x0F, 0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x50 }),
        "378282246310005", 19);
}

TEST_CASE("prefix FR-8 - zero length prefix is read as maximum (characterization)", "[prefix][fr8]") {
    // Vorbestehende Eigenheit (alle Praefix-Encodings): l == 0 -> l = length.
    // Hier festgehalten, NICHT durch FR-8 geaendert.
    L1 p(4, "T");
    auto f = std::make_shared<OpaqueField>(2);
    CHECK(p.unparse(f, B({ 0x00, 0x12, 0x34 }), 0) == 3u);
    CHECK(f->value() == "1234");
}

// =============================================================================
// B) Message-Roundtrip mit der realen VISA-Struktur
// =============================================================================

TEST_CASE("prefix FR-8 - VISA DE2/DE32/DE35 build is byte-exact", "[prefix][fr8][spec][roundtrip]") {
    TempYaml y(kVisaSpec);
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(2), kPan));
    REQUIRE(msg->set(TNG_KEY_TYPE(32), kAcq));
    REQUIRE(msg->set(TNG_KEY_TYPE(35), kTrk));
    CHECK(msg->parse(msg) == kVisaWire);
}

TEST_CASE("prefix FR-8 - VISA DE2/DE32/DE35 decode and re-encode is byte-identical", "[prefix][fr8][spec][roundtrip]") {
    TempYaml y(kVisaSpec);
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, kVisaWire) == kVisaWire.size());
    CHECK(msg->tryGetValue<OpaqueField>(2) == kPan);
    CHECK(msg->tryGetValue<OpaqueField>(32) == kAcq);
    // Track 2: 37 Ziffern, fuehrendes Padding-Nibble (left_zero), 'D' -> '='.
    CHECK(msg->tryGetValue<OpaqueField>(35) == kTrk);
    CHECK(parser->parse(msg) == kVisaWire);
}

// =============================================================================
// C) Introspektion, bcd_pad, Track-2-Nibble
// =============================================================================

TEST_CASE("prefix FR-8 - introspection reports BINARY prefix and BCD payload", "[prefix][fr8][spec]") {
    TempYaml y(kVisaSpec);
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    for (int de : { 2, 32, 35 }) {
        REQUIRE(spec->field(TNG_KEY_TYPE(de)).has_value());
        CHECK(spec->field(TNG_KEY_TYPE(de))->encoding == "BCD");
        CHECK(spec->field(TNG_KEY_TYPE(de))->prefix_encoding == "BINARY");
        CHECK(spec->field(TNG_KEY_TYPE(de))->bcd_pad == "left_zero");
        CHECK(spec->field(TNG_KEY_TYPE(de))->format.prefix_digits == 1);
    }
}

TEST_CASE("prefix FR-8 - bcd_pad right_f with binary prefix", "[prefix][fr8][spec][roundtrip]") {
    TempYaml y("spec: \"FR-8 right_f\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: lnum, encoding: bcd, prefix_encoding: binary, bcd_pad: right_f, length: 19 }\n");
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(2), std::string("378282246310005")));
    const auto wire = msg->parse(msg);
    // Praefix bleibt 0x0F (15 Ziffern) - bcd_pad wirkt nie auf das Praefix.
    CHECK(wire == B({ 0x02, 0x00, 0x40, 0, 0, 0, 0, 0, 0, 0,
                      0x0F, 0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x5F }));

    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(2) == "378282246310005");
}

TEST_CASE("prefix FR-8 - LL variant loads and roundtrips through a spec", "[prefix][fr8][spec][roundtrip]") {
    TempYaml y("spec: \"FR-8 LL\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: llnum, encoding: bcd, prefix_encoding: binary, length: 19 }\n");
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    CHECK(spec->field(2)->prefix_encoding == "BINARY");
    CHECK(spec->field(2)->format.prefix_digits == 2);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(2), std::string("4111111111111111")));
    const auto wire = msg->parse(msg);
    // MTI(2) + Bitmap(8) + 2-Byte-Praefix 0x0010 + 8 Byte
    CHECK(wire.size() == 2u + 8u + 2u + 8u);
    CHECK(wire[10] == 0x00);
    CHECK(wire[11] == 0x10);

    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(2) == "4111111111111111");
}

TEST_CASE("prefix FR-8 - track 2 nibble mapping A-F to : ; < = > ?", "[prefix][fr8]") {
    // Nibble n wird als '0'+n dekodiert: D (13) -> '=' (Track-2-Trenner).
    L1 p(19, "T");
    auto f = std::make_shared<OpaqueField>(2);
    p.unparse(f, B({ 0x06, 0xAB, 0xCD, 0xEF }), 0);
    CHECK(f->value() == ":;<=>?");
    // ... und verlustfrei zurueck.
    CHECK(p.parse(f) == B({ 0x06, 0xAB, 0xCD, 0xEF }));
}

// =============================================================================
// D) Fehlerpfade
// =============================================================================

TEST_CASE("prefix FR-8 - truncated payload throws in strict mode", "[prefix][fr8][error]") {
    // Praefix sagt 37 Ziffern (19 Byte), im Puffer nur 5 Byte.
    L1 p(37, "Track2");
    auto f = std::make_shared<OpaqueField>(35);
    REQUIRE_THROWS_AS(p.unparse(f, B({ 0x25, 0x04, 0x52, 0x47, 0x20, 0x00 }), 0), std::runtime_error);
}

TEST_CASE("prefix FR-8 - truncated 2-byte prefix throws in strict mode", "[prefix][fr8][error]") {
    LL p(19, "PAN");
    auto f = std::make_shared<OpaqueField>(2);
    REQUIRE_THROWS_AS(p.unparse(f, B({ 0x00 }), 0), std::runtime_error);
}

TEST_CASE("prefix FR-8 - oversized value is rejected in strict mode", "[prefix][fr8][error]") {
    L1 p(8, "Short");
    auto f = std::make_shared<OpaqueField>(2);
    f->value(std::string("123456789"));   // 9 Ziffern > Maximum 8
    REQUIRE_THROWS_AS(p.parse(f), std::runtime_error);
}

TEST_CASE("prefix FR-8 - lbinary with bcd + binary stays rejected", "[prefix][fr8][spec][error]") {
    TempYaml y("spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: lbinary, encoding: bcd, prefix_encoding: binary, length: 8 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("prefix_encoding"));
}

TEST_CASE("prefix FR-8 - llllchar with bcd + binary stays rejected (no LLLLCHAR|BCD family)", "[prefix][fr8][spec][error]") {
    TempYaml y("spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: llllchar, encoding: bcd, prefix_encoding: binary, length: 8 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("prefix_encoding=BINARY"));
}

TEST_CASE("prefix FR-8 - fixed numeric with bcd + binary stays rejected", "[prefix][fr8][spec][error]") {
    TempYaml y("spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: numeric, encoding: bcd, prefix_encoding: binary, length: 8 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=NUMERIC"));
}

TEST_CASE("prefix FR-8 - existing FR-6 identity forms stay byte-identical", "[prefix][fr8][roundtrip]") {
    // llnum|bcd (BCD-Praefix, 2-teiliger Key) darf durch die neuen
    // 3-teiligen Keys nicht beruehrt werden: Praefix '0016' -> 1 Byte 0x16.
    roundtrip<IFB_LLNUM>(B({ 0x16, 0x45, 0x24, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00 }),
        "4524720000000000", 19);
}
