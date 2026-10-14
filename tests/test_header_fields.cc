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
