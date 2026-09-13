vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO            Xemorph/iso8583
    REF             v${VERSION}
    SHA512          56901d0b120e7f1e4bb8946e795d2151efc040b3b3b2a187f911c44c75d1267c1cb3455b7cee3db4d9c7be77c5622948dda56ae2e7641f3a231a638ce284ca87
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
