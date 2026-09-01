// Fuzz-Ziel: YAML-Spec-Loading (SpecDecoder::loadFromYaml) mit zuefaelligem Inhalt
//
// Invariante: beliebige Eingabe duerfe nie crashen (OOB/UB/Stack Overflow),
// sondern im Fehlerfall eine saubere [ISO8583]-std::runtime_error werfen
// (s. Security-Plan, Phase 4).
//
// PHASE-0-KELETT (nicht voll ausgefuehrt): In Phase 4 wird hier ein
// minimaler, eingebauter ISO-8583-Parser aufgebaut (einmalig via
// SpecDecoder::loadFromYaml aus eingebettetem YAML) und die Fuzz-Bytes
// durch die jeweilige API-Route getrieben. Derzeit wird die Eingabe nur
// gegraenzt konsumiert, damit das Skelett kompilierbar bleibt.

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    (void)data;
    // TODO(Phase 4): echten Fuzz-Durchlauf (parse/unparse/TLV/Spec) anbinden.
    if (size > 1'000'000) return 0;
    return 0;
}
