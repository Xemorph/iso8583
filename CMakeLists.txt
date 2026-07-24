cmake_minimum_required(VERSION 3.21)

project(libiso8583
    VERSION 0.1.1
    DESCRIPTION "ISO 8583 financial transaction message parser/encoder"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# Standard-Optionen
# -----------------------------------------------------------------------------
option(ISO8583_BUILD_SHARED   "Shared Library (.dll/.so) bauen"          ON)
option(ISO8583_ENABLE_ICONV   "libiconv für EBCDIC-Konvertierung nutzen" ON)
option(ISO8583_INSTALL        "Install-Targets erzeugen"                  ON)
option(ISO8583_BUILD_TESTS    "Unit-Tests bauen (benötigt Catch2)"        OFF)
option(ISO8583_BERTLV         "DE-Schlüsseltyp auf int32_t erweitern (für volle \
BER-TLV/EMV-Tag-Unterstützung, z.B. 2-Byte-Tags wie 9F26, die als Big-Endian-Wert \
> 32767 nicht in int16_t passen)" OFF)

# -----------------------------------------------------------------------------
# C++17 global enforced
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Abhängigkeiten via vcpkg / find_package
# -----------------------------------------------------------------------------
find_package(nlohmann_json  CONFIG REQUIRED)
find_package(fmt            CONFIG REQUIRED)
find_package(yaml-cpp       CONFIG REQUIRED)
find_package(tsl-robin-map  CONFIG REQUIRED)

if(ISO8583_ENABLE_ICONV)
    find_package(Iconv REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Quell-Dateien
# -----------------------------------------------------------------------------
set(ISO8583_SOURCES
    src/config.cc
    src/_codec.cc
    src/_components.cc
    src/_logger.cc
    src/_padder.cc
    src/_parser.cc
    src/_preprocessor.cc
    src/_sourcemap.cc
    src/_spec.cc
    src/_tlv.cc
    src/_utils.cc
)

# _iconv_wrapper.cc/.hh nutzen <iconv.h> UNBEDINGT (kein #ifdef-Guard darin) -
# dürfen deshalb nur Teil des Builds sein, wenn ISO8583_ENABLE_ICONV
# tatsächlich gesetzt ist. Vorher war _iconv_wrapper.cc IMMER Teil von
# ISO8583_SOURCES, unabhängig von der Option - auf Plattformen ohne in libc
# eingebautes iconv (anders als bei glibc/Linux) hätte das den Build entweder
# nicht mehr gelinkt oder eine vom Nutzer bewusst abgeschaltete Abhängigkeit
# klammheimlich zurückgeholt.
if(ISO8583_ENABLE_ICONV)
    list(APPEND ISO8583_SOURCES src/_iconv_wrapper.cc)
endif()

set(ISO8583_PRIVATE_HEADERS
    src/_date.hh
    src/_iconv_wrapper.hh
    src/_logger.hh
    src/_padder.hh
    src/_parser.hh          # ISOBaseParser, ISOFieldParser<> – bewusst NICHT öffentlich
    src/_preprocessor.hh
    src/_sourcemap.hh
    src/_spec.hh
    src/_tlv.hh
    src/_tlv_policy.hh
    src/_utils.hh
    src/fmt_types.hh        # IF_BINARY, IFE_CHAR usw. – nur intern für SpecDecoder
)

set(ISO8583_PUBLIC_HEADERS
    # -- Master-Header --------------------------------------------------------
    include/iso8583/iso8583.h

    # -- Standardanwender-API -------------------------------------------------
    include/iso8583/ISOMessage.hh       # ISOMessage, alle Feldtypen, Header-Klassen
    include/iso8583/ISOSpec.hh          # SpecDecoder::loadFromYaml()
    include/iso8583/ISOLog.hh           # setLevel(), setLogger(), ISOLogger
    include/iso8583/ISOUtils.hh         # utils::makeBitmap()
    include/iso8583/POSDataCode.hh      # pos::POSDataCode - lesbare POS-Fähigkeiten
    include/iso8583/Currency.hh         # currency::Currency, findByAlpha()/findByNumeric()

    # -- Experten-API ----------------------------------------------------------
    include/iso8583/ISOParser.hh        # ISOParserPtrBase (für eigene Parser)

    # -- Implementierungs-Support-Header --------------------------------------
    # (werden von ISOMessage.hh / _interfaces.hh transitiv gebraucht)
    include/iso8583/config.h
    include/iso8583/_codec.hh           # Prefixer + Encoder: Enums, Tabellen, Deklarationen
)

set(ISO8583_DETAIL_HEADERS
    include/iso8583/detail/_codec_impl.hh
    include/iso8583/detail/_interfaces.hh
    include/iso8583/detail/_components.hh      # vollständige ISOMessage-Definition (tsl::robin_map via vcpkg)
    include/iso8583/detail/_currency_table.hh  # generiert, siehe scripts/generate_currency_table.py
)

set(ISO8583_INCLUDED_EXTERNAL_HEADERS
    include/iso8583/detail/extern/dynamic_bitset.hpp
    include/iso8583/detail/extern/libpopcnt.hpp
    include/iso8583/detail/extern/string_view.hpp
)

# -----------------------------------------------------------------------------
# Library-Target
# -----------------------------------------------------------------------------
if(ISO8583_BUILD_SHARED)
    add_library(iso8583 SHARED ${ISO8583_SOURCES} ${ISO8583_PRIVATE_HEADERS})
else()
    add_library(iso8583 STATIC ${ISO8583_SOURCES} ${ISO8583_PRIVATE_HEADERS})
endif()

# Alias für find_package(iso8583 CONFIG)-Nutzung
add_library(iso8583::iso8583 ALIAS iso8583)

# -----------------------------------------------------------------------------
# Compile-Definitionen
# -----------------------------------------------------------------------------
# WICHTIG: ISO8583_BERTLV steht bewusst unter PUBLIC (nicht PRIVATE)!
# Es ändert TNG_KEY_TYPE (include/iso8583/config.h) - eine ABI-relevante
# Einstellung, die in die virtuelle Signatur von ISOComponentPtrBase::key()
# einfließt. PUBLIC sorgt dafür, dass die Definition über den exportierten
# CMake-Alias iso8583::iso8583 automatisch an jeden Konsumenten weitergereicht
# wird (target_link_libraries(... iso8583::iso8583) genügt) - Bibliothek und
# Konsument bauen so garantiert mit demselben Schlüsseltyp.
target_compile_definitions(iso8583
    PUBLIC
        $<$<BOOL:${ISO8583_BERTLV}>:ISO8583_BERTLV>
    PRIVATE
        $<$<BOOL:${ISO8583_ENABLE_ICONV}>:ENABLE_ICONV=1>
        # Windows: beim Bauen der DLL exportieren, beim Nutzen importieren
        $<$<AND:$<BOOL:${ISO8583_BUILD_SHARED}>,$<CXX_COMPILER_ID:MSVC>>:ISO8583_DLL_EXPORTS>
)

# GCC/Clang: Standard-Visibility auf hidden setzen →
#            nur TNG_EXPORT-Symbole sind sichtbar
if(NOT MSVC AND ISO8583_BUILD_SHARED)
    target_compile_options(iso8583 PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
    # _components.cc contains explicit template instantiations for ISOComponent<>.
    # GCC ignores visibility attributes on explicit instantiations when the TU is
    # compiled with -fvisibility=hidden, causing undefined-reference link errors in
    # consumers. Override to default visibility for this file so vtable, typeinfo
    # and all member functions are exported correctly into the shared library.
    set_source_files_properties(src/_components.cc PROPERTIES
        COMPILE_OPTIONS "-fvisibility=default"
    )
endif()

# -----------------------------------------------------------------------------
# Include-Pfade
# -----------------------------------------------------------------------------
target_include_directories(iso8583
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include/iso8583
)

# -----------------------------------------------------------------------------
# Link-Abhängigkeiten
# -----------------------------------------------------------------------------
# WICHTIG: fmt und yaml-cpp sind PRIVATE, nicht PUBLIC!
# Beide sind reine Implementierungsdetails (fmt: nur in src/_logger.hh und
# den .cc-Dateien; yaml-cpp: nur in src/_spec.cc/_preprocessor.cc/_sourcemap.cc)
# und tauchen in KEINEM öffentlichen Header (include/iso8583/*.hh) auf -
# SpecDecoder::loadFromYaml() z.B. nimmt nur einen std::string Pfad entgegen,
# kein YAML::Node. PRIVATE bedeutet: Konsumenten von iso8583::iso8583 müssen
# selbst kein find_package(fmt)/find_package(yaml-cpp) mehr aufrufen, und wenn
# fmt/yaml-cpp als STATISCHE vcpkg-Libs gebaut werden (z.B. Triplet
# x64-windows-static-md statt x64-windows), landet ihr Objektcode direkt in
# libiso8583.so/.dll - kein separates fmt.dll/yaml-cpp.dll mehr zur Laufzeit
# nötig. Mit dem Standard-x64-windows-Triplet (dynamische vcpkg-Libs) ändert
# PRIVATE daran nichts an der Laufzeit-DLL-Abhängigkeit, aber am Compile-Zeit-
# "Konsumenten brauchen find_package(fmt) nicht mehr" schon.
#
# Für statisches fmt/yaml-cpp/... (Windows): CMakePresets.json bringt dafür
# "msvc-debug-static"/"msvc-release-static" mit (VCPKG_TARGET_TRIPLET =
# x64-windows-static-md - statische vcpkg-Libs, aber weiterhin dynamische
# CRT/MD, um Konflikte mit der eigenen iso8583-DLL zu vermeiden).
#
# nlohmann_json und tsl::robin_map bleiben bewusst PUBLIC: `json` (nlohmann::json)
# ist Rückgabetyp von ISOComponentPtrBase::to_json() (öffentlich), und
# tsl::robin_map steckt über ISO_MAP/flat_map in der öffentlichen
# ISOMessage-API (value()/keys()) - beide sind also echte, notwendige
# öffentliche Abhängigkeiten.
target_link_libraries(iso8583
    PUBLIC
        nlohmann_json::nlohmann_json
        tsl::robin_map
    PRIVATE
        fmt::fmt
        yaml-cpp::yaml-cpp
        $<$<BOOL:${ISO8583_ENABLE_ICONV}>:Iconv::Iconv>
)

# -----------------------------------------------------------------------------
# Diagnose: PRIVATE-Linkage (oben) bestimmt NICHT, ob fmt/yaml-cpp/iconv als
# separate DLL neben iso8583.dll landen oder statisch mit hineinkompiliert
# werden - PRIVATE steuert nur, ob Konsumenten von iso8583::iso8583 selbst
# find_package(fmt) etc. aufrufen müssen. Ob es am Ende eine oder mehrere
# DLLs gibt, entscheidet allein der Bibliothekstyp der jeweiligen vcpkg-
# Pakete (gesteuert über VCPKG_TARGET_TRIPLET, z.B. x64-windows = dynamisch,
# x64-windows-static-md = statisch - siehe CMakePresets.json,
# msvc-*-static-Presets). Diese Meldung macht beim Konfigurieren sichtbar,
# was tatsächlich aktiv ist, statt es erst im Output-Verzeichnis zu erraten.
foreach(_iso8583_dep fmt::fmt yaml-cpp::yaml-cpp)
    if(TARGET ${_iso8583_dep})
        get_target_property(_iso8583_dep_type ${_iso8583_dep} TYPE)
        if(_iso8583_dep_type STREQUAL "SHARED_LIBRARY")
            message(STATUS "${_iso8583_dep}: SHARED -> landet als eigene DLL neben iso8583.dll")
        elseif(_iso8583_dep_type STREQUAL "STATIC_LIBRARY")
            message(STATUS "${_iso8583_dep}: STATIC -> wird in iso8583.dll mit hineinkompiliert")
        else()
            message(STATUS "${_iso8583_dep}: ${_iso8583_dep_type} (INTERFACE/header-only - keine eigene Binärdatei)")
        endif()
    endif()
endforeach()
if(ISO8583_ENABLE_ICONV AND TARGET Iconv::Iconv)
    get_target_property(_iso8583_iconv_loc Iconv::Iconv IMPORTED_LOCATION)
    if(NOT _iso8583_iconv_loc)
        get_target_property(_iso8583_iconv_loc Iconv::Iconv IMPORTED_LOCATION_RELEASE)
    endif()
    if(_iso8583_iconv_loc MATCHES "\\.dll$" OR _iso8583_iconv_loc MATCHES "\\.so")
        message(STATUS "Iconv::Iconv: SHARED (${_iso8583_iconv_loc}) -> landet als eigene DLL/SO neben iso8583")
    elseif(_iso8583_iconv_loc)
        message(STATUS "Iconv::Iconv: STATIC (${_iso8583_iconv_loc}) -> wird in iso8583.dll mit hineinkompiliert")
    endif()
endif()

# Auf POSIX: dl für shared-lib-Laufzeitladung (quill benötigt es manchmal)
if(UNIX)
    target_link_libraries(iso8583 PRIVATE dl)
endif()

# -----------------------------------------------------------------------------
# MSVC-spezifisch
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Optionales Maintainer-Target: ISO-4217-Tabelle neu generieren
# -----------------------------------------------------------------------------
# EXCLUDE_FROM_ALL: läuft NICHT bei einem normalen `cmake --build .` mit -
# das ist Absicht (siehe data/iso4217/README.md für die Begründung gegen
# automatisches Nachladen bei jedem Build). Bewusst manuell aufrufen:
#   cmake --build <builddir> --target update-currency-table
# Aktualisiert nur die generierte Datei im Quellbaum; das Ergebnis muss
# separat geprüft und committed werden wie jede andere Code-Änderung.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(update-currency-table
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_currency_table.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generiere include/iso8583/detail/_currency_table.hh aus data/iso4217/codes-all.csv"
        EXCLUDE_FROM_ALL
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
# Install & CMake-Paket-Konfiguration
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Tests (optional, Catch2 via vcpkg)
# -----------------------------------------------------------------------------
if(ISO8583_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
