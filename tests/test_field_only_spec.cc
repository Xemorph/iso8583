// (0.6.0, FE-1) Field-only-Specs: Validierung (WP1) + decodeField (WP4).
//
// WP1 (hier): Fehler-Cases der Validierung:
//   - fields: in Field-only-Dokument,
//   - fehlendes field: (leeres Dokument bzw. nur Root-Keys),
//   - header: in Field-only-Dokument (Entscheidung c),
//   - ungültiger Feld-Key im field:-Block (validateFieldKeys),
//   - field: in Message-Dokument (erwischt über die Message-Validierung,
//     über die öffentliche loadFromYaml).
//
// WP1: Die Field-only-Loader-Eintritte (loadField*) existieren erst ab WP3,
// daher fahren die Validierungs-Cases die interne Testnaht
// spec::validateFieldSpecYamlFile() (src/_spec.hh) – dieselbe
// Preprocessor-/SourceMap-Pipeline wie der Loader.
//
// WP4: Regressionstests für SpecDecoder::decodeField() über die öffentliche
// loadField*-API: fixe TLV (Mastercard/EBCDIC), BERTLV (EMV),
// Nested-Sequenzen, Äquivalenz zum manuellen Pattern (leere Message +
// unparse), Full-Message-Integration (DE55-Payload aus einem
// Voll-Nachrichten-Decode) und byte-identische Roundtrips.

// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>
#include <iso8583/_codec.hh>
#include <iso8583/ISOUtils.hh>
// [tng/internal]
#include "_spec.hh"
// [stdc++]
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <process.h> // getpid()
#else
#include <unistd.h>  // getpid()
#endif
#include <atomic>

using namespace TNG_NAMESPACE;
using TNG_NAMESPACE::utils::makeBitmap;

// =============================================================================
// Helpers (TempYaml: Identisch mit test_spec_loader.cc – PID+Zaehler, da
// ctest -j mehrere Test-Prozesse parallel fährt)
// =============================================================================

namespace {

struct TempYaml {
    std::filesystem::path path;

    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }

    ~TempYaml() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.string(); }
};

// Field-only-Validierung über die Testnaht (wirft ab → what() zurückgeben).
static std::string field_error(const std::string& yaml_text) {
    TempYaml y(yaml_text);
    try {
        spec::validateFieldSpecYamlFile(y.str());
        return "";
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

// Message-Validierung über die öffentliche API (Message-Seiten-Konflikt).
static std::string msg_error(const std::string& yaml_text) {
    TempYaml y(yaml_text);
    try {
        spec::SpecDecoder::loadFromYaml(y.str());
        return "";
    }
    catch (const std::exception& e) {
        return e.what();
    }
}

// Wire-Byte-Helfer (analog test_e2e_full_message.cc).
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
// WP1 – Validierungs-Fehler (validateFieldSpecYaml / validateSpecYaml)
// =============================================================================

TEST_CASE("FE-1 Error - fields: in Field-only-Dokument", "[fe1][error][validation]") {
    const auto msg = field_error(R"YAML(
spec: "ICC"
field:
  format: lllbinary
  tlv: {tag_bytes: 2, len_bytes: 2}
  children:
    "71": {format: lllchar}
fields:
  "000": {format: bitmap, length: 8}
)YAML");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Konflikt") != std::string::npos);
    CHECK(msg.find("fields") != std::string::npos);
}

TEST_CASE("FE-1 Error - fehlendes field: (leeres Dokument)", "[fe1][error][validation]") {
    // Eine 0-Byte-Datei wird schon im Preprocessor fail-closed abgewiesen
    // ("Leere YAML-Datei", src/_preprocessor.cc), noch bevor
    // validateFieldSpecYaml greift. (Eine Datei, die nur aus
    // Leerzeichen/Zeilenumbrüchen besteht, ist für den Preprocessor
    // nicht "leer" – sie läuft in den Validator, s. Nachbar-Test.)
    const auto msg = field_error("");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Leere YAML-Datei") != std::string::npos);
}

TEST_CASE("FE-1 Error - fehlendes field: (nur Root-Keys)", "[fe1][error][validation]") {
    const auto msg = field_error(R"YAML(
spec: "ICC"
encoding: ebcdic
)YAML");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("field") != std::string::npos);
}

TEST_CASE("FE-1 Error - header: in Field-only-Dokument (Entscheidung c)",
    "[fe1][error][validation]") {
    const auto msg = field_error(R"YAML(
spec: "ICC"
field:
  format: lllbinary
header: 4
)YAML");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("header") != std::string::npos);
    CHECK(msg.find("Konflikt") != std::string::npos);
}

TEST_CASE("FE-1 Error - ungueltiger Key im field:-Block (validateFieldKeys)",
    "[fe1][error][validation]") {
    const auto msg = field_error(R"YAML(
spec: "ICC"
field:
  format: lllbinary
  blubb: 1
)YAML");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Unbekannter Schlüssel") != std::string::npos);
    CHECK(msg.find("blubb") != std::string::npos);
}

TEST_CASE("FE-1 Error - field: in Message-Dokument", "[fe1][error][validation]") {
    const auto msg = msg_error(R"YAML(
spec: "Test"
field:
  format: lllbinary
fields:
  "000": {format: bitmap, length: 8}
)YAML");
    REQUIRE_FALSE(msg.empty());
    CHECK(msg.find("Konflikt") != std::string::npos);
    CHECK(msg.find("fields") != std::string::npos);
}

// =============================================================================
// WP4 – decodeField: Regressionstests Field-only-Specs
// =============================================================================
//
// Wire-Vertrag: Der Field-only-Parser dekodiert exakt die Bytes, die eine
// BinaryField nach einem Voll-Nachrichten-Decode haelt (TLV/BERTLV:
// Kinderframes ohne das eigene LLL-Praefix des DEs; Nested: Kinderframes
// ohne das aussere Praefix; Skalar: Praefix + Payload). Roundtrip:
// parse(decodeField(parser, payload)) == payload (byte-identisch); die
// Source-BinaryField wird nicht veraendert.

TEST_CASE("FE-1 A - decodeField: fixe TLV (Mastercard, EBCDIC)", "[fe1][tlv]") {
    TempYaml yaml(R"YAML(
spec: "DE55 ICC (Mastercard SE)"
encoding: ebcdic
field:
  format: lllbinary
  length: 255
  tlv: { tag_bytes: 2, len_bytes: 2 }
  children:
    "64": { format: char, length: 4, description: "Application PAN" }
    "71": { format: char, length: 3, description: "Terminal Capabilities" }
)YAML");

    const auto [parser, spec] = spec::SpecDecoder::loadFieldBothFromYaml(yaml.str());
    REQUIRE(parser != nullptr);
    REQUIRE(spec != nullptr);

    // EBCDIC-Payload: SE64 "1234", SE71 "ABC", undeclared SE72 0xDE 0xAD
    // (aufsteigende Tag-Reihenfolge fuer den Byte-fuer-Byte-Roundtrip).
    std::vector<uint8_t> payload;
    append(payload, ebcdic_b("64"));
    append(payload, ebcdic_b("04"));
    append(payload, ebcdic_b("1234"));
    append(payload, ebcdic_b("71"));
    append(payload, ebcdic_b("03"));
    append(payload, ebcdic_b("ABC"));
    append(payload, ebcdic_b("72"));
    append(payload, ebcdic_b("02"));
    payload.push_back(0xDE);
    payload.push_back(0xAD);

    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);

    // Deklarierte char-Kinder (EBCDIC vom Dokument geerbt): OpaqueField.
    const auto se64 = msg->get<OpaqueField>(64);
    REQUIRE(se64 != nullptr);
    CHECK(se64->value() == "1234");
    CHECK(se64->description() == "Application PAN");
    const auto se71 = msg->get<OpaqueField>(71);
    REQUIRE(se71 != nullptr);
    CHECK(se71->value() == "ABC");
    CHECK(se71->description() == "Terminal Capabilities");

    // Undeclared Tag: dynamisch (BinaryField), generische "SE72"-Beschreibung.
    const auto se72 = msg->get<BinaryField>(72);
    REQUIRE(se72 != nullptr);
    CHECK(se72->value() == std::vector<uint8_t>{0xDE, 0xAD});
    CHECK(se72->description() == "SE72");

    // Typ-Trennung: char-Kind nie als BinaryField, Binary-Kind nie als OpaqueField.
    CHECK(msg->get<BinaryField>(64) == nullptr);
    CHECK(msg->get<OpaqueField>(72) == nullptr);

    // Roundtrip: exakt die Source-Bytes reproduzieren.
    CHECK(msg->parse(msg) == payload);

    // Introspection: exakt ein Feld (Key 0 = MTI-Key), kein Header.
    CHECK(spec->name() == "DE55 ICC (Mastercard SE)");
    CHECK(spec->encoding() == "EBCDIC");
    CHECK(spec->hasHeader() == false);
    const auto& fields = spec->fields();
    REQUIRE(fields.size() == 1);
    const auto& f = fields[0];
    CHECK(f.key == 0);
    CHECK(f.format.type == "BINARY");  // lllbinary -> BINARY (3 Praefix-Ziffern)
    CHECK(f.format.prefix_digits == 3);
    // TLV-Container wird intern als NESTED-Typ behandelt (children vorhanden)
    // - die TLV-spezifische Struktur steht in tlv_children (unten).
    CHECK(f.is_nested == true);
    REQUIRE(f.tlv_children.size() == 2);
    REQUIRE(f.tlv_children.count(64) == 1);
    REQUIRE(f.tlv_children.count(71) == 1);
    CHECK(f.tlv_children.at(64).format.type == "CHAR");
    CHECK(f.tlv_children.at(64).description == "Application PAN");
    CHECK(f.tlv_children.at(71).format.type == "CHAR");
    CHECK(f.tlv_children.at(71).description == "Terminal Capabilities");
}

TEST_CASE("FE-1 B - decodeField: BERTLV (EMV, variable Tags/Laengen)", "[fe1][tlv]") {
    TempYaml yaml(R"YAML(
spec: "DE55 ICC (EMV)"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:
    "5A": { format: char, length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
)YAML");

    const auto [parser, spec] = spec::SpecDecoder::loadFieldBothFromYaml(yaml.str());
    REQUIRE(parser != nullptr);
    REQUIRE(spec != nullptr);

    // BERTLV: hex Tags, keine TCC-Byte; undeclared 0x8A zwischen 5A und 95.
    const std::vector<uint8_t> payload = {
        0x5A, 0x04, '1', '2', '3', '4',
        0x8A, 0x01, 0x42,
        0x95, 0x02, 0xDE, 0xAD
    };
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);

    const auto se5a = msg->get<OpaqueField>(0x5A);
    REQUIRE(se5a != nullptr);
    CHECK(se5a->value() == "1234");
    CHECK(se5a->description() == "Application PAN");

    const auto se95 = msg->get<BinaryField>(0x95);
    REQUIRE(se95 != nullptr);
    CHECK(se95->value() == std::vector<uint8_t>{0xDE, 0xAD});
    CHECK(se95->description() == "PIN Block");

    // Undeclared Tag: generische dekadische "SE138"-Beschreibung (0x8A = 138).
    const auto se8a = msg->get<BinaryField>(0x8A);
    REQUIRE(se8a != nullptr);
    CHECK(se8a->value() == std::vector<uint8_t>{0x42});
    CHECK(se8a->description() == "SE138");

    CHECK(msg->get<BinaryField>(0x5A) == nullptr);
    CHECK(msg->get<OpaqueField>(0x95) == nullptr);

    CHECK(msg->parse(msg) == payload);

    // Introspection: hex-Kinder-Keys (0x5A -> 90, 0x95 -> 149).
    CHECK(spec->hasHeader() == false);
    const auto& fields = spec->fields();
    REQUIRE(fields.size() == 1);
    const auto& f = fields[0];
    CHECK(f.key == 0);
    CHECK(f.format.type == "BINARY");
    REQUIRE(f.tlv_children.size() == 2);
    REQUIRE(f.tlv_children.count(0x5A) == 1);
    REQUIRE(f.tlv_children.count(0x95) == 1);
    CHECK(f.tlv_children.at(0x5A).format.type == "CHAR");
    CHECK(f.tlv_children.at(0x5A).description == "Application PAN");
    CHECK(f.tlv_children.at(0x95).format.type == "BINARY");
    CHECK(f.tlv_children.at(0x95).description == "PIN Block");
}

TEST_CASE("FE-1 C - decodeField: Nested-Sequenz (llllchar, ascii)", "[fe1][nested]") {
    TempYaml yaml(R"YAML(
spec: "DE48 Nested (Field-only)"
encoding: ascii
field:
  type: nested
  format: llllchar
  length: 9999
  description: "Nested ASCII-Text-Container"
  children:
    - { format: llchar, length: 6, description: "C0" }
    - { format: lllchar, length: 10, description: "C1" }
)YAML");

    const auto [parser, spec] = spec::SpecDecoder::loadFieldBothFromYaml(yaml.str());
    REQUIRE(parser != nullptr);
    REQUIRE(spec != nullptr);

    // Kinderframes OHNE das aussere LLLL-Praefix:
    //   C0: llchar  '03' + "abc"
    //   C1: lllchar '003' + "xyz"
    const auto payload = ascii_b("03abc003xyz");
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(msg != nullptr);

    const auto c0 = msg->get<OpaqueField>(0);
    REQUIRE(c0 != nullptr);
    CHECK(c0->value() == "abc");
    CHECK(c0->description() == "C0");
    const auto c1 = msg->get<OpaqueField>(1);
    REQUIRE(c1 != nullptr);
    CHECK(c1->value() == "xyz");
    CHECK(c1->description() == "C1");

    // Typ-Trennung: Text-Kinder sind OpaqueFields, keine BinaryFields.
    CHECK(msg->get<BinaryField>(0) == nullptr);
    CHECK(msg->get<BinaryField>(1) == nullptr);

    // Roundtrip: exakt die Source-Bytes reproduzieren.
    CHECK(msg->parse(msg) == payload);

    // Introspection: ein (nested) Feld, Key 0, keine TLV-Kinder.
    CHECK(spec->hasHeader() == false);
    const auto& fields = spec->fields();
    REQUIRE(fields.size() == 1);
    const auto& f = fields[0];
    CHECK(f.key == 0);
    CHECK(f.is_nested == true);
    CHECK(f.format.type == "CHAR");   // llllchar -> CHAR (4 Praefix-Ziffern)
    CHECK(f.format.prefix_digits == 4);
    REQUIRE(f.tlv_children.empty());
    REQUIRE(f.children.size() == 2);
    CHECK(f.children[0].format.type == "CHAR");
    CHECK(f.children[0].format.prefix_digits == 2);
    CHECK(f.children[0].description == "C0");
    CHECK(f.children[1].format.prefix_digits == 3);
    CHECK(f.children[1].description == "C1");
}

TEST_CASE("FE-1 D - decodeField == manueller Pattern (leere Message + unparse)",
    "[fe1]") {
    TempYaml yaml(R"YAML(
spec: "DE55 ICC (EMV)"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  children:
    "5A": { format: char, length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
)YAML");

    const auto parser = spec::SpecDecoder::loadFieldFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    const std::vector<uint8_t> payload = {
        0x5A, 0x04, '1', '2', '3', '4',
        0x8A, 0x01, 0x42,
        0x95, 0x02, 0xDE, 0xAD
    };
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto before = bf->value();

    // decodeField ...
    const auto m1 = spec::SpecDecoder::decodeField(parser, *bf);
    REQUIRE(m1 != nullptr);
    // ... und der aequivalente zweiseitige Pattern.
    auto m2 = std::make_shared<Message>();
    m2->parser(parser);
    const auto consumed = m2->unparse(m2, payload);
    REQUIRE(consumed == payload.size());

    // Identische Komponentenwerte (und -beschreibungen) in beiden Messages.
    REQUIRE(m1->get<OpaqueField>(0x5A) != nullptr);
    REQUIRE(m2->get<OpaqueField>(0x5A) != nullptr);
    CHECK(m1->get<OpaqueField>(0x5A)->value() == m2->get<OpaqueField>(0x5A)->value());
    CHECK(m1->get<OpaqueField>(0x5A)->description() == m2->get<OpaqueField>(0x5A)->description());

    REQUIRE(m1->get<BinaryField>(0x95) != nullptr);
    REQUIRE(m2->get<BinaryField>(0x95) != nullptr);
    CHECK(m1->get<BinaryField>(0x95)->value() == m2->get<BinaryField>(0x95)->value());

    // Undeclared SE in beiden Messages.
    REQUIRE(m1->get<BinaryField>(0x8A) != nullptr);
    REQUIRE(m2->get<BinaryField>(0x8A) != nullptr);
    CHECK(m1->get<BinaryField>(0x8A)->value() == m2->get<BinaryField>(0x8A)->value());

    // Typ-Trennung in beiden Messages.
    CHECK(m1->get<BinaryField>(0x5A) == nullptr);
    CHECK(m2->get<BinaryField>(0x5A) == nullptr);

    // Die Source-BinaryField bleibt unangetastet.
    CHECK(bf->value() == before);

    // Identische Re-Seralisierung.
    CHECK(m1->parse(m1) == payload);
    CHECK(m1->parse(m1) == m2->parse(m2));
}

TEST_CASE("FE-1 E - Integration: Full-Message-Decode -> DE55-Payload -> decodeField",
    "[fe1][tlv]") {
    // Message-Spec (Voll-Nachricht, identisch zum e2e-Szenario "BERTLV-Kinder").
    TempYaml msgYaml(R"YAML(
spec: "E2E typisierte BERTLV-Kinder 0.5.0"
encoding: ascii

fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "002": { format: llchar,   length: 19, description: "PAN" }
  "004": { format: numeric,  length: 12, description: "Amount" }
  "055":
    format: lllbertlv
    length: 999
    description: "ICC Data"
    children:
      "5A": { format: char,   length: 4, encoding: ascii, description: "Application PAN" }
      "95": { format: binary, length: 2, description: "PIN Block" }
)YAML");
    // Field-only-Spec fuer dasselbe DE (gleiche Kind-Deklaration).
    TempYaml fieldYaml(R"YAML(
spec: "DE55 ICC (Field-only)"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:
    "5A": { format: char, length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
)YAML");

    const auto msgParser = spec::SpecDecoder::loadFromYaml(msgYaml.str());
    REQUIRE(msgParser != nullptr);
    const auto fieldParser = spec::SpecDecoder::loadFieldFromYaml(fieldYaml.str());
    REQUIRE(fieldParser != nullptr);

    std::vector<uint8_t> frame;
    append(frame, ascii_b("0100"));
    append(frame, makeBitmap({2, 4, 55}));
    append(frame, ascii_b("16"));
    append(frame, ascii_b("4111111111111111"));
    append(frame, ascii_b("000000012345"));
    const std::vector<uint8_t> de55_payload = {
        0x5A, 0x04, '1', '2', '3', '4',
        0x8A, 0x01, 0x42,
        0x95, 0x02, 0xDE, 0xAD
    };
    append(frame, ascii_b("013"));
    append(frame, de55_payload);

    // ── Voll-Nachrichten-Decode ──────────────────────────────
    auto msg = std::make_shared<Message>();
    msg->parser(msgParser);
    const auto consumed = msg->unparse(msg, frame);
    REQUIRE(consumed == frame.size());
    CHECK(msg->parse(msg) == frame);

    // DE55 als typisierter Sub-Parser: exakt der Payload (ohne das eigene
    // LLL-Praefix) – die Bytes, die eine BinaryField haelt.
    const auto de55 = msg->get<Message>(55);
    REQUIRE(de55 != nullptr);
    const auto payload = de55->parse(de55);
    REQUIRE(payload == de55_payload);

    // ── decodeField mit der Field-only-Spec ──────────────────
    const auto bf = std::make_shared<BinaryField>(0, payload);
    const auto m = spec::SpecDecoder::decodeField(fieldParser, *bf);
    REQUIRE(m != nullptr);

    // Identische Kinder (Werte UND Typen) in beiden Decode-Routen.
    REQUIRE(de55->get<OpaqueField>(0x5A) != nullptr);
    REQUIRE(m->get<OpaqueField>(0x5A) != nullptr);
    CHECK(m->get<OpaqueField>(0x5A)->value() == de55->get<OpaqueField>(0x5A)->value());
    CHECK(m->get<OpaqueField>(0x5A)->description() == "Application PAN");

    REQUIRE(de55->get<BinaryField>(0x95) != nullptr);
    REQUIRE(m->get<BinaryField>(0x95) != nullptr);
    CHECK(m->get<BinaryField>(0x95)->value() == de55->get<BinaryField>(0x95)->value());
    CHECK(m->get<BinaryField>(0x95)->description() == "PIN Block");

    REQUIRE(de55->get<BinaryField>(0x8A) != nullptr);
    REQUIRE(m->get<BinaryField>(0x8A) != nullptr);
    CHECK(m->get<BinaryField>(0x8A)->value() == de55->get<BinaryField>(0x8A)->value());
    CHECK(m->get<BinaryField>(0x8A)->description() == "SE138");

    // Roundtrip: m re-serialisiert exakt den DE55-Payload (ohne LLL).
    CHECK(m->parse(m) == payload);
}

TEST_CASE("FE-1 F - Roundtrip: BERTLV- und Nested-Payloads byte-identisch", "[fe1]") {
    // Kombination der Payloads aus Case B (BERTLV) und Case C (Nested):
    // parse(decodeField(parser, payload)) reproduziert die Source-Bytes.
    {
        TempYaml yaml(R"YAML(
spec: "FE-1 F (bertlv)"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  children:
    "5A": { format: char, length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
)YAML");
        const auto parser = spec::SpecDecoder::loadFieldFromYaml(yaml.str());
        REQUIRE(parser != nullptr);
        const std::vector<uint8_t> payload = {
            0x5A, 0x04, '1', '2', '3', '4',
            0x8A, 0x01, 0x42,
            0x95, 0x02, 0xDE, 0xAD
        };
        const auto bf = std::make_shared<BinaryField>(0, payload);
        const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
        REQUIRE(msg != nullptr);
        CHECK(msg->parse(msg) == payload);
    }
    {
        TempYaml yaml(R"YAML(
spec: "FE-1 F (nested)"
encoding: ascii
field:
  type: nested
  format: llllchar
  length: 9999
  children:
    - { format: llchar, length: 6, description: "C0" }
    - { format: lllchar, length: 10, description: "C1" }
)YAML");
        const auto parser = spec::SpecDecoder::loadFieldFromYaml(yaml.str());
        REQUIRE(parser != nullptr);
        const auto payload = ascii_b("03abc003xyz");
        const auto bf = std::make_shared<BinaryField>(0, payload);
        const auto msg = spec::SpecDecoder::decodeField(parser, *bf);
        REQUIRE(msg != nullptr);
        CHECK(msg->parse(msg) == payload);
    }
}