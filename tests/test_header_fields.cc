// =============================================================================
// test_header_fields.cc - FR-16 (0.10.0): 'header:' als Feldbeschreibung
// =============================================================================
//
// Statt 'header: N' (feste Byte-Zahl) darf das Root-Schluessel 'header:' ein
// nested-Container mit positionellen Skalar-Kindern sein. Die Header-Laenge ist
// die Summe der verbrauchten Kind-Bytes - damit sind Header mit
// laengenpraefixierten Feldern (LLL + Daten) dekodier- und baubar.
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
            / ("libiso8583_test_hdr_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
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

// EBCDIC (IBM-1047) der im Test verwendeten Zeichen
Bytes ebc(const std::string& s) {
    Bytes out;
    for (char c : s) {
        switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': out.push_back(static_cast<uint8_t>(0xF0 + (c - '0'))); break;
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'G': case 'H': case 'I': out.push_back(static_cast<uint8_t>(0xC1 + (c - 'A'))); break;
        case 'x': out.push_back(0xA7); break;
        case 'y': out.push_back(0xA8); break;
        case 'z': out.push_back(0xA9); break;
        case 'u': out.push_back(0xA4); break;
        case 'v': out.push_back(0xA5); break;
        default: FAIL("ebc(): Zeichen nicht abgebildet");
        }
    }
    return out;
}

// Header: 4 + 2 feste Zeichen, dann zwei LLL-Felder (EBCDIC-Praefix und -Daten).
const std::string kHeader =
    "header:\n"
    "  type: nested\n"
    "  children:\n"
    "    - { format: char,    length: 4,   description: \"Fest 1\" }\n"
    "    - { format: char,    length: 2,   description: \"Fest 2\" }\n"
    "    - { format: lllchar, length: 999, description: \"Variabel 1\" }\n"
    "    - { format: lllchar, length: 999, description: \"Variabel 2\" }\n";

std::string specYaml(const std::string& header = kHeader) {
    return "spec: \"FR-16 header fields\"\n"
           "encoding: ebcdic\n" + header +
           "fields:\n"
           "  \"000\": { format: numeric, length: 4 }\n"
           "  \"001\": { format: bitmap,  length: 8 }\n"
           "  \"003\": { format: numeric, length: 6 }\n";
}

// Header ABCD EF 003 xyz 002 uv = 17 Byte
Bytes headerBytes() {
    return cat({ ebc("ABCD"), ebc("EF"), ebc("003"), ebc("xyz"), ebc("002"), ebc("uv") });
}

// MTI 0200 + Bitmap (DE3 = Bit 3) + DE3 "000000"
Bytes body() {
    return cat({ ebc("0200"), Bytes{ 0x20, 0, 0, 0, 0, 0, 0, 0 }, ebc("000000") });
}

std::shared_ptr<ISOParserPtrBase> load(const TempYaml& y) {
    return spec::SpecDecoder::loadFromYaml(y.str());
}

std::string hval(const std::shared_ptr<Message>& msg, int key) {
    auto h = msg->headerFields();
    REQUIRE(h);
    auto f = h->get<OpaqueField>(static_cast<TNG_KEY_TYPE>(key));
    REQUIRE(f);
    return f->value();
}

bool setv(const std::shared_ptr<Message>& m, int key, const std::string& v) {
    return m->set(static_cast<TNG_KEY_TYPE>(key), v);
}

} // namespace

TEST_CASE("header fields FR-16 - variable header decodes into named fields, MTI starts after 17 bytes",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    const Bytes w = cat({ headerBytes(), body() });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    CHECK(hval(msg, 0) == "ABCD");
    CHECK(hval(msg, 1) == "EF");
    CHECK(hval(msg, 2) == "xyz");
    CHECK(hval(msg, 3) == "uv");
    CHECK(msg->headerFields()->get<OpaqueField>(2)->wire_offset() == 6);
    CHECK(msg->mti() == "0200");
    CHECK(msg->get<OpaqueField>(0)->wire_offset() == 17);
    CHECK(msg->tryGetValue<OpaqueField>(3).value() == "000000");
}

TEST_CASE("header fields FR-16 - empty LLL block (000) decodes as empty field",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    const Bytes w = cat({ ebc("ABCD"), ebc("EF"), ebc("000"), ebc("002"), ebc("uv"), body() });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    CHECK(hval(msg, 2).empty());
    CHECK(hval(msg, 3) == "uv");
    CHECK(msg->mti() == "0200");
}

TEST_CASE("header fields FR-16 - LLL beyond the buffer and non-numeric LLL fail with a position",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    SECTION("LLL reaches past the end of the message") {
        // Variabel 1 behauptet 900 Byte, es folgen nur wenige
        const Bytes w = cat({ ebc("ABCD"), ebc("EF"), ebc("900"), ebc("xyz") });
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, w), ContainsSubstring("Header-Feld 2"));
    }
    SECTION("non-numeric LLL") {
        const Bytes w = cat({ ebc("ABCD"), ebc("EF"), Bytes{ 0xC1, 0xC2, 0xC3 }, ebc("xyz") });
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, w), ContainsSubstring("Nicht-Ziffer"));
    }
    SECTION("header ends before the fixed fields are complete") {
        const Bytes w = ebc("ABC");
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, w), ContainsSubstring("Header-Feld 0"));
    }
}

TEST_CASE("header fields FR-16 - build is byte-identical, LLL is computed",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    auto h = msg->headerFields();
    REQUIRE(h);
    REQUIRE(setv(h, 0, "ABCD"));
    REQUIRE(setv(h, 1, "EF"));
    REQUIRE(setv(h, 2, "xyz"));
    REQUIRE(setv(h, 3, "uv"));
    REQUIRE(setv(msg, 3, "000000"));

    CHECK(msg->parse(msg) == cat({ headerBytes(), body() }));
}

TEST_CASE("header fields FR-16 - empty block (000) is built and round-trips",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    auto h = msg->headerFields();
    REQUIRE(setv(h, 0, "ABCD"));
    REQUIRE(setv(h, 1, "EF"));
    REQUIRE(setv(h, 2, ""));
    REQUIRE(setv(h, 3, "uv"));
    REQUIRE(setv(msg, 3, "000000"));

    const Bytes w = msg->parse(msg);
    CHECK(w == cat({ ebc("ABCD"), ebc("EF"), ebc("000"), ebc("002"), ebc("uv"), body() }));

    auto back = std::make_shared<Message>();
    back->parser(parser);
    REQUIRE(back->unparse(back, w) == w.size());
    CHECK(hval(back, 2).empty());
    CHECK(back->parse(back) == w);
}

TEST_CASE("header fields FR-16 - decode then re-encode is byte-identical",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    const Bytes w = cat({ headerBytes(), body() });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());
    CHECK(msg->parse(msg) == w);
}

TEST_CASE("header fields FR-16 - build fails closed on missing field and on raw header bytes",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);

    SECTION("header field not set") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        auto h = msg->headerFields();
        REQUIRE(setv(h, 0, "ABCD"));
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("Header-Feld 1"));
    }
    SECTION("no header fields at all") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("Header-Feld 0"));
    }
    SECTION("header(bytes) together with header fields") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        msg->header(Bytes{ 1, 2, 3 });
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("headerFields()"));
    }
    SECTION("value larger than the field maximum") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        auto h = msg->headerFields();
        REQUIRE(setv(h, 0, "ABCDE"));   // char(4)
        REQUIRE(setv(h, 1, "EF"));
        REQUIRE(setv(h, 2, "xyz"));
        REQUIRE(setv(h, 3, "uv"));
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("Header-Feld 0"));
    }
}

TEST_CASE("header fields FR-16 - describe lists the header children, numeric header unchanged",
    "[header-fields][fr16]") {
    SECTION("field-description header") {
        const TempYaml y(specYaml());
        auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
        REQUIRE(spec);
        CHECK(spec->hasHeader());
        CHECK(spec->hasHeaderFields());
        CHECK(spec->headerSize() == 0);

        const auto& hf = spec->headerFields();
        REQUIRE(hf.size() == 4);
        CHECK(hf[0].key == 0);
        CHECK(hf[0].description == "Fest 1");
        CHECK(hf[0].format.type == "CHAR");
        CHECK(hf[0].format.prefix_digits == 0);
        CHECK(hf[0].format.max_length == 4);
        CHECK(hf[0].encoding == "EBCDIC");
        CHECK(hf[2].key == 2);
        CHECK(hf[2].format.prefix_digits == 3);
        CHECK(hf[2].format.max_length == 999);
        CHECK(hf[3].description == "Variabel 2");
        // Die Header-Kinder sind keine DEs der Nachricht.
        CHECK_FALSE(spec->has(4));
    }
    SECTION("numeric header: size as before, no header fields") {
        const TempYaml y("spec: \"num\"\nencoding: ebcdic\nheader: 5\nfields:\n"
                         "  \"000\": { format: numeric, length: 4 }\n"
                         "  \"001\": { format: bitmap,  length: 8 }\n");
        auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml(y.str());
        CHECK(spec->hasHeader());
        CHECK(spec->headerSize() == 5);
        CHECK_FALSE(spec->hasHeaderFields());
        CHECK(spec->headerFields().empty());

        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK(msg->headerFields() == nullptr);

        const Bytes w = cat({ Bytes{ 1, 2, 3, 4, 5 }, ebc("0200"), Bytes{ 0, 0, 0, 0, 0, 0, 0, 0 } });
        REQUIRE(msg->unparse(msg, w) == w.size());
        CHECK(msg->mti() == "0200");
        CHECK(msg->headerFields() == nullptr);
    }
}

TEST_CASE("header fields FR-16 - to_json carries the header fields",
    "[header-fields][fr16]") {
    const TempYaml y(specYaml());
    auto parser = load(y);
    const Bytes w = cat({ headerBytes(), body() });
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, w) == w.size());

    const auto j = msg->to_json();
    REQUIRE(j.contains("header_fields"));
    CHECK(j["header_fields"].size() == 4);
}

TEST_CASE("header fields FR-16 - loader rejects meaningless header descriptions",
    "[header-fields][fr16]") {
    auto loads = [](const std::string& header) {
        const TempYaml y(specYaml(header));
        return spec::SpecDecoder::loadFromYaml(y.str());
    };

    SECTION("children missing") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n"), ContainsSubstring("children"));
    }
    SECTION("children as map (TLV form)") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n  children:\n    \"1\": { format: char, length: 2 }\n"),
            ContainsSubstring("children"));
    }
    SECTION("own length frame") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n  format: lllbinary\n  length: 99\n"
                                "  children:\n    - { format: char, length: 2 }\n"),
            ContainsSubstring("format"));
    }
    SECTION("tlv container") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n  tlv: { ber: true }\n"
                                "  children:\n    - { format: char, length: 2 }\n"),
            ContainsSubstring("tlv"));
    }
    SECTION("wrong type") {
        CHECK_THROWS_WITH(loads("header:\n  type: scalar\n  children:\n    - { format: char, length: 2 }\n"),
            ContainsSubstring("nested"));
    }
    SECTION("remaining child would swallow the message") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n  children:\n"
                                "    - { format: remaining, length: 10 }\n"),
            ContainsSubstring("header"));
    }
    SECTION("unknown key") {
        CHECK_THROWS_WITH(loads("header:\n  type: nested\n  bogus: 1\n  children:\n"
                                "    - { format: char, length: 2 }\n"),
            ContainsSubstring("bogus"));
    }
}

TEST_CASE("header fields FR-16 - field-only spec still rejects header",
    "[header-fields][fr16]") {
    const TempYaml y("spec: \"fo\"\nencoding: ebcdic\n" + kHeader +
                     "field: { format: char, length: 2 }\n");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFieldFromYaml(y.str()),
        ContainsSubstring("header"));
}

// -----------------------------------------------------------------------------
// Teil B (FR-16): binaere Daten hinter einem EBCDIC-LLL. Kein neues Format noetig:
// 'lllbinary' mit 'encoding: ebcdic' ist EBCDIC-Praefix (Byte-Zaehlung) + Roh-Bytes
// (parserTable: LLLBINARY|EBCDIC); der Wert ist ein BinaryField (Hex-String beim set()).
// -----------------------------------------------------------------------------
namespace {

// Header: char(4), lllchar (Text), lllbinary (ICC-Daten) - gemischte Kinder.
const std::string kMixedHeader =
    "header:\n"
    "  type: nested\n"
    "  children:\n"
    "    - { format: char,      length: 4,   description: \"Fest\" }\n"
    "    - { format: lllchar,   length: 999, description: \"Text\" }\n"
    "    - { format: lllbinary, length: 10,  description: \"ICC-Daten\" }\n";

std::vector<uint8_t> binValue(const std::shared_ptr<Message>& msg, int key) {
    auto h = msg->headerFields();
    REQUIRE(h);
    auto f = h->get<BinaryField>(static_cast<TNG_KEY_TYPE>(key));
    REQUIRE(f);
    return f->value();
}

} // namespace

TEST_CASE("header fields FR-16 part B - binary data behind an EBCDIC LLL decodes, builds and round-trips",
    "[header-fields][fr16][fr16b]") {
    const TempYaml y(specYaml(kMixedHeader));
    auto parser = load(y);

    const Bytes hdr = cat({ ebc("ABCD"), ebc("003"), ebc("xyz"), ebc("003"), Bytes{ 0x9F, 0x26, 0x08 } });
    const Bytes w = cat({ hdr, body() });

    SECTION("decode: LLL=003 gives the three raw bytes, MTI follows") {
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        REQUIRE(msg->unparse(msg, w) == w.size());
        CHECK(hval(msg, 1) == "xyz");
        CHECK(binValue(msg, 2) == Bytes{ 0x9F, 0x26, 0x08 });
        CHECK(msg->headerFields()->get<BinaryField>(2)->readable_value() == "9F2608");
        CHECK(msg->mti() == "0200");
        CHECK(msg->parse(msg) == w);
    }
    SECTION("build from the hex string is byte-identical") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        auto h = msg->headerFields();
        REQUIRE(setv(h, 0, "ABCD"));
        REQUIRE(setv(h, 1, "xyz"));
        REQUIRE(setv(h, 2, "9F2608"));
        REQUIRE(setv(msg, 3, "000000"));
        CHECK(msg->parse(msg) == w);
    }
    SECTION("LLL=000 is an empty field and rebuilds") {
        const Bytes w0 = cat({ ebc("ABCD"), ebc("003"), ebc("xyz"), ebc("000"), body() });
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        REQUIRE(msg->unparse(msg, w0) == w0.size());
        CHECK(binValue(msg, 2).empty());
        CHECK(msg->parse(msg) == w0);
    }
    SECTION("data longer than length fails when building") {
        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        auto h = msg->headerFields();
        REQUIRE(setv(h, 0, "ABCD"));
        REQUIRE(setv(h, 1, "xyz"));
        REQUIRE(setv(h, 2, "0102030405060708090A0B"));   // 11 Byte > length 10
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("Header-Feld 2"));
    }
    SECTION("LLL beyond the declared maximum fails when decoding") {
        const Bytes wl = cat({ ebc("ABCD"), ebc("003"), ebc("xyz"), ebc("011"), Bytes(11, 0x01), body() });
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        CHECK_THROWS_WITH(msg->unparse(msg, wl), ContainsSubstring("Header-Feld 2"));
    }
}
