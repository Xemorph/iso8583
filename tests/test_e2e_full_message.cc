// =============================================================================
// test_e2e_full_message.cc - Der "ultimative" End-to-End-Test
// =============================================================================
//
// Baut komplette, realistische ISO-8583-Autorisierungsanfragen Byte für Byte
// zusammen (MTI, Bitmap, Standardfelder) und lässt sie über den kompletten
// Stack laufen: YAML-Spec laden -> Wire-Bytes dekodieren -> Felder prüfen ->
// wieder serialisieren -> Byte-für-Byte-Vergleich mit dem Original.
//
// Drei Szenarien, jeweils mit dem passenden TLV-Parser für das Zusatzfeld:
//   1) BMP 55 - BER-TLV (ISO/IEC 8825-1)   -> format: lllbertlv (BERTLVParser)
//   2) BMP 48 - Mastercard-TLV             -> tlv: {tag_bytes:2,len_bytes:2,tcc:true}
//   3) BMP 48 - Visa-TLV                   -> tlv: {tag_bytes:2,len_bytes:1,tcc:false,encoding:bcd}
//
// Bewusst in einer eigenen Datei UND als letzter Eintrag in
// tests/CMakeLists.txt gehalten: Catch2 registriert TEST_CASEs in der
// Reihenfolge, in der ihre statischen Registrar-Objekte initialisiert werden.
// Das ist über Translation-Units hinweg vom Standard her nicht garantiert,
// folgt in der Praxis bei allen gängigen Toolchains (GCC/Clang/MSVC) aber der
// Reihenfolge, in der die Objektdateien dem Linker übergeben werden - also der
// Reihenfolge der Quelldateien in add_executable(). Wer eine harte Garantie
// braucht: `libiso8583_tests "[e2e]"` läuft als expliziter, isolierter
// Abschlusslauf nach der vollen Suite.

#include <catch2/catch_test_macros.hpp>

#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>
#include <iso8583/_codec.hh>
#include <iso8583/ISOUtils.hh>
// [stdc++]
#include <filesystem>
#include <fstream>
#include <thread>

using namespace TNG_NAMESPACE;
using TNG_NAMESPACE::utils::makeBitmap;

namespace {

    // Schreibt eine temporäre YAML-Datei und gibt den Pfad zurück (RAII-Cleanup
    // beim Verlassen des Scopes). Eigenständige, minimale Variante des
    // TempYaml-Helfers aus test_spec_loader.cc - bewusst lokal in dieser Datei
    // gehalten statt geteilt, damit test_e2e_full_message.cc unabhängig von
    // den anderen Testdateien lesbar bleibt.
    struct E2ETempYaml {
        std::filesystem::path path;

        explicit E2ETempYaml(const std::string& content) {
            path = std::filesystem::temp_directory_path()
                / ("libiso8583_e2e_test_" +
                   std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                   "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".yml");
            std::ofstream f(path);
            f << content;
        }

        ~E2ETempYaml() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            std::filesystem::remove(path.string() + ".smap", ec);
        }

        std::string str() const { return path.string(); }
    };

    std::vector<uint8_t> ascii_b(const std::string& s) {
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    std::vector<uint8_t> ebcdic_b(const std::string& s) {
        std::vector<uint8_t> buf(s.size());
        codec::to<codec::Encoder::EBCDIC>(s, buf, 0);
        return buf;
    }

    void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& part) {
        out.insert(out.end(), part.begin(), part.end());
    }

} // namespace

// =============================================================================
// Szenario 1: BMP 55 - BER-TLV (EMV ICC-Daten)
// =============================================================================

TEST_CASE("E2E - Authorization Request with BMP 55 (BER-TLV)", "[e2e][ber]") {
    E2ETempYaml yaml(R"(
spec: "E2E BER-TLV Auth Request"
encoding: ascii

fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "002": { format: llchar,   length: 19, description: "PAN" }
  "003": { format: numeric,  length: 6,  description: "Processing Code" }
  "004": { format: numeric,  length: 12, description: "Amount" }
  "011": { format: numeric,  length: 6,  description: "STAN" }
  "049": { format: numeric,  length: 3,  description: "Currency Code" }
  "055":
    format: lllbertlv
    length: 999
    description: "ICC Data"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // ── Wire-Buffer aufbauen ─────────────────────────────────────────────────
    std::vector<uint8_t> raw;
    append(raw, ascii_b("0100"));                          // MTI: Auth Request
    append(raw, makeBitmap({ 2, 3, 4, 11, 49, 55 }));       // Bitmap

    append(raw, ascii_b("16"));                             // DE2: LL-Prefix
    append(raw, ascii_b("4111111111111111"));               // DE2: PAN
    append(raw, ascii_b("000000"));                          // DE3: Proc. Code
    append(raw, ascii_b("000000012345"));                    // DE4: Amount
    append(raw, ascii_b("000042"));                          // DE11: STAN
    append(raw, ascii_b("978"));                             // DE49: Currency (EUR)

    // DE55: BER-TLV-Payload - Tag 0x1F22 (Mehrbyte-Form), Länge 4 (Short-Form).
    // Mit ISO8583_BERTLV wären auch reale EMV-Tags wie 9F26 als Message-Key
    // möglich (siehe test_tlv_parser.cc) - hier bewusst ein Tag-Wert, der
    // unabhängig vom Build (int16_t oder int32_t Keys) funktioniert, damit
    // dieser E2E-Test in jeder Konfiguration läuft.
    const std::vector<uint8_t> icc_data{ 0x01, 0x02, 0x03, 0x04 };
    std::vector<uint8_t> de55_payload{ 0x1F, 0x22, 0x04 };
    append(de55_payload, icc_data);
    append(raw, ascii_b("007"));                             // DE55: LLL-Prefix (7 Bytes Payload)
    append(raw, de55_payload);

    // ── Dekodieren ───────────────────────────────────────────────────────────
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto consumed = msg->unparse(msg, raw);
    REQUIRE(consumed == raw.size());

    CHECK(msg->hasMTI());
    CHECK(msg->mti() == "0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());

    CHECK(msg->get<OpaqueField>(2)->value() == "4111111111111111");
    CHECK(msg->get<OpaqueField>(3)->value() == "000000");
    CHECK(msg->get<OpaqueField>(4)->value() == "000000012345");
    CHECK(msg->get<OpaqueField>(11)->value() == "000042");
    CHECK(msg->get<OpaqueField>(49)->value() == "978");

    auto de55 = msg->get<Message>(55);
    REQUIRE(de55 != nullptr);
    auto se = de55->get<BinaryField>(0x1F22);
    REQUIRE(se != nullptr);
    CHECK(se->value() == icc_data);

    // ── Roundtrip: erneutes Serialisieren muss das Original reproduzieren ────
    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// Szenario 2: BMP 48 - Mastercard-TLV
// =============================================================================

TEST_CASE("E2E - Authorization Request with BMP 48 (Mastercard-TLV)", "[e2e][tlv][mc]") {
    E2ETempYaml yaml(R"(
spec: "E2E Mastercard TLV Auth Request"
encoding: ebcdic

fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "002": { format: llchar,   length: 19, description: "PAN" }
  "003": { format: numeric,  length: 6,  description: "Processing Code" }
  "004": { format: numeric,  length: 12, description: "Amount" }
  "011": { format: numeric,  length: 6,  description: "STAN" }
  "048":
    format: lllbinary
    length: 999
    description: "Additional Data - Private Use"
    tlv:
      tag_bytes: 2
      len_bytes: 2
      tcc: true
    children:
      "72":
        format: binary
        length: 50
        description: "SE72 - Some Mastercard Sub-Element"
  "049": { format: numeric,  length: 3,  description: "Currency Code" }
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // ── Wire-Buffer aufbauen (alles EBCDIC außer den TLV-Rohdaten) ───────────
    std::vector<uint8_t> raw;
    append(raw, ebcdic_b("0100"));                          // MTI
    append(raw, makeBitmap({ 2, 3, 4, 11, 48, 49 }));        // Bitmap

    append(raw, ebcdic_b("16"));
    append(raw, ebcdic_b("5555555555554444"));               // DE2: PAN
    append(raw, ebcdic_b("000000"));                          // DE3
    append(raw, ebcdic_b("000000098765"));                    // DE4
    append(raw, ebcdic_b("000123"));                          // DE11

    // DE48: TCC='P' + SE72 (TAG="72" EBCDIC, LEN="04" EBCDIC, 4 rohe Bytes)
    const std::vector<uint8_t> se72_data{ 0xDE, 0xAD, 0xBE, 0xEF };
    std::vector<uint8_t> de48_payload;
    append(de48_payload, ebcdic_b("P"));
    append(de48_payload, ebcdic_b("72"));
    append(de48_payload, ebcdic_b("04"));
    append(de48_payload, se72_data);
    append(raw, ebcdic_b("009"));                             // LLL-Prefix (9 Bytes Payload)
    append(raw, de48_payload);

    append(raw, ebcdic_b("978"));                             // DE49

    // ── Dekodieren ───────────────────────────────────────────────────────────
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto consumed = msg->unparse(msg, raw);
    REQUIRE(consumed == raw.size());

    CHECK(msg->mti() == "0100");
    CHECK(msg->isAuthorization());

    CHECK(msg->get<OpaqueField>(2)->value() == "5555555555554444");
    CHECK(msg->get<OpaqueField>(3)->value() == "000000");
    CHECK(msg->get<OpaqueField>(4)->value() == "000000098765");
    CHECK(msg->get<OpaqueField>(11)->value() == "000123");
    CHECK(msg->get<OpaqueField>(49)->value() == "978");

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);

    constexpr TNG_KEY_TYPE TCC_KEY = -2;
    auto tcc = de48->get<OpaqueField>(TCC_KEY);
    REQUIRE(tcc != nullptr);
    CHECK(tcc->value() == "P");

    auto se72 = de48->get<BinaryField>(72);
    REQUIRE(se72 != nullptr);
    CHECK(se72->value() == se72_data);

    // ── Roundtrip: erneutes Serialisieren muss das Original reproduzieren ────
    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// Szenario 3: BMP 48 - Visa-TLV
// =============================================================================

TEST_CASE("E2E - Authorization Request with BMP 48 (Visa-TLV)", "[e2e][tlv][visa]") {
    E2ETempYaml yaml(R"(
spec: "E2E Visa TLV Auth Request"
encoding: ascii

fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "002": { format: llchar,   length: 19, description: "PAN" }
  "003": { format: numeric,  length: 6,  description: "Processing Code" }
  "004": { format: numeric,  length: 12, description: "Amount" }
  "011": { format: numeric,  length: 6,  description: "STAN" }
  "048":
    format: lllbinary
    length: 999
    description: "Visa Private Use Fields"
    tlv:
      tag_bytes: 2
      len_bytes: 1
      tcc: false
      encoding: bcd
    children:
      "10":
        format: binary
        length: 20
        description: "Some Visa Sub-Element"
  "049": { format: numeric,  length: 3,  description: "Currency Code" }
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // ── Wire-Buffer aufbauen ─────────────────────────────────────────────────
    std::vector<uint8_t> raw;
    append(raw, ascii_b("0200"));                            // MTI: Financial Request
    append(raw, makeBitmap({ 2, 3, 4, 11, 48, 49 }));          // Bitmap

    append(raw, ascii_b("16"));
    append(raw, ascii_b("4000000000000002"));                 // DE2: PAN
    append(raw, ascii_b("000000"));                            // DE3
    append(raw, ascii_b("000000005000"));                       // DE4
    append(raw, ascii_b("000002"));                             // DE11

    // DE48: Visa-Style TLV - kein TCC, Tag/Länge BCD-kodiert.
    // Tag "0010" (2 BCD-Bytes: 0x00,0x10), Länge 4 (1 BCD-Byte: 0x04), 4 rohe Bytes.
    const std::vector<uint8_t> se10_data{ 0x11, 0x22, 0x33, 0x44 };
    std::vector<uint8_t> de48_payload{ 0x00, 0x10, 0x04 };
    append(de48_payload, se10_data);
    append(raw, ascii_b("007"));                              // LLL-Prefix (7 Bytes Payload)
    append(raw, de48_payload);

    append(raw, ascii_b("840"));                              // DE49 (USD)

    // ── Dekodieren ───────────────────────────────────────────────────────────
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto consumed = msg->unparse(msg, raw);
    REQUIRE(consumed == raw.size());

    CHECK(msg->mti() == "0200");
    CHECK(msg->isFinancial());
    CHECK(msg->isRequest());

    CHECK(msg->get<OpaqueField>(2)->value() == "4000000000000002");
    CHECK(msg->get<OpaqueField>(3)->value() == "000000");
    CHECK(msg->get<OpaqueField>(4)->value() == "000000005000");
    CHECK(msg->get<OpaqueField>(11)->value() == "000002");
    CHECK(msg->get<OpaqueField>(49)->value() == "840");

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);

    // Visa-Style: kein TCC-Feld vorhanden
    constexpr TNG_KEY_TYPE TCC_KEY = -2;
    CHECK(de48->get<OpaqueField>(TCC_KEY) == nullptr);

    auto se10 = de48->get<BinaryField>(10);
    REQUIRE(se10 != nullptr);
    CHECK(se10->value() == se10_data);

    // ── Roundtrip: erneutes Serialisieren muss das Original reproduzieren ────
    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// Szenario 4: Tertiäre Bitmap (DE > 128)
// =============================================================================
//
// Zeigt, dass die auf 192 erweiterte utils::makeBitmap() korrekt mit dem
// kompletten Stack zusammenspielt: Felder jenseits DE128 erzwingen eine
// 24-Byte-Bitmap mit gesetztem Secondary- UND Tertiary-Präsenzbit.

TEST_CASE("E2E - Authorization Request with fields > DE128 (tertiary bitmap)", "[e2e][bitmap192]") {
    E2ETempYaml yaml(R"(
spec: "E2E Tertiary Bitmap Test"
encoding: ascii

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 24 }
  "002": { format: llchar,  length: 19, description: "PAN" }
  "003": { format: numeric, length: 6,  description: "Processing Code" }
  "011": { format: numeric, length: 6,  description: "STAN" }
  "140": { format: numeric, length: 4,  description: "Some Field beyond DE128" }
  "190": { format: numeric, length: 2,  description: "Another Field beyond DE128" }
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // makeBitmap() erkennt automatisch, dass DE140/DE190 eine 24-Byte-Bitmap
    // erfordern, und setzt Byte0-Bit7 (Secondary) UND Byte8-Bit7 (Tertiary).
    const auto bmp = makeBitmap({ 2, 3, 11, 140, 190 });
    REQUIRE(bmp.size() == 24);
    CHECK((bmp[0] & 0x80) == 0x80); // Secondary Bitmap Present
    CHECK((bmp[8] & 0x80) == 0x80); // Tertiary Bitmap Present

    std::vector<uint8_t> raw;
    append(raw, ascii_b("0100"));
    append(raw, bmp);
    append(raw, ascii_b("16"));
    append(raw, ascii_b("4111111111111111"));   // DE2
    append(raw, ascii_b("000000"));              // DE3
    append(raw, ascii_b("000042"));              // DE11
    append(raw, ascii_b("9999"));                // DE140
    append(raw, ascii_b("42"));                  // DE190

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    const auto consumed = msg->unparse(msg, raw);
    REQUIRE(consumed == raw.size());

    CHECK(msg->get<OpaqueField>(2)->value() == "4111111111111111");
    CHECK(msg->get<OpaqueField>(3)->value() == "000000");
    CHECK(msg->get<OpaqueField>(11)->value() == "000042");
    CHECK(msg->get<OpaqueField>(140)->value() == "9999");
    CHECK(msg->get<OpaqueField>(190)->value() == "42");

    // ── keys() liefert exakt die gesetzten DE-Nummern zurück (sortiert,
    //    ohne die internen Sentinels -1/-2) - und daraus lässt sich die
    //    identische Bitmap ohne manuelle Buchführung neu ableiten.
    CHECK(msg->keys() == std::vector<TNG_KEY_TYPE>{ 0, 2, 3, 11, 140, 190 });
    CHECK(makeBitmap(*msg) == bmp);

    // ── Roundtrip: erneutes Serialisieren muss das Original reproduzieren ────
    CHECK(msg->parse(msg) == raw);
}
