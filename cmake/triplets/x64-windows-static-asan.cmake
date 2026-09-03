# =============================================================================
# x64-windows-static-asan — Custom Triplet fuer den MSVC-ASan-Build
# =============================================================================
#
# Wird vom "debug-asan"-Preset und vom CI-Job "build-windows-asan" verwendet.
#
# Warum nicht einfach x64-windows-static?
#   MSVC-ASan instrumentiert die STL-Typen (std::vector/string/optional) mit
#   "annotate_*"-Wrapper-Symbolen. Werden vcpkg-Abhaengigkeiten OHNE ASan
#   gebaut (z.B. Catch2d.lib, fmt) und gegen ASan-instrumentierte Test-Objekte
#   in EINEM Link-Schritt gelinkt, bricht das mit:
#       error LNK2038: mismatch detected for 'annotate_vector' ...
#   Deshalb bekommen hier ALLE vcpkg-Pakete die ASan-Flags injiziert, damit
#   auch fmt/catch2 (die einzigen nicht-header-only C++-Deps) identisch
#   instrumentiert sind. (iconv ist C-Only -> ohne STL -> irrelevant.)
#
#   Mechanismus (vcpkg 2026, Pin 1f5e0348): `VCPKG_CMAKE_CONFIGURE_FLAGS`
#   existiert nicht mehr; vcpkg leitet stattdessen die Triplet-Variablen
#   VCPKG_CXX_FLAGS / VCPKG_C_FLAGS in jeden CMake-Build der Abhaengigkeiten
#   weiter (vcpkg_configure_cmake -> -DVCPKG_CXX_FLAGS=...).
#
# CRT/Linkage: wie x64-windows-static (statische CRT /MT[d], statische Libs)
# — passt zur MultiThreadedDebug-Einstellung des Presets.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CXX_FLAGS "/fsanitize=address")
set(VCPKG_C_FLAGS "/fsanitize=address")