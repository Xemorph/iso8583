cmake_minimum_required(VERSION 3.21)

project(libiso8583
    VERSION 0.1.1
    DESCRIPTION "ISO 8583 financial transaction message parser/encoder"
    LANGUAGES CXX
)

# ─────────────────────────────────────────────────────────────────────────────
# Standard-Optionen
# ─────────────────────────────────────────────────────────────────────────────
option(ISO8583_BUILD_SHARED   "Shared Library (.dll/.so) bauen"          ON)
option(ISO8583_ENABLE_ICONV   "libiconv für EBCDIC-Konvertierung nutzen" ON)
option(ISO8583_INSTALL        "Install-Targets erzeugen"                  ON)
option(ISO8583_BUILD_TESTS    "Unit-Tests bauen (benötigt Catch2)"        OFF)

# ─────────────────────────────────────────────────────────────────────────────
# C++17 global enforced
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Auf Windows: .lib-Datei neben der .dll ablegen
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)   # Wir nutzen TNG_EXPORT explizit
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Abhängigkeiten via vcpkg / find_package
# ─────────────────────────────────────────────────────────────────────────────
find_package(nlohmann_json  CONFIG REQUIRED)
find_package(fmt            CONFIG REQUIRED)
find_package(yaml-cpp       CONFIG REQUIRED)
find_package(quill          CONFIG REQUIRED)
find_package(tsl-robin-map  CONFIG REQUIRED)

if(ISO8583_ENABLE_ICONV)
    find_package(Iconv REQUIRED)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Quell-Dateien
# ─────────────────────────────────────────────────────────────────────────────
set(ISO8583_SOURCES
    src/config.cc
    src/_codec.cc
    src/_components.cc
    src/_iconv_wrapper.cc
    src/_logger.cc
    src/_padder.cc
    src/_parser.cc
    src/_pos.cc
    src/_preprocessor.cc
    src/_spec.cc
    src/_utils.cc
)

set(ISO8583_PRIVATE_HEADERS
    src/_date.hh
    src/_iconv_wrapper.hh
    src/_logger.hh
    src/_padder.hh
    src/_parser.hh          # ISOBaseParser, ISOFieldParser<> – bewusst NICHT öffentlich
    src/_pos.hh
    src/_preprocessor.hh
    src/_spec.hh
    src/_utils.hh
    src/fmt_types.hh        # IF_BINARY, IFE_CHAR usw. – nur intern für SpecDecoder
)

set(ISO8583_PUBLIC_HEADERS
    # ── Master-Header ────────────────────────────────────────────────────────
    include/iso8583/iso8583.h

    # ── Standardanwender-API ─────────────────────────────────────────────────
    include/iso8583/ISOMessage.hh       # ISOMessage, alle Feldtypen, Header-Klassen
    include/iso8583/ISOSpec.hh          # SpecDecoder::loadFromYaml()
    include/iso8583/ISOLog.hh           # setLevel(), setLogger(), ISOLogger

    # ── Experten-API ──────────────────────────────────────────────────────────
    include/iso8583/ISOParser.hh        # ISOParserPtrBase (für eigene Parser)

    # ── Implementierungs-Support-Header ──────────────────────────────────────
    # (werden von ISOMessage.hh / _interfaces.hh transitiv gebraucht)
    include/iso8583/config.h
    include/iso8583/_codec.hh           # Prefixer + Encoder: Enums, Tabellen, Deklarationen
)

set(ISO8583_DETAIL_HEADERS
    include/iso8583/detail/_codec_impl.hh
    include/iso8583/detail/_interfaces.hh
    include/iso8583/detail/_components.hh      # vollständige ISOMessage-Definition (tsl::robin_map via vcpkg)
)

set(ISO8583_INCLUDED_EXTERNAL_HEADERS
    include/iso8583/detail/extern/dynamic_bitset.hpp
    include/iso8583/detail/extern/libpopcnt.hpp
    include/iso8583/detail/extern/string_view.hpp
)

# ─────────────────────────────────────────────────────────────────────────────
# Library-Target
# ─────────────────────────────────────────────────────────────────────────────
if(ISO8583_BUILD_SHARED)
    add_library(iso8583 SHARED ${ISO8583_SOURCES} ${ISO8583_PRIVATE_HEADERS})
else()
    add_library(iso8583 STATIC ${ISO8583_SOURCES} ${ISO8583_PRIVATE_HEADERS})
endif()

# Alias für find_package(iso8583 CONFIG)-Nutzung
add_library(iso8583::iso8583 ALIAS iso8583)

# ─────────────────────────────────────────────────────────────────────────────
# Compile-Definitionen
# ─────────────────────────────────────────────────────────────────────────────
target_compile_definitions(iso8583
    PRIVATE
        $<$<BOOL:${ISO8583_ENABLE_ICONV}>:ENABLE_ICONV=1>
        # Windows: beim Bauen der DLL exportieren, beim Nutzen importieren
        $<$<AND:$<BOOL:${ISO8583_BUILD_SHARED}>,$<CXX_COMPILER_ID:MSVC>>:ISO8583_DLL_EXPORTS>
)

# GCC/Clang: Standard-Visibility auf hidden setzen →
#            nur TNG_EXPORT-Symbole sind sichtbar
if(NOT MSVC AND ISO8583_BUILD_SHARED)
    target_compile_options(iso8583 PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Include-Pfade
# ─────────────────────────────────────────────────────────────────────────────
target_include_directories(iso8583
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include/iso8583
)

# ─────────────────────────────────────────────────────────────────────────────
# Link-Abhängigkeiten
# ─────────────────────────────────────────────────────────────────────────────
target_link_libraries(iso8583
    PUBLIC
        nlohmann_json::nlohmann_json
        fmt::fmt
        yaml-cpp::yaml-cpp
        quill::quill
        tsl::robin_map
    PRIVATE
        $<$<BOOL:${ISO8583_ENABLE_ICONV}>:Iconv::Iconv>
)

# Auf POSIX: dl für shared-lib-Laufzeitladung (quill benötigt es manchmal)
if(UNIX)
    target_link_libraries(iso8583 PRIVATE dl)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# MSVC-spezifisch
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(iso8583 PRIVATE
        /W4          # Hohe Warnstufe
        /WX-         # Warnungen NICHT als Fehler (vorhandener Code hat C4251 etc.)
        /utf-8       # Quelldateien als UTF-8 lesen
        /MP          # Parallele Kompilierung
        /wd4251      # class 'X' benötigt dll-interface für Clients
        /wd4275      # non-dll-interface base class
    )
    # Generiere .pdb auch für Release
    target_compile_options(iso8583 PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(iso8583 PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install & CMake-Paket-Konfiguration
# ─────────────────────────────────────────────────────────────────────────────
if(ISO8583_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS iso8583
        EXPORT  iso8583Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}          # .dll (Windows)
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}          # .so  (Linux)
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}          # .lib / .a
    )

    # Öffentliche Header installieren
    install(FILES ${ISO8583_PUBLIC_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iso8583
    )

    install(FILES ${ISO8583_DETAIL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iso8583/detail
    )

    install(FILES ${ISO8583_INCLUDED_EXTERNAL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iso8583/detail/extern
    )

    # CMake-Targets-Datei exportieren
    install(EXPORT iso8583Targets
        FILE        iso8583Targets.cmake
        NAMESPACE   iso8583::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iso8583
    )

    # Config-Datei generieren
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/iso8583Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/iso8583Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iso8583
    )

    # Version-Datei generieren
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/iso8583ConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/iso8583Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/iso8583ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iso8583
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Tests (optional, Catch2 via vcpkg)
# ─────────────────────────────────────────────────────────────────────────────
if(ISO8583_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
