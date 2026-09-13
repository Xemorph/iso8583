// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/detail/_components.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#include <thread>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path()
             / ("iso8583_rem_" + std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        std::ofstream f(p); f << content;
        return p.string();
    }
};
} // namespace

// =============================================================================
// ISORemainderFieldParser direkt
// =============================================================================

TEST_CASE("IFE_REMAINING - reads all remaining bytes", "[remaining][direct]") {
    // Buffer: 4 feste Bytes + 3 variable Bytes
    auto buf = B({ 0xF1, 0xF2, 0xF3, 0xF4,   // 4 Bytes feste Felder (schon konsumiert)
                   0xC1, 0xC2, 0xC3 });        // 3 Bytes verbleibend

    IFE_REMAINING parser(10, "Trailing Field");
    auto component = std::make_shared< OpaqueField >(14);

    std::size_t consumed = parser.unparse(component, buf, 4);  // Offset = 4

    CHECK(consumed == 3);
    CHECK(component->wire_length() == 3);
    CHECK(component->value() == "ABC");
}

/*
TEST_CASE("IF_REMAINING - empty buffer returns 0 (no crash)", "[remaining][direct]") {
    auto buf = B({ 0xF1, 0xF2 });
    IF_REMAINING parser(0, "Optional Trailing");
    auto component = std::make_shared<ISOOpaqueField>(15);

    // Offset = Buffer-Ende -> nichts zu lesen
    REQUIRE_NOTHROW(parser.unparse(component, buf, 2));
    std::size_t consumed = parser.unparse(component, buf, 2);
    CHECK(consumed == 0);
    CHECK(component->value().empty());
}

TEST_CASE("IF_REMAINING - full buffer (no preceding field)", "[remaining][direct]") {
    auto buf = B({ 0xC1, 0xC2, 0xC3, 0xC4, 0xC5 });
    IF_REMAINING parser(0, "All Bytes");
    auto component = std::make_shared<ISOOpaqueField>(1);

    std::size_t consumed = parser.unparse(component, buf, 0);
    CHECK(consumed == 5);
    CHECK(component->wire_length() == 5);
}
*/

TEST_CASE("IF_REMAINING - type() returns REMAINING", "[remaining][direct]") {
    IF_REMAINING parser(0, "test");
    CHECK(parser.type() == ISOFieldParserType::REMAINING);
}

// =============================================================================
// BMP_061 Simulation: nested mit trailing REMAINING Subfeld
// =============================================================================

// Baut einen nested Parser der BMP_061 simuliert:
//   Subfelder 1-14: fix (16 Bytes total)
//   Subfeld 15:     remaining (0-10 Bytes, kein Prefix)
static std::shared_ptr<ISOBaseParser> make_bmp061_parser() {
    auto sub = std::make_shared<ISOBaseParser>("BMP061 children", 0);

    // Subfeld 0: NOP (0 Bytes)
    sub->add(std::make_shared<IF_NOP>());
    // Subfeld 1: 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "Terminal Attendance"));
    // Subfeld 2: reserved 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "reserved"));
    // Subfelder 3-8: je 1 Byte EBCDIC numeric/char
    for (int i = 0; i < 6; ++i)
        sub->add(std::make_shared<IFE_NUMERIC>(1, "POS SF" + std::to_string(i+2)));
    // Subfeld 9: reserved 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "reserved"));
    // Subfelder 10-11: je 1 Byte
    sub->add(std::make_shared<IFE_NUMERIC>(1, "CAT Level"));
    sub->add(std::make_shared<IFE_NUMERIC>(1, "Input Cap"));
    // Subfeld 12: 2 Bytes
    sub->add(std::make_shared<IFE_NUMERIC>(2, "Auth Life Cycle"));
    // Subfeld 13: 3 Bytes
    sub->add(std::make_shared<IFE_NUMERIC>(3, "Country Code"));
    // Subfeld 14: REMAINING (0-10 Bytes, kein Prefix)
    sub->add(std::make_shared<IFE_REMAINING>(10, "POS Postal Code"));

    return sub;
}

TEST_CASE("BMP_061 - without postal code (16 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 16 Bytes, Subfeld 15 bekommt 0 Bytes
    auto buf = B({
        // 16 EBCDIC-Bytes fuer Subfelder 1-13
        // SF0(NOP)=0, SF1-8=je 0xF0, SF9=0xF0, SF10=0xF0, SF11=0xF0,
        // SF12=0xF0 0xF0, SF13=0xF0 0xF0 0xF0
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,  // SF1-8
        0xF0,                                              // SF9
        0xF0, 0xF0,                                        // SF10-11
        0xF0, 0xF0,                                        // SF12
        0xF0, 0xF0, 0xF0,                                  // SF13
        // No SF14
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    // SF14 (Postal Code) sollte leer sein - kein Crash
    // (key = 14, 0-basiert in der Subparser-Liste)
    auto sf14 = msg->get< OpaqueField >(14);
    CHECK(sf14 == nullptr);
}

TEST_CASE("BMP_061 - with full postal code (26 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 26 Bytes, Subfeld 15 bekommt 10 Bytes
    auto buf = B({
        // SF2-9 (8 Bytes)
        0xF1, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
        // SF10 reserved
        0xF0,
        // SF11-12
        0xF0, 0xF0,
        // SF13 (2 Bytes)
        0xF0, 0xF0,
        // SF14 (3 Bytes)
        0xF4, 0xF9, 0xF2,   // "492" (PLZ-Vorwahl)
        // SF15 Postal Code (10 Bytes EBCDIC "6900      ")
        0xF6, 0xF9, 0xF0, 0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    // SF13 Country Code
    auto sf13 = msg->get< OpaqueField >(13);
    REQUIRE(sf13 != nullptr);
    CHECK(sf13->value() == "492");

    // SF14 Postal Code: 10 Bytes remaining
    auto sf14 = msg->get< OpaqueField >(14);
    REQUIRE(sf14 != nullptr);
    //CHECK(sf14->wire_length() == 10);
    CHECK(sf14->value() == "6900      ");
}

TEST_CASE("BMP_061 - with short postal code (19 bytes payload)", "[remaining][bmp061]") {
    // LLL Payload = 19 Bytes, Subfeld 14 has 3 Bytes
    auto buf = B({
        0xF1, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,    // SF1-8
        0xF0,                                              // SF9
        0xF0, 0xF0,                                        // SF10-11
        0xF0, 0xF0,                                        // SF12
        0xF4, 0xF9, 0xF2,                                  // SF13 "492"
        0xF6, 0xF9, 0xF0,                                  // SF14 "690" (3 Bytes)
    });

    auto sub = make_bmp061_parser();
    auto msg = std::make_shared< Message >();
    sub->unparse(msg, buf);

    auto sf14 = msg->get< OpaqueField >(14);
    REQUIRE(sf14 != nullptr);
    //CHECK(sf14->wire_length() == 3);
    CHECK(sf14->value() == "690");
}

// =============================================================================
// YAML Spec: format: remaining
// =============================================================================

TEST_CASE("SpecDecoder - format: remaining loads without error", "[remaining][spec]") {
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "Remaining Field Test"
encoding: ebcdic

fields:
  "000":
    type: scalar
    format: numeric
    length: 4
  "001":
    type: scalar
    format: bitmap
    length: 8
  "061":
    type: nested
    format: binary
    length: 26
    description: "POS Data"
    children:
      - type: scalar
        format: nop
        length: 0
      - type: scalar
        format: numeric
        length: 1
        description: "POS Terminal Attendance"
      - type: scalar
        format: numeric
        length: 1
        description: "reserved"
      - type: scalar
        format: numeric
        length: 3
        description: "POS Country Code"
      - type: scalar
        format: remaining
        description: "POS Postal Code"
        length: 10
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(spec_path));
    CHECK(parser != nullptr);
}

// =============================================================================
// (0.6.0) remaining ist encoding-aware: Encoding-Matrix, Roundtrip, strict,
// Fail-closed-Validierung
// =============================================================================

static std::string matrixYaml(const std::string& globalEnc,
    const std::string& mtiFormat,
    const std::string& fieldEnc,
    int remainingLen = 6,
    const std::string& strictLine = "")
{
    std::string y;
    y += "spec: \"Rem Enc Matrix\"\n";
    if (!globalEnc.empty())
        y += "encoding: " + globalEnc + "\n";
    if (!strictLine.empty())
        y += "strict: " + strictLine + "\n";
    y += "fields:\n";
    y += "  \"000\": { type: scalar, format: " + mtiFormat + ", length: 4 }\n";
    y += "  \"001\": { type: scalar, format: bitmap, length: 8 }\n";
    if (fieldEnc.empty())
        y += "  \"010\": { type: scalar, format: remaining, length: " + std::to_string(remainingLen)
             + ", description: \"Tail\" }\n";
    else
        y += "  \"010\": { type: scalar, format: remaining, length: " + std::to_string(remainingLen)
             + ", encoding: " + fieldEnc + ", description: \"Tail\" }\n";
    return y;
}

// Wire: MTI + 8-Byte-Primär-Bitmap (bit 10 -> byte 1 = 0x40) + Payload.
static std::vector<uint8_t> makeWire(const std::vector<uint8_t>& mti,
    const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> w = mti;
    w.push_back(0x00);   // byte 0: DE1..16 nicht gesetzt
    w.push_back(0x40);   // byte 1: DE10 gesetzt
    for (int i = 0; i < 6; ++i)
        w.push_back(0x00);
    for (auto b : payload)
        w.push_back(b);
    return w;
}

// (0.6.0) Globale Encoding bestimmt den Codec des remaining-Feldes:
// ""/binary -> roh (BinaryField), ascii/ebcdic -> Text (OpaqueField),
// bcd -> gepackte Ziffern (OpaqueField).
TEST_CASE("(0.6.0) Remaining encoding matrix - global encoding", "[remaining][spec]") {
    struct Row {
        std::string globalEnc;      // "" = keine Encoding
        std::string mtiFormat;      // numeric oder binary
        bool        mtiIsString;    // numeric-MTI -> mti() == "0200"
        int         remainingLen;   // BCD zählt Ziffern (1 Byte = 2 Ziffern)
        std::vector<uint8_t> mtiWire;
        std::vector<uint8_t> payload;
        bool        expectBinary;   // true -> BinaryField, false -> OpaqueField
        std::string expectText;
    };
    const std::vector<Row> rows = {
        // keine Encoding: everything raw
        { "",       "binary",  false, 6, {0x30,0x32,0x30,0x30}, {0x00,0xFF,0x10,0x20,0x9C,0x7F}, true,  "" },
        // binary: ebenfalls roh
        { "binary", "binary",  false, 6, {0x30,0x32,0x30,0x30}, {0x00,0xFF,0x10,0x20,0x9C,0x7F}, true,  "" },
        // ascii: Text
        { "ascii",  "numeric", true,  6, {0x30,0x32,0x30,0x30}, {0x48,0x45,0x4C,0x4C,0x4F,0x20}, false, "HELLO " },
        // ebcdic: EBCDIC-Text (IBM-1047: H=0xC8 E=0xC5 L=0xD3 O=0xD6 SP=0x40)
        { "ebcdic", "numeric", true,  6, {0xF0,0xF2,0xF0,0xF0}, {0xC8,0xC5,0xD3,0xD3,0xD6,0x40}, false, "HELLO " },
        // bcd: 6 Bytes = 12 Ziffern -> length = 12; Nibbles bleiben im
        // Ziffern-Bereich (0xA/0xB maepfte die Legacy-':'/';'-Konvention
        // ab, die hier nicht Gegenstand ist)
        { "bcd",    "numeric", true,  12, {0x02,0x00},            {0x01,0x23,0x45,0x67,0x89,0x12}, false, "012345678912" },
    };

    for (const auto& r : rows) {
        TempDir dir;
        const std::string fname = "rem_matrix"
            + (r.globalEnc.empty() ? "_none" : ("_" + r.globalEnc)) + ".yml";
        const std::string path = dir.write(fname,
            matrixYaml(r.globalEnc, r.mtiFormat, "", r.remainingLen));
        auto parser = spec::SpecDecoder::loadFromYaml(path);

        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        const auto wire = makeWire(r.mtiWire, r.payload);
        const auto consumed = msg->unparse(msg, wire);
        CHECK(consumed == wire.size());
        CHECK(msg->hasMTI());
        if (r.mtiIsString)
            CHECK(msg->mti() == "0200");

        if (r.expectBinary) {
            auto f = msg->get<BinaryField>(10);
            REQUIRE(f != nullptr);
            CHECK(f->value() == r.payload);
            CHECK(msg->get<OpaqueField>(10) == nullptr);
        }
        else {
            auto f = msg->get<OpaqueField>(10);
            REQUIRE(f != nullptr);
            CHECK(f->value() == r.expectText);
            CHECK(msg->get<BinaryField>(10) == nullptr);
        }
    }
}

// (0.6.0) Feld-Override schlägt globales Encoding.
TEST_CASE("(0.6.0) Remaining field-level encoding override", "[remaining][spec]") {
    TempDir dir;
    // global ascii, aber DE10 explizit ebcdic
    const std::string path = dir.write("rem_override.yml",
        matrixYaml("ascii", "numeric", "ebcdic"));
    auto parser = spec::SpecDecoder::loadFromYaml(path);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto wire = makeWire(B({ 0x30, 0x32, 0x30, 0x30 }),
                               B({ 0xC8, 0xC5, 0xD3, 0xD3, 0xD6, 0x40 }));
    msg->unparse(msg, wire);

    auto f = msg->get<OpaqueField>(10);
    REQUIRE(f != nullptr);
    CHECK(f->value() == "HELLO ");   // EBCDIC dekodiert, trotz globaler ascii

    // Umgekehrt: global ebcdic, DE10 explizit binary -> roh
    TempDir dir2;
    const std::string path2 = dir2.write("rem_override2.yml",
        matrixYaml("ebcdic", "numeric", "binary"));
    auto parser2 = spec::SpecDecoder::loadFromYaml(path2);
    auto msg2 = std::make_shared<Message>();
    msg2->parser(parser2);
    const auto wire2 = makeWire(B({ 0xF0, 0xF2, 0xF0, 0xF0 }),
                                B({ 0x9C, 0x00, 0x41 }));
    msg2->unparse(msg2, wire2);

    auto f2 = msg2->get<BinaryField>(10);
    REQUIRE(f2 != nullptr);
    CHECK(f2->value() == (std::vector<uint8_t>{ 0x9C, 0x00, 0x41 }));
}

// (0.6.0) Roundtrip: parse() (Encode) -> unparse() (Decode) für die neuen
// Codecs; BCD-Zeile pinnt die Byte->Ziffern-Umrechnung im remaining-Pfad.
TEST_CASE("(0.6.0) Remaining roundtrip parse/unparse", "[remaining][roundtrip]") {
    struct Row {
        std::string globalEnc;
        std::string value;
        int         expectTailBytes;
        int         mtiBytes;   // BCD-MTI belegt nur 2 Bytes (2 Ziffern-Paare)
    };
    const std::vector<Row> rows = {
        { "ascii",  "HELLO",    5, 4 },   // 5 ASCII-Bytes, kein Padding
        { "ebcdic", "HELLO",    5, 4 },   // 5 EBCDIC-Bytes
        { "bcd",    "012345",   3, 2 },   // 6 Ziffern = 3 BCD-Bytes
    };

    for (const auto& r : rows) {
        TempDir dir;
        const std::string path = dir.write("rem_rt_" + r.globalEnc + ".yml",
            matrixYaml(r.globalEnc, "numeric", ""));
        auto parser = spec::SpecDecoder::loadFromYaml(path);

        auto msg = std::make_shared<Message>("0200");
        msg->parser(parser);
        msg->set(10, r.value);
        // msg->parse(msg) (Nebenebene) rekalkuliert die Bitmap automatisch;
        // parser->parse(msg) (Expert-API) erwartet eine vorhandene Bitmap.
        const auto wire = msg->parse(msg);

        auto msg2 = std::make_shared<Message>();
        msg2->parser(parser);
        const auto consumed = msg2->unparse(msg2, wire);
        CHECK(consumed == wire.size());
        CHECK(msg2->mti() == "0200");

        const auto tail = wire.size() - static_cast<std::size_t>(r.mtiBytes) - 8;
        CHECK(tail == static_cast<std::size_t>(r.expectTailBytes));

        auto f = msg2->get<OpaqueField>(10);
        REQUIRE(f != nullptr);
        CHECK(f->value() == r.value);
    }
}

// (0.6.0) Strict-Propagierung im remaining-Pfad: EBCDIC-Bytes außerhalb
// der Whitelist werfen bei strict=true; strict=false macht Legacy-'.'.
TEST_CASE("(0.6.0) Remaining strict vs legacy EBCDIC mapping", "[remaining][spec]") {
    // 0x9C ist in EBCDIC außerhalb der IBM-1047-Whitelist
    const std::vector<uint8_t> mtiEbc = { 0xF0, 0xF2, 0xF0, 0xF0 };
    const std::vector<uint8_t> bad    = { 0x9C, 0xC5, 0xD3, 0xD3, 0xD6, 0x40 };

    {   // strict: true (Default)
        TempDir dir;
        const std::string path = dir.write("rem_strict.yml",
            matrixYaml("ebcdic", "numeric", "", 6, "true"));
        auto parser = spec::SpecDecoder::loadFromYaml(path);
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        const auto wire = makeWire(mtiEbc, bad);
        REQUIRE_THROWS_AS(msg->unparse(msg, wire), std::runtime_error);
    }

    {   // strict: false -> Legacy-Mapping 0x9C -> '.'
        TempDir dir;
        const std::string path = dir.write("rem_legacy.yml",
            matrixYaml("ebcdic", "numeric", "", 6, "false"));
        auto parser = spec::SpecDecoder::loadFromYaml(path);
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        const auto wire = makeWire(mtiEbc, bad);
        REQUIRE_NOTHROW(msg->unparse(msg, wire));

        auto f = msg->get<OpaqueField>(10);
        REQUIRE(f != nullptr);
        CHECK(f->value() == ".ELLO ");
    }
}

// (0.6.0) Fail-closed: 'remaining' ohne 'length' -> positioniertes
// SpecValidationError statt silent 0-Byte-Decode.
TEST_CASE("(0.6.0) Remaining without length -> load error", "[remaining][error]") {
    TempDir dir;
    const std::string yaml =
        "spec: \"Rem NoLength\"\n"
        "encoding: ascii\n"
        "fields:\n"
        "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
        "  \"001\": { type: scalar, format: bitmap, length: 8 }\n"
        "  \"010\": { type: scalar, format: remaining, description: \"No length\" }\n";
    const std::string path = dir.write("rem_nolen.yml", yaml);

    bool caught = false;
    std::string what;
    try {
        (void)spec::SpecDecoder::loadFromYaml(path);
    }
    catch (const std::runtime_error& e) {
        caught = true;
        what = e.what();
    }
    CHECK(caught);
    CHECK(what.find("remaining") != std::string::npos);
    CHECK(what.find("length") != std::string::npos);
}

// (0.6.0) 'length' bei remaining ist ein Maximum: überlanger Payload wird
// auf length gekürzt, Rest-Bytes bleiben unkonsumiert (strict: Würde
// 'Unverbrauchte Bytes' werfen – hier Legacy-Modus zur reinen Clamp-Prüfung).
TEST_CASE("(0.6.0) Remaining length is a max (clamping)", "[remaining][spec]") {
    TempDir dir;
    const std::string yaml =
        "spec: \"Rem Clamped\"\n"
        "encoding: ascii\n"
        "strict: false\n"
        "fields:\n"
        "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
        "  \"001\": { type: scalar, format: bitmap, length: 8 }\n"
        "  \"010\": { type: scalar, format: remaining, length: 3 }\n";
    const std::string path = dir.write("rem_clamp.yml", yaml);
    auto parser = spec::SpecDecoder::loadFromYaml(path);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto wire = makeWire(B({ 0x30, 0x32, 0x30, 0x30 }),
                               B({ 0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20 }));
    const auto consumed = msg->unparse(msg, wire);

    // Nur die ersten 3 Bytes ("HEL") werden als DE10 dekodiert.
    CHECK(consumed == 4 + 8 + 3);
    auto f = msg->get<OpaqueField>(10);
    REQUIRE(f != nullptr);
    CHECK(f->value() == "HEL");
}
