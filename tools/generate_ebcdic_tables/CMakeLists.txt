# =============================================================================
# tools/generate_ebcdic_tables — EBCDIC-Orakel-Tool (ICU 78.3)
#
# Reines Build-/CI-Tool: ICU wird hier ausschließlich an dieses Executable
# gelinkt und NIEMALS an Runtime-Targets der Bibliothek. Die Laufzeit-
# EBCDIC-Konvertierung nutzt ausschliesslich die gecheckten Tabellen in
# include/iso8583/_codec.hh (vom Orakel verifiziert, s. pinned/).
#
# Targets (beide EXCLUDE_FROM_ALL-Anhänger, manuell ausführen):
#   update-ebcdic-tables  regneriert pinned/icu_verdicts_{e2a,a2e}.json
#   verify-ebcdic-tables  prüft die gecheckten JSONs gegen das ICU-Orakel
#
# Voraussetzung: ICU 78.3 (vcpkg-Pin in der Wurzel vcpkg.json). Fehlt ICU
# in der Umgebung, wird das Tool still nicht gebaut (Status-Message) —
# die Bibliothek selbst ist davon nicht betroffen.
# =============================================================================

find_package(ICU QUIET COMPONENTS uc data)
if (NOT (ICU_FOUND AND ICU_UC_FOUND AND ICU_DATA_FOUND))
    message(STATUS "ISO8583: ICU nicht gefunden - EBCDIC-Orakel-Tool "
            "(tools/generate_ebcdic_tables) wird nicht gebaut. "
            "ICU 78.3 wird via vcpkg bereitgestellt (Pin in der Wurzel vcpkg.json); "
            "neues Configure ausloesen, damit die Manifest-Abhaengigkeiten installiert werden.")
    return()
endif()

add_executable(iso8583_ebcdic_table_gen main.cc)
target_link_libraries(iso8583_ebcdic_table_gen PRIVATE
    ICU::uc
    ICU::data)
target_compile_features(iso8583_ebcdic_table_gen PRIVATE cxx_std_20)
set_target_properties(iso8583_ebcdic_table_gen PROPERTIES
    EXCLUDE_FROM_ALL ON)

add_custom_target(update-ebcdic-tables
    COMMAND $<TARGET_FILE:iso8583_ebcdic_table_gen> --out ${CMAKE_CURRENT_SOURCE_DIR}/pinned
    DEPENDS iso8583_ebcdic_table_gen
    COMMENT "Regeneriere ICU-78.3-Orakel-Verdicts nach tools/generate_ebcdic_tables/pinned/"
    VERBATIM)

add_custom_target(verify-ebcdic-tables
    COMMAND $<TARGET_FILE:iso8583_ebcdic_table_gen> --expect ${CMAKE_CURRENT_SOURCE_DIR}/pinned
    DEPENDS iso8583_ebcdic_table_gen
    COMMENT "Verifiziere gecheckte EBCDIC-Tabellen-Verdicts gegen das ICU-78.3-Orakel"
    VERBATIM)