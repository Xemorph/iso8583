// [ISO8583] Phase-1-Verifikation: Fail-closed-Verhalten (strict, Default) vs.
// Legacy-Verhalten (strict(false)).
//
// Ab 0.3.0 ist der strikte Modus DEFAULT (Entscheidung Q2, Invariante P2):
//   - abgeschnittene Felder/Bitmaps/Praefixe  -> positioniertes std::runtime_error
//   - ueberdimensionierte Serialisierungen    -> std::runtime_error statt stillem Auslassen
//   - unkonvertierte Rest-Bytes / leeres Bild -> std::runtime_error
// Der alte, tolerante Legacy-Pfad bleibt expliziv erhaltbar via parser->strict(false)
// (inklusive Propagation auf verschachtelte Sub-Parserv).
//
// [catch2]
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
// [tng]
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

using namespace TNG_NAMESPACE;

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// ASCII-Parser: MTI fix4 + Bitmap fix8 + DE2 LLASCII(max).
static std::shared_ptr<ISOBaseParser> make_ascii_parser(std::size_t de2_max = 5,
    const std::string& desc = "StrictTest") {
    auto p = std::make_shared<ISOBaseParser>(desc, 0);
    p->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
    p->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
    p->add(std::make_shared<IFA_LLCHAR>(de2_max, "Amount"));
    return p;
}

// MTI "0100" + Bitmap (nur DE2) + DE2-LLASCII-Präfix "02" + "12".
static std::vector<uint8_t> base_frame() {
    return B({
        0x30, 0x31, 0x30, 0x30,               // MTI "0100"
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Bitmap: DE2
        0x30, 0x32,                            // DE2-LL "02"
        0x31, 0x32,                            // DE2 "12"
    });
}

// Temporaere YAML-Datei (RAII), wie in test_spec_loader.cc.
namespace {

struct TempYaml {
    std::filesystem::path path;
    explicit TempYaml(const std::string& content) {
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_strict_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};
} // namespace

// =============================================================================
// Default: strikt
// =============================================================================

TEST_CASE("strict-Modus ist Default fuer API-Parserv", "[strict][default]") {
    auto p = std::make_shared<ISOBaseParser>("Dflt", 0);
    p->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
    REQUIRE(p->strict() == true);

    IFA_LLCHAR fp(5, "field");
    REQUIRE(fp.strict() == true);
    fp.strict(false);
    REQUIRE(fp.strict() == false);
    fp.strict(true);
    REQUIRE(fp.strict() == true);
}

TEST_CASE("strict-Modus ist Default fuer loadFromYaml (ohne YAML-Key)", "[strict][default][spec]") {
    TempYaml y(R"(
spec: "StrictDefault"
encoding: ascii
fields:
  "000":
    format: numeric
    length: 4
    description: "MTI"
  "001":
    format: bitmap
    length: 8
    description: "Primary Bitmap"
  "002":
    format: llchar
    length: 5
    description: "Amount"
)");
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    REQUIRE(parser->strict() == true);
}

TEST_CASE("YAML-Key 'strict: false' schaltet strikten Modus ab", "[strict][spec]") {
    TempYaml y(R"(
spec: "StrictOff"
encoding: ascii
strict: false
fields:
  "000":
    format: numeric
    length: 4
    description: "MTI"
  "001":
    format: bitmap
    length: 8
    description: "Primary Bitmap"
  "002":
    format: llchar
    length: 5
    description: "Amount"
)");
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    REQUIRE(parser->strict() == false);

    // E2E: DE2Praefix sagt 5 Zeichen, nur 3 vorhanden.
    // - strict(false): Legacy (Kuerzung, kein Throw)
    // - Default (ohne Key): strikter Throw (s. naechster Test)
    auto frame = B({
        0x30, 0x31, 0x30, 0x30,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x35,                       // LL "05"
        0x31, 0x32, 0x33,                 // nur 3 Zeichen
    });
    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, frame));
}

TEST_CASE("YAML-Key fehlt -> strikter Throw auf abgschnittenem DE2", "[strict][spec]") {
    TempYaml y(R"(
spec: "StrictOn"
encoding: ascii
fields:
  "000":
    format: numeric
    length: 4
    description: "MTI"
  "001":
    format: bitmap
    length: 8
    description: "Primary Bitmap"
  "002":
    format: llchar
    length: 5
    description: "Amount"
)");
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    REQUIRE(parser->strict() == true);

    auto frame = B({
        0x30, 0x31, 0x30, 0x30,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x35,                       // LL "05"
        0x31, 0x32, 0x33,                 // nur 3 Zeichen
    });
    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    REQUIRE_THROWS_AS(msg->unparse(msg, frame), std::runtime_error);
}

// =============================================================================
// B1: abgeschnittene Felder (Nachrichten-Ebene)
// =============================================================================

TEST_CASE("Abgeschnittenes DE2: strict verwirft, legacy kuerzt", "[strict][B1]") {
    // Präfix sagt 5 Zeichen, nur 3 Daten-Bytes vorhanden.
    auto frame = B({
        0x30, 0x31, 0x30, 0x30,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x35,                       // LL "05"
        0x31, 0x32, 0x33,                 // nur 3 Zeichen
    });

    // strict (Default)
    {
        auto parser = make_ascii_parser();
        auto msg = std::make_shared< Message >();
        msg->parser(parser);
        REQUIRE_THROWS_AS(msg->unparse(msg, frame), std::runtime_error);
    }
    // legacy
    {
        auto parser = make_ascii_parser();
        parser->strict(false);
        auto msg = std::make_shared< Message >();
        msg->parser(parser);
        std::size_t consumed = 0;
        REQUIRE_NOTHROW(consumed = msg->unparse(msg, frame));
        CHECK(consumed == frame.size());
        auto de2 = msg->tryGet< OpaqueField >(2);
        REQUIRE(de2.has_value());
        CHECK((*de2)->value() == "123"); // auf 3 Zeichen gekürzt
    }
}

// =============================================================================
// B2/P2: überdimensionierte Serialisierung
// =============================================================================

TEST_CASE("Ueberdimensionierter Wert: strict verwirft, legacy laesst Feld aus", "[strict][B2]") {
    auto fill_msg = [](std::shared_ptr< ISOBaseParser > parser) {
        auto msg = std::make_shared< Message >();
        msg->parser(parser);
        auto mti = std::make_shared< OpaqueField >(Message::MTI_KEY);
        mti->value("0100");
        msg->set(mti);
        auto de2 = std::make_shared< OpaqueField >(2);
        de2->value("123456789"); // 9 Zeichen > Maximum 5
        msg->set(de2);
        return msg;
    };

    // strict (Default): Serialisierung verworfen
    {
        auto parser = make_ascii_parser();
        auto msg = fill_msg(parser);
        REQUIRE_THROWS_AS(msg->parse(msg), std::runtime_error);
    }
    // legacy: Feld wird ausgelassen, kein Throw
    {
        auto parser = make_ascii_parser();
        parser->strict(false);
        auto msg = fill_msg(parser);
        std::vector<uint8_t> bytes;
        REQUIRE_NOTHROW(bytes = msg->parse(msg));
        // mindestens MTI + Bitmap, das überdimensionierte Feld fehlt
        CHECK(bytes.size() >= 12);
    }
}

// =============================================================================
// P2: Rest-Bytes / leeres Byte-Image
// =============================================================================

TEST_CASE("Unkonvertierte Rest-Bytes: strict verwirft, legacy akzeptiert", "[strict][P2]") {
    auto frame = base_frame();
    frame.insert(frame.end(), { 0xFF, 0xFF, 0xFF }); // 3 Junk-Bytes am Ende

    // strict (Default)
    {
        auto parser = make_ascii_parser();
        auto msg = std::make_shared< Message >();
        msg->parser(parser);
        REQUIRE_THROWS_AS(msg->unparse(msg, frame), std::runtime_error);
    }
    // legacy
    {
        auto parser = make_ascii_parser();
        parser->strict(false);
        auto msg = std::make_shared< Message >();
        msg->parser(parser);
        std::size_t consumed = 0;
        REQUIRE_NOTHROW(consumed = msg->unparse(msg, frame));
        CHECK(consumed == base_frame().size()); // Junk-Bytes nicht konsumiert
    }
}

// =============================================================================
// Propagation: strikter Modus erreicht verschachtelte Sub-Parserv
// =============================================================================

TEST_CASE("strict propagiert auf nested Sub-Parserv", "[strict][nested]") {
    // Innere Sub-Nachricht: MTI fix4 + Bitmap + DE2 fix12 (ASCII).
    auto inner = std::make_shared<ISOBaseParser>("Inner", 0);
    inner->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
    inner->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
    inner->add(std::make_shared<IFA_NUMERIC>(12, "Amount"));

    // Außerer Hüll-Parser (L-präfix, binär) + nested Feld.
    using NestedParser = ISOFieldParser< ISOBaseParser,
        codec::Length::L, codec::PrefixEncoder::BINARY, codec::Encoder::BINARY, codec::Padder::NONE >;
    auto envelope = std::make_shared< IF_LBINARY >(32, "Envelope");
    auto nested   = std::make_shared< NestedParser >(envelope, "NestedField");
    nested->subParser(inner);

    auto outer = std::make_shared<ISOBaseParser>("Outer", 0);
    outer->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
    outer->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
    outer->add(nested);

    // Frame: äusser MTI "0100", Bitmap (nur DE2), DE2 = 0x16 (22) +
    // innere Nachricht mit nur 10 Ziffern statt 12 (abgeschnittenes inneres DE2).
    auto frame = B({
        0x30, 0x31, 0x30, 0x30,                       // MTI "0100"
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Bitmap: DE2
        0x16,                                          // DE2-Länge 22
        // innere Nachricht (22 Bytes):
        0x30, 0x31, 0x30, 0x30,                       // inner MTI "0100"
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // inner Bitmap: DE2
        0x30, 0x30, 0x30, 0x30, 0x30,                 // inner DE2: 10 von 12
        0x30, 0x30, 0x30, 0x31, 0x30,
    });

    // strict (Default): das abgeschnittene INNERE Feld wird verworfen -
    // nur möglich, weil strict() bis in den Sub-Parser propagiert.
    {
        REQUIRE(outer->strict() == true);
        auto msg = std::make_shared< Message >();
        msg->parser(outer);
        bool threw = false;
        std::string what;
        try {
            msg->unparse(msg, frame);
        } catch (const std::runtime_error& e) {
            threw = true;
            what = e.what();
        }
        REQUIRE(threw);
        CHECK(what.find("DE002") != std::string::npos);
    }

    // legacy: strict(false) am äusseren Parser erreicht auch den Sub-Parser.
    {
        outer->strict(false);
        REQUIRE(outer->strict() == false);
        REQUIRE(inner->strict() == false); // Propagation verifiziert
        auto msg = std::make_shared< Message >();
        msg->parser(outer);
        std::size_t consumed = 0;
        REQUIRE_NOTHROW(consumed = msg->unparse(msg, frame));
        CHECK(consumed == frame.size());
    }
}