// =============================================================================
// Fuzz-Ziel F3: ISOMessage::parse (Serialisierung/Encoding) aus Fuzz-Daten
// =============================================================================
//
// Invariante: aus Fuzz-Bytes abgeleitete Feldinhalte duerfen beim Enkodieren
// NIE crashen (OOB/UB/Stack Overflow/ASan-Fund); die Bitmap wird korrekt
// neu berechnet (s. Security-Plan, Phase 4, F3).
//
// Der Parser wird EINMAL pro Prozess aus der eingebetteten ASCII-Referenzspec
// geladen (_fuzz_common.hh). Aus den Fuzz-Bytes werden MTI + ein paar
// OPAQUE/numeric-Felder abgeleitet; msg->parse() erzeugt das Byte-Bild.

#include "_fuzz_common.hh"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    if (!data || size > 1'000'000) {
        return 0;
    }
    fuzz::encode(data, size);
    return 0;
}

#if !defined(__clang__) && !defined(__APPLE__)
// Non-Clang (MSVC/GCC): Mini-Harness gegen feste Seeds + 256-Byte-Sweep.
int main() {
    fuzz::run_seeds();
    return 0;
}
#endif