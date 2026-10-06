// =============================================================================
// test_bitmap_container.cc - FR-12 (0.9.0): bitmap-gesteuerte nested-Container
//   (deckt FR-11 mit ab: Bit 1 ist im Container ein NORMALES Kind)
// =============================================================================
//
// Spec-Form:  type: nested + 'bitmap: { length: N }' + 'children' als Map
// (Bit-Nummer -> Kind). Ausgangspunkt: VISA DE62 (CPS-Felder), Beleg-Nachricht
// des Konsumenten: Bitmap 42 00.. (Bits 2 und 7) + 62.2 (8 Byte BCD) + 62.7
// (26 Byte EBCDIC) = 42 Byte.
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
            / ("libiso8583_test_bmpc_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

using Bytes = std::vector<uint8_t>;

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// EBCDIC (IBM-1047) fuer A..Z
Bytes ebcdicAZ() {
    Bytes b;
    for (uint8_t c = 0xC1; c <= 0xC9; ++c) b.push_back(c); // A-I
    for (uint8_t c = 0xD1; c <= 0xD9; ++c) b.push_back(c); // J-R
    for (uint8_t c = 0xE2; c <= 0xE9; ++c) b.push_back(c); // S-Z
    return b;
}

const std::string kAZ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const Bytes kTid = { 0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45 }; // 62.2 = 0123456789012345

// BCD-MTI (2 Byte) + Bitmap (8 Byte, DE62 gesetzt) + DE62 als Bitmap-Container.
std::string specYaml(const std::string& de62Body) {
    return "spec: \"FR-12 bitmap container\"\n"
           "encoding: ebcdic\n"
           "fields:\n"
           "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
           "  \"001\": { format: bitmap, length: 8 }\n"
           "  \"062\":\n"
           "    type: nested\n"
           "    format: lbinary\n"
           "    encoding: binary\n"
           "    length: 255\n"
           "    description: \"CPS\"\n" + de62Body;
}

const std::string kDe62Ok =
    "    bitmap: { length: 8 }\n"
    "    children:\n"
    "      \"1\": { format: char, encoding: ebcdic, length: 1, description: \"62.1 ACI\" }\n"
    "      \"2\": { format: numeric, encoding: bcd, length: 16, description: \"62.2 TID\" }\n"
    "      \"7\": { format: char, encoding: ebcdic, length: 26, description: \"62.7 PID\" }\n";

// MTI 0200 + Bitmap (nur DE62) + DE62 (L + Nutzdaten)
Bytes wire(const Bytes& de62Payload) {
    return cat({ Bytes{ 0x02, 0x00 },
                 Bytes{ 0, 0, 0, 0, 0, 0, 0, 0x04 },
                 Bytes{ static_cast<uint8_t>(de62Payload.size()) },
                 de62Payload });
}

std::shared_ptr<ISOParserPtrBase> load(const TempYaml& y) {
    return spec::SpecDecoder::loadFromYaml(y.str());
}

std::string val(const std::shared_ptr<Message>& msg, int sub) {
    auto de62 = msg->get<Message>(62);
    REQUIRE(de62);
    auto f = de62->get<OpaqueField>(static_cast<TNG_KEY_TYPE>(sub));
    REQUIRE(f);
    return f->value();
}

} // namespace

TEST_CASE("bitmap container FR-12 - real DE62 payload (bits 2 and 7) decodes and rebuilds byte-exact",
    "[bitmap-container][fr12]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto parser = load(y);

    const Bytes payload = cat({ Bytes{ 0x42, 0, 0, 0, 0, 0, 0, 0 }, kTid, ebcdicAZ() });
    REQUIRE(payload.size() == 42);
    const Bytes w = wire(payload);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    CHECK(val(msg, 2) == "0123456789012345");
    CHECK(val(msg, 7) == kAZ);
    CHECK_FALSE(msg->get<Message>(62)->has(1));       // Bit 1 nicht gesetzt

    // Dot-Notation + Re-Encode (rekursiver Pfad, Kind-Nachricht traegt den Sub-Parser)
    CHECK(msg->parse(msg) == w);

    // Bauen aus Punkt-Notation: Bitmap wird aus den gesetzten Kindern berechnet
    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    REQUIRE(built->set("62.2", std::string("0123456789012345")));
    REQUIRE(built->set("62.7", kAZ));
    CHECK(built->parse(built) == w);
}

TEST_CASE("bitmap container FR-12/FR-11 - bit 1 is a normal child (not a secondary bitmap)",
    "[bitmap-container][fr12][fr11]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto parser = load(y);

    // Bitmap C0 00.. (Bits 1 und 2) + 62.1 'Y' (EBCDIC E8) + 62.2
    const Bytes payload = cat({ Bytes{ 0xC0, 0, 0, 0, 0, 0, 0, 0 }, Bytes{ 0xE8 }, kTid });
    const Bytes w = wire(payload);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    CHECK(val(msg, 1) == "Y");
    CHECK(val(msg, 2) == "0123456789012345");
    CHECK(msg->parse(msg) == w);

    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    REQUIRE(built->set("62.1", std::string("Y")));
    REQUIRE(built->set("62.2", std::string("0123456789012345")));
    CHECK(built->parse(built) == w);
}

TEST_CASE("bitmap container FR-12 - set bit without child definition fails closed (strict) / stops (non-strict)",
    "[bitmap-container][fr12]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto parser = load(y);

    // Bit 3 gesetzt (0x20), keine Definition fuer 62.3
    const Bytes w = wire(cat({ Bytes{ 0x20, 0, 0, 0, 0, 0, 0, 0 }, Bytes{ 0x01, 0x02 } }));

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    try {
        (void)msg->unparse(msg, w);
        FAIL("expected exception");
    } catch (const std::runtime_error& e) {
        CHECK_THAT(std::string(e.what()), ContainsSubstring("Bit 3"));
        CHECK_THAT(std::string(e.what()), ContainsSubstring("kein Kind"));
    }

    parser->strict(false);
    auto lax = std::make_shared<Message>();
    lax->parser(parser);
    CHECK_NOTHROW((void)lax->unparse(lax, w));
}

TEST_CASE("bitmap container FR-12 - truncated child and trailing bytes are positioned errors",
    "[bitmap-container][fr12]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto parser = load(y);

    // Bit 2 gesetzt, aber nur 3 statt 8 Datenbytes
    {
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        const Bytes w = wire(cat({ Bytes{ 0x40, 0, 0, 0, 0, 0, 0, 0 }, Bytes{ 1, 2, 3 } }));
        CHECK_THROWS_WITH(msg->unparse(msg, w), ContainsSubstring("abgeschnitten"));
    }
    // Bitmap leer, aber Rest-Bytes
    {
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        const Bytes w = wire(cat({ Bytes(8, 0), Bytes{ 0xAA } }));
        CHECK_THROWS_WITH(msg->unparse(msg, w), ContainsSubstring("Unverbrauchte Bytes"));
    }
}

TEST_CASE("bitmap container FR-12 - building an undeclared child is rejected in strict mode",
    "[bitmap-container][fr12]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto parser = load(y);

    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    REQUIRE(built->set("62.2", std::string("0123456789012345")));
    REQUIRE(built->set("62.3", std::string("XX")));      // kein Kind fuer Bit 3
    CHECK_THROWS_WITH(built->parse(built), ContainsSubstring("nicht deklariert"));
}

TEST_CASE("bitmap container FR-12 - introspection reports bitmap size and bit numbers as child keys",
    "[bitmap-container][fr12]") {
    const TempYaml y(specYaml(kDe62Ok));
    auto [parser, sp] = spec::SpecDecoder::loadBothFromYaml(y.str());
    (void)parser;
    auto f = sp->field(62);
    REQUIRE(f);
    CHECK(f->is_nested);
    CHECK(f->container_bitmap_bytes == 8);
    CHECK(f->pack.empty());
    REQUIRE(f->children.size() == 3);
    CHECK(f->children[0].key == 1);
    CHECK(f->children[1].key == 2);
    CHECK(f->children[2].key == 7);
}

TEST_CASE("bitmap container FR-12 - loader fails closed on contradictory declarations",
    "[bitmap-container][fr12][loader]") {
    const auto expectFail = [](const std::string& body, const std::string& needle) {
        const TempYaml y(specYaml(body));
        try {
            (void)spec::SpecDecoder::loadFromYaml(y.str());
            FAIL("expected SpecValidationError for: " << needle);
        } catch (const std::exception& e) {
            CHECK_THAT(std::string(e.what()), ContainsSubstring(needle));
        }
    };

    // bitmap ohne Map-children (Liste)
    expectFail("    bitmap: { length: 8 }\n"
               "    children:\n"
               "      - { format: char, encoding: ebcdic, length: 1 }\n",
               "nicht-leere Map");
    // Bit ausserhalb der Bitmap
    expectFail("    bitmap: { length: 1 }\n"
               "    children:\n"
               "      \"9\": { format: char, encoding: ebcdic, length: 1 }\n",
               "erlaubt 1..");
    // Bit 0
    expectFail("    bitmap: { length: 8 }\n"
               "    children:\n"
               "      \"0\": { format: char, encoding: ebcdic, length: 1 }\n",
               "erlaubt 1..");
    // doppelte Bit-Nummer (01 / 1)
    expectFail("    bitmap: { length: 8 }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n"
               "      \"01\": { format: char, encoding: ebcdic, length: 1 }\n",
               "doppelte Bit-Nummer");
    // nicht numerischer Schluessel
    expectFail("    bitmap: { length: 8 }\n"
               "    children:\n"
               "      \"x\": { format: char, encoding: ebcdic, length: 1 }\n",
               "Bit-Nummer 'x'");
    // length fehlt / 0 / zu gross / unbekannter Schluessel (secondary)
    expectFail("    bitmap: { }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "'length'");
    expectFail("    bitmap: { length: 0 }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "1..16");
    expectFail("    bitmap: { length: 17 }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "1..16");
    expectFail("    bitmap: { length: 8, secondary: always }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "'secondary'");
    // bitmap/nop als Kind
    expectFail("    bitmap: { length: 8 }\n"
               "    children:\n"
               "      \"1\": { format: nop }\n",
               "nicht sinnvoll");
    // bitmap + tlv
    expectFail("    bitmap: { length: 8 }\n"
               "    tlv: { tag_bytes: 1, len_bytes: 1 }\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "TLV");
    // bitmap + pack
    expectFail("    bitmap: { length: 8 }\n"
               "    pack: nibble\n"
               "    children:\n"
               "      \"1\": { format: char, encoding: ebcdic, length: 1 }\n",
               "gegenseitig aus");
}

TEST_CASE("bitmap container FR-12 - bitmap on a scalar field is rejected",
    "[bitmap-container][fr12][loader]") {
    const TempYaml y(
        "spec: \"x\"\n"
        "encoding: ebcdic\n"
        "fields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n"
        "  \"003\": { format: numeric, encoding: bcd, length: 6, bitmap: { length: 8 } }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(y.str()), ContainsSubstring("nested"));
}
