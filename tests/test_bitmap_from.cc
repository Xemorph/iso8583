// =============================================================================
// test_bitmap_from.cc - Bitmap-Container mit Bitmap aus einem frueheren Feld
//   (`bitmap: { from: "4.1", length: 2 }`, Opt-in `extensions: [bitmap_from]`)
// =============================================================================
//
// Beispielnachricht (APB-Layout, GitHub-Issue #1):
//   0100 | 04 1111 A000 | 0003 414243 | 12345678 1234
//   MTI    DE4 (Kopf, INFO-2)  5.1 ISO-Daten   5.3 zwei feste Felder
// INFO-2 (DE4.1) steuert, welche Kinder von DE5 hinter dem Container folgen.
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
            / ("libiso8583_test_bmpfrom_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

using Bytes = std::vector<uint8_t>;

// Kinder von DE5: Bit 1 ISO-Daten, Bit 2 Marker (ohne Daten), Bit 3 zwei feste
// Felder, Bit 8 Skript-Daten.
const std::string kChildren =
    "      \"1\": { format: llbinary, encoding: binary, length: 255, description: \"ISO\" }\n"
    "      \"2\": { format: binary, length: 0, description: \"Chip\" }\n"
    "      \"3\":\n"
    "        type: nested\n"
    "        format: binary\n"
    "        length: 6\n"
    "        description: \"Fest\"\n"
    "        children:\n"
    "          - { format: numeric, encoding: bcd, length: 8 }\n"
    "          - { format: numeric, encoding: bcd, length: 4 }\n"
    "      \"8\": { format: llbinary, encoding: binary, length: 255, description: \"Skript\" }\n";

std::string indented(const std::string& block) {
    std::string out;
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (i == 0 || block[i - 1] == '\n')
            out += "  ";
        out += block[i];
    }
    return out;
}

std::string msgSpec(const std::string& root, const std::string& bitmapBody) {
    return "spec: \"bitmap from\"\n" + root +
           "encoding: ascii\n"
           "fields:\n"
           "  \"000\": { format: numeric, encoding: bcd, length: 4 }\n"
           "  \"004\":\n"
           "    type: nested\n"
           "    format: lbinary\n"
           "    encoding: binary\n"
           "    length: 255\n"
           "    children:\n"
           "      - { format: binary, length: 2, description: \"Kopf\" }\n"
           "      - { format: binary, length: 2, description: \"INFO-2\" }\n"
           "  \"005\":\n"
           "    type: nested\n"
           "    format: remaining\n"
           "    encoding: binary\n"
           "    length: 255\n" + bitmapBody +
           "    children:\n" + kChildren;
}

const std::string kExt = "extensions: [bitmap_from]\n";
const std::string kFrom = "    bitmap: { from: \"4.1\", length: 2 }\n";

std::shared_ptr<ISOParserPtrBase> load(const std::string& yaml) {
    const TempYaml y(yaml);
    return spec::SpecDecoder::loadFromYaml(y.str());
}

Bytes wireWith(uint16_t info2, const Bytes& tail) {
    Bytes w = { 0x01, 0x00, 0x04, 0x11, 0x11,
                static_cast<uint8_t>(info2 >> 8), static_cast<uint8_t>(info2 & 0xFF) };
    w.insert(w.end(), tail.begin(), tail.end());
    return w;
}

const Bytes kIso = { 0x00, 0x03, 0x41, 0x42, 0x43 };
const Bytes kFixed = { 0x12, 0x34, 0x56, 0x78, 0x12, 0x34 };

bool put(const std::shared_ptr<Message>& m, const char* path, const char* hex) {
    return m->set(std::string(path), std::string(hex));
}

std::shared_ptr<Message> decode(const std::shared_ptr<ISOParserPtrBase>& p, const Bytes& w) {
    auto msg = std::make_shared<Message>();
    msg->parser(p);
    REQUIRE(msg->unparse(msg, w) == w.size());
    return msg;
}

std::string hexOf(const std::shared_ptr<Message>& m, const char* sub, int key) {
    auto c = m->get<Message>(static_cast<TNG_KEY_TYPE>(std::atoi(sub)));
    REQUIRE(c);
    auto f = c->get<BinaryField>(static_cast<TNG_KEY_TYPE>(key));
    REQUIRE(f);
    std::string s;
    for (auto b : f->value()) { char h[3]; std::snprintf(h, sizeof h, "%02X", b); s += h; }
    return s;
}

} // namespace

TEST_CASE("bitmap from - A000 decodes ISO data and fixed fields, no bitmap inside DE5",
    "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    Bytes tail = kIso; tail.insert(tail.end(), kFixed.begin(), kFixed.end());
    auto msg = decode(p, wireWith(0xA000, tail));

    CHECK(hexOf(msg, "4", 0) == "1111");
    CHECK(hexOf(msg, "4", 1) == "A000");
    CHECK(hexOf(msg, "5", 1) == "414243");
    auto de5 = msg->get<Message>(5);
    REQUIRE(de5);
    auto fixed = de5->get<Message>(3);
    REQUIRE(fixed);
    CHECK(fixed->get<OpaqueField>(0)->value() == "12345678");
    CHECK(fixed->get<OpaqueField>(1)->value() == "1234");
    CHECK_FALSE(de5->has(2));
    CHECK_FALSE(de5->tryGet<Bitmap>(Message::BITMAP_KEY).has_value());
}

TEST_CASE("bitmap from - single bits 2000 and 0100 decode", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    auto m3 = decode(p, wireWith(0x2000, kFixed));
    CHECK(m3->get<Message>(5)->get<Message>(3)->get<OpaqueField>(1)->value() == "1234");
    CHECK_FALSE(m3->get<Message>(5)->has(1));

    auto m8 = decode(p, wireWith(0x0100, kIso));
    CHECK(hexOf(m8, "5", 8) == "414243");

    // reiner Marker (Bit 2) und leere INFO-2: nichts hinter dem Container
    auto m2 = decode(p, wireWith(0x4000, {}));
    CHECK(m2->get<Message>(5)->has(2));
    CHECK(p->parse(m2) == wireWith(0x4000, {}));
    auto m0 = decode(p, wireWith(0x0000, {}));
    CHECK(p->parse(m0) == wireWith(0x0000, {}));
}

TEST_CASE("bitmap from - roundtrip builds INFO-2 from the set children", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    Bytes tail = kIso; tail.insert(tail.end(), kFixed.begin(), kFixed.end());

    auto msg = std::make_shared<Message>("0100");
    msg->parser(p);
    REQUIRE(put(msg, "4.0", "1111"));
    REQUIRE(put(msg, "5.1", "414243"));
    REQUIRE(put(msg, "5.3.0", "12345678"));
    REQUIRE(put(msg, "5.3.1", "1234"));
    CHECK(p->parse(msg) == wireWith(0xA000, tail));

    // decodierte Nachricht erneut serialisieren: gleiche Bytes, kein Fehler
    auto dec = decode(p, wireWith(0xA000, tail));
    CHECK(p->parse(dec) == wireWith(0xA000, tail));
}

TEST_CASE("bitmap from - container without children writes an empty INFO-2", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    auto msg = std::make_shared<Message>("0100");
    msg->parser(p);
    REQUIRE(put(msg, "4.0", "1111"));
    CHECK(p->parse(msg) == wireWith(0x0000, {}));
}

TEST_CASE("bitmap from - a conflicting source value at build time is an error", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    auto msg = std::make_shared<Message>("0100");
    msg->parser(p);
    REQUIRE(put(msg, "4.0", "1111"));
    REQUIRE(put(msg, "4.1", "FFFF"));
    REQUIRE(put(msg, "5.1", "414243"));
    REQUIRE_THROWS_WITH(p->parse(msg), ContainsSubstring("4.1"));
}

TEST_CASE("bitmap from - set bit without declared child is a strict error", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    auto msg = std::make_shared<Message>();
    msg->parser(p);
    REQUIRE_THROWS_WITH(msg->unparse(msg, wireWith(0x1000, { 0x01 })), ContainsSubstring("kein Kind"));
}

TEST_CASE("bitmap from - truncated child and leftover bytes are errors", "[bitmap-from]") {
    auto p = load(msgSpec(kExt, kFrom));
    auto m1 = std::make_shared<Message>();
    m1->parser(p);
    REQUIRE_THROWS(m1->unparse(m1, wireWith(0x8000, {})));      // ISO-Daten fehlen
    auto m2 = std::make_shared<Message>();
    m2->parser(p);
    Bytes extra = kIso; extra.push_back(0xFF);
    REQUIRE_THROWS(m2->unparse(m2, wireWith(0x8000, extra)));   // Rest hinter dem Kind
}

TEST_CASE("bitmap from - field-only spec resolves the source relative to the field", "[bitmap-from]") {
    const std::string yaml =
        "spec: \"bitmap from field\"\n"
        "extensions: [bitmap_from]\n"
        "encoding: ascii\n"
        "field:\n"
        "  type: nested\n"
        "  format: binary\n"
        "  length: 64\n"
        "  children:\n"
        "    - { format: binary, length: 2, description: \"Kopf\" }\n"
        "    - { format: binary, length: 2, description: \"INFO-2\" }\n"
        "    - type: nested\n"
        "      format: remaining\n"
        "      encoding: binary\n"
        "      length: 255\n"
        "      bitmap: { from: \"1\", length: 2 }\n"
        "      children:\n" + indented(kChildren);
    const TempYaml y(yaml);
    auto p = spec::SpecDecoder::loadFieldFromYaml(y.str());
    Bytes w = { 0x11, 0x11, 0x80, 0x00 };
    w.insert(w.end(), kIso.begin(), kIso.end());
    auto msg = spec::SpecDecoder::decodeField(p, BinaryField(0, w));
    REQUIRE(msg);
    CHECK(hexOf(msg, "2", 1) == "414243");
}

TEST_CASE("bitmap from - load errors are positioned and fail closed", "[bitmap-from]") {
    // ohne Schalter
    REQUIRE_THROWS_WITH(load(msgSpec("", kFrom)), ContainsSubstring("extensions: [bitmap_from]"));
    // unbekannte Erweiterung
    REQUIRE_THROWS_WITH(load(msgSpec("extensions: [bitmap_from, foo]\n", kFrom)), ContainsSubstring("foo"));
    // Quelle nicht binary
    {
        std::string y = msgSpec(kExt, kFrom);
        const std::string src = "{ format: binary, length: 2, description: \"INFO-2\" }";
        y.replace(y.find(src), src.size(), "{ format: char, length: 2, description: \"INFO-2\" }");
        REQUIRE_THROWS_WITH(load(y), ContainsSubstring("binary"));
    }
    // falsche Laenge
    REQUIRE_THROWS_WITH(load(msgSpec(kExt, "    bitmap: { from: \"4.1\", length: 1 }\n")),
        ContainsSubstring("length"));
    // Quelle existiert nicht
    REQUIRE_THROWS_WITH(load(msgSpec(kExt, "    bitmap: { from: \"4.7\", length: 2 }\n")),
        ContainsSubstring("4.7"));
    // Quelle steht nicht vor dem Container (Selbstbezug)
    REQUIRE_THROWS_WITH(load(msgSpec(kExt, "    bitmap: { from: \"5.1\", length: 2 }\n")),
        ContainsSubstring("5.1"));
    // ungueltiger Pfad
    REQUIRE_THROWS(load(msgSpec(kExt, "    bitmap: { from: \"x\", length: 2 }\n")));
}

TEST_CASE("bitmap from - plain bitmap containers are unchanged without the switch", "[bitmap-from]") {
    REQUIRE_NOTHROW(load(msgSpec("", "    bitmap: { length: 2 }\n")));
}
