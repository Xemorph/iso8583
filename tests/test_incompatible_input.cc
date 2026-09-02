// =============================================================================
// test_incompatible_input.cc
// [ISO8583] Regressionstests für das incident aus issues/a (unbehandeltes
// std::system_error aus der EBCDIC-Konvertierung in 0.2.x; damals gelöst
// durch den sauber umgeworfenen iconv-Fehlerpfad in _codec.cc).
//
// Seit Phase 2 (0.3.0) ist der EBCDIC-Codec VOLL TABELLENBASIERT
// (IBM-1047-Lookup-Tabellen, vom ICU-78.3-Orakel verifiziert, s.
// tools/generate_ebcdic_tables). Damit ist die Ablehnung von
// Whitelist-Ausßen-Bytes auf JEDEM Toolchain identisch deterministisch –
// die früheren libiconv-Umgebungsguards (EILSEQ je nach Build-Variante)
// sind damit überflüssig geworden und entfernt. Die Tests asserten den
// Determinismus direkt:
//
//   * strict (Default von Parser/Spec): ungültige EBCDIC-Bytes werfen ein
//     sauberes, positioniertes std::runtime_error (kein system_error,
//     kein uncaught throw).
//   * strict:false: Legacy-Verhalten – alle 256 Bytes werden 1:1 gemappt
//     ('.'-Füllung), kein Throw.
//
// Der Test mit echter Nachricht (Test 2) lädt das reproduzierbare
// E2E-Artefakt aus issues/a (Einzug 2026-07; lokal, nicht im Repo-Tracking):
// issues/a/test_message.txt (Einzug, hex) + issues/a/schemes/gmc_dmsa.yml (Spec).
// Wenn die issues/a-Artefakte nicht vorhanden sind (frisches Checkout ohne
// issues/a), wird dieser Test übersprungen.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// [tng]
#include <iso8583/iso8583.h>
#include <iso8583/_codec.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

using namespace TNG_NAMESPACE;

// ----------------------------------------------------------------------------
// Hilfswerkzeuge
// ----------------------------------------------------------------------------

static std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// Wandelt eine Hex-Zeile (z.B. "30323030...") in Bytes um.
static std::vector<uint8_t> fromHex(std::string_view hex)
{
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        const auto nib = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
            return 0;
        };
        out.push_back(static_cast<uint8_t>((nib(hex[i]) << 4) | nib(hex[i + 1])));
    }
    return out;
}

// Fängt den unparse()-Aufruf ab und klassifiziert die Exception:
//   threw=false                     → kein Fehler
//   threw + exact_runtime_error     → sauberes std::runtime_error (erwartet)
//   threw + !exact_runtime_error    → fremde Exception (Fail-Loud-Aussage!)
struct Escaped
{
    bool threw = false;
    bool exact_runtime_error = false;
    std::string what;
};

// Führt msg->unparse(msg, buf) aus und fängt jede std::exception. Nicht-
// std::exception-Abbrüche (z.B. roher SEH/Crash) können nicht gefangen
// werden – genau das ist das Versagen, das diese Tests ausschließen sollen.
static Escaped capture_unparse(const std::shared_ptr<ISOMessage>& msg,
                                const std::vector<uint8_t>& buf)
{
    Escaped e;
    try
    {
        (void)msg->unparse(msg, buf);
    }
    catch (const std::runtime_error& r)
    {
        e.threw = true;
        e.exact_runtime_error = !dynamic_cast<const std::logic_error*>(&r);
        e.what = r.what();
    }
    catch (const std::exception& x)
    {
        e.threw = true;
        e.exact_runtime_error = false;
        e.what = std::string("fremde Exception-Typ: ") + x.what();
    }
    return e;
}

// ----------------------------------------------------------------------------
// Test 1 – Deterministische Codec-/Parser-Ablehnung (ohne Fixture)
// ----------------------------------------------------------------------------
// Gift-Sequenz des incidents (DE006-Content):
//   f8 10 24 85 9b 46 a2 3d e3 c6 81 8b  (12 Bytes)
// Seit Phase 2 ist der Codec-Pfad tabellenbasiert: Bytes außerhalb der
// IBM-1047-Whitelist (hier: 0x10, 0x24, 0x81, 0x85, 0x9B, 0x8b, …) werden
// deterministisch abgelehnt – unabhängig von Toolchain, Build-Konfiguration
// und installierten Converter-Modulen.
TEST_CASE("Incompatible input - EBCDIC field with binary bytes throws positioned error",
    "[incompatible-input][ebcdic]")
{
    const std::string poison{'\xF8','\x10','\x24','\x85','\x9B','\x46',
                             '\xA2','\x3D','\xE3','\xC6','\x81','\x8B'};

    // (a) Codec-Ebene – Determinismus in beiden Modi:
    const std::vector<uint8_t> pb{poison.begin(), poison.end()};

    // strict (rejectInvalid=true): erstes Whitelist-Ausßen-Byte (0x10 an
    // Position 1) → positioniertes std::runtime_error.
    bool strict_threw = false;
    try
    {
        codec::as<std::string, codec::Encoder::EBCDIC>(pb, 0, pb.size(), true);
    }
    catch (const std::runtime_error& e)
    {
        strict_threw = (std::string(e.what()).find("EBCDIC->ASCII") != std::string::npos);
    }
    REQUIRE(strict_threw);

    // non-strict: Legacy-'.'-Mapping – alle 256 Werte sind 1:1 abbildbar,
    // kein Throw, Länge bleibt 1:1.
    const auto legacy = codec::as<std::string, codec::Encoder::EBCDIC>(pb, 0, pb.size());
    REQUIRE(legacy.size() == pb.size());

    // (b) Parser-Ebene (strict-Default): sauberes, positioniertes
    // std::runtime_error mit Feldkontext – kein uncaught system_error mehr.
    auto parser = std::make_shared<ISOBaseParser>("IncompatibleTest", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));        // Slot 0
    parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));      // Slot 1
    parser->add(std::make_shared<IFE_NUMERIC>(12, "Amount"));    // Slot 2 (DE2)

    const auto buf = B({
        0xF0, 0xF2, 0xF0, 0xF0,  // MTI 0200 (EBCDIC-Ziffern)
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Bitmap: DE2 gesetzt
        0xF8, 0x10, 0x24, 0x85, 0x9B, 0x46,  // Gift-Bytes 1–6 (DE2)
        0xA2, 0x3D, 0xE3, 0xC6, 0x81, 0x8B,  // Gift-Bytes 7–12 (DE2)
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    const Escaped r = capture_unparse(msg, buf);
    REQUIRE(r.threw);
    REQUIRE(r.exact_runtime_error);
    CHECK(r.what.find("[ISO8583] DE002") != std::string::npos);
    CHECK(r.what.find("@ Offset 12") != std::string::npos);
    CHECK(r.what.find("EBCDIC->ASCII") != std::string::npos);
    CHECK(r.what.find("strict-Modus") != std::string::npos);

    // (c) strict:false → Legacy-Verhalten: kein Throw, Feld wird 1:1 gemappt.
    parser->strict(false);
    auto msg2 = std::make_shared<ISOMessage>();
    msg2->parser(parser);
    const Escaped r2 = capture_unparse(msg2, buf);
    REQUIRE_FALSE(r2.threw);
}

// ----------------------------------------------------------------------------
// Test 2 – E2E mit echter Nachricht (issues/a)
// ----------------------------------------------------------------------------
// Die Nachricht enthält mehrere Whitelist-Ausßen-Bytes: das erste ist 0xAB
// in DE004 ("Amount, Transaction", Position 10 des Feldes), dazu die
// historische DE006-Giftsequenz f8 10 24 85 9b 46 a2 3d e3 c6 81 8b des
// issues/a-Crashs. Seit Phase 2 deterministisch abgelehnt: der strict-Default
// der Spec wirft einen sauberen, positionierten std::runtime_error am ersten
// ungültigen Byte (hier DE004 @ Offset 140) – auf jeder Plattform.
TEST_CASE("Incompatible input - E2E issues/a message rejected with clean error",
    "[incompatible-input][e2e]")
{
    const std::filesystem::path base = std::filesystem::path(ISO8583_SOURCE_DIR) / "issues" / "a";
    const std::filesystem::path hex_path = base / "test_message.txt";
    const std::filesystem::path spec_path = base / "schemes" / "gmc_dmsa.yml";

    if (!std::filesystem::exists(hex_path) || !std::filesystem::exists(spec_path))
    {
        WARN("issues/a-Artefakte nicht vorhanden – E2E-Test übersprungen.");
        return;
    }

    // Nachricht aus test_message.txt laden
    std::ifstream in(hex_path);
    REQUIRE(in.is_open());
    std::string line;
    std::getline(in, line);
    std::vector<uint8_t> bytes = fromHex(line);
    REQUIRE(bytes.size() >= 256);

    auto parser = spec::SpecDecoder::loadFromYaml(spec_path.string());
    REQUIRE(parser != nullptr);

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    const Escaped r = capture_unparse(msg, bytes);

    // Deterministische Strict-Ablehnung (tabellenbasiert, kein iconv):
    REQUIRE(r.threw);
    REQUIRE(r.exact_runtime_error);
    CHECK(r.what.find("[ISO8583] DE004") != std::string::npos);
    CHECK(r.what.find("@ Offset 140") != std::string::npos);
    CHECK(r.what.find("EBCDIC->ASCII") != std::string::npos);
    CHECK(r.what.find("strict-Modus") != std::string::npos);
}

// ----------------------------------------------------------------------------
// Test 3 – Puffer kürzer als Header → sauberer Fehler statt OOB-Read
// ----------------------------------------------------------------------------
TEST_CASE("Incompatible input - buffer shorter than header throws clean error",
    "[incompatible-input][header]")
{
    const std::string spec_path = std::string(ISO8583_SOURCE_DIR) + "/issues/a/schemes/gmc_dmsa.yml";
    if (!std::filesystem::exists(spec_path)) {
        INFO("issues/a-Fixture fehlt (lokal nur); Test wird übersprungen.");
        return;
    }
    auto parser = spec::SpecDecoder::loadFromYaml(spec_path);  // header = 93 Bytes

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    std::vector<uint8_t> tiny(50, 0xF0);  // kürzer als der 93-Byte-Header
    const Escaped r = capture_unparse(msg, tiny);
    REQUIRE(r.threw);
    REQUIRE(r.exact_runtime_error);
    CHECK(r.what.find("[ISO8583]") != std::string::npos);
}

// ----------------------------------------------------------------------------
// Test 4 (A3) – Header-Guards: zu kurze Byte-Bilder werden fail-closed
// verworfen, Offset-Zugriffe auf verkleinerte Header werfen sauber.
// ----------------------------------------------------------------------------
// Hermetisch (keine issues/a-Fixture nötig), Matrix WLP-FO + BASE1:
//   a) unparse() mit Puffer kürzer als Header → sauberer [ISO8583]-Fehler
//   b) From-Bytes-Konstruktor mit falscher Größe → Fail-fast im Konstruktor
//   (Getter/Setter-Guards sind über die öffentliche API nicht erreichbar –
//    Klassen final, Byte-Bild protected; Verteidigung in der Tiefe, s.
//    Code-Kommentar unten.)
TEST_CASE("Incompatible input - header guards (WLP-FO + BASE1) reject truncated byte images",
    "[incompatible-input][header][wlp_fo][base1]")
{
    // ── a) unparse() mit Puffer kürzer als Header (WLP-FO: 93, BASE1: 22) ──
    {
        auto parser = std::make_shared<ISOBaseParser>("A3-WlpShort", WLP_FOHeader::LENGTH);
        parser->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
        parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
        parser->add(std::make_shared<IFE_CHAR>(10, "DE2"));

        auto msg = std::make_shared<ISOMessage>();
        msg->parser(parser);
        std::shared_ptr<ISOHeader> hdr = std::make_shared<WLP_FOHeader>();
        msg->header(hdr);

        std::vector<uint8_t> tiny(50, 0x40);  // kürzer als 93 Bytes
        const Escaped r = capture_unparse(msg, tiny);
        REQUIRE(r.threw);
        REQUIRE(r.exact_runtime_error);
        CHECK(r.what.find("[ISO8583]") != std::string::npos);
        CHECK(r.what.find("zu kurz") != std::string::npos);
    }
    {
        auto parser = std::make_shared<ISOBaseParser>("A3-Base1Short", BASE1Header::LENGTH);
        parser->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));
        parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));
        parser->add(std::make_shared<IFE_CHAR>(10, "DE2"));

        auto msg = std::make_shared<ISOMessage>();
        msg->parser(parser);
        std::shared_ptr<ISOHeader> hdr = std::make_shared<BASE1Header>("123456", "654321");
        msg->header(hdr);

        std::vector<uint8_t> tiny(10, 0x40);  // kürzer als 22 Bytes
        const Escaped r = capture_unparse(msg, tiny);
        REQUIRE(r.threw);
        REQUIRE(r.exact_runtime_error);
        CHECK(r.what.find("[ISO8583]") != std::string::npos);
        CHECK(r.what.find("zu kurz") != std::string::npos);
    }

    // ── b) From-Bytes-Konstruktor mit falscher Größe → Fail-fast ────────────
    {
        bool threw_wlp = false;
        try {
            std::vector<uint8_t> wrong(WLP_FOHeader::LENGTH, 0x40);  // 93 statt 89
            WLP_FOHeader h(wrong);
        } catch (const std::runtime_error& e) {
            threw_wlp = (std::string(e.what()).find("[ISO8583]") != std::string::npos);
        }
        CHECK(threw_wlp);

        bool threw_base1 = false;
        try {
            std::vector<uint8_t> wrong(10, 0x40);  // < 22 Bytes
            BASE1Header h(wrong);
        } catch (const std::runtime_error& e) {
            threw_base1 = (std::string(e.what()).find("[ISO8583]") != std::string::npos);
        }
        CHECK(threw_base1);
    }

    // ── c) Verteidigung in der Tiefe (Klassen sind final, Member geschützt) ──
    // Die Getter-/Setter-Guards (`wlp_fo_ensure`/`base1_ensure`) sind über die
    // öffentliche API heute nicht direkt anreicherbar: die Klassen sind final
    // und das Byte-Bild ist protected. Sie schützen gegen künftige API-Änderungen
    // (z. B. öffentliches Byte-Bild, lockere unpack()) - abgedeckt durch Code-Review
    // und die A3-Regressionen in test_base1_header.cc/test_wlp_fo_header.cc.
    // Hier genügen die Wire-Level-Guards (a) und der Fail-Fast-Konstruktor (b).
}

// ----------------------------------------------------------------------------
// Test 5 – ASCII-Path bleibt unberührt (Regression)
// ----------------------------------------------------------------------------
TEST_CASE("Incompatible input - ASCII fields with arbitrary bytes still parse",
    "[incompatible-input][ascii]")
{
    // ASCII-Encoder hat keine EBCDIC-Whitelist – beliebige Bytes werden
    // 1:1 übernommen (kein Umlaut-/EBCDIC-Verhalten).
    auto parser = std::make_shared<ISOBaseParser>("AsciiPathTest", 0);
    parser->add(std::make_shared<IFA_NUMERIC>(4, "MTI"));   // Slot 0 (ASCII)
    parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap")); // Slot 1
    parser->add(std::make_shared<IFA_NUMERIC>(12, "Amount")); // Slot 2 (ASCII)

    const auto buf = B({
        0x30, 0x32, 0x30, 0x30,  // MTI 0200
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Bitmap: DE2
        // Beliebige Bytes (inkl. Steuer-/Binärbytes) – der ASCII-Codec
        // kennt keine Whitelist und übernimmt alles 1:1:
        0x00, 0x10, 0x24, 0x85, 0x9B, 0x46,
        0xA2, 0x3D, 0xE3, 0xC6, 0x81, 0xFF,
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    // Muss ohne Exception durchlaufen (ASCII toleriert alle Bytes).
    const Escaped r = capture_unparse(msg, buf);
    REQUIRE_FALSE(r.threw);
}