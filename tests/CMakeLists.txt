cmake_minimum_required(VERSION 3.21)

find_package(Catch2 CONFIG REQUIRED)
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
        Threads::Threads
)

target_include_directories(libiso8583_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(libiso8583_tests PRIVATE cxx_std_17)

include(CTest)
include(Catch)
catch_discover_tests(
    libiso8583_tests
    TEST_PREFIX "iso8583::"
    PROPERTIES TIMEOUT 10
)
