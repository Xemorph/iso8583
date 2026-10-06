// =============================================================================
// test_nibble_pack.cc - FR-13 (0.9.0): Nibble-Unterfelder in nested-Containern
// =============================================================================
//
// 'pack: nibble' an einem nested-Container: aufeinanderfolgende BCD-Kinder
// (numeric, encoding bcd, feste Ziffernzahl; 'nop' = Schluessel-Platzhalter)
// bilden einen dichten Ziffern-Strom (1 Ziffer = 1 Nibble). Ausgangspunkt:
// VISA DE60 (Additional POS Information), Beleg-Nachricht: L=05 +
// 00 00 00 00 07 (5 Datenbytes, 60.8 = 07, Byte 7 fehlt).
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
            / ("libiso8583_test_nib_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
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

std::string kid(int digits, const std::string& desc) {
    return "      - { format: numeric, encoding: bcd, length: " + std::to_string(digits) +
           ", description: \"" + desc + "\" }\n";
}

// DE60 mit den zehn VISA-Unterfeldern (60.5 = Filler, 2 Ziffern; 60.8 = 2 Ziffern).
// Das erste Kind ist ein 'nop'-Platzhalter, damit die Schluessel 60.1..60.10 passen.
const std::string kDe60Children =
    "      - { format: nop }\n" +
    kid(1, "60.1 Terminal Type") + kid(1, "60.2 Terminal Entry Capability") +
    kid(1, "60.3 Chip Condition Code") + kid(1, "60.4 Special Condition Indicator") +
    kid(2, "60.5 Filler") +
    kid(1, "60.6 Chip Transaction Indicator") + kid(1, "60.7 CAVV Reliability Indicator") +
    kid(2, "60.8 MOTO/ECI Indicator") +
    kid(1, "60.9 Cardholder ID Method") + kid(1, "60.10 Additional Authorization Indicators");

// BCD-MTI + Bitmap (nur DE60) + DE60 als pack-Container.
std::string specYaml(const std::string& children, const std::string& containerExtra = "",
                     const std::string& rootExtra = "") {
    return "spec: \"FR-13 nibble pack\"\n"
           "encoding: ebcdic\n" + rootExtra +
           "fields:\n"
           "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
           "  \"001\": { format: bitmap, length: 8 }\n"
           "  \"060\":\n"
           "    type: nested\n"
           "    format: lbinary\n"
           "    encoding: binary\n"
           "    length: 255\n"
           "    description: \"Additional POS Information\"\n"
           "    pack: nibble\n" + containerExtra +
           "    children:\n" + children;
}

// MTI 0200 + Bitmap (DE60 = Bit 60: Byte 7, 0x10) + DE60 (L + Nutzdaten)
Bytes wire(const Bytes& payload) {
    return cat({ Bytes{ 0x02, 0x00 }, Bytes{ 0, 0, 0, 0, 0, 0, 0, 0x10 },
                 Bytes{ static_cast<uint8_t>(payload.size()) }, payload });
}

std::shared_ptr<ISOParserPtrBase> load(const TempYaml& y) {
    return spec::SpecDecoder::loadFromYaml(y.str());
}

std::string val(const std::shared_ptr<Message>& msg, int sub) {
    auto de = msg->get<Message>(60);
    REQUIRE(de);
    auto f = de->get<OpaqueField>(static_cast<TNG_KEY_TYPE>(sub));
    REQUIRE(f);
    return f->value();
}

bool has(const std::shared_ptr<Message>& msg, int sub) {
    auto de = msg->get<Message>(60);
    return de && de->has(static_cast<TNG_KEY_TYPE>(sub));
}

void setv(const std::shared_ptr<Message>& m, int sub, const std::string& v) {
    REQUIRE(m->set("60." + std::to_string(sub), v));
}

// Kleiner Container mit n einstelligen Kindern (+ nop-Platzhalter) fuer Padding-Tests.
std::string small(int n) {
    std::string c = "      - { format: nop }\n";
    for (int i = 1; i <= n; ++i) c += kid(1, "d" + std::to_string(i));
    return c;
}

} // namespace

TEST_CASE("nibble pack FR-13 - real DE60 (5 data bytes, byte 7 missing) decodes into single digits and rebuilds",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(kDe60Children));
    auto parser = load(y);

    const Bytes w = wire(Bytes{ 0x00, 0x00, 0x00, 0x00, 0x07 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    for (int i : { 1, 2, 3, 4, 6, 7 }) CHECK(val(msg, i) == "0");
    CHECK(val(msg, 5) == "00");
    CHECK(val(msg, 8) == "07");
    CHECK_FALSE(has(msg, 9));     // Byte 7 fehlt -> 60.9/60.10 ungesetzt
    CHECK_FALSE(has(msg, 10));

    CHECK(msg->parse(msg) == w);

    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    for (int i : { 1, 2, 3, 4, 6, 7 }) setv(built, i, "0");
    setv(built, 5, "00");
    setv(built, 8, "07");
    CHECK(built->parse(built) == w);
}

TEST_CASE("nibble pack FR-13 - full container, every digit lands in its own sub-field",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(kDe60Children));
    auto parser = load(y);

    // 12 34 05 67 08 90  ->  1,2,3,4,"05",6,7,"08",9,0
    const Bytes w = wire(Bytes{ 0x12, 0x34, 0x05, 0x67, 0x08, 0x90 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    CHECK(val(msg, 1) == "1");
    CHECK(val(msg, 2) == "2");
    CHECK(val(msg, 3) == "3");
    CHECK(val(msg, 4) == "4");
    CHECK(val(msg, 5) == "05");
    CHECK(val(msg, 6) == "6");
    CHECK(val(msg, 7) == "7");
    CHECK(val(msg, 8) == "08");
    CHECK(val(msg, 9) == "9");
    CHECK(val(msg, 10) == "0");
    CHECK(msg->parse(msg) == w);

    // Einzeln setzbar (Kernanforderung des Konsumenten)
    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    setv(built, 1, "1"); setv(built, 2, "2"); setv(built, 3, "3"); setv(built, 4, "4");
    setv(built, 5, "05"); setv(built, 6, "6"); setv(built, 7, "7"); setv(built, 8, "08");
    setv(built, 9, "9"); setv(built, 10, "0");
    CHECK(built->parse(built) == w);
}

TEST_CASE("nibble pack FR-13 - wire offsets: start byte and touched bytes (E4)",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(kDe60Children));
    auto parser = load(y);
    const Bytes w = wire(Bytes{ 0x12, 0x34, 0x05, 0x67, 0x08, 0x90 });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    auto de = msg->get<Message>(60);
    // Container-Nutzdaten beginnen bei Offset 2 (MTI) + 8 (Bitmap) + 1 (L) = 11
    CHECK(de->get<OpaqueField>(1)->wire_offset() == 11);
    CHECK(de->get<OpaqueField>(1)->wire_length() == 1);
    CHECK(de->get<OpaqueField>(2)->wire_offset() == 11);   // selbes Byte
    CHECK(de->get<OpaqueField>(3)->wire_offset() == 12);
    CHECK(de->get<OpaqueField>(5)->wire_offset() == 13);
    CHECK(de->get<OpaqueField>(5)->wire_length() == 1);
    CHECK(de->get<OpaqueField>(8)->wire_offset() == 15);
}

TEST_CASE("nibble pack FR-13 - odd total digit count: padding follows bcd_pad",
    "[nibble-pack][fr13]") {
    struct Case { const char* pad; Bytes expect; };
    const std::vector<Case> cases = {
        { "right_zero", { 0x12, 0x30 } },
        { "right_f",    { 0x12, 0x3F } },
        { "left_zero",  { 0x01, 0x23 } },
    };
    for (const auto& c : cases) {
        INFO("bcd_pad = " << c.pad);
        const TempYaml y(specYaml(small(3), std::string("    bcd_pad: ") + c.pad + "\n"));
        auto parser = load(y);
        const Bytes w = wire(c.expect);

        auto built = std::make_shared<Message>("0200");
        built->parser(parser);
        setv(built, 1, "1"); setv(built, 2, "2"); setv(built, 3, "3");
        CHECK(built->parse(built) == w);

        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        REQUIRE(msg->unparse(msg, w) == w.size());
        CHECK(val(msg, 1) == "1");
        CHECK(val(msg, 2) == "2");
        CHECK(val(msg, 3) == "3");
    }

    // Padding-Nibble weicht von der deklarierten Variante ab (right_zero erwartet 0)
    const TempYaml y(specYaml(small(3), "    bcd_pad: right_zero\n"));
    auto parser = load(y);
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    CHECK_THROWS_WITH(msg->unparse(msg, wire(Bytes{ 0x12, 0x3F })), ContainsSubstring("Padding"));
}

TEST_CASE("nibble pack FR-13 - root bcd_pad default applies to the container",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(small(3), "", "bcd_pad: left_zero\n"));
    auto [parser, sp] = spec::SpecDecoder::loadBothFromYaml(y.str());
    CHECK(sp->field(60)->bcd_pad == "left_zero");
    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    setv(built, 1, "1"); setv(built, 2, "2"); setv(built, 3, "3");
    CHECK(built->parse(built) == wire(Bytes{ 0x01, 0x23 }));
}

TEST_CASE("nibble pack FR-13 - gaps, bad values and ambiguous short containers fail closed",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(small(4)));
    auto parser = load(y);

    const auto build = [&](std::initializer_list<std::pair<int, std::string>> kv) {
        auto m = std::make_shared<Message>("0200");
        m->parser(parser);
        for (const auto& [k, v] : kv) setv(m, k, v);
        return m;
    };

    // Luecke: 60.2 fehlt, 60.3 gesetzt
    {
        auto m = build({ { 1, "1" }, { 3, "3" } });
        CHECK_THROWS_WITH(m->parse(m), ContainsSubstring("Luecke"));
    }
    // falsche Ziffernzahl / Nicht-Ziffern
    {
        auto m = build({ { 1, "12" } });
        CHECK_THROWS_WITH(m->parse(m), ContainsSubstring("erwartet 1"));
        auto m2 = build({ { 1, "A" } });
        CHECK_THROWS_WITH(m2->parse(m2), ContainsSubstring("Nicht-Ziffern"));
    }
    // verkuerzter Container mit ungerader Ziffernzahl (3 von 4) ist beim Decode mehrdeutig
    {
        auto m = build({ { 1, "1" }, { 2, "2" }, { 3, "3" } });
        CHECK_THROWS_WITH(m->parse(m), ContainsSubstring("mitten im Byte"));
    }
    // verkuerzt mit gerader Ziffernzahl (2 von 4) ist erlaubt
    {
        auto m = build({ { 1, "1" }, { 2, "2" } });
        CHECK(m->parse(m) == wire(Bytes{ 0x12 }));
    }
    // nicht deklariertes Kind
    {
        auto m = build({ { 1, "1" } });
        REQUIRE(m->set("60.9", std::string("9")));
        CHECK_THROWS_WITH(m->parse(m), ContainsSubstring("nicht deklariert"));
    }
}

TEST_CASE("nibble pack FR-13 - container cut in the middle of a child / trailing bytes fail closed",
    "[nibble-pack][fr13]") {
    // Kinder: 1 + 2 + 1 Ziffern (Summe 4 = 2 Byte)
    const std::string kids = "      - { format: nop }\n" + kid(1, "a") + kid(2, "b") + kid(1, "c");
    const TempYaml y(specYaml(kids));
    auto parser = load(y);

    {   // 1 Byte = 2 Ziffern: a komplett, b nur 1 von 2 Ziffern vorhanden
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, wire(Bytes{ 0x12 })), ContainsSubstring("abgeschnitten"));
    }
    {   // 3 Byte, Container deckt nur 2 ab
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, wire(Bytes{ 0x12, 0x34, 0x56 })),
            ContainsSubstring("Unverbrauchte Bytes"));
    }
    {   // nicht-strikt: Dekodierung des vollstaendigen Anteils, keine Exception
        parser->strict(false);
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_NOTHROW((void)msg->unparse(msg, wire(Bytes{ 0x12 })));
    }
}

TEST_CASE("nibble pack FR-13 - introspection and default behaviour unchanged without pack",
    "[nibble-pack][fr13]") {
    const TempYaml y(specYaml(kDe60Children));
    auto [parser, sp] = spec::SpecDecoder::loadBothFromYaml(y.str());
    (void)parser;
    auto f = sp->field(60);
    REQUIRE(f);
    CHECK(f->pack == "nibble");
    CHECK(f->container_bitmap_bytes == 0);
    CHECK(f->bcd_pad == "right_zero");
    CHECK(f->children.size() == 11);   // nop-Platzhalter + 10 Unterfelder

    // Ohne 'pack' belegt jedes Kind ganze Bytes (Messung aus dem Vault): 1 Ziffer = 1 Byte
    const TempYaml y2(
        "spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n"
        "  \"060\":\n"
        "    type: nested\n    format: lbinary\n    encoding: binary\n    length: 255\n"
        "    children:\n" + kid(1, "a") + kid(1, "b"));
    auto parser2 = load(y2);
    auto built = std::make_shared<Message>("0200");
    built->parser(parser2);
    REQUIRE(built->set("60.0", std::string("1")));
    REQUIRE(built->set("60.1", std::string("2")));
    CHECK(built->parse(built) == wire(Bytes{ 0x10, 0x20 }));
}

TEST_CASE("nibble pack FR-13 - loader fails closed on contradictory declarations",
    "[nibble-pack][fr13][loader]") {
    const auto expectFail = [](const std::string& yaml, const std::string& needle) {
        const TempYaml y(yaml);
        try {
            (void)spec::SpecDecoder::loadFromYaml(y.str());
            FAIL("expected SpecValidationError for: " << needle);
        } catch (const std::exception& e) {
            CHECK_THAT(std::string(e.what()), ContainsSubstring(needle));
        }
    };

    // Kind ist kein BCD-numeric
    expectFail(specYaml("      - { format: char, encoding: ebcdic, length: 1 }\n"), "nur 'format: numeric'");
    expectFail(specYaml("      - { format: numeric, encoding: ascii, length: 1 }\n"), "nur 'format: numeric'");
    expectFail(specYaml("      - { format: numeric, encoding: bcd }\n"), "nur 'format: numeric'");
    expectFail(specYaml("      - { format: llnum, encoding: bcd, length: 5 }\n"), "nur 'format: numeric'");
    // bcd_pad am Kind
    expectFail(specYaml("      - { format: numeric, encoding: bcd, length: 1, bcd_pad: right_f }\n"),
               "nibble'-Container (dichter");
    // unbekannter pack-Wert
    {
        std::string s = specYaml(small(2));
        const auto p = s.find("pack: nibble");
        s.replace(p, 12, "pack: byte");
        expectFail(s, "pack='byte'");
    }
    // pack + bitmap, pack + tlv, pack mit Map-children
    expectFail(specYaml(small(2), "    bitmap: { length: 8 }\n"), "gegenseitig aus");
    expectFail(specYaml(small(2), "    tlv: { tag_bytes: 1, len_bytes: 1 }\n"), "TLV");
    expectFail(specYaml("      \"1\": { format: numeric, encoding: bcd, length: 1 }\n"), "Liste");
    // pack auf Skalar
    expectFail(
        "spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n"
        "  \"003\": { format: numeric, encoding: bcd, length: 6, pack: nibble }\n",
        "nested");
    // bcd_pad auf einem normalen nested-Container bleibt verboten (nur mit pack)
    expectFail(
        "spec: \"x\"\nencoding: ebcdic\nfields:\n"
        "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n"
        "  \"060\":\n"
        "    type: nested\n    format: lbinary\n    encoding: binary\n    length: 9\n"
        "    bcd_pad: left_zero\n"
        "    children:\n" + kid(1, "a"),
        "bcd_pad");
}
