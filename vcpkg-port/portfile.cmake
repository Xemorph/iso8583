vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO            Xemorph/iso8583
    REF             v${VERSION}
    SHA512          293a262b21c31f87772037356f5c1119142efa18ad8060074e02b66a62eb883b7ecbec7dd20d14674bcf1ce8e1d3cceee90c64fbe3cff01d883dd1693227777f
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
