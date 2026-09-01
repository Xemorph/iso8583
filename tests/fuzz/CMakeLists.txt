# =============================================================================
# libFuzzer-Ziele (Security-Plan Phase 0: Skelett, Phase 4: Ausbaustufe)
#
# libFuzzer erfordert clang (+ libFuzzer-Runzeit). Damit ein regulärer Build
# (z.B. MSVC) nicht bricht, werden die Ziele bei Nicht-clang-Toolchains mit
# einer Warning übersprungen — selbst wenn ISO8583_BUILD_FUZZERS=ON ist.
#
# CI (Linux, clang):
#   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ \
#         -DISO8583_BUILD_FUZZERS=ON -DISO8583_BUILD_TESTS=OFF ...
#   cmake --build build-fuzz
#   ./build-fuzz/tests/fuzz/fuzz_codec -max_total_time=3600
# =============================================================================

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING
        "ISO8583_BUILD_FUZZERS=ON, aber Compiler ist "
        "${CMAKE_CXX_COMPILER_ID} (libFuzzer benötigt clang) -> Fuzzer-Ziele werden übersprungen.")
    return()
endif()

set(FUZZ_TARGETS
    fuzz_codec    # EBCDIC/ASCII-Konvertierung (Phase 4: strict/perm. Modi)
    fuzz_tlv      # TLV-/BERTLV-Byte-Parsing
    fuzz_parse    # ISOMessage::parse mit zufälligen Byte-Images
    fuzz_unparse  # ISOMessage::unparse mit zufälligen Feldwerten
    fuzz_spec     # YAML-Spec-Loading (loadFromYaml) mit zufälligen Inhalten
)

foreach(fuzz IN LISTS FUZZ_TARGETS)
    add_executable(${fuzz} ${fuzz}.cc)
    target_link_libraries(${fuzz} PRIVATE iso8583::iso8583)
    # -fsanitize=fuzzer zieht das libFuzzer-main() automatisch ein;
    # -fsanitize=address fängt OOB/Leaks in den Zielen ab.
    target_compile_options(${fuzz} PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
    target_link_options(${fuzz}    PRIVATE -fsanitize=fuzzer,address)
endforeach()