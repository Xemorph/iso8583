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

TEST_CASE("bitmap FR-9 - length 8 builds the secondary bitmap anyway (0.7.1 asymmetry)",
    "[bitmap][fr9][characterization]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8 }"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto wire = build(parser, { 3, 70 });
    CHECK(wire == cat({ B({ 0x02, 0x00 }),
        B({ 0xA0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0, 0, 0, 0, 0, 0 }), kDe3, kDe70 }));

    // ... aber mit length 8 wird die Sekundaer-Bitmap beim Decode nicht
    // gelesen: die eigene Ausgabe ist nicht rueckdekodierbar.
    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    CHECK_THROWS_AS(msg2->unparse(msg2, wire), std::runtime_error);
}
