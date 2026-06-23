cmake_minimum_required(VERSION 3.21)

find_package(Catch2 CONFIG REQUIRED)

add_executable(libiso8583_tests
    test_prefixer_encoder.cc
    test_field_parser.cc
    test_iso_message.cc
    test_full_unparse.cc
    test_spec_loader.cc
)

target_link_libraries(libiso8583_tests
    PRIVATE
        iso8583::iso8583
        Catch2::Catch2WithMain
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
