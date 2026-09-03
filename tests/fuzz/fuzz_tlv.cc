// =============================================================================
// Fuzz-Ziel F5: TLV-/BERTLV-Byte-Parsing (BerTag/BerLength/read_num/store_se)
// =============================================================================
//
// Invariante: beliebige Eingabe-Bytes duerfen NIE crashen (OOB/UB/Stack
// Overflow/ASan-Fund), sondern im Fehlerfall sauber abbrechen (Policy meldet
// consumed==0) bzw. eine [ISO8583]-std::runtime_error werfen (s. Security-
// Plan, Phase 4, F5 — die TLV-Hardening-Pfade von Milestone 4.1).
//
// Getrieben werden DIREKT die drei TLV-Parser-Varianten mit rohen Fuzz-Bytes:
//   * BERTLVParser   — EMV / ISO 8825-1 (BerTag + BerLength, 2-Byte-Tags)
//   * ISOTLVParser_VI— Visa-Fixed-TLV  (2-Byte-TAG, 1-Byte-Len, BCD, kein TCC)
//   * ISOTLVParser_MC— MC-Fixed-TLV    (2-Byte-TAG, 2-Byte-Len, EBCDIC, TCC)
// Damit ueben die Fuzz-Bytes garantiert BerTag::read, BerLength::read
// (num_bytes>8-Grenze), read_num<> (OOB-Vorpruefung) und store_se (Tag-Fit)
// — unabhaengig davon, ob ein voller Message-Dekodierlauf die TLV-Felder
// ueberhaupt erreicht.

#include "_fuzz_common.hh"
#include "_tlv.hh"  // private: ISOTLVParser / BERTLVParser / ISOTLVParser_VI/_MC

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace iso8583;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    if (!data || size > 1'000'000) {
        return 0;
    }
    const std::vector<uint8_t> b(data, data + size);
    try {
        // EMV BER-TLV (variable Tag-/Laengenlaengen, 2-Byte-Tags wie 0x9F26).
        {
            auto msg = std::make_shared<Message>();
            BERTLVParser p;
            (void)p.unparse(msg, b, 0);
        }
        // Visa Fixed-TLV (2-Byte-TAG, 1-Byte-Laenge, BCD, kein TCC).
        {
            auto msg = std::make_shared<Message>();
            ISOTLVParser_VI p;
            (void)p.unparse(msg, b, 0);
        }
        // Mastercard Fixed-TLV (2-Byte-TAG, 2-Byte-Laenge, EBCDIC, mit TCC).
        {
            auto msg = std::make_shared<Message>();
            ISOTLVParser_MC p;
            (void)p.unparse(msg, b, 0);
        }
    } catch (...) {
        // Invariante: kein Crash/UB. Abbrueche/Fehler sind erlaubt.
    }
    return 0;
}

#if !defined(__clang__) && !defined(__APPLE__)
// Non-Clang (MSVC/GCC): Mini-Harness gegen feste Seeds + 256-Byte-Sweep.
int main() {
    fuzz::run_seeds();
    return 0;
}
#endif