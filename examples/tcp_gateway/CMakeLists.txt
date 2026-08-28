# =============================================================================
# examples/tcp_gateway - minimaler ISO-8583 TCP-Gateway (Demo)
# =============================================================================
add_executable(iso8583_tcp_gateway gateway.cc)

target_link_libraries(iso8583_tcp_gateway
    PRIVATE
        iso8583::iso8583
        $<IF:$<PLATFORM_ID:Windows>,ws2_32,>
)

target_compile_features(iso8583_tcp_gateway PRIVATE cxx_std_20)

# Shared-Build: Konsumenten muessen ISO8583_DLL definieren (dllimport) -
# das macht die Export-Makros in config.h zur Import-Seite.
if(ISO8583_BUILD_SHARED AND MSVC)
    target_compile_definitions(iso8583_tcp_gateway PRIVATE ISO8583_DLL)
endif()

if(MSVC)
    target_compile_options(iso8583_tcp_gateway PRIVATE /W4 /utf-8 /MP)
endif()

# Bei Shared-Builds: die Lib-Binary neben das Gateway kopieren, damit die
# Demo ohne extra Setup direkt startet.
if(ISO8583_BUILD_SHARED)
    add_custom_command(TARGET iso8583_tcp_gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:iso8583>
            $<TARGET_FILE_DIR:iso8583_tcp_gateway>
        COMMENT "Kopiere iso8583.dll/.so neben iso8583_tcp_gateway"
    )
endif()

# Spec-Dateien ins Binary-Verzeichnis legen (Default-Pfad des Gateways).
foreach(_spec ascii_spec.yml spec_ebcdic.yml)
    add_custom_command(TARGET iso8583_tcp_gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/${_spec}
            $<TARGET_FILE_DIR:iso8583_tcp_gateway>
    )
endforeach()