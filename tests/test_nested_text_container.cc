// =============================================================================
// libiso8583 — Dauerhafter Regressionstest: text-basierte Nested-Container
// (0.6.0, FR-3)
//
// Deckt zwei zusammenhängende Defekte ab, die in der FR-3-Probe
// (2026-09, v0.5.0/HEAD byte-identisch) aufgetreten sind:
//
// 1) SIGSEGV in JEDEM text-basierten Nested-Container (type: nested +
//    lllchar/llchar/llllchar oder remaining+Text-Encoding), unparse UND
//    parse. Ursache (src/_parser.hh, ISOFieldParser<T=Parser>): der
//    Thread-Sicherheits-Scratch bzw. Wrapper ist immer BinaryField, der
//    text-basierte Container-Basis-Parser macht aber
//    dynamic_pointer_cast<OpaqueField>(scratch) -> nullptr -> SEGV.
//    Seit v0.3.0 latent (Scratch-Pattern). Fix: Loader-Normalisierung auf
//    den binären Zwilling (src/_spec.cc: containerBaseField) +
//    Fail-closed-Guard (src/_parser.hh: checkContainerBase).
//
// 2) FR-3: Deklarierte Kinder im FIXEN tag_bytes/len_bytes-TLV-Modus
//    (Mastercard/Visa-SE) müssen wie im BER/BERTLV-Modus (FR-1/FR-2,
//    v0.5.0) typisiert dekodieren — Text-Kind (char/numeric/nopad_char)
//    -> OpaqueField, binary/undeklariert -> BinaryField. Im binären
//    Container (lllbinary) funktioniert das bereits seit v0.5.0; über die
//    Normalisierung gilt es seit 0.6.0 auch für lllchar-Container.
//
// Fall-Matrix:
//   A: lllbinary + tlv + typisiertes EBCDIC-Kind  -> OpaqueField  (seit v0.5.0)
//   B: lllbinary + tlv + binary-Kind              -> BinaryField  (Kontrolle)
//   C: lllchar   + tlv + typisiertes Kind         -> OpaqueField  (vorher SEGV)
//   D: lllchar   + children-Sequenz (kein tlv)    -> OpaqueField  (vorher SEGV)
//   E: llllchar  + Global-Encoding ASCII (Zwilling IFA_LLLLBINARY, 0.6.0)
//   F: remaining + ebcdic + children (Zwilling IF_REMAINING)
//   G: lllchar   + Feld-Override encoding: ascii (Global ebcdic)
//   H: lllchar   + encoding: bcd + numeric-Kind (Zwilling IFB_LLLBINARY)
//   I: Fail-closed-Guard: manuell konstruierte ISONestedFieldParser mit
//      string-basiertem Basis-Parser (OPAQUE bzw. text-REMAINING) wirft
//      std::runtime_error statt SEGV; J: binärer Basis-Parser bleibt grün.
// =============================================================================

#include <iso8583/iso8583.h>
#include <iso8583/_codec.hh>

#include "_parser.hh"   // private: manuell konstruierte ISONestedFieldParser (Fall I/J)
#include "fmt_types.hh" // private: IFE_/IFA_/IF_-Parser-Aliase (Fälle I–J)

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace iso8583;

namespace {

    // Pro-Datei-Helper (anonymes Namespace — s. AGENTS.md §12.9: globale
    // Definition wäre ein ODR-Verstoß über mehrere Test-TUs hinweg).
    struct TempYaml {
        std::filesystem::path path;

        explicit TempYaml(const std::string& content) {
            path = std::filesystem::temp_directory_path()
                / ("libiso8583_fr3test_" +
                   std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                   "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".yml");
            std::ofstream f(path);
            f << content;
        }

        ~TempYaml() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            std::filesystem::remove(path.string() + ".smap", ec);
        }

        std::string str() const { return path.string(); }
    };

    // ASCII-Text -> EBCDIC-Bytes (IBM-1047-Tabelle der Library; NICHT
    // aus dem Gedächtnis hartcoden!).
    std::vector<uint8_t> ebcdic_b(const std::string& s) {
        std::vector<uint8_t> buf(s.size());
        codec::to<codec::Encoder::EBCDIC>(s, buf, 0);
        return buf;
    }

    void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& part) {
        out.insert(out.end(), part.begin(), part.end());
    }

    void append(std::vector<uint8_t>& out, const std::string& ascii_part) {
        out.insert(out.end(), ascii_part.begin(), ascii_part.end());
    }

    std::string toHex(const std::vector<uint8_t>& v) {
        static const char* d = "0123456789ABCDEF";
        std::string s;
        s.reserve(v.size() * 3);
        for (auto b : v) {
            s.push_back(d[b >> 4]);
            s.push_back(d[b & 0x0F]);
            s.push_back(' ');
        }
        return s;
    }

    // Rahmen ohne DE48-Nutzdaten: MTI 0200 + Bitmap (DE2, DE11, DE48) +
    // DE2 (LL 19 + PAN) + DE11 (000012) — EBCDIC (Global-Encoding der
    // meisten Fallback-Specs).
    std::vector<uint8_t> frameHead() {
        std::vector<uint8_t> raw;
        append(raw, ebcdic_b("0200"));
        std::vector<uint8_t> bmp(8, 0x00);
        bmp[0] = 0x40u; // DE2
        bmp[1] = 0x20u; // DE11
        bmp[5] = 0x01u; // DE48
        append(raw, bmp);
        append(raw, ebcdic_b("19"));
        append(raw, ebcdic_b("5555555555555554444")); // 19 Ziffern
        append(raw, ebcdic_b("000012"));
        return raw;
    }

    const std::string specHead = R"(
spec: "FR-3 Regressionstest"
encoding: ebcdic

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llchar,  length: 19, description: "PAN" }
  "011": { format: numeric, length: 6,  description: "STAN" }
  "048":
    type: nested
    format: )";

    const std::string specTlvTail = R"(
    length: 999
    description: "Network Management Information"
    tlv:
      tag_bytes: 2
      len_bytes: 2
      tcc: true
    children:
      "71":
        format: )";

    // DE48-TLV-Payload (9 Bytes): LLL '009' + TCC 'P' + SE71 (Tag '71',
    // Len '04', Data F3F3E540 = EBCDIC "33V ").
    std::vector<uint8_t> de48TlvPayload() {
        std::vector<uint8_t> p;
        append(p, ebcdic_b("009")); // LLL-Praefix: 9 Bytes
        append(p, ebcdic_b("P"));   // TCC 'P' (0xD7)
        append(p, ebcdic_b("71"));  // Tag SE71
        append(p, ebcdic_b("04"));  // Laenge 4
        append(p, ebcdic_b("33V ")); // SE71-Data (EBCDIC-Text)
        return p;
    }

} // namespace

// =============================================================================
// A: FR-3-Kernfrage — lllbinary-Container, typisiertes EBCDIC-Kind
// =============================================================================

TEST_CASE("FR-3 A - fixed TLV (lllbinary), typisiertes EBCDIC-Kind SE71 -> OpaqueField",
    "[fr3nested][tlv][fixed][unparse][roundtrip][ebcdic]")
{
    TempYaml yaml(specHead + "lllbinary" + specTlvTail +
        "char\n"
        "        encoding: ebcdic\n"
        "        length: 4\n"
        "        description: \"SE71 - Text-Kind\"\n");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    std::vector<uint8_t> raw = frameHead();
    append(raw, de48TlvPayload());
    INFO("raw size: " << raw.size());
    INFO("raw hex:  " << toHex(raw));

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);

    auto asOpaque = de48->get<OpaqueField>(71);
    auto asBinary = de48->get<BinaryField>(71);
    INFO("SE71 OpaqueField: " << (asOpaque ? asOpaque->value() : "<null>"));
    INFO("SE71 BinaryField: " << (asBinary ? toHex(asBinary->value()) : "<null>"));

    // FR-3: typisierter Text-Kind MUSS OpaqueField sein (wie im BER-Modus).
    REQUIRE(asOpaque != nullptr);
    CHECK(asOpaque->value() == "33V ");
    CHECK(asBinary == nullptr);

    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// B: Kontrolle — lllbinary-Container, binary-Kind bleibt BinaryField
// =============================================================================

TEST_CASE("FR-3 B - fixed TLV (lllbinary), binary-Kind SE71 bleibt BinaryField",
    "[fr3nested][tlv][fixed][unparse][roundtrip][ebcdic]")
{
    TempYaml yaml(specHead + "lllbinary" + specTlvTail +
        "binary\n"
        "        length: 4\n"
        "        description: \"SE71 - Binary-Kind\"\n");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    std::vector<uint8_t> raw = frameHead();
    append(raw, de48TlvPayload());

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);

    auto asOpaque = de48->get<OpaqueField>(71);
    auto asBinary = de48->get<BinaryField>(71);
    CHECK(asOpaque == nullptr);
    REQUIRE(asBinary != nullptr);
    CHECK(asBinary->value() == std::vector<uint8_t>{ 0xF3, 0xF3, 0xE5, 0x40 });

    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// C: Ehemaliger Crash-Fall — lllchar + fixed TLV + Kind (AGENTS.md-Beispiel-
//    Form). Vor 0.6.0: SIGSEGV. Jetzt: grün + Introspektion meldet das
//    deklarierte Format (Normalisierung ist intern, Spec-Tree bleibt sauber).
// =============================================================================

TEST_CASE("FR-3 C - fixed TLV (lllchar), typisiertes EBCDIC-Kind SE71 -> OpaqueField (früher SEGV)",
    "[fr3nested][tlv][fixed][unparse][roundtrip][ebcdic][spec]")
{
    TempYaml yaml(specHead + "lllchar" + specTlvTail +
        "char\n"
        "        encoding: ebcdic\n"
        "        length: 4\n"
        "        description: \"SE71 - Text-Kind\"\n");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    std::vector<uint8_t> raw = frameHead();
    append(raw, de48TlvPayload());
    INFO("raw size: " << raw.size());
    INFO("raw hex:  " << toHex(raw));

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);

    auto asOpaque = de48->get<OpaqueField>(71);
    auto asBinary = de48->get<BinaryField>(71);
    REQUIRE(asOpaque != nullptr);
    CHECK(asOpaque->value() == "33V ");
    CHECK(asBinary == nullptr);

    // Byte-exakter Round-Trip (Wire-Präfix-Semantik identisch).
    CHECK(msg->parse(msg) == raw);

    // Introspektion (loadBothFromYaml): das deklarierte Format bleibt
    // sichtbar — die Normalisierung betrifft NUR den internen Parser-Bau.
    auto [parser2, spec] = spec::SpecDecoder::loadBothFromYaml(yaml.str());
    REQUIRE(spec != nullptr);
    REQUIRE(spec->has(48));
    auto f = spec->field(48);
    REQUIRE(f.has_value());
    CHECK(f->is_nested);
    CHECK(f->format.type == "CHAR");
    CHECK(f->format.prefix_digits == 3);
    CHECK(f->encoding == "EBCDIC");
    REQUIRE(f->tlv_children.count(71) == 1);
    CHECK(f->tlv_children.at(71).format.type == "CHAR");

    // Dump: Text-Wert sichtbar (SE71 ist nicht sensitive).
    std::ostringstream dumpOs;
    msg->dump(dumpOs);
    const std::string dump = dumpOs.str();
    INFO("dump: " << dump);
    CHECK(dump.find("33V") != std::string::npos);
}

// =============================================================================
// D: Ehemaliger Crash-Fall — lllchar + children-Sequenz (OHNE tlv): der SEGV
//    war nicht TLV-spezifisch, sondern betraf jeden text-basierten
//    Nested-Container.
// =============================================================================

TEST_CASE("FR-3 D - lllchar + children-Sequenz (kein tlv) -> OpaqueField-Kinder (früher SEGV)",
    "[fr3nested][unparse][roundtrip][ebcdic]")
{
    TempYaml yaml(specHead + "lllchar" + R"YAML(
    length: 999
    description: "Nested Text-Container"
    children:
      - { format: numeric, length: 3, description: "C0" }
      - { format: char,    length: 3, description: "C1" }
)YAML");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    std::vector<uint8_t> raw = frameHead();
    append(raw, ebcdic_b("006"));    // LLL: 6 Bytes
    append(raw, ebcdic_b("123"));    // C0 numeric
    append(raw, ebcdic_b("ABC"));    // C1 char
    INFO("raw size: " << raw.size());
    INFO("raw hex:  " << toHex(raw));

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto c0 = de48->get<OpaqueField>(0);
    auto c1 = de48->get<OpaqueField>(1);
    REQUIRE(c0 != nullptr);
    REQUIRE(c1 != nullptr);
    CHECK(c0->value() == "123");
    CHECK(c1->value() == "ABC");
    CHECK(de48->get<BinaryField>(0) == nullptr);

    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// E: llllchar + Global-Encoding ASCII — der LLLL-Zwilling IFA_LLLLBINARY
//    fehlte in der Parser-Tabelle (0.6.0, WP1); über die Normalisierung
//    wird llllchar|ascii jetzt auf LLLLBINARY|ASCII abgebildet.
// =============================================================================

TEST_CASE("FR-3 E - llllchar-Container (Global ascii, Zwilling IFA_LLLLBINARY) -> OpaqueField-Kind",
    "[fr3nested][unparse][roundtrip]")
{
    TempYaml yaml(R"YAML(
spec: "FR-3 E (ascii)"
encoding: ascii

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llchar,  length: 19, description: "PAN" }
  "011": { format: numeric, length: 6,  description: "STAN" }
  "048":
    type: nested
    format: llllchar
    length: 9999
    description: "Nested ASCII-Text-Container"
    children:
      - { format: llchar, length: 6, description: "C0" }
)YAML");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // Reiner ASCII-Rahmen:
    std::vector<uint8_t> raw;
    append(raw, "0200");
    std::vector<uint8_t> bmp(8, 0x00);
    bmp[0] = 0x40u; bmp[1] = 0x20u; bmp[5] = 0x01u;
    append(raw, bmp);
    append(raw, "19");
    append(raw, "5555555555555554444");
    append(raw, "000012");
    // DE48: LLLL '0008' + Kind llchar '06' + '123456'
    append(raw, "0008");
    append(raw, "06");
    append(raw, "123456");

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto c0 = de48->get<OpaqueField>(0);
    REQUIRE(c0 != nullptr);
    CHECK(c0->value() == "123456");

    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// F: remaining + Text-Encoding (EBCDIC) — Container-Basis normalisiert auf
//    IF_REMAINING (rohe Bytes, Weiterleitung an die Kinder). Seit 0.6.0
//    dekodieren remaining+Text Kindelemente als OpaqueField.
// =============================================================================

TEST_CASE("FR-3 F - remaining-Container (ebcdic, Zwilling IF_REMAINING) -> OpaqueField-Kinder",
    "[fr3nested][unparse][roundtrip][ebcdic]")
{
    TempYaml yaml(R"YAML(
spec: "FR-3 F (remaining)"
encoding: ebcdic

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": { format: llchar,  length: 19, description: "PAN" }
  "011": { format: numeric, length: 6,  description: "STAN" }
  "048":
    type: nested
    format: remaining
    length: 64
    description: "Remaining Text-Container"
    children:
      - { format: char,    length: 3, description: "C0" }
      - { format: numeric, length: 3, description: "C1" }
)YAML");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    std::vector<uint8_t> raw = frameHead();
    append(raw, ebcdic_b("ABC123"));
    INFO("raw hex:  " << toHex(raw));

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto c0 = de48->get<OpaqueField>(0);
    auto c1 = de48->get<OpaqueField>(1);
    REQUIRE(c0 != nullptr);
    REQUIRE(c1 != nullptr);
    CHECK(c0->value() == "ABC");
    CHECK(c1->value() == "123");
}

// =============================================================================
// G: lllchar + Feld-Override encoding: ascii (Global ebcdic) — der
//    Container-Normalisierung bleibt das eigene Encoding erhalten
//    (LLLBINARY|ASCII); die Kinder erben das Container-Encoding.
// =============================================================================

TEST_CASE("FR-3 G - lllchar-Container mit Feld-Override encoding: ascii -> OpaqueField-Kind",
    "[fr3nested][unparse][roundtrip]")
{
    TempYaml yaml(specHead + "lllchar" + R"YAML(
    length: 999
    encoding: ascii
    description: "Nested ASCII-Feld-Override"
    children:
      - { format: llchar, length: 6, description: "C0" }
)YAML");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // EBCDIC-Rahmen (Global) + DE48-Nutzdaten rein in ASCII:
    std::vector<uint8_t> raw = frameHead();
    append(raw, "008");     // LLL-Praefix (ASCII): 8 Bytes
    append(raw, "06");      // llchar-Praefix (ASCII): 6 Bytes
    append(raw, "123456");  // Kind-Daten (ASCII)

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, raw));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto c0 = de48->get<OpaqueField>(0);
    REQUIRE(c0 != nullptr);
    CHECK(c0->value() == "123456");

    CHECK(msg->parse(msg) == raw);
}

// =============================================================================
// H: lllchar + encoding: bcd + numeric-Kind — BCD-Längenpräfix über den
//    binären Zwilling (IFB_LLLBINARY). Encode-first-Round-Trip, damit das
//    BCD-Präfix nicht manuell konstruiert werden muss.
// =============================================================================

TEST_CASE("FR-3 H - lllchar-Container (bcd, Zwilling IFB_LLLBINARY) -> OpaqueField-Kind",
    "[fr3nested][unparse][roundtrip][ebcdic]")
{
    TempYaml yaml(specHead + "lllchar" + R"YAML(
    length: 999
    encoding: bcd
    description: "Nested BCD-Container"
    children:
      - { format: numeric, length: 6, description: "C0" }
)YAML");

    auto parser = spec::SpecDecoder::loadFromYaml(yaml.str());
    REQUIRE(parser != nullptr);

    // Encode erst (prägt das korrekte BCD-Präfix):
    // Encode-first: das MTI kommt ueber den
    // ISOMessage(string_view)-Konstruktor - ISOMessage::parse()
    // materialisiert aus set()-Aufrufen nur die Bitmap
    // (recalcBitmap_locked()), nicht das MTI-Feld.
    auto built = std::make_shared<Message>("0200");
    built->parser(parser);
    built->set(2, std::string("5555555555555554444"));
    built->set(11, std::string("000012"));
    built->set("48.0", std::string("123456"));
    // Message-API statt parser->parse(): ISOMessage::parse() rechnet
    // die Bitmap-Komponente neu (recalcBitmap_locked()), auf die der
    // Encode-Pfad der ISOBaseParser angewiesen ist - ein direkter
    // Parser-Aufruf an einer nie dekodierten Nachricht wirft
    // std::bad_optional_access an dieser Stelle.
    std::vector<uint8_t> wire = built->parse(built);
    INFO("wire hex: " << toHex(wire));

    // Unparse in eine frische Nachricht:
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE_NOTHROW(msg->unparse(msg, wire));

    auto de48 = msg->get<Message>(48);
    REQUIRE(de48 != nullptr);
    auto c0 = de48->get<OpaqueField>(0);
    REQUIRE(c0 != nullptr);
    CHECK(c0->value() == "123456");
    CHECK(de48->get<BinaryField>(0) == nullptr);

    // Re-Encode ist byte-identisch:
    CHECK(msg->parse(msg) == wire);
}

// =============================================================================
// I: Fail-closed-Guard — manuell konstruierte ISONestedFieldParser-Instanzen
//    mit NICHT binär-basiertem Basis-Parser (Spec-Ladung normalisiert
//    solche Kombinationen, dieser Pfad betrifft nur Hand-Instanzen).
//    Vor dem Guard: SIGSEGV (nullptr-Deref im T=parser-Zweig).
// =============================================================================

TEST_CASE("Guard I - ISONestedFieldParser mit string-basiertem Basis-Parser wirft Fail-closed (statt SEGV)",
    "[fr3nested][error]")
{
    // IFE_LLLCHAR = ISOFieldParser<std::string, LLL, EBCDIC, EBCDIC>
    // -> type() == OPAQUE (nicht binär-basiert).
    auto base = std::make_shared<IFE_LLLCHAR>(999, "Text-Basis");
    REQUIRE(base->type() == ISOFieldParserType::OPAQUE);

    auto sub = std::make_shared<ISOBaseParser>("Sub");
    sub->add(std::make_shared<IFE_CHAR>(3, "C0"));

    auto nested = std::make_shared<ISONestedFieldParser<ISOBaseParser>>(
        base, "Nested (defekt konstruiert)");
    nested->subParser(sub);

    std::vector<uint8_t> raw = frameHead();
    append(raw, ebcdic_b("006"));
    append(raw, ebcdic_b("ABC123"));

    auto comp = std::make_shared<Message>();

    // unparse-Zweig:
    {
        std::string what;
        bool caught = false;
        try {
            nested->unparse(comp, raw, 0);
        }
        catch (const std::runtime_error& e) {
            caught = true;
            what = e.what();
        }
        REQUIRE(caught);
        INFO("Fehlermeldung: " << what);
        CHECK(what.find("Fail-closed") != std::string::npos);
    }

    // parse-Zweig:
    {
        auto comp2 = std::make_shared<Message>();
        REQUIRE_THROWS_AS(nested->parse(comp2), std::runtime_error);
    }
}

TEST_CASE("Guard I2 - ISONestedFieldParser mit text-basiertem remaining-Basis wirft Fail-closed (statt SEGV)",
    "[fr3nested][error]")
{
    // IFA_REMAINING = ISOFieldParser<std::string, UNKNOWN, NONE, ASCII>
    // -> type() == REMAINING, aber string-basiert (create_component liefert
    // OpaqueField, kein BinaryField).
    auto base = std::make_shared<IFA_REMAINING>(64, "Text-Remaining-Basis");
    REQUIRE(base->type() == ISOFieldParserType::REMAINING);

    auto sub = std::make_shared<ISOBaseParser>("Sub");
    sub->add(std::make_shared<IFA_CHAR>(3, "C0"));

    auto nested = std::make_shared<ISONestedFieldParser<ISOBaseParser>>(
        base, "Nested (defekt konstruiert)");
    nested->subParser(sub);

    std::vector<uint8_t> raw = frameHead();
    auto comp = std::make_shared<Message>();
    REQUIRE_THROWS_AS(nested->unparse(comp, raw, 0), std::runtime_error);
    REQUIRE_THROWS_AS(nested->parse(std::make_shared<Message>()), std::runtime_error);
}

// =============================================================================
// J: Kontrolle — binär-basierter Basis-Parser bleibt durch den Guard hindurch
//    uneingeschränkt funktionsfähig (False-Positive-Ausschluss).
// =============================================================================

TEST_CASE("Guard J - ISONestedFieldParser mit binärem Basis-Parser bleibt grün",
    "[fr3nested][unparse][roundtrip]")
{
    // IF_LLLBINARY = ISOFieldParser<vector<uint8_t>, LLL, BINARY, BINARY>
    // -> type() == BINARY (Guard lässt durch).
    auto base = std::make_shared<IF_LLLBINARY>(999, "Binary-Basis");
    REQUIRE(base->type() == ISOFieldParserType::BINARY);

    auto sub = std::make_shared<ISOBaseParser>("Sub");
    sub->add(std::make_shared<IFA_CHAR>(3, "C0"));

    auto nested = std::make_shared<ISONestedFieldParser<ISOBaseParser>>(
        base, "Nested (binär)");
    nested->subParser(sub);

    // Wire: LLL-Binary-Präfix 00 00 03 + 'ABC' (ASCII).
    std::vector<uint8_t> raw = { 0x00, 0x00, 0x03,
                                 'A', 'B', 'C' };

    auto comp = std::make_shared<Message>();
    REQUIRE_NOTHROW(nested->unparse(comp, raw, 0));
    auto c0 = comp->get<OpaqueField>(0);
    REQUIRE(c0 != nullptr);
    CHECK(c0->value() == "ABC");

    auto comp2 = std::make_shared<Message>();
    comp2->set(0, std::string("ABC"));
    REQUIRE_NOTHROW(nested->parse(comp2) == raw);
}