vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO            Xemorph/iso8583
    REF             v${VERSION}
    SHA512          398f92fd51a8dbd9af98d8315368e5d2618f7e7e75b92883ce94d40294ed9fdb491cb1c7cf28fe4b76952bcd5c9a2dc0b96cb2422a6f04d0ef14d5f59474d900
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
