// =============================================================================
// test_bitmap_secondary.cc - FR-9 (0.8.0): Sekundaer-Bitmap
//   (a) leere Sekundaer-Bitmap beim Bauen erzwingen ('secondary: always')
//   (b) 'bitmap length' - Bit 1 + length < 16 ist beim Decode/Bauen fail-closed
// =============================================================================
//
// A) Charakterisierung des Ist-Verhaltens (0.7.1) - ausser den in WP9.2/WP9.4
//    bewusst geaenderten Faellen muss alles hier byte-identisch bleiben.
//
// Testnamen bewusst ASCII-only (Windows-ctest, s. AGENTS.md).

// [catch2]
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
// [tng]
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>
// [tng/internal] (Direkttests des Bitmap-Feldparsers)
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
            / ("libiso8583_test_bmp2_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// BCD-MTI (2 Byte) + Bitmap (Parameter) + DE3 (6 Ziffern) + DE4 (12) + DE70 (3).
std::string specYaml(const std::string& bitmapDecl, const std::string& extra = "") {
    return "spec: \"FR-9 bitmap\"\n"
           "encoding: ebcdic\n" + extra +
           "fields:\n"
           "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
           "  \"001\": " + bitmapDecl + "\n"
           "  \"003\": { format: numeric, encoding: bcd, length: 6 }\n"
           "  \"004\": { format: numeric, encoding: bcd, length: 12 }\n"
           "  \"070\": { format: numeric, encoding: bcd, length: 3 }\n";
}

std::vector<uint8_t> build(const std::shared_ptr<ISOParserPtrBase>& parser,
                           std::initializer_list<int> des) {
    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    for (int de : des) {
        if (de == 3)  REQUIRE(msg->set(TNG_KEY_TYPE(3),  std::string("000000")));
        if (de == 4)  REQUIRE(msg->set(TNG_KEY_TYPE(4),  std::string("000000010000")));
        if (de == 70) REQUIRE(msg->set(TNG_KEY_TYPE(70), std::string("301")));
    }
    return msg->parse(msg);
}

const std::vector<uint8_t> kDe3 = { 0x00, 0x00, 0x00 };
const std::vector<uint8_t> kDe4 = { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
const std::vector<uint8_t> kDe70 = { 0x30, 0x10 };

std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

} // namespace

// =============================================================================
// A) Charakterisierung (0.7.1)
// =============================================================================

TEST_CASE("bitmap FR-9 - fields <= 64 build an 8 byte bitmap without bit 1 (length 8 and 16)",
    "[bitmap][fr9][characterization]") {
    const auto expected = cat({ B({ 0x02, 0x00 }),
        B({ 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }), kDe3, kDe4 });
    for (const char* len : { "8", "16" }) {
        TempYaml y(specYaml(std::string("{ format: bitmap, length: ") + len + " }"));
        auto parser = spec::SpecDecoder::loadFromYaml(y.str());
        CHECK(build(parser, { 3, 4 }) == expected);
    }
}

TEST_CASE("bitmap FR-9 - a field > 64 builds a 16 byte bitmap with bit 1 (length 16)",
    "[bitmap][fr9][characterization]") {
    TempYaml y(specYaml("{ format: bitmap, length: 16 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = build(parser, { 3, 70 });
    CHECK(wire == cat({ B({ 0x02, 0x00 }),
        B({ 0xA0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe70 }));

    // Roundtrip der eigenen Ausgabe mit length 16.
    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(70).has_value());
}

TEST_CASE("bitmap FR-9 - an empty secondary bitmap (bit 1 set) decodes with length 16",
    "[bitmap][fr9][characterization]") {
    TempYaml y(specYaml("{ format: bitmap, length: 16 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    // Bit 1 + DE3 + DE4, danach 8 Nullbytes Sekundaer-Bitmap (VISA-Form).
    const auto wire = cat({ B({ 0x02, 0x00 }),
        B({ 0xB0, 0, 0, 0, 0, 0, 0, 0 }), B({ 0, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe4 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, wire) == wire.size());
    CHECK(msg->tryGetValue<OpaqueField>(3).has_value());
    CHECK(msg->tryGetValue<OpaqueField>(4).has_value());
    // Expert-API parser->parse(msg): die beim Decode aus der Wire uebernommene
    // Bitmap bleibt erhalten (Bit 1 + leere Sekundaer-Bitmap) -> byte-identisch.
    CHECK(parser->parse(msg) == wire);
    // Message::parse() rekalkuliert die Bitmap dagegen aus den gesetzten
    // Feldern (0.7.1): die leere Sekundaer-Bitmap entfaellt (= FR-9a).
    CHECK(msg->parse(msg) == cat({ B({ 0x02, 0x00 }),
        B({ 0x30, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe4 }));
}

// =============================================================================
// B) FR-9b: Bit 1 / Felder > 64 mit 'bitmap length' < 16 sind fail-closed
// =============================================================================

TEST_CASE("bitmap FR-9b - building a field > 64 with length 8 is rejected (strict)",
    "[bitmap][fr9][fr9b]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(3),  std::string("000000")));
    REQUIRE(msg->set(TNG_KEY_TYPE(70), std::string("301")));
    CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("length: 8"));
    CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("length: 16"));
}

TEST_CASE("bitmap FR-9b - building a field > 64 with length 8 only warns when not strict (legacy output)",
    "[bitmap][fr9][fr9b]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8 }", "strict: false\n"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = build(parser, { 3, 70 });
    CHECK(wire == cat({ B({ 0x02, 0x00 }),
        B({ 0xA0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe70 }));
}

TEST_CASE("bitmap FR-9b - decoding bit 1 with length 8 is rejected with a positioned error (strict)",
    "[bitmap][fr9][fr9b]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = cat({ B({ 0x02, 0x00 }),
        B({ 0xA0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe70 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    CHECK_THROWS_WITH(msg->unparse(msg, wire), ContainsSubstring("Bit 1"));
    CHECK_THROWS_WITH(msg->unparse(msg, wire), ContainsSubstring("Offset 2"));
    CHECK_THROWS_WITH(msg->unparse(msg, wire), ContainsSubstring("length: 16"));
}

TEST_CASE("bitmap FR-9b - the same message roundtrips with length 16 (own output is decodable)",
    "[bitmap][fr9][fr9b]") {
    for (const char* len : { "16", "24" }) {
        TempYaml y(specYaml(std::string("{ format: bitmap, length: ") + len + " }"));
        auto parser = spec::SpecDecoder::loadFromYaml(y.str());
        const auto wire = build(parser, { 3, 70 });
        auto msg2 = std::make_shared<Message>();
        msg2->parser(parser);
        REQUIRE(msg2->unparse(msg2, wire) == wire.size());
        CHECK(msg2->tryGetValue<OpaqueField>(70) == "301");
    }
}

TEST_CASE("bitmap FR-9b - primary-only messages with length 8 are unaffected",
    "[bitmap][fr9][fr9b]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = build(parser, { 3, 4 });
    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(4) == "000000010000");
}

// =============================================================================
// C) FR-9a: 'secondary: always' erzwingt die Sekundaer-Bitmap beim Bauen
// =============================================================================

TEST_CASE("bitmap FR-9a - secondary always builds an empty secondary bitmap with bit 1",
    "[bitmap][fr9][fr9a]") {
    TempYaml y(specYaml("{ format: bitmap, length: 16, secondary: always }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    // VISA-Form: Bit 1 + DE3 + DE4, danach 8 Nullbytes Sekundaer-Bitmap.
    const auto expected = cat({ B({ 0x02, 0x00 }),
        B({ 0xB0, 0, 0, 0, 0, 0, 0, 0 }), B({ 0, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe4 });
    CHECK(build(parser, { 3, 4 }) == expected);
}

TEST_CASE("bitmap FR-9a - secondary always roundtrips (decode, Message::parse and parser->parse)",
    "[bitmap][fr9][fr9a]") {
    TempYaml y(specYaml("{ format: bitmap, length: 16, secondary: always }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = cat({ B({ 0x02, 0x00 }),
        B({ 0xB0, 0, 0, 0, 0, 0, 0, 0 }), B({ 0, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe4 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, wire) == wire.size());
    CHECK(parser->parse(msg) == wire);
    CHECK(msg->parse(msg) == wire);   // anders als ohne Schalter (s. A): bleibt erhalten
}

TEST_CASE("bitmap FR-9a - secondary always with a field > 64 equals the default output",
    "[bitmap][fr9][fr9a]") {
    TempYaml yAlways(specYaml("{ format: bitmap, length: 16, secondary: always }"));
    TempYaml yAuto(specYaml("{ format: bitmap, length: 16 }"));
    CHECK(build(spec::SpecDecoder::loadFromYaml(yAlways.str()), { 3, 70 }) ==
          build(spec::SpecDecoder::loadFromYaml(yAuto.str()), { 3, 70 }));
}

TEST_CASE("bitmap FR-9a - secondary auto (explicit) equals the default (no key)",
    "[bitmap][fr9][fr9a]") {
    TempYaml yAuto(specYaml("{ format: bitmap, length: 16, secondary: auto }"));
    TempYaml yNone(specYaml("{ format: bitmap, length: 16 }"));
    CHECK(build(spec::SpecDecoder::loadFromYaml(yAuto.str()), { 3, 4 }) ==
          build(spec::SpecDecoder::loadFromYaml(yNone.str()), { 3, 4 }));
}

TEST_CASE("bitmap FR-9a - secondary always also works with a 24 byte bitmap (length 24)",
    "[bitmap][fr9][fr9a]") {
    TempYaml y(specYaml("{ format: bitmap, length: 24, secondary: ALWAYS }"));   // case-insensitive
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = build(parser, { 3, 4 });
    REQUIRE(wire.size() == 2u + 16u + kDe3.size() + kDe4.size());
    CHECK(wire[2] == 0xB0);   // Bit 1 + DE3 + DE4
}

TEST_CASE("bitmap FR-9a - introspection reports the secondary mode", "[bitmap][fr9][fr9a][spec]") {
    TempYaml yAlways(specYaml("{ format: bitmap, length: 16, secondary: always }"));
    auto [pA, sA] = spec::SpecDecoder::loadBothFromYaml(yAlways.str());
    CHECK(sA->field(1)->secondary_bitmap == "always");
    CHECK(sA->field(3)->secondary_bitmap == "");   // Nicht-Bitmap-Feld

    TempYaml yAuto(specYaml("{ format: bitmap, length: 16 }"));
    auto [pB, sB] = spec::SpecDecoder::loadBothFromYaml(yAuto.str());
    CHECK(sB->field(1)->secondary_bitmap == "auto");
}

TEST_CASE("bitmap FR-9a - secondary always with length 8 is rejected at load", "[bitmap][fr9][fr9a][spec][error]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8, secondary: always }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()), ContainsSubstring("length: 16"));
}

TEST_CASE("bitmap FR-9a - secondary on a non-bitmap field is rejected at load", "[bitmap][fr9][fr9a][spec][error]") {
    TempYaml y("spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, length: 4, secondary: always }\n"
        "  \"001\": { format: bitmap, length: 16 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()), ContainsSubstring("format: bitmap"));
}

TEST_CASE("bitmap FR-9a - secondary with an invalid value is rejected at load", "[bitmap][fr9][fr9a][spec][error]") {
    TempYaml y(specYaml("{ format: bitmap, length: 16, secondary: sometimes }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()), ContainsSubstring("erlaubt: auto, always"));
}

TEST_CASE("bitmap FR-9a - encoder never reads past the bitset (forced 16 bytes, small bitset)",
    "[bitmap][fr9][fr9a]") {
    // Bitset fuer eine Primaer-Bitmap (64 Bits + Index 0 = 65), 16 Byte erzwungen.
    auto bm = std::make_shared<Bitmap>(-1);
    dynamic_bitset<> bits(65);
    bits.set(3);
    bm->value(bits);

    IFB_BITMAP p16(16, "Bitmap");
    p16.secondaryAlways(true);
    const auto out = p16.parse(bm);
    REQUIRE(out.size() == 16u);
    CHECK(out[0] == 0xA0);   // Bit 1 (erzwungen) + Bit 3
    for (std::size_t i = 1; i < out.size(); ++i)
        CHECK(out[i] == 0x00);

    // Programmatisch gesetztes Flag mit length 8 -> FR-9b-Guard (strict).
    IFB_BITMAP p8(8, "Bitmap");
    p8.secondaryAlways(true);
    CHECK_THROWS_AS(p8.parse(bm), std::runtime_error);
}
