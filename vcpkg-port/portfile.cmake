vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO            Xemorph/iso8583
    REF             v${VERSION}
    SHA512          5f411fbd63d3e3ad694def76bab7b8bf7872bf675ad0433aa1db236ad2efcd147eb637bb389e0da439c9708f861928591321a8f91759a4ffe3b7747175442db0
    HEAD_REF        main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DISO8583_BUILD_SHARED=ON
        -DISO8583_INSTALL=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME iso8583 CONFIG_PATH lib/cmake/iso8583)

# Doppelte Header-Installation aus dem Debug-Tree entfernen
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# Lizenz-Datei installieren
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# Nutzungshinweis
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
