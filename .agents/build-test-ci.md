# Build system, testing, CI/IDE

> Verbatim section(s) §3, §7, §9 of the former monolithic AGENTS.md (section numbers are stable; other docs cite them). Entry point: `../AGENTS.md`.

## 3. Build system

### Requirements
- C++20 compiler (GCC ≥ 12 recommended — CI uses GCC-13; Clang ≥ 13; MSVC VS2022).
  **C++20 is mandatory, not a preference:** `detail/_codec_impl.hh` contains
  `constexpr` functions (`as<>`/`to<>`) that construct local `std::string` /
  `std::vector` objects — only legal in C++20 (P0980 literal containers).
  GCC-11 / C++17 combinations fail to instantiate the headers.
- CMake ≥ 3.21, [vcpkg](https://vcpkg.io) in **manifest mode**
  (`vcpkg.json` at repo root; presets read `$env{VCPKG_ROOT}`).
- **MSVC + Ninja:** run from a *Developer Command Prompt* (or `vcvars64.bat`
  first) — otherwise the Ninja generator lacks the SDK environment and you
  get `LNK1104: kernel32.lib`. (Also documented in `examples/tcp_gateway/README.md`.)

### Dependencies (all via vcpkg)
| Dep | Linkage | Why |
|---|---|---|
| `nlohmann-json` | **PUBLIC** | `nlohmann::json` is the return type of `ISOComponentPtrBase::to_json()` (public API). |
| `tsl-robin-map` | **PUBLIC** | `tsl::robin_map`/`ISO_MAP` is exposed in the `ISOMessage` API (`value()`/`keys()`). |
| `fmt` | **PRIVATE** | Implementation detail (logger only); consumers must not `find_package(fmt)`. |
| `ryml` (rapidyaml ≥ 0.15.2) | **PRIVATE** | YAML spec loading only (`_spec.cc`, `_preprocessor.cc`). |
| `icu` (**78.3**) | build/CI only — **never linked into runtime targets** | Oracle for `tools/generate_ebcdic_tables` (regenerates/verifies the pinned EBCDIC verdict JSONs; §4, §12). |
| `catch2` | tests only | Unit tests. |

Keep this PUBLIC/PRIVATE split — it is intentional and documented in
`CMakeLists.txt`. (yaml-cpp was removed in 0.2.0 during the rapidyaml
migration — it is no longer used anywhere. `libiconv`/`ISO8583_ENABLE_ICONV`
was removed in 0.4.0 — see the changelog.) Note on the `icu` pin: the
pinned vcpkg baseline (1f5e034) predates manifest support for an exact
`"version"` field, so `vcpkg.json` uses `"version>=": "78.3"` **plus the
identical `builtin-baseline` on all machines/CI** — together they resolve to
exactly 78.3. The generator's hard ICU-major-78 assertion (exit 2) is the
drift watchdog for future baseline bumps.

### CMake options
| Option | Default | Meaning |
|---|---|---|
| `ISO8583_BUILD_SHARED` | `ON` | Build shared library (`.dll`/`.so`) instead of static. |
| `ISO8583_INSTALL` | `ON` | Generate install targets / CMake package. |
| `ISO8583_BUILD_TESTS` | `OFF` | Build Catch2 tests. |
| `ISO8583_BUILD_EXAMPLES` | `OFF` | Build `examples/tcp_gateway`. |
| `ISO8583_BUILD_CODEC_TOOLS` | `OFF` | Build the EBCDIC oracle tool (`tools/generate_ebcdic_tables`; needs ICU 78.3 — if absent the sub-project is skipped with a status message). Targets `update-ebcdic-tables` / `verify-ebcdic-tables` regenerate/verify the pinned oracle verdicts; ICU is linked to the tool executable only, never into the library. |
| `ISO8583_BERTLV` | `OFF` | Widen DE key type to `int32_t` (full BER-TLV/EMV tag support, e.g. 2-byte tags like `9F26`). **ABI-relevant, set as `PUBLIC` compile definition on the target** so it propagates to all consumers via `iso8583::iso8583`. |

### Presets (`CMakePresets.json`, all need `VCPKG_ROOT` set)
| Preset | Generator | Notes |
|---|---|---|
| `debug` | Ninja | `ISO8583_BUILD_TESTS=ON`. Standard dev preset (also the one `.clangd` points at). |
| `release` | Ninja | tests OFF. |
| `debug-bertlv` | Ninja | `debug` + `ISO8583_BERTLV=ON` (int32_t keys). |
| `msvc-debug` / `msvc-release` | VS 2022 x64 | Multi-config. |
| `msvc-debug-static` / `msvc-release-static` | VS 2022 x64 | `VCPKG_TARGET_TRIPLET=x64-windows-static-md` — static vcpkg libs (fmt etc. compiled into `iso8583.dll`), dynamic CRT. |

Typical flow:
```bash
cmake --preset debug            # configure (builds compile_commands.json too)
cmake --build --preset debug
ctest --preset debug            # or: ./build/debug/tests/libiso8583_tests
```

### Compiler flags of note
- MSVC: `/W4 /WX- /utf-8 /MP /wd4251 /wd4275`; `/Zi` + `/OPT:REF /OPT:ICF` in Release.
- GCC/Clang shared builds: `-fvisibility=hidden -fvisibility-inlines-hidden` — but `src/_components.cc` is a deliberate exception (`-fvisibility=default`), because explicit template instantiations of `ISOComponent<>` would otherwise be ignored for visibility by GCC and cause undefined-reference errors in consumers.
- Windows output dirs: `build/<preset>/bin` (dll), `build/<preset>/lib` (lib).
- `CMAKE_EXPORT_COMPILE_COMMANDS ON` always (for clangd).

### Consume in another project
```cmake
find_package(iso8583 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE iso8583::iso8583)
# Windows DLL consumers additionally: target_compile_definitions(your_target PRIVATE ISO8583_DLL)
```
Or via the vcpkg overlay port:
```bash
cmake -B build -DVCPKG_OVERLAY_PORTS=path/to/this-repo/vcpkg-port ...
```

---

## 7. Testing

```bash
cmake --preset debug -DISO8583_BUILD_TESTS=ON
cmake --build --preset debug
ctest --preset debug          # or: ./build/debug/tests/libiso8583_tests
```

- Framework: **Catch2** (`Catch2::Catch2WithMain`), per-test CTest timeout 10 s, prefix `iso8583::` via `catch_discover_tests` (Catch2 tags also surface as CTest labels).
- **New test files must be registered** in the `add_executable(libiso8583_tests …)` list in `tests/CMakeLists.txt` — an unregistered file is neither compiled nor run.
- `ctest --preset debug` **excludes tests tagged `slow`** (the only tag used by a ctest preset filter); to run everything use the binary directly.
- Run the end-to-end suite alone: `libiso8583_tests "[e2e]"` (convention: closing run).
- ⚠️ `tests/CMakeLists.txt`: `test_e2e_full_message.cc` **must remain the last source** — Catch2 registers `TEST_CASE`s in object-file link order (not guaranteed by the standard, but stable in practice), and the E2E test must run last.
- Test areas: codec, field parser, message, full unparse, spec loader (largest file), preprocessor, remaining-field, TLV parser, headers (BASE1/WLP-FO), POS data code, currency, logging, dump operator, utils; e2e full message round-trip.

**Catch2 tags** (run targeted suites via the binary, e.g. `libiso8583_tests "[ber]"`):
`e2e`, `slow`, `message`, `error`, `tlv`, `spec`, `utils`, `prefixer`, `ber`, `preprocessor`, `mti`, `encoder`, `crud`, `field`, `bitmap`, `unparse`, `logging`, `header`, `currency`, `roundtrip`, `ebcdic`.

**Manual E2E of the gateway example:** `python examples/tcp_gateway/send_test.py [host] [port] [ascii|ebcdic]` against a running `iso8583_tcp_gateway` (defaults `127.0.0.1 9000 ascii`). Invariant: the client's `ASCII_TO_EBCDIC` table must stay **identical to `kAsciiToEbcdic` in `include/iso8583/_codec.hh`** (IBM-1047) — changing one side without the other breaks the example silently.

---

## 9. CI & IDE

### CI (`.github/workflows/ci.yml`) — three jobs

| Job | Runner | Toolchain | Details |
|---|---|---|---|
| `build-linux` | `ubuntu-latest` | **GCC-13 + Ninja** (`CC=gcc-13`, `CXX=g++-13`), vcpkg pinned | Debug + Release matrix; `ISO8583_BUILD_SHARED=ON`, `ISO8583_BUILD_TESTS=ON`, `ISO8583_INSTALL=OFF`; `ctest --no-tests=error --timeout 30`; test results uploaded as artifact. |
| `build-windows` | `windows-latest` | **MSVC x64** (`ilammy/msvc-dev-cmd@v1`) + Ninja (`choco install ninja`) | Same flags; `ctest --build-config <cfg>`; artifact upload. |
| `ci-success` | `ubuntu-latest` | — | Summary of both builds; configure it as the **Required status check** for branch protection (repo Settings → Branches → "CI passed"). |

- Both build jobs pin vcpkg to commit `1f5e0348089e8a9b187f57d42866ebc871e815da` — identical to `builtin-baseline` in `vcpkg.json`. Keep the two in sync when bumping the baseline.
- **To reproduce CI locally:** match the compiler (GCC-13 or MSVC v143) *and* the pinned vcpkg commit — "works in CI, fails locally" is usually a vcpkg-baseline or compiler mismatch.
- `docs.yml` (separate workflow): builds Sphinx/Doxygen docs on pushes touching `include/` or `docs/`, publishes to GitHub Pages from `main`.

### clangd (`.clangd`)
- `CompilationDatabase: build/debug` — adjust if you work in another preset tree; **the tree must match the library ABI** (e.g. BERTLV int32 keys), or clangd reports false errors on virtual-override signatures.
- Fallback flags (`-std=c++20 -Iinclude -Isrc`) work without a build tree but lack vcpkg headers.
- Background indexing of `build/` is skipped.
- Very old clangd (< 20) may choke on MSVC-2026 STL headers (`STL1000`); Zed's bundled clangd is fine.

---
