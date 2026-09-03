// =============================================================================
// Fuzz-Ziel F2: YAML-Spec-Loading (SpecDecoder::loadFromYaml) mit Zufallsbytes
// =============================================================================
//
// Invariante: beliebiger (aenderbarer) Dateiinhalt darf beim Spec-Load NIE
// crashen / std::abort() ausloesen (ryml-Default!) — stattdessen positionierte
// [ISO8583]-/SpecValidationError-std::runtime_error (s. Security-Plan, Phase 4,
// F2). Die Fuzz-Bytes werden in eine Temp-Datei geschrieben und geladen; der
// Loader uebt damit: YAML-Parsing (ryml), Preprocessing (!use/!merge/
// !include_files-Sandbox), Feldvalidierung, SourceMap-Sidecar + SHA-256-
// Invalidate bei geaendertem Inhalt.
//
// Bemerkung: der erste Load installiert die prozessweiten ryml-Error-
// Callbacks (aborte → Exception) — im Fuzzer-Prozess gewollt.

#include "_fuzz_common.hh"
#include <iso8583/ISOSpec.hh>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
std::filesystem::path fuzzSpecFile() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "iso8583_fuzz";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / "fuzz_input.yml";
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    if (!data || size > 1'000'000) {
        return 0;
    }
    try {
        const auto path = fuzzSpecFile().string();
        {
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return 0;  // Temp-Ordner nicht beschreibbar — n.o.d.
            }
            out.write(reinterpret_cast<const char*>(data),
                      static_cast<std::streamsize>(size));
        }
        // Default-Optionen (sandbox=true, allowSmapWrite=true) — uebt auch
        // die Sidecar-Write-/Invalidate-Pfade.
        (void)iso8583::spec::SpecDecoder::loadFromYaml(path);
    } catch (...) {
        // Invariante: kein Crash/abort. Positionierte Fehler sind erlaubt.
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