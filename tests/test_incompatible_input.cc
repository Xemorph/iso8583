// =============================================================================
// Regressions-Tests: Decode von mit der Spec inkompatiblen Nachrichten
// =============================================================================
// Hintergrund (issues/a): Die GMC/DMSA-Testnachricht (EBCDIC/IBM-1047)
// enthielt in DE006 "Amount, Cardholder Billing" (NUMERIC|EBCDIC, fix 12)
// Bytes, die das vcpkg-libiconv-Modul (IBM-1047) in der Debug-Umgebung
// mit EILSEQ ablehnt – u.a. weil 0x24 im NUMERIC-Kontext und die
// Sequenz als Ganzes (inkl. C1-Steuerbyte 0x10) kein gültiger numerischer
// EBCDIC-Wert ist. libiconv meldet dies mit EILSEQ – MSVCs strerror()
// kennt den POSIX-Wert nicht und liefert "unknown error". Vor dem Fix
// entwich dadurch ein nacktes std::system_error (was() == "unknown error")
// aus der Bibliothek und beendete die Anwendung als uncaught C++-Exception
// (SEH 0xe06d7363).
//
// Hinweis: Das EILSEQ-Verhalten ist build-spezifisch (der vcpkg-Debug-
// libiconv verhält sich anders als Release-Builds desselben Quellenstands).
// Die Tests unten sind deshalb umweltbewusst formuliert (siehe Test 1).
//
// Gefordertes Verhalten (darf nicht mehr regressieren):
//   * Die Bibliothek crasht bei KEINEM Input.
//   * Das Dekodieren inkompatibler Nachrichten wirft ein sauberes
//     std::runtime_error (die Exceptions-Konvention der Bibliothek) mit
//     Positions-Information (Feld-Schlüssel, absoluter Offset, Ursache) –
//     niemals ein nacktes std::system_error.
//
// Umgebungs- und Fixture-Wächter: Die Tests sind so formuliert, dass sie
// in jeder Umgebung grün bleiben:
//   * Die Fixture-Dateien liegen im lokalen (nicht im Repository
//     commiteten) Ordner issues/a/ — fehlt er, springen die
//     Fixture-Tests (2, 3) über; der hermetische Test (1) ist immer aktiv.
//   * Das libiconv-EILSEQ-Verhalten ist build-spezifisch — jeder Test
//     prüft erst über codec::as<…, EBCDIC>, ob die Giftsequenz in der
//     lokalen Umgebung überhaupt abgelehnt wird.
//
// Hinweis: Diese Datei gehört in der Source-Liste von tests/CMakeLists.txt
// vor test_e2e_full_message.cc (siehe Kommentar dort).
// =============================================================================

// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/iso8583.h>
#include <iso8583/_codec.hh>
// [tng/internal]
#include "_parser.hh"
#include "fmt_types.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#include <string>
#include <typeinfo>

using namespace TNG_NAMESPACE;

namespace {

std::vector<uint8_t> B(std::initializer_list<uint8_t> il) {
    return std::vector<uint8_t>(il);
}

// Gesammelte Fakten zu einer (ggf. geworfenen) unparse()-Ausnahme.
struct Escaped {
    bool threw = false;                 // Hat unparse() geworfen?
    bool exact_runtime_error = false;   // Dynamischer Typ == std::runtime_error?
    std::string what;                   // what() der Ausnahme
};

// Führt msg->unparse(msg, buf) aus und fängt jede std::exception.
// Nicht-std::exception-Abbrüche (z.B. roher SEH/Crash) können nicht gefangen
// werden – genau das ist das Versagen, das diese Tests ausschließen sollen.
Escaped capture_unparse(const std::shared_ptr<ISOMessage>& msg,
                        const std::vector<uint8_t>& buf)
{
    Escaped r;
    try {
        msg->unparse(msg, buf);
    } catch (const std::runtime_error& e) {
        r.threw = true;
        r.exact_runtime_error = (typeid(e) == typeid(std::runtime_error));
        r.what = e.what();
    } catch (const std::exception& e) {
        // std::system_error leitet von std::runtime_error ab und wird oben
        // gefangen (mit exact_runtime_error == false); jede andere
        // Ausnahme-Art landet hier und gilt als Konventions-Verstoß.
        r.threw = true;
        r.what = e.what();
    }
    return r;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

// =============================================================================
// Test 1: EBCDIC-Feld mit binären Bytes → positionierter Fehler
// =============================================================================
// Minimal-Parser ohne YAML: MTI + Bitmap + DE2 (NUMERIC|EBCDIC, fix 12).
// DE2 enthält die reale DE006-Giftsequenz der issues/a-Nachricht
// (f8 10 24 85 9b 46 a2 3d e3 c6 81 8b), die in der vcpkg-libiconv
// (IBM-1047) dieser Umgebungen mit EILSEQ abgelehnt wird. Die Bibliothek
// muss daraus ein sauberes, positioniertes std::runtime_error werden lassen.
//
// Umwelt-Wächter: Der EILSEQ-Befund hängt vom jeweils eingebauten libiconv-
// Build ab (der vcpkg-Debug-Build verhält sich hier anders als Release-
// Builds desselben Quellenstands). Damit die Suite auch in libiconv-
// Umgebungen grün bleibt, die die Sequenz korrekt konvertieren, prüft der
// Test zuerst über die öffentliche codec-API, ob die Sequenz in der lokalen
// Umgebung überhaupt abgelehnt wird – andernfalls wird er abgebrochen.
TEST_CASE("Incompatible input - EBCDIC field with binary bytes throws positioned error",
    "[incompatible-input][ebcdic]")
{
    // Die reale DE006-Sequenz aus issues/a/test_message.txt (IBM-1047-Bytes).
    const std::string poison{'\xF8','\x10','\x24','\x85','\x9B','\x46',
                             '\xA2','\x3D','\xE3','\xC6','\x81','\x8B'};

    // Umwelt-Wächter: Konvertiert das lokale libiconv die Sequenz ab?
    bool env_rejects = false;
    try {
        const std::vector<uint8_t> pb{poison.begin(), poison.end()};
        codec::as<std::string, codec::Encoder::EBCDIC>(pb, 0, pb.size());
    } catch (const std::exception&) {
        env_rejects = true;
    }
    if (!env_rejects) {
        INFO("Lokale libiconv-Umgebung konvertiert die Sequenz; EILSEQ-Test nicht anwendbar.");
        return;
    }

    auto parser = std::make_shared<ISOBaseParser>("IncompatibleTest", 0);
    parser->add(std::make_shared<IFE_NUMERIC>(4, "MTI"));        // Slot 0
    parser->add(std::make_shared<IFB_BITMAP>(8, "Bitmap"));      // Slot 1
    parser->add(std::make_shared<IFE_NUMERIC>(12, "Amount"));    // Slot 2 (DE2)

    const auto buf = B({
        0xF0, 0xF1, 0xF0, 0xF0,                          // MTI: 0100 (EBCDIC)
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Bitmap: DE2 aktiv
        0xF8, 0x10, 0x24, 0x85, 0x9B, 0x46,              // DE2 (Giftsequenz, 1. Hälfte)
        0xA2, 0x3D, 0xE3, 0xC6, 0x81, 0x8B               // DE2 (Giftsequenz, 2. Hälfte)
    });

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    const Escaped r = capture_unparse(msg, buf);
    REQUIRE(r.threw);
    REQUIRE(r.exact_runtime_error);  // keine std::system_error-Entweichung

    // Positions-Information und Ursache müssen in der Meldung stehen:
    CHECK(r.what.find("[ISO8583] DE002") != std::string::npos);
    CHECK(r.what.find("@ Offset 12") != std::string::npos);   // 4 (MTI) + 8 (Bitmap)
    CHECK(r.what.find("EBCDIC->ASCII") != std::string::npos);
    CHECK(r.what.find("EILSEQ") != std::string::npos);
}

// =============================================================================
// Test 2: Fixture issues/a – GMC/DMSA-Spec + Testnachricht
// =============================================================================
// End-to-end-Reproduktion des Original-Vorfalls: Die Nachricht enthält in
// DE006 "Amount, Cardholder Billing" die Bytes
// f8 10 24 85 9b 46 a2 3d e3 c6 81 8b – in libiconv-Umgebungen, die diese
// Sequenz mit EILSEQ ablehnen (vcpkg-Debug-Build), muss das Dekodieren mit
// einem sauberen, positionierten std::runtime_error enden (vor dem Fix:
// uncaught std::system_error "unknown error" → Prozess-Abbruch). In
// Umgebungen, in denen die Sequenz korrekt konvertiert wird, genügt die
// Eigenschaft "crasht nicht und wirft nur saubere [ISO8583]-Fehler".
TEST_CASE("Incompatible input - issues/a fixture (GMC/DMSA EBCDIC message)",
    "[incompatible-input][fixture]")
{
    const std::string spec_path = std::string(ISO8583_SOURCE_DIR) + "/issues/a/schemes/gmc_dmsa.yml";
    const std::string hex_path  = std::string(ISO8583_SOURCE_DIR) + "/issues/a/test_message.txt";

    // Fixture-Wächter: issues/ ist ein lokaler Ordner (nicht im Repo).
    if (!std::filesystem::exists(spec_path) || !std::filesystem::exists(hex_path)) {
        INFO("issues/a-Fixture fehlt (lokal nur); Test wird übersprungen.");
        return;
    }

    auto parser = spec::SpecDecoder::loadFromYaml(spec_path);

    // HEX-Datei laden (alle Hex-Zeichen, Paare -> Bytes)
    std::ifstream in(hex_path);
    REQUIRE(in.good());
    std::string digits;
    std::string line;
    while (std::getline(in, line))
        for (char c : line)
            if (hexval(c) >= 0)
                digits.push_back(c);
    REQUIRE(digits.size() % 2 == 0);
    std::vector<uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < digits.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>((hexval(digits[i]) << 4) | hexval(digits[i + 1])));
    REQUIRE(bytes.size() == 383);  // Länge der dokumentierten Testnachricht

    auto msg = std::make_shared<ISOMessage>();
    msg->parser(parser);

    const Escaped r = capture_unparse(msg, bytes);

    // Umwelt-Wächter: Der dokumentierte EILSEQ-Befund gilt für libiconv-
    // Builds, die die DE006-Sequenz ablehnen (vcpkg-Debug-Build).
    const std::string poison{'\xF8','\x10','\x24','\x85','\x9B','\x46',
                             '\xA2','\x3D','\xE3','\xC6','\x81','\x8B'};
    bool env_rejects = false;
    try {
        const std::vector<uint8_t> pb{poison.begin(), poison.end()};
        codec::as<std::string, codec::Encoder::EBCDIC>(pb, 0, pb.size());
    } catch (const std::exception&) {
        env_rejects = true;
    }

    if (env_rejects) {
        // Dokumentierter Vorfall: DE006 wird mit EILSEQ abgelehnt → die
        // Bibliothek muss einen sauberen, positionierten Fehler werfen.
        REQUIRE(r.threw);
        REQUIRE(r.exact_runtime_error);  // keine std::system_error-Entweichung
        CHECK(r.what.find("[ISO8583] DE006") != std::string::npos);
        CHECK(r.what.find("@ Offset 152") != std::string::npos);
        CHECK(r.what.find("EBCDIC->ASCII") != std::string::npos);
        CHECK(r.what.find("EILSEQ") != std::string::npos);
    } else {
        INFO("Lokale libiconv-Umgebung konvertiert die DE006-Sequenz; "
             "es genügt: kein Crash und – falls ein Fehler – ein sauberer [ISO8583]-Fehler.");
        if (r.threw) {
            REQUIRE(r.exact_runtime_error);
            CHECK(r.what.find("[ISO8583]") != std::string::npos);
        }
    }
}

// =============================================================================
// Test 3: Puffer kürzer als Header → sauberer Fehler statt out-of-bounds-Read
// =============================================================================
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

// =============================================================================
// Test 4 (A3): Header-Guards – zu kurze Byte-Bilder werden fail-closed
// verworfen, Offset-Zugriffe auf verkleinerte Header werfen sauber.
// =============================================================================
// Hermetisch (keine issues/a-Fixture nötig), Matrix WLP-FO + BASE1:
//   a) unparse() mit Puffer kürzer als Header → sauberer [ISO8583]-Fehler
//   b) From-Bytes-Konstruktor mit falscher Größe → Fail-fast im Konstruktor
//   c) Getter/Setter auf verkleinertem Header (öffentliche Member `header`)
//      → fail-closed-Throw statt out-of-bounds-Zugriff
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

    // ── c) Verteidigung in der Tiefe (Klasse sind final, Member geschützt) ──
    // Die Getter-/Setter-Guards (`wlp_fo_ensure`/`base1_ensure`) sind über die
    // öffentliche API heute nicht direkt anreicherbar: die Klassen sind final
    // und das Byte-Bild ist protected. Sie schützen gegen künftige API-Änderungen
    // (z. B. öffentliches Byte-Bild, lockere unpack()) - abgedeckt durch Code-Review
    // und die A3-Regressionen in test_base1_header.cc/test_wlp_fo_header.cc.
    // Hier genügen die Wire-Level-Guards (a) und der Fail-Fast-Konstruktor (b).
}