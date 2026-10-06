# Overview & repository layout

> Verbatim section(s) §1, §2 of the former monolithic AGENTS.md (section numbers are stable; other docs cite them). Entry point: `../AGENTS.md`.

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
| `src/` | Private implementation. Underscore-prefixed headers (`_parser.hh`, `_spec.hh`, `_tlv.hh`, `_preprocessor.hh`, `_sourcemap.hh`, `_padder.hh`, `_utils.hh`, `_logger.hh`, `_date.hh`, `_tlv_policy.hh`) are **deliberately private**; `fmt_types.hh` (IFE_* aliases) is private. `config.cc` implements version/namespace plumbing. |
| `tests/` | Catch2 unit tests (registration in `tests/CMakeLists.txt` — see §7). |
| `examples/tcp_gateway/` | Opt-in example: minimal TCP gateway decoding one ISO-8583 message per connection (ASCII + EBCDIC/IBM-1047 specs, Python test client `send_test.py`). Built only with `ISO8583_BUILD_EXAMPLES=ON`. Manual test: `python send_test.py [host] [port] [ascii\|ebcdic]` (see §12.5). |
| `docs/` | Sphinx + Doxygen + Breathe (+ Furo/MyST) documentation; `internals/yaml_format.md`, `internals/encoding.md`; `Doxyfile`, `conf.py`, `requirements.txt`, `build_docs.bat`. Built output in `docs/_build/`, Doxygen XML in `docs/_doxygen/` (both git-ignored). |
| `data/iso4217/` | ISO-4217 currency source data (`codes-all.csv`, vendored for reproducible/air-gapped builds — see `data/iso4217/README.md`). |
| `scripts/generate_currency_table.py` | Regenerates `include/iso8583/detail/_currency_table.hh` from the CSV (run via CMake target `update-currency-table`, which is `EXCLUDE_FROM_ALL` — manual only, commit the result). |
| `cmake/iso8583Config.cmake.in` | CMake package config template for `find_package(iso8583 CONFIG)`. |
| `vcpkg-port/` | vcpkg port for downstream consumption (`portfile.cmake`, `vcpkg.json`, `usage`). Portfile fetches tag `v${VERSION}`; its SHA512 is filled in per release after the tag is pushed (release step 4 — see §14.2). Repo URL: `Xemorph/iso8583`. |
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

Currently all four say **0.9.0** (synced). If you ever observe skew, the
release that introduced it missed a spot — fix it, don't normalize to the
wrong value.

---
