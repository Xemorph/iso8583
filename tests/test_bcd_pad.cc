// =============================================================================
// test_bcd_pad.cc - FR-7 (0.7.1): konfigurierbares Padding bei gepacktem BCD
// mit ungerader Ziffernzahl (YAML-Key 'bcd_pad')
// =============================================================================
//
// A) Codec (as/to) direkt: right_zero / right_f / left_zero, gerade Ziffernzahl.
// B) Parser-Ebene (IFB_NUMERIC / IFB_LLNUM): Roundtrip, Validierung des
//    Padding-Nibbles (strict/nicht-strikt), Legacy ohne Deklaration.
// C) Loader: Feld-Key, Root-Default + Override, Introspektion, LLL-Praefix
//    unberuehrt, Field-only, TLV-Kinder.
// D) Fail-closed (positionierte SpecValidationError).
//
// Testnamen bewusst ASCII-only (Windows-ctest, s. AGENTS.md).

// [catch2]
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
// [tng]
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <atomic>
#include <filesystem>
#include <fstream>

using namespace TNG_NAMESPACE;
using Catch::Matchers::ContainsSubstring;
using codec::BcdPad;

namespace {

struct TempYaml {
    std::filesystem::path path;
    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_bcdpad_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

using Bytes = std::vector<uint8_t>;

Bytes encodeBcd(const std::string& digits, BcdPad pad) {
    Bytes b((digits.size() + 1) / 2, 0xAA);
    codec::to<codec::Encoder::BCD>(digits, b, 0, false, pad);
    return b;
}

// Minimale Message-Spec mit einem frei waehlbaren Header (Root-Keys) und DE2.
std::string specYaml(const std::string& rootExtra, const std::string& de2,
    const std::string& de3 = "{ format: nop }")
{
    return "spec: \"FR-7 bcd_pad test\"\nencoding: ebcdic\n" + rootExtra +
           "fields:\n"
           "  \"000\": { format: numeric, length: 4 }\n"
           "  \"001\": { format: bitmap,  length: 8 }\n"
           "  \"002\": " + de2 + "\n"
           "  \"003\": " + de3 + "\n";
}

// Message mit DE2 setzen -> Wire; Bytes ab dem DE2 (nach MTI 2 B BCD + 8 B Bitmap).
Bytes encodeDe2(const ISOParserPtrBase::ISOParserPtrBaseSmartPtr& parser, const std::string& value) {
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(2), value));
    const auto wire = msg->parse(msg);
    return wire;
}

} // namespace

// =============================================================================
// A) Codec
// =============================================================================

TEST_CASE("bcd_pad FR-7 - codec encodes odd digits per variant", "[bcdpad][codec][bcd]") {
    CHECK(encodeBcd("123", BcdPad::RIGHT_ZERO) == Bytes{ 0x12, 0x30 });
    CHECK(encodeBcd("123", BcdPad::RIGHT_F)    == Bytes{ 0x12, 0x3F });
    CHECK(encodeBcd("123", BcdPad::LEFT_ZERO)  == Bytes{ 0x01, 0x23 });
    // Ziffernzahl 1
    CHECK(encodeBcd("7", BcdPad::RIGHT_ZERO) == Bytes{ 0x70 });
    CHECK(encodeBcd("7", BcdPad::RIGHT_F)    == Bytes{ 0x7F });
    CHECK(encodeBcd("7", BcdPad::LEFT_ZERO)  == Bytes{ 0x07 });
}

TEST_CASE("bcd_pad FR-7 - even digit count is identical in all variants", "[bcdpad][codec][bcd]") {
    for (auto p : { BcdPad::RIGHT_ZERO, BcdPad::RIGHT_F, BcdPad::LEFT_ZERO })
        CHECK(encodeBcd("1234", p) == Bytes{ 0x12, 0x34 });
}

TEST_CASE("bcd_pad FR-7 - codec decodes odd digits per variant", "[bcdpad][codec][bcd]") {
    CHECK(codec::as<std::string, codec::Encoder::BCD>(Bytes{ 0x12, 0x30 }, 0, 3, false, BcdPad::RIGHT_ZERO) == "123");
    CHECK(codec::as<std::string, codec::Encoder::BCD>(Bytes{ 0x12, 0x3F }, 0, 3, false, BcdPad::RIGHT_F)    == "123");
    CHECK(codec::as<std::string, codec::Encoder::BCD>(Bytes{ 0x01, 0x23 }, 0, 3, false, BcdPad::LEFT_ZERO)  == "123");
    // Default-Argument = RIGHT_ZERO (Legacy, byte-identisch)
    CHECK(codec::as<std::string, codec::Encoder::BCD>(Bytes{ 0x12, 0x30 }, 0, 3) == "123");
}

TEST_CASE("bcd_pad FR-7 - padding nibble check helper", "[bcdpad][codec][bcd]") {
    using codec::detail::bcd_pad_nibble_ok;
    CHECK(bcd_pad_nibble_ok(Bytes{ 0x12, 0x30 }, 0, 3, BcdPad::RIGHT_ZERO));
    CHECK_FALSE(bcd_pad_nibble_ok(Bytes{ 0x12, 0x3F }, 0, 3, BcdPad::RIGHT_ZERO));
    CHECK(bcd_pad_nibble_ok(Bytes{ 0x12, 0x3F }, 0, 3, BcdPad::RIGHT_F));
    CHECK_FALSE(bcd_pad_nibble_ok(Bytes{ 0x12, 0x30 }, 0, 3, BcdPad::RIGHT_F));
    CHECK(bcd_pad_nibble_ok(Bytes{ 0x01, 0x23 }, 0, 3, BcdPad::LEFT_ZERO));
    CHECK_FALSE(bcd_pad_nibble_ok(Bytes{ 0xF1, 0x23 }, 0, 3, BcdPad::LEFT_ZERO));
    // gerade Ziffernzahl: kein Padding -> immer ok
    CHECK(bcd_pad_nibble_ok(Bytes{ 0xFF, 0xFF }, 0, 4, BcdPad::RIGHT_ZERO));
}

// =============================================================================
// B) Parser-Ebene
// =============================================================================

TEST_CASE("bcd_pad FR-7 - IFB_NUMERIC roundtrip per variant", "[bcdpad][field][bcd]") {
    struct Case { BcdPad pad; Bytes wire; };
    // Wert "051" (3 Ziffern)
    for (const auto& c : { Case{ BcdPad::RIGHT_ZERO, { 0x05, 0x10 } },
                           Case{ BcdPad::RIGHT_F,    { 0x05, 0x1F } },
                           Case{ BcdPad::LEFT_ZERO,  { 0x00, 0x51 } } }) {
        IFB_NUMERIC parser(3, "DE22");
        parser.bcdPad(c.pad);
        auto field = std::make_shared<OpaqueField>(22);
        parser.unparse(field, c.wire, 0);
        CHECK(field->value() == "051");
        CHECK(parser.parse(field) == c.wire);
    }
}

TEST_CASE("bcd_pad FR-7 - IFB_LLNUM prefix stays, only data padding changes", "[bcdpad][field][bcd]") {
    // 15 Ziffern: Praefix 0x15 (BCD, zaehlt Ziffern) + 8 Bytes Daten.
    const std::string pan = "378282246310005";
    {
        IFB_LLNUM p(19, "PAN");
        p.bcdPad(BcdPad::RIGHT_ZERO);
        auto f = std::make_shared<OpaqueField>(2);
        f->value(pan);
        CHECK(p.parse(f) == Bytes{ 0x15, 0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x50 });
    }
    {
        IFB_LLNUM p(19, "PAN");
        p.bcdPad(BcdPad::RIGHT_F);
        auto f = std::make_shared<OpaqueField>(2);
        f->value(pan);
        CHECK(p.parse(f) == Bytes{ 0x15, 0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x5F });
    }
    {
        IFB_LLNUM p(19, "PAN");
        p.bcdPad(BcdPad::LEFT_ZERO);
        auto f = std::make_shared<OpaqueField>(2);
        f->value(pan);
        CHECK(p.parse(f) == Bytes{ 0x15, 0x03, 0x78, 0x28, 0x22, 0x46, 0x31, 0x00, 0x05 });
        // und zurueck
        auto g = std::make_shared<OpaqueField>(2);
        p.unparse(g, p.parse(f), 0);
        CHECK(g->value() == pan);
    }
}

TEST_CASE("bcd_pad FR-7 - declared pad validates the nibble (strict throws)", "[bcdpad][field][bcd][strict]") {
    IFB_NUMERIC parser(3, "DE22");
    parser.bcdPad(BcdPad::RIGHT_ZERO);          // erwartet 0 hinten
    auto field = std::make_shared<OpaqueField>(22);
    CHECK_THROWS_WITH(parser.unparse(field, Bytes{ 0x05, 0x1F }, 0),
        ContainsSubstring("bcd_pad"));
    parser.strict(false);                        // nicht-strikt: nur Warnung, Wert wird geliefert
    CHECK_NOTHROW(parser.unparse(field, Bytes{ 0x05, 0x1F }, 0));
    CHECK(field->value() == "051");
}

TEST_CASE("bcd_pad FR-7 - undeclared pad keeps legacy (no validation)", "[bcdpad][field][bcd]") {
    IFB_NUMERIC parser(3, "DE22");               // bcdPad nie gesetzt
    auto field = std::make_shared<OpaqueField>(22);
    CHECK_NOTHROW(parser.unparse(field, Bytes{ 0x05, 0x1F }, 0));   // F-Padding wird toleriert (wie vor 0.7.1)
    CHECK(field->value() == "051");
    CHECK(parser.parse(field) == Bytes{ 0x05, 0x10 });              // Encode: weiter right_zero
}

// =============================================================================
// C) Loader
// =============================================================================

TEST_CASE("bcd_pad FR-7 - field key via message spec roundtrip + introspection", "[bcdpad][spec][roundtrip]") {
    TempYaml y(specYaml("", "{ format: llnum, encoding: bcd, length: 19, bcd_pad: right_f }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    REQUIRE(spec->field(2).has_value());
    CHECK(spec->field(2)->bcd_pad == "right_f");
    CHECK(spec->field(0)->bcd_pad == "");           // DE0 ist EBCDIC numeric -> keine BCD-Nutzdaten

    const auto wire = encodeDe2(parser, "123");
    // MTI "0200" EBCDIC (4 B) + Bitmap (8 B) + DE2: Praefix 0x03 + 12 3F
    REQUIRE(wire.size() >= 15);
    CHECK(Bytes(wire.end() - 3, wire.end()) == Bytes{ 0x03, 0x12, 0x3F });

    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(2) == std::string("123"));
}

TEST_CASE("bcd_pad FR-7 - default reports right_zero for BCD data fields", "[bcdpad][spec]") {
    TempYaml y(specYaml("", "{ format: llnum, encoding: bcd, length: 19 }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    CHECK(spec->field(2)->bcd_pad == "right_zero");
    const auto wire = encodeDe2(parser, "123");
    CHECK(Bytes(wire.end() - 3, wire.end()) == Bytes{ 0x03, 0x12, 0x30 });   // byte-identisch zu 0.7.0
}

TEST_CASE("bcd_pad FR-7 - root default applies; field key overrides", "[bcdpad][spec]") {
    TempYaml y(specYaml("bcd_pad: left_zero\n",
        "{ format: llnum, encoding: bcd, length: 19 }",
        "{ format: numeric, encoding: bcd, length: 3, bcd_pad: right_f }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    CHECK(spec->field(2)->bcd_pad == "left_zero");      // Root-Default
    CHECK(spec->field(3)->bcd_pad == "right_f");        // Feld ueberschreibt
    CHECK(spec->field(0)->bcd_pad == "");               // EBCDIC-Feld bleibt unberuehrt
    const auto wire = encodeDe2(parser, "123");
    CHECK(Bytes(wire.end() - 3, wire.end()) == Bytes{ 0x03, 0x01, 0x23 });
}

TEST_CASE("bcd_pad FR-7 - LLL prefix (2 BCD bytes) is not touched", "[bcdpad][spec][roundtrip]") {
    TempYaml y(specYaml("", "{ format: lllchar, encoding: bcd, length: 99, bcd_pad: right_f }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    const auto wire = encodeDe2(parser, "123");
    CHECK(Bytes(wire.end() - 4, wire.end()) == Bytes{ 0x00, 0x03, 0x12, 0x3F });
}

TEST_CASE("bcd_pad FR-7 - strict decode rejects a deviating padding nibble (message)", "[bcdpad][spec][strict]") {
    TempYaml y(specYaml("", "{ format: llnum, encoding: bcd, length: 19, bcd_pad: right_zero }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    auto wire = encodeDe2(parser, "123");
    wire.back() = 0x3F;                                  // F statt 0 im Padding-Nibble
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    CHECK_THROWS_WITH(msg->unparse(msg, wire), ContainsSubstring("bcd_pad"));
}

TEST_CASE("bcd_pad FR-7 - field-only spec", "[bcdpad][fe1][spec]") {
    TempYaml y(R"YAML(
spec: "FR-7 field-only"
encoding: bcd
bcd_pad: right_f
field:
  format: llnum
  length: 19
)YAML");
    auto [parser, spec] = spec::SpecDecoder::loadFieldBothFromYaml(y.str());
    REQUIRE(spec->field(0).has_value());
    CHECK(spec->field(0)->bcd_pad == "right_f");
    const Bytes payload = { 0x03, 0x12, 0x3F };
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);
    CHECK(msg->tryGetValue<OpaqueField>(0) == std::string("123"));
    CHECK(parser->parse(msg) == payload);
}

TEST_CASE("bcd_pad FR-7 - TLV child (BER) with odd declared length", "[bcdpad][tlv][spec]") {
    TempYaml y(R"YAML(
spec: "FR-7 tlv"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  children:
    "5A": { format: numeric, encoding: bcd, length: 3, bcd_pad: right_f }
    "5B": { format: numeric, encoding: bcd, length: 3, bcd_pad: left_zero }
)YAML");
    auto [parser, spec] = spec::SpecDecoder::loadFieldBothFromYaml(y.str());
    REQUIRE(spec->field(0).has_value());
    CHECK(spec->field(0)->tlv_children.at(0x5A).bcd_pad == "right_f");
    CHECK(spec->field(0)->tlv_children.at(0x5B).bcd_pad == "left_zero");

    // 5A: 3 Ziffern 123 -> 2 Bytes 12 3F ; 5B: 3 Ziffern 456 -> 2 Bytes 04 56
    const Bytes payload = { 0x5A, 0x02, 0x12, 0x3F, 0x5B, 0x02, 0x04, 0x56 };
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);
    CHECK(msg->tryGetValue<OpaqueField>(0x5A) == std::string("123"));
    CHECK(msg->tryGetValue<OpaqueField>(0x5B) == std::string("456"));
    CHECK(parser->parse(msg) == payload);

    // Abweichendes Nibble (5A erwartet F, hier 0) -> strict: Fehler
    const Bytes bad = { 0x5A, 0x02, 0x12, 0x30 };
    const auto bf2 = std::make_shared<BinaryField>(0, bad);
    CHECK_THROWS_WITH(spec::SpecDecoder::decodeField(parser, *bf2), ContainsSubstring("bcd_pad"));
}

// =============================================================================
// D) Fail-closed
// =============================================================================

TEST_CASE("bcd_pad FR-7 - rejected with invalid value", "[bcdpad][spec][error]") {
    TempYaml y(specYaml("", "{ format: llnum, encoding: bcd, length: 19, bcd_pad: right_x }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("erlaubt: right_zero, right_f, left_zero"));
}

TEST_CASE("bcd_pad FR-7 - rejected at root with invalid value", "[bcdpad][spec][error]") {
    TempYaml y(specYaml("bcd_pad: nonsense\n", "{ format: llnum, encoding: bcd, length: 19 }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("bcd_pad"));
}

TEST_CASE("bcd_pad FR-7 - rejected on non-BCD data field", "[bcdpad][spec][error]") {
    TempYaml y(specYaml("", "{ format: llnum, encoding: ascii, length: 19, bcd_pad: right_f }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("nur für Felder mit BCD-Nutzdaten"));
}

TEST_CASE("bcd_pad FR-7 - rejected on binary format with bcd encoding (prefix-only BCD)", "[bcdpad][spec][error]") {
    TempYaml y(specYaml("", "{ format: lllbinary, encoding: bcd, length: 99, bcd_pad: right_f }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("nur für Felder mit BCD-Nutzdaten"));
}

TEST_CASE("bcd_pad FR-7 - rejected on TLV container", "[bcdpad][spec][error]") {
    TempYaml y(R"YAML(
spec: "FR-7 container"
encoding: bcd
field:
  format: lllbertlv
  length: 999
  bcd_pad: right_f
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFieldFromYaml(y.str()),
        ContainsSubstring("bcd_pad"));
}

TEST_CASE("bcd_pad FR-7 - rejected on constructed TLV child", "[bcdpad][spec][error]") {
    TempYaml y(R"YAML(
spec: "FR-7 constructed"
encoding: bcd
field:
  format: lllbertlv
  length: 999
  children:
    "69":
      tlv: { ber: true }
      bcd_pad: right_f
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFieldFromYaml(y.str()),
        ContainsSubstring("bcd_pad"));
}

TEST_CASE("bcd_pad FR-7 - root default silently skips non-BCD fields", "[bcdpad][spec]") {
    TempYaml y(specYaml("bcd_pad: right_f\n", "{ format: llchar, encoding: ascii, length: 19 }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
    CHECK(spec->field(2)->bcd_pad == "");
}
