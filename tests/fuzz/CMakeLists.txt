# =============================================================================
# libFuzzer-Ziele (Security-Plan Phase 4, F1..F5)
#
# Deckung der API-Oberflaechen:
#   fuzz_codec     codec::as<> / to<>  (EBCDIC/ASCII/BCD-Konvertierung, Strict)
#   fuzz_unparse   F1: ISOMessage::unparse (Dekodierung, Wire -> Felder)
#   fuzz_spec      F2: SpecDecoder::loadFromYaml (YAML-Spec, Sandbox/Sidecar)
#   fuzz_serialize F3: ISOMessage::parse (Serialisierung, Bitmap-Recompute)
#   fuzz_header    F4: WLP_FO/BASE1-Header-Byte-Images (Fail-closed-Guards)
#   fuzz_tlv       F5: TLV/BER-TLV-Parsing (BerTag/BerLength/read_num/store_se)
#
# Toolchain-Strategie:
#   * Clang (CI, Linux; auch AppleClang): -fsanitize=fuzzer,address —
#     echtes libFuzzer + ASan; die Fuzzer-Runzeit liefert main().
#   * Nicht-Clang (MSVC/GCC, z.B. lokale Entwicklung): normaler Build OHNE
#     Fuzzer-Runzeit. _fuzz_common.hh liefert ein Mini-Harness (main), das
#     LLVMFuzzerTestOneInput gegen feste Seeds + einen 256-Byte-Sweep laeuft —
#     damit der Harness in jeder Toolchain kompiliert und lokal auf Fehler
#     (CRT/UBSan/ASan) pruefbar bleibt.
#
# CI (Linux, clang):
#   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ \
#         -DISO8583_BUILD_FUZZERS=ON -DISO8583_BUILD_TESTS=OFF ...
#   cmake --build build-fuzz
#   ./build-fuzz/tests/fuzz/fuzz_unparse -max_total_time=3600
# =============================================================================

set(FUZZ_TARGETS
    fuzz_codec
    fuzz_unparse
    fuzz_spec
    fuzz_serialize
    fuzz_header
    fuzz_tlv
)

foreach(fuzz IN LISTS FUZZ_TARGETS)
    add_executable(${fuzz} ${fuzz}.cc)
    target_link_libraries(${fuzz} PRIVATE iso8583::iso8583)
    # Private Header (z.B. _tlv.hh fuer fuzz_tlv) werden direkt eingebunden.
    target_include_directories(${fuzz} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(${fuzz} PRIVATE cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # -fsanitize=fuzzer zieht das libFuzzer-main() automatisch ein;
        # -fsanitize=address fängt OOB/Leaks in den Zielen ab.
        target_compile_options(${fuzz} PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
        target_link_options(${fuzz}    PRIVATE -fsanitize=fuzzer,address)
    else()
        # Nicht-Clang (MSVC/GCC): Mini-Harness via _fuzz_common.hh (Fallback-main).
        if(MSVC)
            # fmt/format.h (via src/_logger.hh) verlangt /utf-8 (wie iso8583-Ziel).
            target_compile_options(${fuzz} PRIVATE /utf-8)
            # WICHTIG: ISO8583_DLL bewusst NICHT definieren (wie das Test-Ziel).
            # Mit ISO8583_DLL wuerde TNG_EXPORT=>dllimport, was bei Template-Klassen
            # (z.B. ISOTLVParser<> aus _tlv.hh) Import-Referenzen erzeugt, die die DLL
            # nicht exportiert (LNK2019). Ohne das Macro ist TNG_EXPORT leer und die
            # Template-Mitglieder werden lokal instantiiert (Inline) - verlinkt sauber.
        endif()
    endif()
endforeach()