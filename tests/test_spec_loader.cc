// [catch2]
#include <catch2/catch_test_macros.hpp>
// [catch2/string_matchers]
#include <catch2/matchers/catch_matchers_string.hpp>

// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>
// [tng/internal]
#include "_tlv.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

// Schreibt eine temporaere YAML-Datei und gibt den Pfad zurueck.
// Die Datei wird nach dem Test automatisch geloescht (RAII).
struct TempYaml {
    std::filesystem::path path;

    explicit TempYaml(const std::string& content) {
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".yml");
        std::ofstream f(path);
        f << content;
    }

    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.string(); }
};

// Verzeichnis mit mehreren Dateien für Multi-File Tests (SourceMap etc.)
struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        dir = std::filesystem::temp_directory_path()
            / ("libiso8583_dir_" + std::to_string(
                std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // Schreibt eine Datei ins Verzeichnis, gibt absoluten Pfad zurück.
    std::string write(const std::string& filename, const std::string& content) {
        auto p = dir / filename;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p.string();
    }
};

// Minimale YAML-Spec:
//   MTI (EBCDIC, fix 4)
//   Bitmap (binary, fix 8)
//   DE2: LLCHAR EBCDIC (PAN, max 19)
//   DE11: NUMERIC EBCDIC (STAN, fix 6)
static constexpr const char* MINIMAL_SPEC = R"(
spec: "Test Spec"
encoding: ebcdic

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
    length: 19
    description: "PAN"
  "011":
    format: numeric
    length: 6
    description: "STAN"
)";

// =============================================================================
// SpecDecoder: loadFromYaml
// =============================================================================

TEST_CASE("SpecDecoder - loads minimal spec without throwing", "[spec][load]") {
    TempYaml yaml(MINIMAL_SPEC);
    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(yaml.str()));
    REQUIRE(parser != nullptr);
}

TEST_CASE("SpecDecoder - throws on missing file", "[spec][load][error]") {
    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml("/nonexistent/path/test.yml"));
}

TEST_CASE("SpecDecoder - throws on invalid YAML", "[spec][load][error]") {
    TempYaml yaml("this: is: not: valid: yaml: ::::");
    REQUIRE_THROWS(spec::SpecDecoder::loadFromYaml(yaml.str()));
}

// =============================================================================
// SpecDecoder: end-to-end unparse
// =============================================================================

TEST_CASE("SpecDecoder - end-to-end unparse with EBCDIC fields", "[spec][unparse]") {
    TempYaml yaml(MINIMAL_SPEC);
    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // MTI:    EBCDIC "0100" = F0 F1 F0 F0
    // Bitmap: DE2 (bit 2) + DE11 (bit 11)
    //   Bit  2 = Byte 0, bit 6 = 0x40
    //   Bit 11 = Byte 1, bit 3 = 0x20
    //   => 0x40, 0x20, 0x00 ...
    // DE2:    EBCDIC LL-Prefix "16" + 16 EBCDIC-Ziffern (PAN)
    //   Prefix: F1 F6 (="16")
    //   Daten:  F4 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 F1 (="4111111111111111")
    // DE11:   EBCDIC "000001" = F0 F0 F0 F0 F0 F1
    auto buf = std::vector<uint8_t>({
        // MTI
        0xF0, 0xF1, 0xF0, 0xF0,
        // Bitmap
        0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // DE2: EBCDIC LL="16" + PAN "4111111111111111"
        0xF1, 0xF6,
        0xF4, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1, 0xF1,
        // DE11: EBCDIC "000001"
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1,
        });

    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    std::size_t consumed = msg->unparse(msg, buf);

    CHECK(consumed == buf.size());
    CHECK(msg->mti() == "0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());

    auto de2 = msg->get< OpaqueField >(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");

    auto de11 = msg->get< OpaqueField >(11);
    REQUIRE(de11 != nullptr);
    CHECK(de11->value() == "000001");
}

TEST_CASE("SpecDecoder - per-field encoding overrides global", "[spec][encoding]") {
    // Globales Encoding: ebcdic
    // DE2 hat eigenes encoding: bcd
    TempYaml yaml(R"(
spec: "Mixed Encoding Spec"
encoding: ebcdic

fields:
  "000":
    format: numeric
    length: 4
    description: "MTI"
  "001":
    format: bitmap
    length: 8
    description: "Bitmap"
  "002":
    format: numeric
    encoding: bcd
    length: 16
    description: "PAN [BCD]"
  "004":
    format: numeric
    length: 12
    description: "Amount [EBCDIC]"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    auto buf = std::vector<uint8_t>({
        // MTI: EBCDIC "0200"
        0xF0, 0xF2, 0xF0, 0xF0,
        // Bitmap: DE2 + DE4 aktiv
        0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // DE2: BCD "4111111111111111" (8 Bytes)
        0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        // DE4: EBCDIC "000000010000" (12 Bytes)
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF1, 0xF0, 0xF0, 0xF0, 0xF0,
        });

    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    msg->unparse(msg, buf);

    auto de2 = msg->get< OpaqueField >(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");  // BCD-kodiert

    auto de4 = msg->get< OpaqueField >(4);
    REQUIRE(de4 != nullptr);
    CHECK(de4->value() == "000000010000");  // EBCDIC-kodiert
}

TEST_CASE("SpecDecoder - binary field is encoding-neutral", "[spec][encoding][binary]") {
    TempYaml yaml(R"(
spec: "Binary Field Spec"
encoding: ebcdic

fields:
  "000":
    format: numeric
    length: 4
    description: "MTI"
  "001":
    format: bitmap
    length: 8
    description: "Bitmap"
  "052":
    format: binary
    length: 8
    description: "PIN Block"
)");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // Bitmap: DE52 aktiv = Bit 52
    // Bit 52 = Byte 6 (0-basiert), Bit 4 von links = 0x10
    auto buf = std::vector<uint8_t>({
        0xF0, 0xF1, 0xF0, 0xF0,                          // MTI: 0100
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,  // Bitmap: DE52
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, // DE52: 8 raw bytes
        });

    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    msg->unparse(msg, buf);

    auto de52 = msg->get< BinaryField >(52);
    REQUIRE(de52 != nullptr);
    CHECK(de52->value() == std::vector<uint8_t>({ 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE }));
}

// =============================================================================
// TLV nested field via YAML
// =============================================================================

TEST_CASE("SpecDecoder - TLV nested field Mastercard style", "[spec][tlv]") {
    TempYaml yaml(R"(
spec: "TLV Test"
encoding: ebcdic

fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "048":
    format: lllbinary
    length: 999
    description: "Additional Data"
    tlv:
      tag_bytes: 2
      len_bytes: 2
      tcc: true
    children:
      "72":
        format: binary
        length: 50
        description: "SE72"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(yaml.str()));
    REQUIRE(parser != nullptr);

    auto ebcdic_b = [](const std::string& s) {
        std::vector<uint8_t> buf(s.size());
        codec::to<codec::Encoder::EBCDIC>(s, buf, 0);
        return buf;
        };

    // ── Wire-Buffer aufbauen ─────────────────────────────────────────────────
    // MTI: "0200" (4 Bytes EBCDIC)
    // Bitmap: DE48 aktiv
    //   DE48 = Bit 48 (1-indexed)
    //   Byte = (48-1) / 8 = 5
    //   Bit  = 7 - (47 % 8) = 7 - 7 = 0  → Byte[5] |= 0x01
    // DE048 payload: TCC='P' + SE72 TAG="72" LEN="04" DATA=4 raw Bytes
    //   payload size = 1 + 2 + 2 + 4 = 9 Bytes
    //   LLL-Prefix: EBCDIC "009"

    std::vector<uint8_t> raw;

    // MTI
    auto mti_bytes = ebcdic_b("0200");
    raw.insert(raw.end(), mti_bytes.begin(), mti_bytes.end());

    // Bitmap (8 raw Bytes, nicht EBCDIC-kodiert)
    std::vector<uint8_t> bmp(8, 0x00);
    bmp[5] = 0x01u;  // DE48
    raw.insert(raw.end(), bmp.begin(), bmp.end());

    // DE048: LLL-Prefix + Payload
    auto lll = ebcdic_b("009");           // 9 Bytes Payload
    auto tcc = ebcdic_b("P");             // TCC
    auto tag72 = ebcdic_b("72");            // SE-Nummer 72
    auto len04 = ebcdic_b("04");            // 4 Bytes Daten
    const std::vector<uint8_t> data = { 0xDE, 0xAD, 0xBE, 0xEF };

    raw.insert(raw.end(), lll.begin(), lll.end());
    raw.insert(raw.end(), tcc.begin(), tcc.end());
    raw.insert(raw.end(), tag72.begin(), tag72.end());
    raw.insert(raw.end(), len04.begin(), len04.end());
    raw.insert(raw.end(), data.begin(), data.end());

    // Total: 4 + 8 + 3 + 1 + 2 + 2 + 4 = 24 Bytes
    REQUIRE(raw.size() == 24);

    // ── Dekodieren ───────────────────────────────────────────────────────────
    auto msg = std::make_shared< Message >();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    // DE048 muss als ISOMessage (composite) vorhanden sein
    auto de48 = msg->get< Message >(48);
    REQUIRE(de48 != nullptr);

    // TCC prüfen (gespeichert unter TCC_KEY = -2)
    constexpr TNG_KEY_TYPE TCC_KEY = -2;
    auto tcc_field = de48->get< OpaqueField >(TCC_KEY);
    REQUIRE(tcc_field != nullptr);
    CHECK(tcc_field->value() == "P");

    // SE72 prüfen
    auto se72 = de48->get< BinaryField >(72);
    REQUIRE(se72 != nullptr);
    CHECK(se72->value() == data);
}

TEST_CASE("SpecDecoder - TLV nested field BER-TLV style", "[spec][tlv][ber]") {
    TempYaml yaml(R"(
spec: "BER-TLV Test"
encoding: ascii

fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "055":
    format: lllbinary
    length: 999
    description: "ICC Data"
    tlv:
      ber: true
    children:
      "26":
        format: binary
        length: 50
        description: "Application Cryptogram"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(yaml.str()));
    REQUIRE(parser != nullptr);

    auto ascii_b = [](const std::string& s) {
        return std::vector<uint8_t>(s.begin(), s.end());
        };

    // ── Wire-Buffer aufbauen ─────────────────────────────────────────────────
    // MTI "0200" (ASCII), Bitmap mit DE55 aktiv (Bit 55 → Byte 6, Bit 1: da
    // (55-1)/8 = 6, 7-((55-1)%8) = 7-6 = 1 → Byte[6] |= 0x02).
    // DE055 payload: 1 SE mit Tag 0x1A (1 Byte, low5 != 0x1F), Länge 3
    // (Short-Form), 3 Datenbytes.
    std::vector<uint8_t> raw;

    auto mti_bytes = ascii_b("0200");
    raw.insert(raw.end(), mti_bytes.begin(), mti_bytes.end());

    std::vector<uint8_t> bmp(8, 0x00);
    bmp[6] = 0x02u; // DE55
    raw.insert(raw.end(), bmp.begin(), bmp.end());

    const std::vector<uint8_t> tlv_payload{ 0x1A, 0x03, 0xDE, 0xAD, 0xBE };
    auto lll = ascii_b("005"); // 5 Bytes Payload
    raw.insert(raw.end(), lll.begin(), lll.end());
    raw.insert(raw.end(), tlv_payload.begin(), tlv_payload.end());

    // ── Dekodieren ───────────────────────────────────────────────────────────
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de55 = msg->get<Message>(55);
    REQUIRE(de55 != nullptr);

    auto se = de55->get<BinaryField>(0x1A);
    REQUIRE(se != nullptr);
    CHECK(se->value() == std::vector<uint8_t>{0xDE, 0xAD, 0xBE});

    // ── Roundtrip: erneutes Serialisieren muss den TLV-Payload reproduzieren ─
    auto tlv_parser = std::dynamic_pointer_cast<ISOBaseParser>(parser)
        ->field_parser(55)->subParser();
    REQUIRE(tlv_parser != nullptr);
    CHECK(tlv_parser->parse(de55) == tlv_payload);
}

TEST_CASE("SpecDecoder - format: lllbertlv shorthand (no type/children/tlv needed)", "[spec][tlv][ber]") {
    // Reine Kurzschreibweise: kein 'type: nested', kein 'children:', kein
    // 'tlv:'-Block - 'format: lllbertlv' allein genügt (siehe parseSpecField).
    TempYaml yaml(R"(
spec: "BER-TLV Shorthand Test"
encoding: ascii

fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "055":
    format: lllbertlv
    length: 999
    description: "ICC Data"
)");

    std::shared_ptr<ISOParserPtrBase> parser;
    REQUIRE_NOTHROW(parser = spec::SpecDecoder::loadFromYaml(yaml.str()));
    REQUIRE(parser != nullptr);

    auto ascii_b = [](const std::string& s) {
        return std::vector<uint8_t>(s.begin(), s.end());
        };

    std::vector<uint8_t> raw;
    raw.insert(raw.end(), { '0','2','0','0' }); // MTI

    std::vector<uint8_t> bmp(8, 0x00);
    bmp[6] = 0x02u; // DE55
    raw.insert(raw.end(), bmp.begin(), bmp.end());

    const std::vector<uint8_t> tlv_payload{ 0x1A, 0x02, 0xAB, 0xCD };
    auto lll = ascii_b("004");
    raw.insert(raw.end(), lll.begin(), lll.end());
    raw.insert(raw.end(), tlv_payload.begin(), tlv_payload.end());

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de55 = msg->get<Message>(55);
    REQUIRE(de55 != nullptr);
    auto se = de55->get<BinaryField>(0x1A);
    REQUIRE(se != nullptr);
    CHECK(se->value() == std::vector<uint8_t>{0xAB, 0xCD});
}

TEST_CASE("Error - format: bertlv combined with children is rejected", "[error][spec][ber]") {
    TempYaml yaml(R"(
spec: "Invalid"
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap, length: 8 }
  "055":
    format: lllbertlv
    length: 999
    children:
      - format: numeric
        length: 1
)");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(yaml.str()),
        Catch::Matchers::ContainsSubstring("scalaren Feldern"));
}

TEST_CASE("Error - format: bertlv combined with tlv block is rejected", "[error][spec][ber]") {
    TempYaml yaml(R"(
spec: "Invalid"
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap, length: 8 }
  "055":
    format: lllbertlv
    length: 999
    tlv:
      tag_bytes: 2
      len_bytes: 2
)");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(yaml.str()),
        Catch::Matchers::ContainsSubstring("scalaren Feldern"));
}

TEST_CASE("Error - format: bertlv combined with type: nested is rejected", "[error][spec][ber]") {
    TempYaml yaml(R"(
spec: "Invalid"
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap, length: 8 }
  "055":
    type: nested
    format: lllbertlv
    length: 999
)");
    CHECK_THROWS_WITH(spec::SpecDecoder::loadFromYaml(yaml.str()),
        Catch::Matchers::ContainsSubstring("scalaren Feldern"));
}

// =============================================================================
// Fehlermeldungs-Tests: verifizieren dass Fehler die richtigen
// Datei/Zeile/Spalte-Angaben enthalten
// =============================================================================

// Hilfsfunktion: wirft die Exception ab und gibt what() zurück
static std::string load_error(const std::string& yaml_text) {
    TempYaml y(yaml_text);
    try {
        spec::SpecDecoder::loadFromYaml(y.str());
        return "";
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

// -- Preprocessor-Fehler ------------------------------------------------------

TEST_CASE("Error - !use unknown definition", "[error][preprocessor]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000": !use nonexistent_key
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("nonexistent_key") != std::string::npos);
    CHECK(msg.find("unbekannte Definition") != std::string::npos);
}

TEST_CASE("Error - !include_files missing file", "[error][preprocessor]") {
    TempYaml y(R"(
!include_files
- does_not_exist.yml

spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
)");
    REQUIRE_THROWS_WITH(
        spec::SpecDecoder::loadFromYaml(y.str()),
        Catch::Matchers::ContainsSubstring("does_not_exist.yml"));
}

TEST_CASE("Error - !template invalid expression", "[error][preprocessor]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002": !template INVALID(FORMAT
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("INVALID") != std::string::npos);
}

TEST_CASE("Error - !template length exceeds prefix maximum", "[error][preprocessor]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002": !template LL(CHAR, 100)
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("100") != std::string::npos);
    CHECK(msg.find("99") != std::string::npos);
}

// -- Validierungs-Fehler (validateSpecYaml) -----------------------------------

TEST_CASE("Error - missing fields section", "[error][validation]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("fields") != std::string::npos);
}

TEST_CASE("Error - non-numeric field key", "[error][validation]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "abc":
    format: numeric
    length: 4
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("abc") != std::string::npos);
    CHECK(msg.find("numerisch") != std::string::npos);
}

TEST_CASE("Error - children must be sequence or map", "[error][validation]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "003":
    format: binary
    length: 6
    children: "not a list"
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("children") != std::string::npos);
}

TEST_CASE("Error - TLV block missing tag_bytes", "[error][validation]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "048":
    format: lllbinary
    length: 999
    tlv:
      len_bytes: 2
    children:
      "72":
        format: binary
        length: 50
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("tag_bytes") != std::string::npos);
}

// -- Feld-Fehler (parseSpecField) ---------------------------------------------

TEST_CASE("Error - negative length", "[error][field]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002":
    format: llchar
    length: -1
    description: "PAN"
)");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("-1") != std::string::npos);
    CHECK(msg.find("0 sein") != std::string::npos);
}

// -- Sourcemap: Fehlermeldung enthält Datei + Zeile ---------------------------

TEST_CASE("Error - sourcemap location in message after !use", "[error][sourcemap]") {
    // Spec definiert eine Definition in schemes/defs.yml und referenziert sie.
    // Wenn in der Definition ein Fehler steckt, soll die Fehlermeldung
    // auf die Original-Datei und -Zeile zeigen – nicht auf die prozessierte.

    TempDir dir;
    dir.write("defs.yml", R"(
definitions:
  bad_field:
    format: llchar
    length: -5
    description: "Broken"
)");

    auto spec_path = dir.write("invalid.yml", R"(
!include_files
- defs.yml

spec: "SourceMap Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002": 
    !use bad_field
)");

    std::string msg;
    try {
        spec::SpecDecoder::loadFromYaml(spec_path);
    }
    catch (const std::exception& e) {
        msg = e.what();
    }

    REQUIRE_FALSE(msg.empty());
    INFO("Error message: " << msg);
    // Fehlermeldung muss die Original-Datei nennen
    CHECK(msg.find("defs.yml") != std::string::npos);
    // Und den Fehler selbst
    CHECK(msg.find("-5") != std::string::npos);
}

TEST_CASE("Error - sourcemap location for non-numeric key", "[error][sourcemap]") {
    // Fehlerhafte Zeile soll auf die originale Stelle in der Spec zeigen
    TempDir dir;
    auto spec_path = dir.write("spec.yml", R"(
spec: "SourceMap Key Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "abc":
    format: char
    length: 3
)");

    std::string msg;
    try {
        spec::SpecDecoder::loadFromYaml(spec_path);
    }
    catch (const std::exception& e) {
        msg = e.what();
    }

    REQUIRE_FALSE(msg.empty());
    INFO("Error message: " << msg);
    CHECK(msg.find("abc") != std::string::npos);
    // Zeilennummer muss vorhanden sein (entweder als Datei:Zeile oder als "Zeile N")
    CHECK((msg.find("spec.yml") != std::string::npos ||
        msg.find("Zeile") != std::string::npos));
}

TEST_CASE("Error - unknown encoding combination", "[error][codec]") {
    const auto msg = load_error(R"(
spec: "Test"
encoding: EBCIDIC
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002":
    format: llchar
    length: 19
    description: "PAN"
)");
    REQUIRE_FALSE(msg.empty());
    INFO("Error: " << msg);
    CHECK(msg.find("EBCIDIC") != std::string::npos);
    CHECK(msg.find("Encoding") != std::string::npos);
}

// -- SourceMap Persistenz (save/load/hash) ------------------------------------

TEST_CASE("SourceMap - sidecar written next to spec file", "[sourcemap][persistence]") {
    TempDir dir;
    auto spec_path = dir.write("mc.yml", R"(
spec: "Sidecar Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
  "002":
    format: llchar
    length: 19
    description: "PAN"
)");

    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));

    // Sidecar muss neben der Spec liegen
    const std::string smap_path = spec_path + ".smap";
    CHECK(std::filesystem::exists(smap_path));
}

TEST_CASE("SourceMap - sidecar reused on second load", "[sourcemap][persistence]") {
    TempDir dir;
    auto spec_path = dir.write("mc.yml", R"(
spec: "Sidecar Reuse Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
)");

    // Erstes Laden erzeugt Sidecar
    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
    const std::string smap_path = spec_path + ".smap";
    REQUIRE(std::filesystem::exists(smap_path));

    // Zeitstempel der Sidecar merken
    const auto t1 = std::filesystem::last_write_time(smap_path);

    // Kurz warten damit Zeitstempel unterschiedlich wären
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Zweites Laden soll die Sidecar wiederverwenden (kein Neuschreiben)
    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
    const auto t2 = std::filesystem::last_write_time(smap_path);

    CHECK(t1 == t2);
}

TEST_CASE("SourceMap - sidecar invalidated when file changes", "[sourcemap][persistence]") {
    TempDir dir;
    auto spec_path = dir.write("mc.yml", R"(
spec: "Hash Test"
encoding: ebcdic
fields:
  "000":
    format: numeric
    length: 4
  "001":
    format: bitmap
    length: 8
)");

    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
    const std::string smap_path = spec_path + ".smap";
    const auto t1 = std::filesystem::last_write_time(smap_path);

    // Spec-Datei ändern
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::ofstream f(spec_path, std::ios::app);
        f << "\n  # changed\n";
    }

    // Drittes Laden: Hash stimmt nicht mehr → Sidecar neu schreiben
    REQUIRE_NOTHROW(spec::SpecDecoder::loadFromYaml(spec_path));
    const auto t2 = std::filesystem::last_write_time(smap_path);

    CHECK(t1 != t2);  // Sidecar wurde neu geschrieben
}

// =============================================================================
// loadFromYaml/loadBothFromYaml - trackSourceMap-Parameter
// =============================================================================

TEST_CASE("SpecDecoder - loadFromYaml(path, false) baut denselben Parser wie trackSourceMap=true", "[spec][perf]") {
    TempYaml yaml(R"(
spec: "TrackSourceMap Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llchar,  length: 19, description: "PAN" }
)");

    auto parser_tracked = spec::SpecDecoder::loadFromYaml(yaml.str(), true);
    auto parser_fast = spec::SpecDecoder::loadFromYaml(yaml.str(), false);
    REQUIRE(parser_tracked != nullptr);
    REQUIRE(parser_fast != nullptr);

    // Beide müssen dieselbe Nachricht identisch dekodieren - trackSourceMap
    // beeinflusst nur Fehlermeldungs-Präzision, nie das Ergebnis.
    auto msgA = std::make_shared<Message>();
    msgA->parser(parser_tracked);
    auto msgB = std::make_shared<Message>();
    msgB->parser(parser_fast);

    std::vector<uint8_t> raw;
    auto ascii_b = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    raw.insert(raw.end(), { '0','2','0','0' });
    std::vector<uint8_t> bmp(8, 0x00);
    bmp[0] = 0x40; // DE2
    raw.insert(raw.end(), bmp.begin(), bmp.end());
    auto pan = ascii_b("4111111111111111");
    raw.insert(raw.end(), { '1','6' });
    raw.insert(raw.end(), pan.begin(), pan.end());

    REQUIRE_NOTHROW(msgA->unparse(msgA, raw));
    REQUIRE_NOTHROW(msgB->unparse(msgB, raw));
    CHECK(msgA->get<OpaqueField>(2)->value() == msgB->get<OpaqueField>(2)->value());
}

TEST_CASE("SpecDecoder - trackSourceMap=false liefert bei Fehlern in einer Einzeldatei-Spec dieselbe Fehlermeldung", "[spec][perf][error]") {
    // Ohne !use/!include_files/!template/!merge ist die "prozessierte" Position
    // identisch zur Original-Position - hier darf sich also NICHTS an der
    // Fehlermeldung ändern, nur die interne Tracking-Arbeit entfällt.
    TempYaml yaml(R"(
spec: "Error Test"
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap, length: 8 }
  "002": { format: unknownformat, length: 5 }
)");

    std::string msg_tracked, msg_fast;
    try { spec::SpecDecoder::loadFromYaml(yaml.str(), true); }
    catch (const std::exception& e) { msg_tracked = e.what(); }
    try { spec::SpecDecoder::loadFromYaml(yaml.str(), false); }
    catch (const std::exception& e) { msg_fast = e.what(); }

    REQUIRE_FALSE(msg_tracked.empty());
    CHECK(msg_tracked == msg_fast);
}

// =============================================================================
// loadFromYamlCached/loadBothFromYamlCached
// =============================================================================

TEST_CASE("SpecDecoder - loadFromYamlCached returns the same parser if file remains unchanged", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "Cache Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto p1 = spec::SpecDecoder::loadFromYamlCached(yaml.str());
    auto p2 = spec::SpecDecoder::loadFromYamlCached(yaml.str());

    // Objektidentität, nicht nur Gleichheit - beweist einen echten Cache-Treffer
    // (kein zweites Parsen/Aufbauen), nicht bloß ein zufällig gleiches Ergebnis.
    CHECK(p1.get() == p2.get());
}

TEST_CASE("SpecDecoder - loadFromYamlCached fails when file has been modified (mtime)", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "Cache Invalidation Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llchar,  length: 19 }
)");

    auto p1 = spec::SpecDecoder::loadFromYamlCached(yaml.str());

    // Datei ändern UND die mtime explizit vorstellen (statt auf Uhr-Auflösung
    // zu hoffen - manche Dateisysteme haben nur Sekundenauflösung, ein
    // sleep_for wäre hier ein flakiger Test).
    {
        std::ofstream f(yaml.path, std::ios::app);
        f << "\n  \"003\": { format: numeric, length: 3 }\n";
    }
    const auto newTime = std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5);
    std::filesystem::last_write_time(yaml.path, newTime);

    auto p2 = spec::SpecDecoder::loadFromYamlCached(yaml.str());

    // Andere Instanz - Cache wurde invalidiert und neu geladen
    CHECK(p1.get() != p2.get());

    // Erneuter Aufruf mit demselben (jetzt neuen) Stand trifft wieder den Cache
    auto p3 = spec::SpecDecoder::loadFromYamlCached(yaml.str());
    CHECK(p2.get() == p3.get());
}

TEST_CASE("SpecDecoder - loadBothFromYamlCached returns same objects if file remains unchanged", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "Cache Both Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto [p1, s1] = spec::SpecDecoder::loadBothFromYamlCached(yaml.str());
    auto [p2, s2] = spec::SpecDecoder::loadBothFromYamlCached(yaml.str());

    CHECK(p1.get() == p2.get());
    CHECK(s1.get() == s2.get());
}

TEST_CASE("SpecDecoder - loadFromYamlCached and loadBothFromYamlCached use separate caches", "[spec][cache]") {
    // Sind unabhängige Caches (siehe Doku in ISOSpec.hh) - beide funktionieren
    // für denselben Pfad, ohne sich gegenseitig zu beeinflussen.
    TempYaml yaml(R"(
spec: "Separate Caches Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto parserOnly = spec::SpecDecoder::loadFromYamlCached(yaml.str());
    auto [parserBoth, specBoth] = spec::SpecDecoder::loadBothFromYamlCached(yaml.str());

    REQUIRE(parserOnly != nullptr);
    REQUIRE(parserBoth != nullptr);
    REQUIRE(specBoth != nullptr);
}

TEST_CASE("SpecDecoder - loadFromYamlCached throws an error if spec is invalid (no silent fallback)", "[spec][cache][error]") {
    TempYaml yaml(R"(
spec: "Invalid"
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap, length: 8 }
  "002": { format: totally_unknown_format, length: 5 }
)");

    CHECK_THROWS_AS(spec::SpecDecoder::loadFromYamlCached(yaml.str()), std::exception);
}

TEST_CASE("SpecDecoder - TrustUntilInvalidated does NOT automatically detect file changes", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "TrustUntilInvalidated Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto p1 = spec::SpecDecoder::loadFromYamlCached(
        yaml.str(), true, spec::CacheValidation::TrustUntilInvalidated);

    // Datei ändern + mtime explizit vorstellen - TrustUntilInvalidated darf
    // das trotzdem NICHT bemerken (das ist der ganze Punkt).
    {
        std::ofstream f(yaml.path, std::ios::app);
        f << "\n  \"002\": { format: llchar, length: 19 }\n";
    }
    const auto newTime = std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5);
    std::filesystem::last_write_time(yaml.path, newTime);

    auto p2 = spec::SpecDecoder::loadFromYamlCached(
        yaml.str(), true, spec::CacheValidation::TrustUntilInvalidated);

    CHECK(p1.get() == p2.get()); // immer noch derselbe (veraltete) Parser
}

TEST_CASE("SpecDecoder - invalidateCache() forces TrustUntilInvalidated to reload", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "Invalidate Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto p1 = spec::SpecDecoder::loadFromYamlCached(
        yaml.str(), true, spec::CacheValidation::TrustUntilInvalidated);

    {
        std::ofstream f(yaml.path, std::ios::app);
        f << "\n  \"002\": { format: llchar, length: 19 }\n";
    }
    std::filesystem::last_write_time(yaml.path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));

    spec::SpecDecoder::invalidateCache(yaml.str());

    auto p2 = spec::SpecDecoder::loadFromYamlCached(
        yaml.str(), true, spec::CacheValidation::TrustUntilInvalidated);

    CHECK(p1.get() != p2.get()); // nach invalidateCache() ein frischer Parser
}

TEST_CASE("SpecDecoder - clearCache() clears both caches", "[spec][cache]") {
    TempYaml yaml(R"(
spec: "ClearCache Test"
encoding: ascii
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
)");

    auto p1 = spec::SpecDecoder::loadFromYamlCached(yaml.str());
    auto [pb1, sb1] = spec::SpecDecoder::loadBothFromYamlCached(yaml.str());

    spec::SpecDecoder::clearCache();

    auto p2 = spec::SpecDecoder::loadFromYamlCached(yaml.str());
    auto [pb2, sb2] = spec::SpecDecoder::loadBothFromYamlCached(yaml.str());

    // Nach clearCache() sind es frische Objekte, obwohl die Datei unverändert ist
    CHECK(p1.get() != p2.get());
    CHECK(pb1.get() != pb2.get());
    CHECK(sb1.get() != sb2.get());
}

TEST_CASE("SpecDecoder - invalidateCache() for a non-cached path is a no-op", "[spec][cache]") {
    CHECK_NOTHROW(spec::SpecDecoder::invalidateCache("/pfad/der/nie/gecacht/wurde.yml"));
}
