vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO            Xemorph/iso8583
    REF             v${VERSION}
    SHA512          56204867dc627156feb87c7eba98f0486b0ef33d5fd526f00db7d6464dde593b8aca61342fbb66becae97b6921f8c05e5cda02f12963f7d478f0c554ee7943bf
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
