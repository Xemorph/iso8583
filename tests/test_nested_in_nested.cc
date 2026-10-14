// =============================================================================
// test_nested_in_nested.cc - FR-14/FR-15 (0.11.0): Container in Container
// =============================================================================
//
// Ein nested-Kind darf selbst Container sein: positionell (FR-15), als Kind
// eines Bitmap-Containers (FR-14) und als 'header:'-Kind. Frueher wurde das
// Kind still wie ein Blattfeld behandelt (Dekodieren: ein Rohfeld, Bauen: set()
// der Unterkinder scheitert).
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
            / ("libiso8583_test_nin_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
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

Bytes asc(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::shared_ptr<ISOParserPtrBase> load(const TempYaml& y) {
    return spec::SpecDecoder::loadFromYaml(y.str());
}

// ASCII-Nachricht: MTI + Primaer-Bitmap + DE3 bzw. DE62 (je nach body).
std::string msgSpec(const std::string& fieldYaml) {
    return "spec: \"FR-14/15 nested in nested\"\n"
           "encoding: ascii\n"
           "fields:\n"
           "  \"000\": { format: numeric, length: 4 }\n"
           "  \"001\": { format: bitmap, length: 8 }\n" + fieldYaml;
}

// FR-15 Repro 2: DE3 (7 Byte) = innerer Container (4 Byte) + outer C (3 Byte)
const std::string kDe3 =
    "  \"003\":\n"
    "    type: nested\n"
    "    format: binary\n"
    "    length: 7\n"
    "    children:\n"
    "      - { format: nop }\n"
    "      - type: nested\n"
    "        format: binary\n"
    "        length: 4\n"
    "        children:\n"
    "          - { format: nop }\n"
    "          - { format: char, length: 2, description: \"inner A\" }\n"
    "          - { format: char, length: 2, description: \"inner B\" }\n"
    "      - { format: char, length: 3, description: \"outer C\" }\n";

// MTI 0200 + Bitmap (Bit 3) + DE3
Bytes de3Wire(const std::string& payload) {
    return cat({ asc("0200"), Bytes{ 0x20, 0, 0, 0, 0, 0, 0, 0 }, asc(payload) });
}

std::shared_ptr<Message> sub(const std::shared_ptr<Message>& m, int key) {
    REQUIRE(m);
    auto c = m->get<Message>(static_cast<TNG_KEY_TYPE>(key));
    REQUIRE(c);
    return c;
}

std::string opaque(const std::shared_ptr<Message>& m, int key) {
    auto f = m->get<OpaqueField>(static_cast<TNG_KEY_TYPE>(key));
    REQUIRE(f);
    return f->value();
}

} // namespace

TEST_CASE("nested in nested FR-15 - positional inner container decodes into its children",
    "[nested-in-nested][fr15]") {
    const TempYaml y(msgSpec(kDe3));
    auto parser = load(y);
    const Bytes w = de3Wire("ABCDEFG");

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    auto de3 = sub(msg, 3);
    auto inner = sub(de3, 1);
    CHECK(opaque(inner, 1) == "AB");
    CHECK(opaque(inner, 2) == "CD");
    CHECK(opaque(de3, 2) == "EFG");

    CHECK(msg->parse(msg) == w); // Round-Trip byte-identisch
}

TEST_CASE("nested in nested FR-15 - inner children are settable by dot path and rebuild the wire",
    "[nested-in-nested][fr15]") {
    const TempYaml y(msgSpec(kDe3));
    auto parser = load(y);

    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    REQUIRE(built->set("3.1.1", std::string("AB")));
    REQUIRE(built->set("3.1.2", std::string("CD")));
    REQUIRE(built->set("3.2", std::string("EFG")));
    CHECK(built->parse(built) == de3Wire("ABCDEFG"));
}

TEST_CASE("nested in nested FR-15 - truncated payload fails closed",
    "[nested-in-nested][fr15]") {
    const TempYaml y(msgSpec(kDe3));
    auto parser = load(y);
    const Bytes w = de3Wire("ABCDEF"); // 6 statt 7 Byte

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    CHECK_THROWS(msg->unparse(msg, w));
}

TEST_CASE("nested in nested FR-15 - two inner containers side by side",
    "[nested-in-nested][fr15]") {
    const std::string fld =
        "  \"003\":\n"
        "    type: nested\n"
        "    format: binary\n"
        "    length: 7\n"
        "    children:\n"
        "      - { format: nop }\n"
        "      - type: nested\n"
        "        format: binary\n"
        "        length: 4\n"
        "        children:\n"
        "          - { format: nop }\n"
        "          - { format: char, length: 4, description: \"Sys\" }\n"
        "      - type: nested\n"
        "        format: binary\n"
        "        length: 3\n"
        "        children:\n"
        "          - { format: nop }\n"
        "          - { format: char, length: 3, description: \"Visa\" }\n";
    const TempYaml y(msgSpec(fld));
    auto parser = load(y);
    const Bytes w = de3Wire("SYS1VIS");

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    CHECK(opaque(sub(sub(msg, 3), 1), 1) == "SYS1");
    CHECK(opaque(sub(sub(msg, 3), 2), 1) == "VIS");
    CHECK(msg->parse(msg) == w);
}

TEST_CASE("nested in nested FR-14 - nested child of a bitmap container decodes and rebuilds",
    "[nested-in-nested][fr14]") {
    const std::string fld =
        "  \"062\":\n"
        "    type: nested\n"
        "    format: lbinary\n"
        "    encoding: binary\n"
        "    length: 255\n"
        "    bitmap: { length: 8 }\n"
        "    children:\n"
        "      \"6\":\n"
        "        type: nested\n"
        "        format: binary\n"
        "        length: 3\n"
        "        children:\n"
        "          - { format: nop }\n"
        "          - { format: binary, length: 1 }\n"
        "          - { format: binary, length: 2 }\n";
    const TempYaml y(msgSpec(fld));
    auto parser = load(y);

    const Bytes payload = { 0x04, 0, 0, 0, 0, 0, 0, 0, 0xAA, 0xBB, 0xCC }; // Bit 6
    const Bytes w = cat({ asc("0200"), Bytes{ 0, 0, 0, 0, 0, 0, 0, 0x04 },
                          Bytes{ static_cast<uint8_t>(payload.size()) }, payload });

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    auto k6 = sub(sub(msg, 62), 6);
    CHECK(k6->get<OpaqueField>(1)->value() == "AA");
    CHECK(k6->get<OpaqueField>(2)->value() == "BBCC");
    CHECK(msg->parse(msg) == w);

    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    REQUIRE(built->set("62.6.1", std::string("AA")));
    REQUIRE(built->set("62.6.2", std::string("BBCC")));
    CHECK(built->parse(built) == w); // Bit 6 gesetzt, sobald ein Unterkind gesetzt ist
}

TEST_CASE("nested in nested FR-15 - container with bitmap as child of a positional container",
    "[nested-in-nested][fr15]") {
    const std::string fld =
        "  \"003\":\n"
        "    type: nested\n"
        "    format: binary\n"
        "    length: 5\n"
        "    children:\n"
        "      - { format: nop }\n"
        "      - type: nested\n"
        "        format: binary\n"
        "        length: 3\n"
        "        bitmap: { length: 1 }\n"
        "        children:\n"
        "          \"2\": { format: char, length: 2, description: \"bit2\" }\n"
        "      - { format: char, length: 2, description: \"outer\" }\n";
    const TempYaml y(msgSpec(fld));
    auto parser = load(y);
    const Bytes w = de3Wire(std::string("\x40", 1) + "ABXY"); // Bitmap 0x40 = Bit 2

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    CHECK(opaque(sub(sub(msg, 3), 1), 2) == "AB");
    CHECK(opaque(sub(msg, 3), 2) == "XY");
    CHECK(msg->parse(msg) == w);
}

TEST_CASE("nested in nested FR-15 - header child may be a container",
    "[nested-in-nested][fr15][header]") {
    const std::string yaml =
        "spec: \"FR-15 header\"\n"
        "encoding: ascii\n"
        "header:\n"
        "  type: nested\n"
        "  children:\n"
        "    - type: nested\n"
        "      format: binary\n"
        "      length: 4\n"
        "      children:\n"
        "        - { format: char, length: 2, description: \"inner A\" }\n"
        "        - { format: char, length: 2, description: \"inner B\" }\n"
        "    - { format: lllchar, length: 999 }\n"
        "fields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n";
    const TempYaml y(yaml);
    auto parser = load(y);
    const Bytes w = cat({ asc("ABCD"), asc("003xyz"), asc("0200"),
                          Bytes{ 0, 0, 0, 0, 0, 0, 0, 0 } });

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    auto h = msg->headerFields();
    REQUIRE(h);
    auto inner = sub(h, 0);
    CHECK(opaque(inner, 0) == "AB");
    CHECK(opaque(inner, 1) == "CD");
    CHECK(opaque(h, 1) == "xyz");
    CHECK(msg->parse(msg) == w);
}

TEST_CASE("nested in nested FR-15 - header container still rejects bitmap and remaining children",
    "[nested-in-nested][fr15][header]") {
    const std::string yaml =
        "spec: \"FR-15 header bad\"\n"
        "encoding: ascii\n"
        "header:\n"
        "  type: nested\n"
        "  children:\n"
        "    - type: nested\n"
        "      format: binary\n"
        "      length: 4\n"
        "      children:\n"
        "        - { format: remaining }\n"
        "fields:\n"
        "  \"000\": { format: numeric, length: 4 }\n"
        "  \"001\": { format: bitmap, length: 8 }\n";
    const TempYaml y(yaml);
    CHECK_THROWS_AS(load(y), std::runtime_error);
}

TEST_CASE("nested in nested FR-15 - nesting beyond the depth limit is a load error",
    "[nested-in-nested][fr15]") {
    // 201 Container-Ebenen > MAX_RECURSION_DEPTH (200), als Flow-YAML
    std::string node = "{ format: char, length: 1 }";
    for (int i = 0; i < 201; ++i)
        node = "{ type: nested, format: binary, length: 1, children: [ " + node + " ] }";
    const std::string fld = "  \"003\": " + node + "\n";
    const TempYaml y(msgSpec(fld));
    CHECK_THROWS_AS(load(y), std::runtime_error);
}
