// =============================================================================
// _fuzz_common.hh — Gemeinsame Infrastruktur für die libFuzzer-Ziele (Phase 4)
// =============================================================================
//
// Header-only-Helfer für tests/fuzz/fuzz_*.cc:
//   * eingebettete ASCII-Referenzspec (int16-safe Tags, lllbertlv ohne
//     children:) — build-unabhängig, wird in den Temp-Ordner geschrieben und
//     EINMAL pro Prozess via SpecDecoder::loadFromYaml geladen (Lazy Static).
//   * decode()  — F1/F3: Message::unparse auf Fuzz-Bytes (niemals throw).
//   * encode()  — F3:   Felder aus Fuzz-Bytes setzten + Message::parse.
//   * Non-Clang-Fallback: liefert ein Mini-Harness, das
//     LLVMFuzzerTestOneInput gegen feste Seeds + 256-Byte-Sweep läuft, damit
//     der Harness auch unter MSVC/GCC kompiliert und lokal geprüft werden kann.
//     CI (Clang) nutzt echtes libFuzzer + ASan (eigene main).
//
// Invariant (alle Ziele): kein Crash / keine UB / kein std::terminate. Alle
// Aufrufe laufen in try/catch — der Fuzzer prüft, dass ASan/UBSan/MSVC-CRT
// trotz "fehlerhafter" Eingaben sauber bleiben.

#pragma once

#include <iso8583/iso8583.h>
#include <iso8583/ISOSpec.hh>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fuzz {

// ---------------------------------------------------------------------------
// Eingebettete ASCII-Referenzspec.
//   * encoding: ascii — die meisten Fuzz-Bytes sind gültige ASCII-Zeichen,
//     daher viele "erfolgreiche" Decode-Pfade; ungültige Bytes üben die
//     (non-strict) Sentinel-Pfade.
//   * nur int16-sichere Tags; lllbertlv OHNE children: — damit die Spec in
//     DEFAULT(int16)- und ISO8583_BERTLV(int32)-Builds identisch lädt.
//   * BER-TLV-Wide-Tags (0x9F26) werden zur Laufzeit über die F5 store_se-
//     Tag-Fit-Prüfung behandelt (int16: Warnung+Skip, int32: gespeichert).
// ---------------------------------------------------------------------------
inline const char* embeddedSpecYaml() {
    return R"YAML(
spec: "fuzz-reference"
encoding: ascii

fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "002": { format: llchar,   length: 19 }
  "003": { format: numeric,  length: 6 }
  "004": { format: numeric,  length: 12 }
  "006": { format: numeric,  length: 4 }
  "011": { format: numeric,  length: 6 }
  "025": { format: char,     length: 3 }
  "035": { format: llchar,   length: 15 }
  "041": { format: char,     length: 11 }
  "043": { format: char,     length: 14 }
  "048":
    type: nested
    format: lllchar
    length: 999
    children:
      - { format: char,    length: 2 }
      - { format: char,    length: 5 }
      - { format: numeric, length: 3 }
  "049": { format: numeric,  length: 3 }
  "055":
    format: lllbertlv
    length: 999
  "056":
    format: lllbertlv
    length: 999
)YAML";
}

// Lazy Parser: schreibt die eingebettete Spec in den Temp-Ordner und lädt sie
// einmal pro Prozess. Liefert nullptr, wenn das Laden scheitert (z.B.
// read-only Temp-Ordner) — die Ziele fallen dann auf ein No-Decode zurück.
inline std::shared_ptr<iso8583::ISOParserPtrBase> parser() {
    static std::shared_ptr<iso8583::ISOParserPtrBase> p = [] {
        try {
            namespace fs = std::filesystem;
            auto dir = fs::temp_directory_path() / "iso8583_fuzz";
            std::error_code ec;
            fs::create_directories(dir, ec);
            const auto specPath = (dir / "fuzz_spec.yml").string();
            {
                std::ofstream out(specPath, std::ios::binary);
                if (out) {
                    out << embeddedSpecYaml();
                }
            }
            return iso8583::spec::SpecDecoder::loadFromYaml(specPath);
        } catch (...) {
            return std::shared_ptr<iso8583::ISOParserPtrBase>{};
        }
    }();
    return p;
}

// F1/F3: Fuzz-Bytes dekodieren (Wire → Felder). Nimmt nie den Exception-Pfad.
inline void decode(const uint8_t* data, std::size_t size) {
    auto p = parser();
    if (!p) {
        return;
    }
    try {
        auto msg = std::make_shared<iso8583::Message>();
        msg->parser(p);
        std::vector<uint8_t> buf(data, data + size);
        msg->unparse(msg, buf);
    } catch (...) {
        // Invariante: kein Crash/UB. Fehler sind erlaubte Ergebnisse.
    }
}

// F3: aus Fuzz-Bytes ein paar Felder ableiten und ein Byte-Bild erzeugen.
// Sanitisiert die MTI auf Ziffern, damit der numeric-MTI-Feldtyp konsistent
// bleibt; die übrigen OPAQUE-Felder nehmen beliebige Zeichen.
inline void encode(const uint8_t* data, std::size_t size) {
    auto p = parser();
    if (!p) {
        return;
    }
    try {
        auto take = [&](std::size_t off, std::size_t n) {
            std::string s;
            for (std::size_t i = 0; i < n && off + i < size; ++i) {
                s.push_back(static_cast<char>(data[off + i]));
            }
            return s;
        };
        // MTI: 4 Ziffern aus den Bytes ableiten (nicht-ziffern → '0').
        std::string mti;
        mti.reserve(4);
        for (int i = 0; i < 4; ++i) {
            const char c = (size_t(i) < size) ? static_cast<char>(data[i]) : '0';
            mti.push_back((c >= '0' && c <= '9') ? c : '0');
        }
        auto msg = std::make_shared<iso8583::Message>(nonstd::string_view(mti));
        msg->parser(p);
        msg->set(2, take(4, 10));   // OPAQUE (llchar) — beliebige Zeichen ok
        msg->set(3, std::string("000000"));
        msg->set(4, take(14, 8));   // OPAQUE
        msg->set(11, std::string("000001"));
        (void)msg->parse(msg);      // encodiert + rechnet Bitmap neu
    } catch (...) {
        // Invariante: kein Crash/UB.
    }
}

// ---------------------------------------------------------------------------
// Non-Clang-Fallback (MSVC/GCC): Mini-Harness ohne libFuzzer-Runtime.
// Läuft LLVMFuzzerTestOneInput gegen feste Seeds + 256-Byte-Sweep, damit der
// Harness lokal kompiliert/ausgeführt und auf Fehler geprüft werden kann.
// ---------------------------------------------------------------------------
#if !defined(__clang__) && !defined(__APPLE__)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

inline void run_seeds() {
    std::vector<std::vector<uint8_t>> seeds;
    seeds.emplace_back();                              // leer
    seeds.emplace_back(1, 0x00);                       // 1 Null-Byte
    seeds.emplace_back(64, 0x00);
    seeds.emplace_back(256, 0xFF);
    // Plausibles ASCII-Fragment (0200 + einige Bytes) → übt Decode-Pfade.
    {
        std::string s = "0200";
        std::vector<uint8_t> v;
        for (char c : s) v.push_back(static_cast<uint8_t>(c));
        for (int i = 0; i < 40; ++i) v.push_back(static_cast<uint8_t>('0' + (i % 10)));
        seeds.push_back(std::move(v));
    }
    // Deterministischer Pseudozufall (LCG) für strukturelle Vielfalt.
    {
        std::vector<uint8_t> v;
        uint32_t x = 0x12345678u;
        for (int i = 0; i < 512; ++i) {
            x = x * 1664525u + 1013904223u;
            v.push_back(static_cast<uint8_t>(x >> 24));
        }
        seeds.push_back(std::move(v));
    }
    for (auto& s : seeds) {
        for (int r = 0; r < 50; ++r) {
            LLVMFuzzerTestOneInput(s.data(), s.size());
        }
    }
    // 256-Byte-Sweep: jedes Einzelbyte, dann Paare, dann Tripel.
    for (int b = 0; b < 256; ++b) {
        const uint8_t v1 = static_cast<uint8_t>(b);
        LLVMFuzzerTestOneInput(&v1, 1);
        const uint8_t v2[2] = { static_cast<uint8_t>(b), static_cast<uint8_t>(b) };
        LLVMFuzzerTestOneInput(v2, 2);
        const uint8_t v3[3] = { static_cast<uint8_t>(b), static_cast<uint8_t>(255 - b), static_cast<uint8_t>(b) };
        LLVMFuzzerTestOneInput(v3, 3);
    }
}
#endif  // !__clang__ && !__APPLE__

}  // namespace fuzz