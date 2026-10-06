cmake_minimum_required(VERSION 3.21)

find_package(Catch2 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(libiso8583_tests
    test_codec.cc
    test_field_parser.cc
    test_iso_message.cc
    test_full_unparse.cc
    test_spec_loader.cc
    test_logging.cc
    test_preprocessor.cc
    test_remaining_field.cc
    test_base1_header.cc
    test_wlp_fo_header.cc
    test_pos_data_code.cc
    test_currency.cc
    test_dump_operator.cc
    test_tlv_parser.cc
    test_iso_utils.cc
    test_utils_protect.cc
    test_incompatible_input.cc
    test_strict_mode.cc
    test_encoding_determinism.cc
    test_spec_sandbox.cc
    test_spec_cache.cc
    test_threading.cc
    test_pci_logging.cc
    # (0.6.0, FR-3) Dauerhafter Regressionstest für text-basierte
    # Nested-Container (frühere FR-3-Probe test_tlv_fixed_typed_probe.cc)
    test_nested_text_container.cc
    _crt_diag.cc
    # (0.6.0, FE-1) Field-only-Specs (Validierung WP1, decodeField WP4)
    test_field_only_spec.cc
    # (0.6.0, AMOUNT) AmountField: jPOS-ISOAmount-Konvention (Währung+Skala+Betrag)
    test_amount.cc
    test_strict_length.cc
    # (0.7.0, FR-6) Längenpräfix-Encoding unabhängig vom Nutzdaten-Encoding
    test_prefix_encoding.cc
    # (0.8.0, FR-8) BINAERES Laengenpraefix (Einheit Ziffern) vor gepacktem BCD
    test_prefix_bcd_binary.cc
    # (0.8.0, FR-9) Sekundaer-Bitmap: 'bitmap length', 'secondary: always'
    test_bitmap_secondary.cc
    # (0.7.1, FR-7) konfigurierbares BCD-Padding bei ungerader Ziffernzahl
    test_bcd_pad.cc
    # (0.9.0, FR-12/FR-11) bitmap-gesteuerte nested-Container
    test_bitmap_container.cc
    # test_e2e_full_message.cc MUSS der letzte Eintrag bleiben: Catch2
    # registriert TEST_CASEs in der Reihenfolge, in der ihre statischen
    # Registrar-Objekte initialisiert werden. Das ist laut Standard über
    # Translation-Units hinweg nicht garantiert, folgt in der Praxis bei
    # GCC/Clang/MSVC aber der Reihenfolge, in der die Objektdateien dem Linker
    # übergeben werden - also der Reihenfolge dieser Liste. Der ultimative
    # E2E-Test soll dadurch immer zuletzt laufen. Harte Garantie:
    # `libiso8583_tests "[e2e]"` als expliziter Abschlusslauf.
    test_e2e_full_message.cc
)

target_link_libraries(libiso8583_tests
    PRIVATE
        iso8583::iso8583
        Catch2::Catch2WithMain
        fmt::fmt
        Threads::Threads
)

target_include_directories(libiso8583_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# Quellbaum-Wurzel für Fixture-Tests (z.B. issues/a)
target_compile_definitions(libiso8583_tests PRIVATE
    ISO8583_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_features(libiso8583_tests PRIVATE cxx_std_20)

# MSVC: interne Headers (src/*.hh) werden hier direkt eingebunden und ziehen
# über TNG_LOG_* (src/_logger.hh) fmt/format.h mit. fmt stellt eine
# Static-Assertion, die /utf-8 als Kompilierungsoption erfordert - daher
# dasselbe Flag wie für das iso8583-Ziel (s. Haupt-CMakeLists.txt).
if(MSVC)
    target_compile_options(libiso8583_tests PRIVATE /utf-8)
endif()

include(CTest)
include(Catch)
catch_discover_tests(
    libiso8583_tests
    TEST_PREFIX "iso8583::"
    PROPERTIES TIMEOUT 10
)
