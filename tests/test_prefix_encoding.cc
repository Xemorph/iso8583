// =============================================================================
// test_prefix_encoding.cc - FR-6 (0.7.0): Laengepraefix-Encoding unabhaengig
// vom Nutzdaten-Encoding (YAML-Key 'prefix_encoding')
// =============================================================================
//
// A) Codec-Roundtrips ueber die privaten Parser-Typen (FR-6-Dispatch-
//    Kombinationen): unparse -> Wert -> parse -> byte-identisches Wire-Bild.
//    Die gemischten 3-teile-Kombinationen (Präfix-/Nutzdaten-Encoding
//    unterschiedlich) haben keine benannten Aliase und werden als
//    ISOOpaqueFieldParser<...>-Instanziierung geschrieben.
// B) Fehlerpfade (strict, Default): Nutzdaten- bzw. Praefix-Trunkation.
// C) Loader-/Validierungstests (Fail-closed-Regeln, positionierte
//    SpecValidationError) + Introspektion + Field-only + !merge.
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

// Temp-YAML in einem Eindeutigen Dateinamen (Pattern: test_strict_length.cc).
struct TempYaml {
    std::filesystem::path path;
    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_pe_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
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
    parser.unparse(field, wire, 0);
    CHECK(field->value() == expect);
    CHECK(parser.parse(field) == wire);
}

// Minimale Message-Spec: MTI + Bitmap + ein testbares Feld (DE2).
std::string specYaml(const std::string& de2) {
    return "spec: \"FR-6 prefix test\"\nencoding: ebcdic\n"
           "fields:\n"
           "  \"000\": { format: numeric, length: 4 }\n"
           "  \"001\": { format: bitmap,  length: 8 }\n"
           "  \"002\": " + de2 + "\n";
}

} // namespace

// =============================================================================
// A) Codec-Roundtrips (FR-6-Dispatch-Kombinationen)
// =============================================================================

TEST_CASE("prefix FR-6 - IFB_LLNUM roundtrip (llnum|bcd, Identitaetsluecke)",
    "[prefix][field][bcd]") {
    // 16 Ziffern: BCD-Präfix 0x16 (zwei BCD-Ziffern: '1' '6') + 8 gepackte Bytes.
    roundtrip<IFB_LLNUM>(
        B({ 0x16, 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 }),
        "4111111111111111", 19);
}

TEST_CASE("prefix FR-6 - LLCHAR|ASCII|BCD roundtrip (BCD-Praefix, ASCII-Text)",
    "[prefix][field][bcd]") {
    // 5 Zeichen: BCD-Präfix 0x05 + ASCII-Bytes.
    roundtrip<ISOOpaqueFieldParser<codec::Length::LL, codec::PrefixEncoder::BCD,
        codec::Encoder::ASCII>>(
        B({ 0x05, 0x48, 0x45, 0x4C, 0x4C, 0x4F }),
        "HELLO", 20);
}

TEST_CASE("prefix FR-6 - LLCHAR|EBCDIC|BCD roundtrip (BCD-Praefix, EBCDIC-Text)",
    "[prefix][field][bcd][ebcdic]") {
    // 2 Zeichen: BCD-Präfix 0x02 + EBCDIC 'A'(C1) 'B'(C2).
    roundtrip<ISOOpaqueFieldParser<codec::Length::LL, codec::PrefixEncoder::BCD,
        codec::Encoder::EBCDIC>>(
        B({ 0x02, 0xC1, 0xC2 }),
        "AB", 10);
}

TEST_CASE("prefix FR-6 - LLLCHAR|ASCII|BINARY roundtrip (3-Byte-BE-Praefix)",
    "[prefix][field]") {
    // 3 Zeichen: 3-Byte-Binary-Präfix 0x00 0x00 0x03 + ASCII-Bytes
    // (BINARY-Präfixbreite = L-Zahl in Bytes: LLL = 3).
    roundtrip<ISOOpaqueFieldParser<codec::Length::LLL, codec::PrefixEncoder::BINARY,
        codec::Encoder::ASCII>>(
        B({ 0x00, 0x00, 0x03, 0x41, 0x42, 0x43 }),
        "ABC", 10);
}

TEST_CASE("prefix FR-6 - LLNUM|ASCII|BCD roundtrip (Ziffern-Praefix zaehlt Ziffern)",
    "[prefix][field][bcd]") {
    // 6 ASCII-Ziffern: BCD-Präfix 0x06 (zaehlt Ziffern, nicht Bytes, da bei
    // ASCII Ziffern=Bytes) + ASCII-Ziffern-Bytes.
    roundtrip<ISOOpaqueFieldParser<codec::Length::LL, codec::PrefixEncoder::BCD,
        codec::Encoder::ASCII>>(
        B({ 0x06, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 }),
        "123456", 10);
}

TEST_CASE("prefix FR-6 - LLNUM|EBCDIC|BINARY roundtrip (2-Byte-BE-Praefix, EBCDIC-Ziffern)",
    "[prefix][field]") {
    // 4 EBCDIC-Ziffern: 2-Byte-Binary-Präfix 0x00 0x04 + EBCDIC-Ziffern.
    roundtrip<ISOOpaqueFieldParser<codec::Length::LL, codec::PrefixEncoder::BINARY,
        codec::Encoder::EBCDIC>>(
        B({ 0x00, 0x04, 0xF1, 0xF2, 0xF3, 0xF4 }),
        "1234", 10);
}

// =============================================================================
// B) Fehlerpfade (strict, Default)
// =============================================================================

TEST_CASE("prefix FR-6 - truncated payload throws in strict mode",
    "[prefix][field][error]") {
    // Präfix komplett (0x05 = 5), aber nur 2 Nutzdaten-Bytes vorhanden.
    ISOOpaqueFieldParser<codec::Length::L, codec::PrefixEncoder::BCD,
        codec::Encoder::ASCII> parser(20, "TruncTest");
    auto field = std::make_shared<OpaqueField>(2);
    CHECK_THROWS_WITH(parser.unparse(field, B({ 0x05, 0x48, 0x45 }), 0),
        ContainsSubstring("Feld am Pufferende"));
}

TEST_CASE("prefix FR-6 - truncated 2-byte BINARY prefix throws in strict mode",
    "[prefix][field][error]") {
    // 2-Byte-Binary-Präfix am Pufferende abgeschnitten (nur 1 Byte).
    ISOOpaqueFieldParser<codec::Length::LLL, codec::PrefixEncoder::BINARY,
        codec::Encoder::ASCII> parser(20, "TruncTest");
    auto field = std::make_shared<OpaqueField>(2);
    CHECK_THROWS_WITH(parser.unparse(field, B({ 0x00 }), 0),
        ContainsSubstring("Pufferende"));
}

// =============================================================================
// C1) Voll-Spec mit gemischten Feldern (VISA BASE-I-artig) + handgebastelter
//     Frame + Introspektion
// =============================================================================

TEST_CASE("prefix FR-6 - full spec: mixed prefix/payload encodings roundtrip",
    "[prefix][spec][roundtrip]") {
    TempYaml y(R"YAML(
spec: "FR-6 prefix test"
encoding: ebcdic
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llnum,   encoding: bcd,    prefix_encoding: bcd,    length: 19,  description: "Pan" }
  "035": { format: llchar,  encoding: ebcdic, prefix_encoding: bcd,    length: 37,  description: "Track2" }
  "048": { format: lllchar, encoding: ascii,  prefix_encoding: binary, length: 255, description: "AddData" }
)YAML");
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());

    // ── Introspektion: effektives Präfix-Encoding (D5) ──────────────
    REQUIRE(spec->field(2).has_value());
    CHECK(spec->field(2)->prefix_encoding == "BCD");      // deklariert
    CHECK(spec->field(2)->encoding == "BCD");
    CHECK(spec->field(2)->format.type == "NUM");          // Luecke 1: NUM, nicht CHAR
    REQUIRE(spec->field(35).has_value());
    CHECK(spec->field(35)->prefix_encoding == "BCD");     // deklariert, != encoding
    CHECK(spec->field(35)->encoding == "EBCDIC");
    REQUIRE(spec->field(48).has_value());
    CHECK(spec->field(48)->prefix_encoding == "BINARY");  // deklariert
    CHECK(spec->field(48)->encoding == "ASCII");
    // Default ohne Key (DE000): praefix_encoding == encoding.
    REQUIRE(spec->field(0).has_value());
    CHECK(spec->field(0)->prefix_encoding == "EBCDIC");

    // ── Handgebastelter Frame (VISA-BASE-I-artiges Wire-Bild) ────────
    // MTI "0200" (EBCDIC) | Bitmap (Bits 2, 35, 48) |
    // DE002: BCD-Präfix 0x16 (16 Ziffern) + 8 BCD-Bytes |
    // DE035: BCD-Präfix 0x35 (BCD-Ziffern '3' '5' = 35 Zeichen)
    //            + 35 EBCDIC-Bytes (0xC1) |
    // DE048: 3-Byte-BE-Binary-Präfix 0x00 0x00 0x0B (11) + 11 ASCII-Bytes.
    // MTI "0200" in EBCDIC: '0'=0xF0, '2'=0xF2.
    std::vector<uint8_t> frame = { 0xF0, 0xF2, 0xF0, 0xF0 };
    // Bits (MSB-first, DE d = Bit-Index d-1, Maske 0x80 >> ((d-1) % 8)):
    // DE2 = Byte1 (0x40) | DE35 = Byte5 (0x20) | DE48 = Byte6 (0x01).
    frame.insert(frame.end(), { 0x40, 0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00 });
    frame.insert(frame.end(), { 0x16, 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 });
    frame.push_back(0x35);
    frame.insert(frame.end(), 35, 0xC1);
    frame.insert(frame.end(), { 0x00, 0x00, 0x0B });
    frame.insert(frame.end(), { 0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20,
                                 0x57, 0x4F, 0x52, 0x4C, 0x44 });

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto consumed = msg->unparse(msg, frame);
    REQUIRE(consumed == frame.size());

    CHECK(msg->mti() == "0200");
    REQUIRE(msg->tryGetValue<OpaqueField>(2).has_value());
    CHECK(msg->tryGetValue<OpaqueField>(2) == "4111111111111111");
    CHECK(msg->tryGetValue<OpaqueField>(35) == std::string(35, 'A'));
    CHECK(msg->tryGetValue<OpaqueField>(48) == "HELLO WORLD");

    // Re-Encode: byte-identisch zum handgebauten Frame.
    CHECK(parser->parse(msg) == frame);

    // Wire-Abgleich der einzelnen Felder (Offsets aus dem Decode):
    const auto de2 = msg->get<OpaqueField>(2);
    REQUIRE(de2 != nullptr);
    const std::vector<uint8_t> de2wire(
        frame.begin() + de2->wire_offset(),
        frame.begin() + de2->wire_offset() + de2->wire_length());
    CHECK(de2wire == B({ 0x16, 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 }));
    const auto de48 = msg->get<OpaqueField>(48);
    REQUIRE(de48 != nullptr);
    const std::vector<uint8_t> de48wire(
        frame.begin() + de48->wire_offset(),
        frame.begin() + de48->wire_offset() + de48->wire_length());
    CHECK(de48wire == B({ 0x00, 0x00, 0x0B, 0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20,
                          0x57, 0x4F, 0x52, 0x4C, 0x44 }));
}

// =============================================================================
// C2) Explizite Identität (2-teiler Key): llnum + bcd + prefix_encoding: bcd
// =============================================================================

TEST_CASE("prefix FR-6 - explicit identity (llnum|bcd + prefix bcd) loads and roundtrips",
    "[prefix][spec][roundtrip]") {
    TempYaml y(specYaml(
        "{ format: llnum, encoding: bcd, prefix_encoding: bcd, length: 19 }"));
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());

    REQUIRE(spec->field(2).has_value());
    CHECK(spec->field(2)->prefix_encoding == "BCD");

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(2), std::string("4111111111111111")));
    const auto wire = msg->parse(msg);

    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(2) == "4111111111111111");
    CHECK(parser->parse(msg2) == wire);
}

// =============================================================================
// C3) Default: ohne prefix_encoding gilt prefix_encoding == encoding
// =============================================================================

TEST_CASE("prefix FR-6 - default: prefix_encoding equals encoding (and empty for neutral)",
    "[prefix][spec]") {
    TempYaml y("spec: \"FR-6 default\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n"
        "  \"002\": { format: llnum, encoding: bcd, length: 19 }\n"
        "  \"035\": { format: llchar, length: 37 }\n"
        "  \"055\": { format: binary, length: 8 }\n");
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());

    REQUIRE(spec->field(2).has_value());
    CHECK(spec->field(2)->encoding == "BCD");
    CHECK(spec->field(2)->prefix_encoding == "BCD");   // Default = encoding
    REQUIRE(spec->field(35).has_value());
    CHECK(spec->field(35)->encoding == "EBCDIC");      // vererbt (global)
    CHECK(spec->field(35)->prefix_encoding == "EBCDIC");
    REQUIRE(spec->field(55).has_value());
    CHECK(spec->field(55)->encoding == "");            // neutral
    CHECK(spec->field(55)->prefix_encoding == "");     // und daher auch hier
}

// =============================================================================
// C4) Fail-closed (D6): positionierte SpecValidationError
// =============================================================================

TEST_CASE("prefix FR-6 - rejected on fixed numeric", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: numeric, length: 4, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=NUMERIC"));
}

TEST_CASE("prefix FR-6 - rejected on fixed char", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: char, length: 10, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=CHAR"));
}

TEST_CASE("prefix FR-6 - rejected on amount", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: amount, length: 16, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=AMOUNT"));
}

TEST_CASE("prefix FR-6 - rejected on remaining", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: remaining, length: 8, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=REMAINING"));
}

TEST_CASE("prefix FR-6 - rejected on bitmap", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: bitmap, length: 8, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format=BITMAP"));
}

TEST_CASE("prefix FR-6 - rejected on nop", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: nop, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("format: nop"));
}

TEST_CASE("prefix FR-6 - rejected on variable binary (lllbinary)", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: lllbinary, length: 8, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("bestimmt bei BINARY"));
}

TEST_CASE("prefix FR-6 - rejected on bertlv shorthand (lllbertlv)", "[prefix][spec][error]") {
    TempYaml y(specYaml("{ format: lllbertlv, length: 999, prefix_encoding: bcd }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("bestimmt bei BINARY"));
}

TEST_CASE("prefix FR-6 - rejected with invalid value (hex)", "[prefix][spec][error]") {
    TempYaml y(specYaml(
        "{ format: llnum, encoding: bcd, prefix_encoding: hex, length: 19 }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("erlaubt: ascii, ebcdic, bcd, binary"));
}

TEST_CASE("prefix FR-6 - rejected for unavailable combination (llchar|ascii + ebcdic)",
    "[prefix][spec][error]") {
    // D4-Scope: ASCII-/EBCDIC-Präfix × fremde Text-Nutzdaten werden
    // bewusst nicht ausgeliefert → Fail-closed über die Tabellennachfrage.
    TempYaml y(specYaml(
        "{ format: llchar, encoding: ascii, prefix_encoding: ebcdic, length: 10 }"));
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("prefix_encoding=EBCDIC"));
}

TEST_CASE("prefix FR-6 - rejected on TLV child (tlv block, decimal SE keys)",
    "[prefix][spec][error]") {
    TempYaml y(R"YAML(
spec: "FR-6 tlv"
encoding: ebcdic
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "048":
    format: lllchar
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "26": { format: char, length: 10, prefix_encoding: bcd }
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("TLV-Kindern"));
}

TEST_CASE("prefix FR-6 - rejected on BERTLV child", "[prefix][spec][error]") {
    TempYaml y(R"YAML(
spec: "FR-6 bertlv"
encoding: ebcdic
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "055":
    format: lllbertlv
    length: 999
    children:
      "9F26": { format: binary, prefix_encoding: bcd }
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("TLV-Kindern"));
}

TEST_CASE("prefix FR-6 - rejected on constructed (container) TLV child",
    "[prefix][spec][error]") {
    TempYaml y(R"YAML(
spec: "FR-6 container child"
encoding: ebcdic
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "057":
    format: lllbinary
    length: 999
    tlv: { ber: true }
    children:
      "69":
        tlv: { ber: true }
        prefix_encoding: bcd
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("nicht deklarieren"));
}

TEST_CASE("prefix FR-6 - rejected at root level (message spec)",
    "[prefix][spec][error]") {
    // Root-Level-'prefix_encoding' ist ein Stille-Auswahl-Falle (es gibt
    // keinen Root-Default, D1) -> Fail-closed statt stiller Ignorierung.
    TempYaml y("spec: \"FR-6 root\"\nencoding: ebcdic\nprefix_encoding: bcd\n"
        "fields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap,  length: 8 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()),
        ContainsSubstring("Root-Key"));
}

TEST_CASE("prefix FR-6 - rejected at root level (field-only document)",
    "[prefix][field][error]") {
    TempYaml y(R"YAML(
spec: "FR-6 root field"
encoding: ascii
prefix_encoding: bcd
field:
  format: llchar
  length: 20
)YAML");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFieldFromYaml(y.str()),
        ContainsSubstring("Root-Key"));
}

// =============================================================================
// C5) Field-only-Dokument (FE-1-Pfad) mit prefix_encoding
// =============================================================================

TEST_CASE("prefix FR-6 - field-only spec loads and decodes",
    "[prefix][fe1][field][roundtrip]") {
    TempYaml y(R"YAML(
spec: "FR-6 field-only"
encoding: ascii
field:
  format: llchar
  encoding: ascii
  prefix_encoding: bcd
  length: 20
)YAML");
    auto parser = spec::SpecDecoder::loadFieldFromYaml(y.str());

    // Payload: BCD-Präfix 0x05 (5 Zeichen) + ASCII "HELLO".
    const std::vector<uint8_t> payload = { 0x05, 0x48, 0x45, 0x4C, 0x4C, 0x4F };
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);

    CHECK(msg->tryGetValue<OpaqueField>(0) == "HELLO");
    CHECK(parser->parse(msg) == payload);
}

// =============================================================================
// C6) !merge-Komposition (Template + prefix_encoding aus der Merge-Map)
// =============================================================================

TEST_CASE("prefix FR-6 - !merge with template composes prefix_encoding",
    "[prefix][spec][roundtrip]") {
    TempYaml y(R"YAML(
spec: "FR-6 merge"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "035":
    !merge
    - !template LL(CHAR, 37)
    - { encoding: ebcdic, prefix_encoding: bcd, description: "Track2" }
)YAML");
    auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());

    REQUIRE(spec->field(35).has_value());
    CHECK(spec->field(35)->format.prefix_digits == 2);
    CHECK(spec->field(35)->encoding == "EBCDIC");
    CHECK(spec->field(35)->prefix_encoding == "BCD");

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(35), std::string(37, 'A')));
    const auto wire = msg->parse(msg);

    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) == wire.size());
    CHECK(msg2->tryGetValue<OpaqueField>(35) == std::string(37, 'A'));
    CHECK(parser->parse(msg2) == wire);
}
