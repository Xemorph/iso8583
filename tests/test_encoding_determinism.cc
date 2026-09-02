// =============================================================================
// Encoding determinism (Phase 2) — Der EBCDIC-Codec ist voll tabellenbasiert
// und gegen das ICU-78.3-Orakel gepinnt.
//
// 256-Byte-Sweeps in beiden Richtungen:
//   * nicht-strikt: as<> mappt Whitelist-Aussehen-Bytes auf den '.'-Sentinel
//     (0x2E), to<> auf 0x6F ('?') — jedes Byte wird 1:1 konvertiert,
//     es wird NIE geworfen.
//   * strict: es wird genau und nur auf Whitelist-Verstoesse ein
//     std::runtime_error geworfen.
//   * Die gecheckten Tabellen stimmen fuer alle Whitelist-akzeptierten Bytes
//     mit den gepinnten ICU-78.3-Orakel-Verdicts ueberein
//     (tools/generate_ebcdic_tables/pinned/*.json). Das Orakel konvertiert
//     alle 256 Bytes (inkl. C1-/Binaerbytes); die Whitelist ist bewusst
//     strenger — abgelehnte Bytes sind dokumentierte Abweichungen.
//
// Hermetisch: nur ISO8583_SOURCE_DIR wird benoetigt (Pinned-JSON). Kein
// Laufzeit-Converter (iconv/ICU) ist an der Bibliothek beteiligt.
// =============================================================================

#include <iso8583/iso8583.h>
#include <iso8583/_codec.hh>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace TNG_NAMESPACE;
using namespace TNG_NAMESPACE::codec;

namespace {

// Liest das gepinnte ICU-78.3-Orakel-Verdict-JSON und prueft die Grundform.
nlohmann::json load_oracle(const char* name, const char* expected_converter)
{
    const std::filesystem::path p =
        std::filesystem::path(ISO8583_SOURCE_DIR) / "tools" / "generate_ebcdic_tables" / "pinned" / name;
    REQUIRE(std::filesystem::exists(p));
    std::ifstream f(p);
    REQUIRE(static_cast<bool>(f));
    nlohmann::json j = nlohmann::json::parse(f);
    REQUIRE(j.contains("converter"));
    REQUIRE(j["converter"].get<std::string>() == expected_converter);
    REQUIRE(j.contains("bytes"));
    REQUIRE(j["bytes"].size() == 256);
    return j;
}

// E2A: ein Byte, nicht-strikt (1:1-Mapping, nie werfend).
char e2a_nonstrict(uint8_t b)
{
    std::vector<uint8_t> in = { b };
    const std::string out = as<std::string, Encoder::EBCDIC>(in, 0, 1, false);
    return out.empty() ? '\0' : out[0];
}

// E2A: ein Byte, strikt (true = std::runtime_error geworfen).
bool e2a_strict_throws(uint8_t b)
{
    std::vector<uint8_t> in = { b };
    try {
        (void)as<std::string, Encoder::EBCDIC>(in, 0, 1, true);
        return false;
    }
    catch (const std::runtime_error&) {
        return true;
    }
}

// A2E: ein Zeichen, nicht-strikt (1:1-Mapping inkl. 0x6F-Fallback).
uint8_t a2e_nonstrict(uint8_t c)
{
    std::string in(1, static_cast<char>(c));
    std::vector<uint8_t> out(1, 0xFF);
    to<Encoder::EBCDIC>(in, out, 0, false);
    return out[0];
}

// A2E: ein Zeichen, strikt (true = std::runtime_error geworfen).
bool a2e_strict_throws(uint8_t c)
{
    std::string in(1, static_cast<char>(c));
    std::vector<uint8_t> out(1, 0xFF);
    try {
        to<Encoder::EBCDIC>(in, out, 0, true);
        return false;
    }
    catch (const std::runtime_error&) {
        return true;
    }
}

} // namespace

TEST_CASE("Encoding determinism - E2A (EBCDIC->ASCII) sweep matches pinned ICU 78.3 oracle verdicts",
    "[encoding-determinism][ebcdic][codec]")
{
    const auto oracle = load_oracle("icu_verdicts_e2a.json", "IBM-1047 -> US-ASCII");

    std::size_t valid_count = 0;
    for (int b = 0; b < 256; ++b)
    {
        const bool valid = kEbcdicValid[static_cast<std::size_t>(b)];
        if (valid) ++valid_count;

        // Nicht-strikt: Whitelist-Byte -> Tabellenspiegelung, sonst '.'-Sentinel.
        const char got = e2a_nonstrict(static_cast<uint8_t>(b));
        const char expect = valid ? static_cast<char>(kEbcdicToAscii[b]) : '.';
        CHECK(got == expect);

        // Das ICU-Orakel konvertiert alle 256 EBCDIC-Bytes (inkl. C1/Binaer):
        const auto& entry = oracle["bytes"][b];
        REQUIRE(entry["ok"].get<bool>());
        if (valid)
        {
            // Fuer Whitelist-akzeptierte Bytes stimmen Tabelle und Orakel exakt.
            CHECK(entry["out"].get<int>() == static_cast<unsigned char>(expect));
        }
    }

    // Pin: 85 Whitelist-Bytes (bewusst strenger als das Orakel).
    CHECK(valid_count == 85);
}

TEST_CASE("Encoding determinism - E2A strict mode rejects exactly the non-whitelist bytes",
    "[encoding-determinism][ebcdic][strict]")
{
    std::size_t rejected = 0;
    for (int b = 0; b < 256; ++b)
    {
        const bool valid = kEbcdicValid[static_cast<std::size_t>(b)];
        CHECK(e2a_strict_throws(static_cast<uint8_t>(b)) == !valid);
        if (!valid) ++rejected;
    }
    CHECK(rejected == 256 - 85);
}

TEST_CASE("Encoding determinism - A2E (ASCII->EBCDIC) sweep matches pinned ICU 78.3 oracle verdicts",
    "[encoding-determinism][ebcdic][codec]")
{
    const auto oracle = load_oracle("icu_verdicts_a2e.json", "US-ASCII -> IBM-1047");

    std::size_t mappable_count = 0;
    for (int c = 0; c < 256; ++c)
    {
        const uint8_t uc = static_cast<uint8_t>(c);

        // Nicht-strikt: mappbares Zeichen -> Tabellenwert, sonst 0x6F-Fallback.
        const uint8_t got = a2e_nonstrict(uc);
        const uint8_t expect = kAsciiToEbcdic[uc];
        CHECK(got == expect);

        const bool mappable = expect != 0x6F;
        if (mappable) ++mappable_count;

        // Das ICU-Orakel mappt alle ASCII-Zeichen (u.a. '?' 0x3F -> 0x73):
        const auto& entry = oracle["bytes"][c];
        REQUIRE(entry["ok"].get<bool>());
        if (mappable)
            CHECK(entry["out"].get<int>() == static_cast<int>(expect));
    }

    // Pin: 84 mappbare ASCII-Zeichen (172 fallen auf 0x6F zurueck).
    CHECK(mappable_count == 84);

    // Dokumentierte Ausnahme: '?' (0x3F) ist ohne Tabellen-Mapping (Fallback
    // 0x6F) auch im strict-Modus immer erlaubt (s. to<>, c != '?'-Klausel).
    CHECK(a2e_nonstrict('?') == 0x6F);
    CHECK_FALSE(a2e_strict_throws('?'));
}

TEST_CASE("Encoding determinism - A2E strict mode rejects exactly the unmappable characters",
    "[encoding-determinism][ebcdic][strict]")
{
    std::size_t rejected = 0;
    for (int c = 0; c < 256; ++c)
    {
        const uint8_t uc = static_cast<uint8_t>(c);
        // '?' (0x3F) ist die einzige dokumentierte strict-Ausnahme: es hat
        // keinen Tabellen-Mapping (Fallback 0x6F), wird aber nie verworfen.
        const bool rejectable = kAsciiToEbcdic[uc] == 0x6F && uc != '?';
        CHECK(a2e_strict_throws(uc) == rejectable);
        if (rejectable) ++rejected;
    }
    CHECK(rejected == 256 - 84 - 1); // 171 (172 Fallbacks minus der '?'-Ausnahme)
}