// Fuzz-Ziel: EBCDIC/ASCII-Codec-Konvertierung (codec::as<>).
//
// Invariante: beliebige Eingabe-Bytes duerfen NIE crashen (OOB/UB/Stack
// Overflow), sondern im Fehlerfall eine saubere [ISO8583]-std::runtime_error
// werfen (s. Security-Plan, Phase 4).
//
// Skelett (Phase 0) — Phase 4: strict/permittive Modi, Richtung A2E,
// groessere Zufalls-Verteilungen und -coveragen-gestuetzte Corpus-Saat.

#include <iso8583/_codec.hh>

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!data || size > 1'000'000) return 0;

    std::vector<uint8_t> buf(data, data + size);

    try {
        (void)iso8583::codec::as<std::string, iso8583::codec::Encoder::EBCDIC>(
            buf, 0, size);
    } catch (const std::exception&) {
        // erwartet: saubere [ISO8583]-Fehler, kein Crash
    }

    try {
        (void)iso8583::codec::as<std::string, iso8583::codec::Encoder::ASCII>(
            buf, 0, size);
    } catch (const std::exception&) {
        // erwartet: saubere [ISO8583]-Fehler, kein Crash
    }

    return 0;
}