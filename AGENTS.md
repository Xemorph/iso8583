# AGENTS.md — libiso8583 project reference

Consolidated context for AI agents / developers working on this repository.
Read this before making changes. It summarizes build setup, architecture,
public API, extension workflows, and project-specific pitfalls. For the
detailed public-API reference see [`include/iso8583/AGENTS.md`](include/iso8583/AGENTS.md)
(canonical API doc, embedded into the Sphinx docs via
[`docs/agents.md`](docs/agents.md)).

---

## 1. What this project is

**libiso8583** (CMake project name `libiso8583`, target `iso8583`) is a
**C++20 library to parse and build ISO-8583 financial messages** — the
protocol used by Visa, Mastercard and most payment networks.

Core workflow (terminology is inverted vs. most parsers, by design):

```
YAML-Spec ─► SpecDecoder::loadFromYaml()      ─► ISOParserPtrBase
          └► SpecDecoder::loadBothFromYaml()  ─► ISOParserPtrBase + ISOSpec (introspection)

parser + wire bytes ─► Message::unparse() ─► Message (DECODED fields)
Message              ─► parser->parse()    ─► wire bytes  (ENCODED)
```

- `unparse()` = decode (wire bytes → fields)
- `parse()` = encode (fields → wire bytes)

Message formats are **declarative, described in YAML spec files** — nothing
is hard-wired per network (Visa/MC/etc.). The loader supports
`!use`, `!merge`, `!template`, `!include_files`, nested/TLV fields, and
EBCDIC/BCD/ASCII/BINARY encodings.

**License: proprietary, source-available — NOT open source.** Usage outside
private use, education, research, and internal evaluation requires written
permission from the author. See [`LICENSE`](LICENSE). Do not publish derived
code as OSS. (A small set of permissively licensed third-party files is
vendored inside the tree — see §14.3; their license headers are kept.)

---

## 2. Repository layout

| Path | Purpose |
|---|---|
| `include/iso8583/` | **Public headers.** `iso8583.h` (master), `ISOMessage.hh`, `ISOSpec.hh`, `ISOLog.hh`, `ISOUtils.hh`, `POSDataCode.hh`, `Currency.hh`, `ISOParser.hh` (expert/custom-parser API), `_codec.hh` (codec enums/tables), `config.h` (namespace/visibility/key-type), `AGENTS.md` (primary API reference). |
| `include/iso8583/detail/` | Implementation-support headers (`_components.hh` = full `Message`/`ISOComponent` definitions, `_interfaces.hh`, `_codec_impl.hh`, `_currency_table.hh` (generated), `extern/` = bundled `dynamic_bitset`, `libpopcnt`, `string_view`). **Never include `detail/` from user code.** |
| `src/` | Private implementation. Underscore-prefixed headers (`_parser.hh`, `_spec.hh`, `_tlv.hh`, `_preprocessor.hh`, `_sourcemap.hh`, `_padder.hh`, `_utils.hh`, `_logger.hh`, `_date.hh`, `_iconv_wrapper.hh`, `_tlv_policy.hh`) are **deliberately private**; `fmt_types.hh` (IFE_* aliases) is private. `config.cc` implements version/namespace plumbing. |
| `tests/` | Catch2 unit tests (registration in `tests/CMakeLists.txt` — see §7). |
| `examples/tcp_gateway/` | Opt-in example: minimal TCP gateway decoding one ISO-8583 message per connection (ASCII + EBCDIC/IBM-1047 specs, Python test client `send_test.py`). Built only with `ISO8583_BUILD_EXAMPLES=ON`. Manual test: `python send_test.py [host] [port] [ascii\|ebcdic]` (see §12.5). |
| `docs/` | Sphinx + Doxygen + Breathe (+ Furo/MyST) documentation; `internals/yaml_format.md`, `internals/encoding.md`; `Doxyfile`, `conf.py`, `requirements.txt`, `build_docs.bat`. Built output in `docs/_build/`, Doxygen XML in `docs/_doxygen/` (both git-ignored). |
| `data/iso4217/` | ISO-4217 currency source data (`codes-all.csv`, vendored for reproducible/air-gapped builds — see `data/iso4217/README.md`). |
| `scripts/generate_currency_table.py` | Regenerates `include/iso8583/detail/_currency_table.hh` from the CSV (run via CMake target `update-currency-table`, which is `EXCLUDE_FROM_ALL` — manual only, commit the result). |
| `cmake/iso8583Config.cmake.in` | CMake package config template for `find_package(iso8583 CONFIG)`. |
| `vcpkg-port/` | vcpkg port for downstream consumption (`portfile.cmake`, `vcpkg.json`, `usage`). Portfile fetches tag `v${VERSION}`; its SHA512 is a placeholder `0` until filled after the first release tag is pushed (release step — see §14.2). Repo URL: `Xemorph/iso8583`. |
| `vcpkg/` | **Local, untracked** vcpkg checkout (`.gitignore`) — NOT part of the repository. `vcpkg-configuration.json` (tracked) is `{}` — no custom registries/overrides. Fresh clone: point `VCPKG_ROOT` at any vcpkg checkout; manifest mode resolves `vcpkg.json` automatically. |
| `tools/generate_ebcdic_tables/` | **Build/CI-only** EBCDIC oracle tool (ICU 78.3): regenerates/verifies the pinned IBM-1047 verdict JSONs that prove the checked-in codec tables (`include/iso8583/_codec.hh`) are deterministic. Built only with `ISO8583_BUILD_CODEC_TOOLS=ON`; ICU is linked to the tool executable, never into the library. See §4 (Determinism & oracle pin). |
| `.gitattributes` | Forces LF on `tools/generate_ebcdic_tables/pinned/*.json` so the byte-stable oracle pin survives checkouts with `core.autocrlf=true`. |
| `CMakeLists.txt`, `CMakePresets.json` | Build definition (see §3). |
| `.clangd` | clangd/IDE config (see §9). |
| `.github/workflows/ci.yml`, `docs.yml` | CI (Linux GCC-13 **and** Windows MSVC, both Ninja — see §9) and GitHub-Pages docs publishing. |
| `.cache/` | Local CMake-LSP (neocmakelsp) cache — git-ignored, do not edit. |
| `scratch/` | Local-only scratch area: diagnostic probes, build logs, hand-off notes, one-off scripts (git-ignored, disposable, never committed). |
| `binaries/` | Local-only probe binaries (`.exe`/`.obj`/`.pdb`/`.ilk`) moved out of the repo root (git-ignored). |
| `changelog.md` | Change history (tracked; `docs/changelog.md` is a tracked mirror — **update both**, see §14.2). |
| `IDEA.md` | One-liner: "C++20 library to parse ISO 8583 messages". |

**Version strings** appear in four places and must be bumped together on
every release (procedure: §14.2):
1. `project(VERSION …)` in `CMakeLists.txt`
2. `TNG_CORE_VERSION` in `include/iso8583/config.h`
3. `version` in root `vcpkg.json`
4. `version` in `vcpkg-port/vcpkg.json`

Currently all four say **0.2.1** (synced). If you ever observe skew, the
release that introduced it missed a spot — fix it, don't normalize to the
wrong value.

---

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
| `libiconv` | optional (`ISO8583_ENABLE_ICONV`, default ON; not needed on Linux/glibc) | **[DEPRECATED since 0.3.0, removal 0.4]** transitional EBCDIC fallback only — the codec is fully table-driven (ICU-78.3-oracle-verified tables, §4) and does not use libiconv at runtime. |
| `icu` (**78.3**) | build/CI only — **never linked into runtime targets** | Oracle for `tools/generate_ebcdic_tables` (regenerates/verifies the pinned EBCDIC verdict JSONs; §4, §12). |
| `catch2` | tests only | Unit tests. |

Keep this PUBLIC/PRIVATE split — it is intentional and documented in
`CMakeLists.txt`. (yaml-cpp was removed in 0.2.0 during the rapidyaml
migration — it is no longer used anywhere.) Note on the `icu` pin: the
pinned vcpkg baseline (1f5e034) predates manifest support for an exact
`"version"` field, so `vcpkg.json` uses `"version>=": "78.3"` **plus the
identical `builtin-baseline` on all machines/CI** — together they resolve to
exactly 78.3. The generator's hard ICU-major-78 assertion (exit 2) is the
drift watchdog for future baseline bumps.

### CMake options
| Option | Default | Meaning |
|---|---|---|
| `ISO8583_BUILD_SHARED` | `ON` | Build shared library (`.dll`/`.so`) instead of static. |
| `ISO8583_ENABLE_ICONV` | `ON` | **[DEPRECATED since 0.3.0, removal 0.4]** transitional libiconv fallback for EBCDIC — the codec is fully table-driven and does not call libiconv at runtime anymore; configure emits a WARNING when ON. Set OFF to drop the libiconv dependency. Only when ON is `src/_iconv_wrapper.cc` compiled (it unconditionally uses `<iconv.h>`). |
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

## 4. Core concepts

### Namespace & key type
- Everything public lives in `iso8583` (`TNG_NAMESPACE`, macro overridable by consumers).
- DE keys: `TNG_KEY_TYPE` / `iso8583::key_type` — **`int16_t` by default**, `int32_t` when `ISO8583_BERTLV` is defined, freely overridable via `#define ISO8583_KEY_TYPE <type>` (highest priority).
  - Needed for EMV: real 2-byte BER-TLV tags with first byte ≥ 0x80 (`9F26`, `5F24`, …) exceed `int16_t`'s max (32767) as big-endian values.
  - **ABI-critical**: flows into the virtual signature of `ISOComponentPtrBase::key()`. Every consumer of a shared library must be compiled with the same key type. Via CMake this is automatic (PUBLIC definition); with manual includes the macro must be set identically in library *and* consumers.
- Special key values: `-1` = root `Message` (not a sub-field), `0` = MTI slot, `1` = primary bitmap slot, `-2` = internally reserved (TLV TCC fields). DEs 2–64 primary bitmap, 65–128 secondary, 129–192 tertiary.

### Object model
All field components derive from `ISOComponentPtrBase` (abstract) /
`ISOComponent<key,value>` (concrete template) and are always held in
`std::shared_ptr`. Public leaf/composite types:

| Type | Value type | Typical use |
|---|---|---|
| `iso8583::OpaqueField` | `std::string` | text, PAN, amounts, EBCDIC/BCD as char string |
| `iso8583::BinaryField` | `std::vector<uint8_t>` | PIN-block, ICC/EMV data, cryptograms (set via **uppercase hex string**) |
| `iso8583::FastBinaryField` | `std::vector<std::byte>` | binary with `std::byte` storage |
| `iso8583::Bitmap` | `dynamic_bitset<>` (bundled in `detail/extern/`) | primary/secondary bitmap — **never set manually, auto-computed** |
| `iso8583::CodeField` | `int32_t` | numeric answer codes |
| `iso8583::Message` | `ISO_MAP` (see below) | composite/nested sub-message |
| `iso8583::ISOTaggedField` | — | decoded TLV SE (tag + referenced field) |

> Old names `ISOOpaqueField`, `ISOBinaryField`, `ISOBitmap`, `ISOCodeField`,
> `ISOFastBinaryField`, `ISOMessage` are **deprecated aliases** — write new
> code with the new names (`OpaqueField`, `Message`, …).

**`ISO_MAP` is fixed, not user-selectable.** It is a typedef
(`include/iso8583/detail/_components.hh`):
`detail::flat_map<TNG_KEY_TYPE, std::shared_ptr<ISOComponentPtrBase>>` with
`detail::flat_map = tsl::robin_map`. There is no `TNG_MAP_TYPE`-style hook —
the container is part of the public API and the library ABI, so do not try
to swap in e.g. `std::map`.

### Message API in one paragraph
```cpp
auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mastercard.yml");

auto msg = std::make_shared<iso8583::Message>("0200");  // MTI in ctor (optional)
msg->parser(parser);                                     // REQUIRED before unparse/parse
msg->unparse(msg, rawWireBytes);                         // decode
// or build:
msg->set(2, "4111111111111111");   // plain DE → OpaqueField
msg->set(52, "0102030405060708");  // BinaryField: uppercase hex string
msg->set("48.72.1", "ABC");        // dot-notation nesting (DE48 → SE72 → tag 1)
std::vector<uint8_t> wire = parser->parse(msg);          // encode
```

Field accessors (choose the right one):
- `msg->get<T>(key)` → `shared_ptr<T>` or `nullptr`
- `msg->tryGet<T>(key)` → `std::optional<shared_ptr<T>>`
- `msg->tryGetValue<T>(key)` → `std::optional<ValueType>` (copy)
- `msg->tryGetValueRef<T>(key)` → `std::optional<reference_wrapper>` (zero-copy)
- `iso8583::utils::getOrThrow<T>(msg, key)` / `getOrDefault<T>(msg, key, fallback)` / `ifPresent<T>(msg, key, fn)` / `flatten(msg)` (flat map `"48.72.1" → value`) / `utils::makeBitmap(des...)`.

Other `Message` features: `mti()` (throws `std::logic_error` if no MTI — check `hasMTI()` first), `to_json()`, `dump(os)` / `operator<<`, wire-position tracking (`wire_offset()`/`wire_length()` per field, set by `unparse()` — beware the `0`-ambiguity, §12.2), and MTI classification helpers: `isAuthorization()`, `isRequest()`, `isResponse()`, `isFinancial()`, `isFileAction()`, `isReversal()`, `isChargeback()`, `isReconciliation()`, `isAdministrative()`, `isFeeCollection()`, `isNetworkManagement()`, `isRetransmission()`.

Headers (network framing attached to a message): `BaseHeader`,
`BASE1Header` (Visa; `isRejected()`, `getRejectCode()`), `WLP_FOHeader`
(Worldline). `msg->header(hdr)` before encode/decode.

### Spec loading & introspection
- `spec::SpecDecoder::loadFromYaml(path)` → `ISOParserPtrBase` (cheap).
- `spec::SpecDecoder::loadBothFromYaml(path)` → `tuple<parser, shared_ptr<ISOSpec>>` (extra pass over the field map; use when you need runtime field metadata).
- `...Cached` variants: process-wide cache (max 64 entries total, LRU eviction). `CacheValidation::CheckEveryCall` (default; mtime pre-filter ~1 µs, SHA-256 re-hash over all source files when mtime changed, publish-then-verify — a parser is published only under the exact file snapshot it was built from, so a hot-swapped spec can never yield a "mixed" parser) vs `CacheValidation::TrustUntilInvalidated` (~25 ns hit, no change detection — **unsafe for specs that may change at runtime**: restart the process on spec change, call `SpecDecoder::invalidateCache(path)` manually, or use `CheckEveryCall`). `SpecDecoder::clearCache()` empties everything.
- **`SpecLoadOptions`** (0.3.0, full reference in `docs/internals/yaml_format.md`): the load overloads `load{Both}FromYaml{Cached}(path, const SpecLoadOptions&, …)` take a trust model. Defaults: `sandbox=true` (`!include_files` entries resolving outside `roots` — `../`-traversals, absolute/UNC paths, symlink escapes — are **rejected fail-closed**; empty `roots` = parent dir of the top-level spec, which is user-supplied and not sandboxed), `allowSmapWrite=true` (sidecar is only written if inside the sandbox roots), `maxSpecBytes=32 MiB` (per source file, enforced while streaming), `maxIncludeFiles=1024` (distinct files per load), `maxSmapBytes=16 MiB` (oversize sidecars are discarded and regenerated). The legacy `bool trackSourceMap` overloads remain source-compatible and build defaults internally.
- `ISOSpec` introspection: `spec->name()`, `spec->encoding()`, `spec->has(de)`, `spec->field(de)` → `SpecFieldInfo{key, description, format{type, prefix_digits, max_length}, encoding, is_nested, is_bitmap, children}`, `spec->fields()` (key-ordered range).

### Logging (`iso8583::log`)
- `log::setLevel(log::Level)` — default is **WARN**; levels up to `OFF`.
- `log::setLogger(&myLogger)` with a class deriving `log::ISOLogger` (pure virtual `log(Level, file, line, message)`).
- **Quill integration with a DLL build: use `log::QuillBridge` (include `<quill/LogMacros.h>` BEFORE `<iso8583/ISOLog.hh>`), never `setQuillLogger()`** — Quill's process-singleton is broken across the DLL boundary.
- `fmt` is a PRIVATE dependency: the library does not leak fmt into public headers.
- **PCI/production: keep the level at WARN or lower.** `INFO`/`DEBUG` add per-field encode/decode detail (sizes, offsets, descriptions — never raw values of `sensitive` fields). The only surface where raw field values reach log output is `dump()` (e.g. when the application logs a dump) — mark card data with `sensitive: true` (§5) so dumps show `***`.

### Thread safety
- **One `ISOMessage` from N threads: supported** (model since 0.3.0): every public entry point (`set`/`unset`/`has`/`get`/`tryGet`/`tryGetValue`/`tryGetValueRef`/`reset`/`keys`/`size`/`to_json`/`dump`/`parser`/`parse`/`unparse`/`header`/`direction`/`hasMTI`/`mti`/`isRequest`/… ) acquires the same **recursive message lock** exactly once; internal call chains (e.g. `parse → recalcBitmap → set`, parser callbacks into `set()`) run under the already-held lock. Writers and readers are mutually exclusive (single lock, no parallel-reader mode). `to_json`/`dump` snapshot the field set under the lock and format outside it.
- **Parsers are immutable after load** → shareable across threads and across messages (concurrent `parse`/`unparse` on *different* messages using the same parser is safe).
- Logger globals (`setLevel`/`setLogger`/`currentLogger`/`getLevel`) are atomic (F3).
- Residual hazards (documented in `ISOMessage.hh`): `mti()` returns a `string_view` **into the mutable field storage** — copy it before cross-thread use (`std::string m = msg->mti();`); `tryGetValueRef` is a zero-copy reference with the same caveat.

### Encoding system (see `docs/internals/encoding.md`)
Resolution order per field: **field-level `encoding` > global spec `encoding` > `""`** (only allowed for encoding-neutral formats).

Encoding-neutral (raw bytes, ignore all encoding settings): `BINARY` (fixed), `BITMAP`, `NOP`/`UNUSED`, `REMAINING`. Note: `LBINARY`/`LLBINARY`/`LLLBINARY`/`LLLLBINARY` are **not** neutral — their length prefixes use the spec encoding.

| Encoding | Length prefixes use | Data |
|---|---|---|
| `ascii` | ASCII digits | ASCII text |
| `bcd` | BCD nibbles | BCD digits |
| `ebcdic` | EBCDIC digits `0xF0`–`0xF9` | EBCDIC text |
| `binary` | big-endian bytes | raw bytes |

Child inheritance: encoding-neutral fields pass the **global** encoding to children; encoding-aware fields pass their own resolved encoding — keeps mixed specs (e.g. EBCDIC container with a `binary` DE inside) consistent.

**Determinism & oracle pin (since 0.3.0).** EBCDIC conversion is **fully table-driven**: `kEbcdicToAscii` / `kAsciiToEbcdic` / `kEbcdicValid` in `include/iso8583/_codec.hh` — no runtime converter (no libiconv, no ICU) in the default build. The tables are proven against a pinned **ICU 78.3** oracle: `tools/generate_ebcdic_tables/` regenerates/verifies the checked-in verdict JSONs (`pinned/icu_verdicts_{e2a,a2e}.json`, all 256 bytes per direction via `ucnv_convertEx`), and `tests/test_encoding_determinism.cc` sweeps all 256 bytes of both codecs against those verdicts plus the strict-mode throw rules. Consequences worth knowing:

- The library's EBCDIC **whitelist is intentionally stricter than ICU**: ICU 78.3 converts all 256 EBCDIC bytes (C1 controls, binary bytes included), while strict mode accepts only the 85-byte IBM-1047 printable/digit whitelist (E2A) and — for A2E — the 84 mappable ASCII characters. `tests/` pins both counts; a table change that moves them fails determinism tests.
- Non-strict (legacy) behavior is unchanged: unmappable E2A bytes map to the `.` sentinel (`0x2E`), unmappable A2E characters to `0x6F` (`?`). Documented A2E exception: `'?'` (`0x3F`) has no table mapping (falls back to `0x6F`) yet is **never rejected**, even in strict mode (`c != '?'` clause in `to<>`) — it always serializes as `0x6F`.
- The parser's `strict()` flag is propagated to **all four** codec conversion call sites in `src/_parser.hh` (encode/decode × string/binary) — a new codec call site that forgets `strict_` silently downgrades strict specs to legacy behavior.
- Residual limitation: EBCDIC **length prefixes** are decoded as raw low nibbles (`b[i] & 0x0F`, `decode_length`) without whitelist validation — `constexpr` cannot throw; a corrupted prefix is caught fail-closed by the downstream strict data-byte guard (and by the B1 length checks).
- Header unpacking (`WLP_FOHeader`/`BASE1Header`) stays **non-strict** by design: header classes carry no strict state and keep the legacy `rejectInvalid=false` conversion.

---

## 5. YAML spec format (summary — full reference: `docs/internals/yaml_format.md` and `include/iso8583/AGENTS.md`)

```yaml
!include_files            # optional, must be first document in the file
- common_definitions.yml
---                       # required document separator (YAML 1.2, strict since 0.2.0)
spec:     "My Spec"
encoding: ebcdic          # global: ascii | bcd | ebcdic | binary
definitions:              # reusable named building blocks
  pan_field: { type: scalar, format: llchar, length: 19 }
fields:
  "000": { type: scalar, format: numeric, length: 4 }   # MTI — always slot 000
  "001": { type: scalar, format: bitmap,  length: 8 }   # Bitmap — always slot 001
  "002": !use pan_field
  "003": { type: scalar, format: numeric, length: 6, encoding: bcd }  # per-field override
  "052": { type: scalar, format: binary, length: 8, sensitive: true }  # PCI: value → "***" in dumps/logs
  "055": { !merge [ !template LLL(BINARY, 255), description: "ICC Data" ] }
  "056": { format: lllbertlv, length: 999 }             # BER-TLV container, scalar only (ISO/IEC 8825-1, EMV Book 3 Annex B)
  "057":                                                  # TLV with declared tags
    type: nested
    format: lllbinary
    length: 999
    tlv: { ber: true }
    children:                    # Map = TLV mode
      "9F26": { format: binary, length: 8, description: "Application Cryptogram" }
      "5A":   { format: binary, length: 10, description: "Application PAN" }
  "048":                                                  # fixed-format TLV (MC/Visa style)
    type: nested
    format: lllchar
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "26": { format: char, length: 10 }   # decimal SE numbers (no ber: true)
  "061":
    type: nested
    format: binary
    length: 26
    children:                          # list = plain nested sub-fields
      - { format: numeric, length: 1 }
      - { format: remaining }          # consumes all remaining bytes of the parent
```

Directives: `!include_files [a.yml, b.yml]` (root level, **must be followed by `---`**), `!use <name>`, `!template P(F, N)` (e.g. `LL(CHAR, 19)`), `!merge [...]`, `!include` (deprecated alias of `!use`, emits a warning).

Formats: `numeric`, `char`, `binary`, `bitmap`, `nop`, `remaining`, plus L-prefix variants (`llchar`, `lllchar`, `llbinary`, `lllbinary`, `llllbinary`, …) and `bertlv` (optionally `l/ll/lll/llllbertlv`). `bertlv` is **scalar-only** — it must not be combined with `type: nested`, `children`, or a `tlv:` block; at runtime it produces a nested `Message` whose child keys are the raw BER tag values (`BERTLVParser` in `src/_tlv.hh`).

TLV `children` key notation:
- `tlv: {ber: true}` → keys are **hex** (`"9F26"`, `"5A"`), per EMV Book 3 / ISO 7816 convention.
- fixed-format TLV (`tag_bytes`/`len_bytes`) → keys are **decimal** SE numbers (`"26"`).
- Explicit `"0x1A"` prefix forces hex regardless of mode.
- Currently **only `description` is propagated** to decoded fields; every SE/tag is still decoded as a raw `BinaryField` (`format`/`length` in children are documentation only). Undeclared tags fall back to a generic `"SE<n>"` description.

Loader behaviors worth knowing:
- **PCI masking (0.3.0):** `sensitive: true` on a field (or on a TLV `children` entry; also valid in `definitions:`) marks the field sensitive — its value is rendered as `***` in `dump()`/`operator<<` (description stays visible). On nested/TLV/BERTLV containers it propagates to all children/tags. `value()`/`to_json()` are deliberately **unmasked** (programmatic data API). Full reference: `docs/internals/yaml_format.md`.
- **Include sandbox (0.3.0):** with the default `SpecLoadOptions`, every `!include_files` entry is resolved and **rejected** (`[ISO8583] Sandbox: …`) if it lands outside `roots` (empty = parent dir of the top-level spec) — lexically (`../`, absolute, UNC) and, when the file exists, on its fully canonicalized (symlink-resolving) path. Source files are streamed with a per-file size cap (`maxSpecBytes`), the total number of distinct files is capped (`maxIncludeFiles`), and `fields:` must be a **non-empty map** — empty maps, sequences, and digit-overflow DE keys produce positioned `SpecValidationError`s, never raw `std::stoi` exceptions. Sidecar **writes** are gated on `allowSmapWrite` + sandbox roots; sidecar **reads** are size-capped (`maxSmapBytes`).
- Errors from rapidyaml are converted to catchable, **positioned** `std::runtime_error` via **process-wide, once-installed** `ryml::set_callbacks` (ryml's default is `std::abort()`). This overrides any host app's own ryml callbacks on first load. Full taxonomy + rules for new code: §13.
- Recursion-depth protection + circular `!use` detection → clean `std::runtime_error` instead of stack overflow.
- Error positions are tracked via tree-internal node identity through a `.smap` sidecar (SourceMap, `src/_sourcemap.cc`). **Sidecar contract:** the sidecar (`<spec>.smap`, next to the spec file) carries a **SHA-256 hash over all source files**; on load it is accepted only if that hash matches, else it is **discarded and regenerated** (same for corruption or missing file). Sidecars from older library versions are detected via a `format_version` field and regenerated once. Operational consequences (read-only dirs, cleanup): §12.1.
- ⚠️ 0.2.0 breaking change: `!include_files` requires the `---` separator (yaml-cpp tolerated its absence).
- 0.2.1 fixed `!merge` definitions whose value is a **sequence** (referenced via `!use`) being lost during preprocessing (rapidyaml same-tree `merge_with` empties the source node's val-tag).

---

## 6. Private implementation map (`src/`)

| File | Role |
|---|---|
| `_components.cc` (1001 ln) + `include/.../detail/_components.hh` | `ISOComponent<key,value>` template, `ISOTaggedField`, `ISOMessage`, headers (`BaseHeader`/`BASE1Header`/`WLP_FOHeader`). Contains **explicit template instantiations** — needs `-fvisibility=default` under GCC/Clang hidden-visibility. New value types must be instantiated here (see §11.2). |
| `_spec.cc` (971 ln) | `SpecDecoder` + field-map → parser-tree construction; installs the global ryml error callbacks; recursion/circular-`!use` guards; positioned-error formatting (SourceMap node identity, `lookup_nearest` fallback for generated nodes). |
| `_preprocessor.cc` (708 ln) | YAML preprocessing: `definitions` extraction, `!use`/`!merge`/`!include_files` expansion (rapidyaml); builds and persists the `.smap` sidecar. |
| `_sourcemap.cc/.hh` | Node-identity-based error positions, `.smap` sidecar cache (format documented in the header). |
| `_parser.cc/.hh` (private) | `ISOBaseParser`, `ISOFieldParser<>` — the concrete parser machinery; deliberately **not** part of the public API (only the abstract `ISOParserPtrBase`/`ISOFieldParserPtrBase`/`ISOFieldParserType`/`ISOHeader` are, via `ISOParser.hh`). |
| `_tlv.cc/.hh`, `_tlv_policy.hh` | TLV parsers: fixed-format TLV, `BERTLVParser` (ISO/IEC 8825-1, `BerTag`), tag/length policies. |
| `_codec.cc` + `include/.../_codec.hh` + `detail/_codec_impl.hh` | Prefixer/encoder tables (`PrefixEncoder`, `Length`, `Encoder` enums; EBCDIC digit tables, ASCII↔EBCDIC conversion tables `kEbcdicToAscii`/`kAsciiToEbcdic`/`kEbcdicValid` = IBM-1047, **ICU-78.3-oracle-pinned**, §4) and the constexpr codecs. **This is the reason C++20 is mandatory.** |
| `_padder.cc`, `_date.hh`, `_utils.cc`, `_logger.cc/.hh`, `config.cc` | Padding, date helpers (vendored Hinnant `date.h`), misc utils, default logger backend, config plumbing. |
| `_iconv_wrapper.cc/.hh` | **[DEPRECATED since 0.3.0, removal 0.4]** iconv wrapper for EBCDIC — kept only as a transitional fallback behind `ISO8583_ENABLE_ICONV` (configure WARNING when ON); the runtime codec path no longer calls it. Compiled only when the option is ON (unconditionally includes `<iconv.h>`). |
| `fmt_types.hh` | Private `IFE_*`/`IFA_*` field-type aliases used by `SpecDecoder`. |

`include/iso8583/detail/extern/` bundles `dynamic_bitset.hpp` (+ `libpopcnt.hpp`) and `nonstd::string_view` — no external fetch needed; `config.h` includes the bitset with `DYNAMIC_BITSET_*` guards. Vendored third-party files and their licenses: §14.3.

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

## 8. Documentation

Sphinx + Doxygen + Breathe (Furo theme, MyST markdown):
```bash
doxygen docs/Doxyfile                      # C++ API → docs/_doxygen/xml
sphinx-build -b html docs docs/_build/html # → docs/_build/html/index.html
# Windows: docs/build_docs.bat
# deps: pip install -r docs/requirements.txt
```
- CI runs `sphinx -W` (warnings = errors) — keep doc references valid. Any new public header or renamed `///` comment must keep Doxygen/Sphinx references resolvable, or the docs CI fails.
- `.github/workflows/docs.yml` publishes to **GitHub Pages** on push to `main` when `include/` or `docs/` changes (repo Settings → Pages → Source: GitHub Actions).
- `docs/agents.md` embeds `include/iso8583/AGENTS.md` (the canonical API reference) via `{include}` and is listed in the `docs/index.rst` toctree.
- `docs/internals/yaml_format.md` (YAML spec reference) and `docs/internals/encoding.md` (encoding resolution) are the deep-dives; keep them in sync when loader behavior changes.

---

## 9. CI & IDE

### CI (`.github/workflows/ci.yml`) — three jobs

| Job | Runner | Toolchain | Details |
|---|---|---|---|
| `build-linux` | `ubuntu-latest` | **GCC-13 + Ninja** (`CC=gcc-13`, `CXX=g++-13`), vcpkg pinned | Debug + Release matrix; `ISO8583_BUILD_SHARED=ON`, `ISO8583_ENABLE_ICONV=ON`, `ISO8583_BUILD_TESTS=ON`, `ISO8583_INSTALL=OFF`; `ctest --no-tests=error --timeout 30`; test results uploaded as artifact. |
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

## 10. Recent history (see `changelog.md`)

- **0.2.1** — fix: `!merge` definitions with **sequence** values (used via `!use`) were lost during preprocessing (rapidyaml same-tree `merge_with` clears the source node's val-tag; restored from the still-readable source node before the self-merge). Regression test added.
- **0.2.0** —
  - C++20 becomes the hard baseline (was C++17).
  - yaml-cpp → **rapidyaml** migration (~1.6–2× faster spec loading; ~25 ns cached hit with `TrustUntilInvalidated`). Breaking: `!include_files` now strictly requires `---`; process-wide ryml error callbacks installed; recursion/circular-`!use` guards; SourceMap now node-identity-based with `format_version` sidecar invalidation; `SpecPreProcessor::preprocessFile()` removed.
  - Hex tag notation for BER-TLV `children` (`ber: true` → hex keys); invalid keys now give precise positioned errors.
  - TLV `children` `description` is actually propagated now (format/length still documentation-only, per-tag typing deferred).
  - Fixed pre-existing dangling `string_view` UB in generic TLV description fallback (verified with ASan) — see the lifetime rule §12.3.
  - Namespace rename to `iso8583::` with new type names (`Message`, `OpaqueField`, …); old names kept as deprecated aliases for one release.

---

## 11. Extension workflows (checklists)

### 11.1 Adding a new public header
1. Create `include/iso8583/<Name>.hh`: `TNG_EXPORT` on exported classes, `///` Doxygen comments on everything public, self-contained includes.
2. Master header: add `#include "<Name>.hh"` **and** a row to the API-layer comment table in `include/iso8583/iso8583.h`.
3. `CMakeLists.txt` (root): register in `ISO8583_PUBLIC_HEADERS` (or `ISO8583_DETAIL_HEADERS` for implementation-support headers) — this drives the install rules; without it the header silently is not installed.
4. Docs: Doxygen group + new `docs/api/<name>.rst` + toctree entry in `docs/index.rst` (CI runs `sphinx -W` — a missing or broken reference **fails the docs build**).
5. Add the header row + usage notes to `include/iso8583/AGENTS.md` (canonical API reference).
6. Verify: fresh configure + build, `cmake --install <builddir> --prefix /tmp/x --dry-run` shows the header, docs build green.

### 11.2 Adding a new field type / format / encoding
Touch list (in rough dependency order):
1. `ISOFieldParserType` enum in `include/iso8583/detail/_interfaces.hh` — current values: `UNUSED, EXCEPTIONAL, OPAQUE, BINARY, BITMAP, NESTED, REMAINING`. **TLV is not an enum value** — it is `NESTED` plus a policy selected in `src/_spec.cc` from the `tlv:` block (`src/_tlv_policy.hh`).
2. Codec enums/tables: `include/iso8583/_codec.hh` (`PrefixEncoder`, `Length`, `Encoder`) and `detail/_codec_impl.hh` (constexpr codecs — mind the C++20 literal-container constraint).
3. Format-string → type mapping in `src/_spec.cc` (and `src/_preprocessor.cc` if the new format is a new YAML spelling); update `docs/internals/yaml_format.md` and the `SpecFieldFormat::type` value list documented in `include/iso8583/ISOSpec.hh` (introspection returns `type`/`prefix_digits`/`max_length`).
4. Aliases in `src/fmt_types.hh` (private `IFE_*`/`IFA_*` used by `SpecDecoder`).
5. New **encoding**: extend `resolveEncoding()` semantics and the encoding-neutral list (documented in `docs/internals/encoding.md`) plus any conversion tables in `_codec.hh`.
6. New **value type** (beyond a new format of an existing type): explicit template instantiation of `ISOComponent<key,value>` in `src/_components.cc` **and** a matching `extern template` declaration in `include/iso8583/detail/_components.hh`. `_components.cc` is the one TU compiled with `-fvisibility=default`; a value type instantiated nowhere produces **undefined-reference errors in shared-library consumers** (tests against the shared lib catch it first).
7. Update the field-type table in `include/iso8583/AGENTS.md` and add tests (extend `test_codec.cc` / `test_spec_loader.cc` patterns; register new files per §7).

### 11.3 Adding a CMake option
1. `option(...)` in root `CMakeLists.txt` + wire it into the build; add the compile-definition/target logic next to the existing options (mind PUBLIC vs PRIVATE for ABI-relevant definitions).
2. Row in the options table in this file (§3).
3. Mirror in `CMakePresets.json` if any preset should set it (pattern: `debug-bertlv`).
4. Add the flag to **both** `ci.yml` jobs if CI must exercise it.

### 11.4 Adding tests
Register the new `test_*.cc` in `tests/CMakeLists.txt` (keep `test_e2e_full_message.cc` last), use Catch2 tags from the §7 tag list, and keep per-test runtime well under the 10 s CTest timeout (tag genuinely slow tests `slow`).

---

## 12. Runtime side effects & operational pitfalls

1. **`.smap` sidecar is written next to the spec file** (`abs_path + ".smap"`, JSON; format in `src/_sourcemap.hh`, written in `src/_preprocessor.cc`). Consequences:
   - Since 0.3.0 the sidecar is only written if `SpecLoadOptions::allowSmapWrite` is true **and** the sidecar path is inside the sandbox roots (default: the spec's own directory). Set `allowSmapWrite=false` for read-only deployments (common in bank environments) — loading still succeeds, and there is no more per-load WARN in that case.
   - Sidecar **reads** are size-capped (`maxSmapBytes`, default 16 MiB); oversize files are discarded and regenerated.
   - `.smap` files are **cache only**: safe to delete at any time (they are regenerated/validated via the SHA-256 source-hash contract, §5), never commit them, and they are not needed at runtime for correctness — only for stable error positions.
2. **`wire_offset() == 0` is ambiguous**: it means both "not set" and "field starts at buffer offset 0" (the MTI does). `std::numeric_limits<std::size_t>::max()` means "unknown" (documented in `detail/_interfaces.hh`).
3. **`description()` / `explanation()` are non-owning `nonstd::string_view`s** — the library stores a view, not a copy. Any code that *sets* descriptions (custom parsers, TLV handlers) must guarantee the referenced storage outlives the component. Precedent: 0.2.0 fixed real dangling-view UB in `ISOTLVParser` (ASan-verified) — keep its long-lived-storage pattern (both declared and generated fallback descriptions).
4. **MSVC + Ninja requires a Developer Command Prompt** (`vcvars64.bat`) — symptom without it: `LNK1104: kernel32.lib`.
5. **Gateway example E2E invariant**: `send_test.py`'s `ASCII_TO_EBCDIC` table must stay byte-identical to `kAsciiToEbcdic` in `include/iso8583/_codec.hh` (IBM-1047); update both together or the example breaks silently.
6. **Memory-safety rules (A4, security plan `docs/plans/security-implementation-plan.md`):**
   - The bundled `dynamic_bitset::operator[]` bounds-checks via `assert()` only — **disabled in Release builds**, so any index beyond the constructed size is undefined behavior (historical source of OOB crashes in bitmap handling). **Never** index `bmp[n]` without first guarding `bmp.size() > n`. The bitmap decode path in `src/_parser.hh` validates buffer offsets before reading every byte — keep it that way.
   - Header byte images (`BaseHeader::header` is a **protected** member; `BASE1Header`/`WLP_FOHeader` are `final` and their from-bytes constructors/`unpack()` enforce the full size) — the getter/setter guards are defense-in-depth and **fail closed** (throw a positioned `[ISO8583] … Fail-closed` `std::runtime_error`) instead of reading out of bounds, should the buffer ever be exposed or shrunk by a future API change.
   - The iconv E2BIG retry loop in `src/_iconv_wrapper.cc` is bounded: no-progress detection (two consecutive E2BIG without input/output advancement → throw) plus a hard output cap (2× input + reserve; EBCDIC↔ASCII is 1:1). The wrapper is **deprecated since 0.3.0** (removal 0.4) and not on the runtime codec path anymore — the bullets above remain true only while `ISO8583_ENABLE_ICONV=ON`.
   - Loader recursion (`processNode`/`propagateOrigins`/`finalize` in `src/_preprocessor.cc`) is depth-capped (`MAX_RECURSION_DEPTH = 200`) — malicious or corrupt specs produce a positioned `std::runtime_error`, never a stack overflow.
7. **EBCDIC oracle pinning (0.3.0)**: the checked-in tables in `include/iso8583/_codec.hh` are pinned against the ICU-78.3 verdicts in `tools/generate_ebcdic_tables/pinned/` (LF-enforced via `.gitattributes`). To re-verify after a table change: `cmake --preset <p> -DISO8583_BUILD_CODEC_TOOLS=ON` + `cmake --build <p> --target verify-ebcdic-tables` (requires ICU 78.3 via vcpkg; the tool exits 2 if the ICU major differs from 78). To regenerate: `update-ebcdic-tables` and commit the new `pinned/*.json` **plus** any resulting count changes in `tests/test_encoding_determinism.cc`.
8. **ctest reports early-returned (skipped) tests as Passed** — fixture-guarded tests (e.g. `issues/a` presence checks) return before any assertion; always scan the ctest output for skip warnings before trusting a green run.
9. **File hot-swapping is NOT atomic on Windows** (`std::filesystem::rename` = `MoveFileEx` + `REPLACE_EXISTING` with an existing target is delete+move as two kernel operations). While the AV minifilter is scanning the source file, the target can be transiently MISSING between the two steps — a load racing into that window throws `Datei nicht lesbar`. The TOCTOU test (`tests/test_spec_cache.cc`) therefore tolerates such OS-level races (discriminator: only the loader's I/O-level `Datei nicht lesbar` failures are treated as transient swap/AV races; every other load error is rethrown immediately, sustained I/O failures are capped by a retry counter) and requires version-consistency only for *successful* loads; on Linux (atomic `rename`) the strict path holds. Never rely on rename-based hot-swap being atomic in production Windows code — use a new-name + re-point design if consistency is required.

---

## 13. Error-handling conventions & global state

### Exception taxonomy
| Source | Exception | Notes |
|---|---|---|
| YAML spec loading/validation (any loader entry point) | catchable `std::runtime_error` **with `file:line:col` position** | ryml's default `std::abort()` is converted via a **process-wide, once-installed** `ryml::set_callbacks` (installed on first load); positions come from the SourceMap (node identity, `lookup_nearest` fallback for generated nodes — `src/_spec.cc`). |
| `Message::mti()` without MTI set | `std::logic_error` | Guard with `hasMTI()`. |
| `utils::getOrThrow<T>()` on missing/wrong-type field | throws | For optional reads use `tryGet*` / `getOrDefault` instead. |

**Rule for new loader/validation code:** always produce positioned errors through the SourceMap mechanism; **never let raw standard-library exceptions escape** (precedent: 0.2.0 replaced raw `std::stoi` escapes with clear, positioned error messages).

### Process-global state the library installs
| State | Installed when | Consequence for host apps |
|---|---|---|
| ryml error callbacks (`ryml::set_callbacks`) | first spec load | **Overwrites the host app's own ryml callbacks** if it uses rapidyaml independently — usually harmless (exceptions instead of `abort()` are the right choice), but known to cause surprises in exotic setups. |
| Spec cache (per absolute path) | `...Cached` loader variants | In-memory, lives for the process; `invalidateCache(path)` / `clearCache()`. |
| Logger + log level (`iso8583::log`) | always (default level WARN) | Global; replace via `setLogger()`. |
| DE key type (`TNG_KEY_TYPE` / `ISO8583_BERTLV`) | compile time (ABI) | Library and all consumers must match — see §4. |

---

## 14. Process & release

### 14.1 Commit-message convention
`<prefix>(<kind>) <imperative summary>` — prefixes observed across the history (see `git log`):
`[+](Added)`, `[-](Removed)`, `[#](Fixed)`, `[~](FIX | Updated | Changed)`, `[!](BREAKING | WARNING | FIX)`, `[/](Activated)`, `[i](Info)`, `[R]`.
Use `[!](BREAKING)` for breaking changes. One commit per logical change.

### 14.2 Release procedure (ordered)
1. Bump **all four** version spots: `project(VERSION …)` in `CMakeLists.txt`, `TNG_CORE_VERSION` in `include/iso8583/config.h`, `version` in root `vcpkg.json`, `version` in `vcpkg-port/vcpkg.json`.
2. Update `changelog.md` **and** its tracked mirror `docs/changelog.md` (both must end up identical).
3. Push + create tag `vX.Y.Z` (the vcpkg portfile fetches `REF v${VERSION}` from `Xemorph/iso8583`).
4. **After the tag exists**: compute the tag's SHA512 → fill it into `vcpkg-port/portfile.cmake` (currently placeholder `0` — the port is unusable until this is filled).
5. Push to `main` → docs auto-publish to GitHub Pages (`docs.yml`).
6. Release commit: `[~](FIX) Release vX.Y.Z: <summary>`.

### 14.3 Vendored third-party code & licenses
| File | Upstream | License |
|---|---|---|
| `src/_date.hh` | Howard Hinnant et al., `date.h` (~8.3 kloc) | MIT |
| `include/iso8583/detail/extern/dynamic_bitset.hpp` | Maxime Pinard (`sul::dynamic_bitset`) | MIT |
| `include/iso8583/detail/extern/libpopcnt.hpp` | Kim Walisch / Wojciech Muła | 2-clause BSD-style (in-file header) |
| `include/iso8583/detail/extern/string_view.hpp` | Martin Moene, `string-view-lite` | Boost Software License 1.0 |

Maintenance rules: **never strip the embedded copyright/license headers**; a proprietary library may bundle permissively licensed code only because of them. The vcpkg port installs only the proprietary `LICENSE`. Keep this table current whenever new code is vendored (and prefer the existing vendored pieces over adding new dependencies).

---

## 15. Pitfalls & conventions for agents (index)

1. **C++20 everywhere** — never relax to C++17 (constexpr containers in `_codec_impl.hh`).
2. **`detail/` headers are private.** User code includes `<iso8583/iso8583.h>` or the specific public headers.
3. **Key type is ABI-relevant** — any change touching `TNG_KEY_TYPE` or virtual signatures needs the `ISO8583_BERTLV` PUBLIC-definition mechanism to keep consumers in sync; clangd must use a matching build tree.
4. **`unparse` = decode, `parse` = encode** — counter-intuitive, do not "fix" it.
5. **Bitmap fields are auto-computed** — never set manually; nested DEs are set via dot-notation (`"48.72.1"`), not plain string keys.
6. **`BinaryField` values are uppercase hex strings** (e.g. `"DEADBEEF"`), not raw bytes.
7. **`mti()` throws** without an MTI — guard with `hasMTI()`.
8. **Quill + DLL → `QuillBridge`**, never `setQuillLogger()`.
9. **fmt/ryml stay PRIVATE**, nlohmann-json/robin-map stay PUBLIC — see §3.
10. **`_iconv_wrapper.cc` is conditional** on `ISO8583_ENABLE_ICONV` — keep it that way; the option is **deprecated since 0.3.0** (removal 0.4, configure WARNING) and the runtime codec path is table-only (§4).
11. **Don't reorder `tests/CMakeLists.txt`** (e2e last); register new test files there (§7).
12. **Docs are written in German** (repo convention: `docs/`, comments); the root `AGENTS.md` is English — keep new prose in the same language/style as the surrounding file.
13. **Generated file**: `include/iso8583/detail/_currency_table.hh` comes from `scripts/generate_currency_table.py` (data: `data/iso4217/codes-all.csv`) via `cmake --build <builddir> --target update-currency-table` (EXCLUDE_FROM_ALL, manual); commit the result like any other change.
14. **Version strings** live in 4 places (see §2) — bump all on release (§14.2).
15. **Licensing is proprietary** — no OSS attribution/publication assumptions; the vcpkg port's `license: null` is intentional; vendored permissive files keep their headers (§14.3).
16. **License check before redistribution**: read `LICENSE` (source-available terms).
17. When adding public API: declare `TNG_EXPORT` (visibility), prefer the new type names, keep headers self-contained, document with `///` Doxygen comments (CI enforces `sphinx -W`) — full checklist §11.1.
18. **`ISO_MAP` is fixed** (`tsl::robin_map`) — no user-selectable map type (§4).
19. **Spec dirs must be writable** for the `.smap` sidecar; read-only deployments log a WARN per load but keep working (§12.1).
20. **`wire_offset()==0` is ambiguous** (not set / MTI at 0) (§12.2).
21. **`description()`/`explanation()` are non-owning views** — storage lifetime is the setter's responsibility (§12.3).
22. **Loader code must emit positioned `runtime_error`s** — never raw std exceptions; beware the process-wide ryml-callback side effect (§13).
23. **MSVC + Ninja needs a Developer Command Prompt** (§12.4).
24. **ASCII↔EBCDIC table invariant** between `_codec.hh` and `send_test.py` (§12.5).
25. **Follow the commit convention** (§14.1); releases go through §14.2.
26. **Memory-safety rules (A4)**: `dynamic_bitset` indexing is unchecked in Release — guard every `bmp[n]` with `bmp.size() > n`; header byte offsets and iconv/retry loops are fail-closed/bounded (§12.6).
27. **EBCDIC codec changes must stay oracle-pinned**: after touching `kEbcdicToAscii`/`kAsciiToEbcdic`/`kEbcdicValid`, run `verify-ebcdic-tables` (or regenerate + update the count constants in `tests/test_encoding_determinism.cc`) — the tables are ICU-78.3-pinned, and the whitelist is deliberately stricter than ICU (§4, §12.7).
28. **Strict mode propagates through every codec call site** — parser encode/decode of string/binary fields passes `strict_` to `codec::to<>`/`as<>` in `src/_parser.hh`; a new call site that forgets it silently downgrades strict specs to legacy conversion.
29. **EBCDIC length prefixes are raw low nibbles** (`decode_length`) — constexpr can't throw, so misreads are caught fail-closed by the downstream strict data-byte guard, not at the prefix (§4).
30. **Header unpack is non-strict by design** (`WLP_FOHeader`/`BASE1Header` have no strict state) — do not "fix" it into strictness without an API decision.
31. **A2E `'?'` is a strict-mode exception**: no table mapping (`0x6F` fallback) yet never rejected — keep the `c != '?'` clause and the pinned counts (85/84/171) in `test_encoding_determinism.cc` in sync with any table change.
32. **WLP-FO timestamp width is platform-sensitive**: `getFormattedTimestamp()` must emit a fixed 26-char timestamp (subseconds via `std::chrono::duration_cast<microseconds>` + `std::setw(6)`); raw 100-ns-clock ticks + `setw(4)` produced 24–29 chars on Windows and a ~10 % `Timestamp format error` flake (fixed in 0.3.0, `src/_components.cc`).
33. **ctest skip == Passed**: early-returned fixture tests count as green — inspect the output for skip warnings before trusting a full-suite green (§12.8).
34. **vcpkg ICU pin is `version>=` + baseline**, not an exact manifest field (baseline predates exact-version support) — the `builtin-baseline` and the tool's ICU-major-78 assertion together enforce 78.3 (§3).
35. **Spec loading is sandboxed by default (0.3.0)**: `!include_files` entries resolving outside `SpecLoadOptions::roots` (empty = the spec's own directory) fail closed, file reads are size-capped, and `fields:` must be a non-empty map. If a load of a legitimate out-of-root include fails, raise `roots` (or set `sandbox=false` only for fully trusted spec trees) — do not weaken the loader (§4, §5).
36. **Concurrency model (0.3.0)**: `ISOMessage` entry points each take the one recursive message lock exactly once (internal `*_locked` chains assume it); parsers are immutable after load → shareable; logger globals are atomic. One message from N threads is supported — do not reintroduce lock-free fast paths or per-call locking in internal chains (§4).
37. **Test helper structs (`TempDir`/`TempYaml`) live in per-file anonymous namespaces** — a global-scope definition is an ODR violation across test TUs: in-class (inline) members COMDAT-fold, so all TUs silently share one constructor body (observed: one test file's directory prefix leaking into another's tests) (§12.9 context).
38. **Windows rename-based hot-swapping is non-atomic** (delete+move, AV-filter windows) — the TOCTOU test tolerates those OS-level races via an I/O-signature discriminator (only `Datei nicht lesbar` counts as transient; any other load error rethrows hard); successful loads must still be version-consistent (§12.9).
39. **PCI masking is dump/log-surface only (0.3.0):** `sensitive: true` (spec key) masks the value as `***` in `dump()`/`operator<<` (description stays visible); `value()`/`to_json()` are deliberately unmasked — never feed `to_json()` into a log sink for sensitive data, and keep the log level ≤ WARN in PCI environments (§4, §5).
40. **Bitmap-derived field ranges must guard `find_first() == npos` (P4, 0.3.0):** `ISOBaseParser::unparse()` narrows the field-loop range to the last set bitmap bit — with an all-zero bitmap `find_first()` returns `npos` (`SIZE_MAX`), and casting that to `TNG_KEY_TYPE` wraps to `-1` for the default int16 keys, inverting the range (`_begin > _end`) and making `std::for_each` step out of the parser deque ("cannot seek deque iterator out of range" in debug / UB in release). Rule: never convert `dynamic_bitset::npos` to a key type; check `!= npos` first, and keep the defensive `_end < _begin` clamp on every iterator-range construction from bitmap state (found by F1 fuzzing, 64×`0x00` repro; regression tests in `tests/test_full_unparse.cc`).
41. **Sandbox/sidecar containment checks must be alias-aware** (`isWithinRoot`, 0.3.1): candidate paths (`fs::absolute`, not canonicalized) and the canonicalized root can be lexically different yet physically identical — Windows 8.3 short names (`TEMP=C:\Users\RUNNER~1\AppData\Local\Temp` on GitHub Windows runners vs. `C:\Users\runneradmin\...` after `fs::canonical`) and symlinks/junctions (macOS `/tmp` → `/private/tmp`). A purely lexical prefix comparison wrongly rejects in-root `!include_files` and `.smap` writes (12 test failures on Windows CI). `isWithinRoot()` tries the lexical check first, then falls back to `fs::weakly_canonical` on the candidate (fail-closed property preserved: genuinely outside paths canonicalize to outside forms). Any new containment check must follow this pattern — never compare an absolute path against a canonicalized root purely lexically.
