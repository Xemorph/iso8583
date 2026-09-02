// =============================================================================
// test_pci_logging.cc - PCI-Logging-Hygiene (Phase 3, Meilenstein 3.4)
// =============================================================================
//
// [ISO8583] 3.4 (Security-Audit):
//   * Spec-Key 'sensitive: true' markiert Felder, deren Wert in dump() (und
//     damit in jedem Log-Sink, der operator<</dump nutzt) als "***"
//     maskiert wird. Die Beschreibung bleibt sichtbar.
//   * value()/to_json() liefern bewusst weiterhin den Klartext (program-
//     matische Daten-API, kein Logging-Pfad).
//   * Die TNG_LOG_*-Stellen der Bibliothek drucken ausschließlich
//     Größen/Offsets/Beschreibungen — dieser Test wacht darüber, dass der
//     Klartext-Wert eines sensitive Feldes nie in einer Log-Zeile landet.
//   * TLV-Kinder: 'sensitive: true' auf dem Container vererbt sich auf alle
//     Kinder; pro-Tag-Deklaration ('children: <tag>: {sensitive: true}')
//     markiert genau dieses SE.
//
// Produktionshinweis (s. AGENTS.md): Log-Level in PCI-Umgebung = WARN.
// DEBUG zeigt Feld-Werte — nie an geteilte/long-lived Sinks.

#include <catch2/catch_test_macros.hpp>

// [tng]
#include <iso8583/iso8583.h>
#include <iso8583/ISOSpec.hh>
#include "_logger.hh"   // TNG_LOG_* + log::Globalzustand (privater Header)

// [stdc++]
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace TNG_NAMESPACE;
using namespace TNG_NAMESPACE::spec;

namespace fs = std::filesystem;

namespace {

// Lokaler RAII-Verzeichnis-Helfer (3.3-Konvention: eigener Helfer im
// anonymen Namensraum, keine COMDAT-Faltung über TUs hinweg).
struct TempDir {
    fs::path dir;

    TempDir() {
        static std::atomic<int> seq{0};
        std::error_code ec;
        const fs::path base = fs::temp_directory_path();
        do {
            dir = base / ("iso8583_pci_" + std::to_string(seq.fetch_add(1)));
        } while (fs::exists(dir, ec) && !ec);
        fs::create_directories(dir, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

// Catcht alle Log-Zeilen (alle Level) im Klartext.
struct CaptureLogger : public iso8583::log::ISOLogger {
    std::string text;

    void log(iso8583::log::Level, std::string_view file, int line,
        std::string_view message) override
    {
        (void)file; (void)line;
        text += std::string(message) + "\n";
    }
};

// Stellt Logger + Level um und stellt beide am Ende wieder her, damit
// andere Testfälle nicht beeinflusst werden.
struct LoggerGuard {
    CaptureLogger* cap;
    iso8583::log::Level oldLevel;

    explicit LoggerGuard(CaptureLogger* c)
        : cap(c)
        , oldLevel(iso8583::log::getLevel())
    {
        iso8583::log::setLogger(cap);
        iso8583::log::setLevel(iso8583::log::Level::DEBUG);
    }

    ~LoggerGuard() {
        iso8583::log::setLevel(oldLevel);
        iso8583::log::setLogger(nullptr);
    }
};

constexpr const char* kPan = "4111111111111111111";   // 19 Ziffern (LLCHAR 19)

// ASCII-Spec: DE2 (sensitive, mit Beschreibung), DE3 (nicht sensitive).
std::string makeSensitiveSpec() {
    return
"spec: \"PciSpec\"\n"
"encoding: ascii\n"
"fields:\n"
"  \"000\": { format: numeric,  length: 4 }\n"
"  \"001\": { format: bitmap,   length: 8 }\n"
"  \"002\": { format: llchar,   length: 19, description: \"Primary Account Number\", sensitive: true }\n"
"  \"003\": { format: numeric,  length: 2 }\n";
}

std::vector<uint8_t> ascii_b(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& part) {
    out.insert(out.end(), part.begin(), part.end());
}

// ASCII-Wire: MTI 0200, Bitmap {2,3}, DE2 = LL "19" + PAN, DE3 = "06".
std::vector<uint8_t> buildWire() {
    std::vector<uint8_t> raw;
    append(raw, ascii_b("0200"));
    const auto bm = utils::makeBitmap({ 2, 3 });
    for (const auto b : bm)
        raw.push_back(static_cast<uint8_t>(b));
    append(raw, ascii_b("19"));
    append(raw, ascii_b(kPan));
    append(raw, ascii_b("06"));
    return raw;
}

// ASCII-Spec mit TLV-Container: DE48 mit pro-Tag-Sensitivität (SE01
// sensitive, SE02 nicht) + DE52 (BinaryField, sensitive) als
// Second-Scenario für den BINARY-Dump-Pfad.
std::string makeTlvSpec() {
    return
"spec: \"PciTlvSpec\"\n"
"encoding: ascii\n"
"fields:\n"
"  \"000\": { format: numeric,  length: 4 }\n"
"  \"001\": { format: bitmap,   length: 8 }\n"
"  \"048\":\n"
"    format: lllbinary\n"
"    length: 999\n"
"    tlv:\n"
"      tag_bytes: 2\n"
"      len_bytes: 2\n"
"    children:\n"
"      \"01\": { format: char,   length: 2, description: \"SE01 Sensitive\", sensitive: true }\n"
"      \"02\": { format: char,   length: 2 }\n"
"  \"052\": { format: binary,   length: 8, sensitive: true }\n";
}

// ASCII-Wire: MTI 0200, Bitmap {48,52},
// DE48 = LLL "012" + SE01 ("01" "02" 'A1') + SE02 ("02" "02" 'B2'),
// DE52 = 8 Byte (0xDE 0xAD ...).
std::vector<uint8_t> buildTlvWire() {
    std::vector<uint8_t> raw;
    append(raw, ascii_b("0200"));
    const auto bm = utils::makeBitmap({ 48, 52 });
    for (const auto b : bm)
        raw.push_back(static_cast<uint8_t>(b));
    append(raw, ascii_b("012"));
    append(raw, ascii_b("01"));
    append(raw, ascii_b("02"));
    append(raw, ascii_b("A1"));
    append(raw, ascii_b("02"));
    append(raw, ascii_b("02"));
    append(raw, ascii_b("B2"));
    const std::vector<uint8_t> pin{ 0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44 };
    append(raw, pin);
    return raw;
}

std::string dumpToString(const std::shared_ptr<ISOMessage>& msg) {
    std::ostringstream oss;
    msg->dump(oss);
    return oss.str();
}

}  // namespace

// =============================================================================
// 1) Decode-Pfad: unparse() setzt den Sensitive-Marker vom Parser auf die
//    Component — dump() maskiert den Wert, die Beschreibung bleibt sichtbar,
//    der nicht-sensitive Nachbar-DE bleibt im Klartext.
// =============================================================================

TEST_CASE("PCI Logging - dump masks sensitive DE after unparse", "[pci][spec][dump]") {
    const TempDir tmp;
    const fs::path file = tmp.dir / "pci.yml";
    {
        std::ofstream out(file);
        out << makeSensitiveSpec();
    }

    auto parser = SpecDecoder::loadFromYaml(file.string());
    REQUIRE(parser != nullptr);

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, buildWire()) > 0);

    const std::string dump = dumpToString(msg);

    // Sensitive-DE: maskiert, aber mit Beschreibung.
    REQUIRE(dump.find("***") != std::string::npos);
    REQUIRE(dump.find("Primary Account Number") != std::string::npos);
    REQUIRE(dump.find(kPan) == std::string::npos);

    // Nicht-sensitive DE3 bleibt im Klartext.
    REQUIRE(dump.find("06") != std::string::npos);

    // Programatische API bleibt unmasked (bewusste API-Entscheidung, 3.4).
    const auto pan = msg->tryGetValue<OpaqueField>(2);
    REQUIRE(pan.has_value());
    CHECK(pan.value() == kPan);

    const json j = msg->to_json();
    bool panFound = false;
    for (const auto& f : j["fields"])
        if (f["key"] == 2)
        {
            panFound = true;
            CHECK(f["value"] == kPan);
        }
    REQUIRE(panFound);
}

// =============================================================================
// 2) Set-Pfad: set(key, value) übernimmt den Sensitive-Marker aus dem
//    zugehörigen Feld-Parser (make_component_from_string).
// =============================================================================

TEST_CASE("PCI Logging - dump masks sensitive DE after set", "[pci][message][dump]") {
    const TempDir tmp;
    const fs::path file = tmp.dir / "pci_set.yml";
    {
        std::ofstream out(file);
        out << makeSensitiveSpec();
    }

    auto parser = SpecDecoder::loadFromYaml(file.string());
    REQUIRE(parser != nullptr);

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    REQUIRE(msg->set(2, std::string(kPan)));
    REQUIRE(msg->set(3, std::string("06")));

    const std::string dump = dumpToString(msg);

    REQUIRE(dump.find("***") != std::string::npos);
    REQUIRE(dump.find(kPan) == std::string::npos);
    REQUIRE(dump.find("06") != std::string::npos);

    // value() bleibt unmasked.
    const auto pan = msg->tryGetValue<OpaqueField>(2);
    REQUIRE(pan.has_value());
    CHECK(pan.value() == kPan);
}

// =============================================================================
// 3) Logger-Pfad: bei DEBUG-Logging darf der Klartext-Wert eines sensitive
//    Feldes in KEINER Log-Zeile landen (die TNG_LOG_*-Stellen drucken nur
//    Größen/Offsets/Beschreibungen — Wächter-Test gegen Regressions).
// =============================================================================

TEST_CASE("PCI Logging - DEBUG logs never contain sensitive field values", "[pci][logging]") {
    const TempDir tmp;
    const fs::path file = tmp.dir / "pci_log.yml";
    {
        std::ofstream out(file);
        out << makeSensitiveSpec();
    }

    CaptureLogger cap;
    const LoggerGuard guard(&cap);

    auto parser = SpecDecoder::loadFromYaml(file.string());
    REQUIRE(parser != nullptr);

    cap.text.clear();   // Load-Logs nicht zählen

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    REQUIRE(msg->set(2, std::string(kPan)));
    REQUIRE(msg->set(3, std::string("06")));
    // msg->parse(msg) (nicht parser->parse(msg)): der öffentliche Wrapper
    // triggert recalcBitmap_locked() – der direkte Parser-Aufruf würde bei
    // einer frisch konstruierten Nachricht ohne Bitmap-Komponente auf dem
    // Bitmap-Encode-Pfad mit bad_optional_access scheitern.
    const auto wire = msg->parse(msg);
    REQUIRE(!wire.empty());

    auto msg2 = std::make_shared<Message>("0200");
    msg2->parser(parser);
    REQUIRE(msg2->unparse(msg2, wire) > 0);
    // Der PAN-Klartext darf in keiner Log-Zeile vorkommen.
    REQUIRE(cap.text.find(kPan) == std::string::npos);

    // Und die dump()-Oberfläche maskiert trotzdem.
    const std::string dump = dumpToString(msg2);
    REQUIRE(dump.find("***") != std::string::npos);
    REQUIRE(dump.find(kPan) == std::string::npos);
}

// =============================================================================
// 4) TLV-Pfad: pro-Tag 'sensitive: true' im children-Block markiert genau
//    dieses SE (sensitiveMap); das Nachbar-SE bleibt im Klartext. Zusätzlich
//    DE52 (BinaryField, sensitive) → der hex-Dump-Pfad wird maskiert.
// =============================================================================

TEST_CASE("PCI Logging - TLV children and binary DE are masked", "[pci][tlv][dump]") {
    const TempDir tmp;
    const fs::path file = tmp.dir / "pci_tlv.yml";
    {
        std::ofstream out(file);
        out << makeTlvSpec();
    }

    auto parser = SpecDecoder::loadFromYaml(file.string());
    REQUIRE(parser != nullptr);

    auto msg = std::make_shared<Message>("0200");
    msg->parser(parser);
    REQUIRE(msg->unparse(msg, buildTlvWire()) > 0);

    const std::string dump = dumpToString(msg);

    // SE01 (sensitive) maskiert ...
    REQUIRE(dump.find("***") != std::string::npos);
    // ... mit sichtbarer Beschreibung ...
    REQUIRE(dump.find("SE01 Sensitive") != std::string::npos);
    // ... das SE01-Hex (A1 = "4131") fehlt im dump ...
    REQUIRE(dump.find("4131") == std::string::npos);
    // ... SE02 (nicht sensitive) bleibt im Klartext (B2 = "4232").
    REQUIRE(dump.find("4232") != std::string::npos);
    // DE52 (BinaryField, sensitive): sein Hex fehlt komplett.
    REQUIRE(dump.find("DEAD") == std::string::npos);

    // Programatische API: SE01 unmasked lesbar.
    const auto sub = msg->get<Message>(48);
    REQUIRE(sub != nullptr);
    const auto se1 = sub->tryGetValue<BinaryField>(1);
    REQUIRE(se1.has_value());
    const bool se1Expected = (se1->size() == 2 &&
        se1->at(0) == static_cast<uint8_t>('A') && se1->at(1) == static_cast<uint8_t>('1'));
    CHECK(se1Expected);
}

// =============================================================================
// 5) Introspection: der Sensitive-Marker ist ein reines Laufzeit-/Logging-
//    Attribut — 'sensitive' ist ein gültiger Spec-Key (kein
//    SpecValidationError) und ändert nichts an Format/Encoding der Introspection.
// =============================================================================

TEST_CASE("PCI Logging - sensitive key accepted and does not alter introspection", "[pci][spec]") {
    const TempDir tmp;
    const fs::path file = tmp.dir / "pci_spec.yml";
    {
        std::ofstream out(file);
        out << makeSensitiveSpec();
    }

    auto [parser, spec] = SpecDecoder::loadBothFromYaml(file.string());
    REQUIRE(parser != nullptr);
    REQUIRE(spec != nullptr);

    REQUIRE(spec->has(2));
    const auto info = spec->field(2);
    REQUIRE(info.has_value());
    REQUIRE(info->description == "Primary Account Number");
    REQUIRE(info->is_nested == false);
    REQUIRE(info->is_bitmap == false);
    REQUIRE(info->format.type == "CHAR");
    REQUIRE(info->format.max_length == 19);
}