# AGENTS.md — libiso8583 (compact entry point)

C++20 library (CMake project `libiso8583`, target `iso8583`, currently **0.8.0**) that decodes/encodes ISO-8583 messages from declarative **YAML specs**. Proprietary, source-available license (not OSS).
Read this file fully; load the deep-dive files below **only when the task touches that topic**.

## Terminology (inverted on purpose — do not "fix")
- `Message::unparse(msg, bytes)` = **decode** (wire → fields); `parser->parse(msg)` = **encode** (fields → wire).
- Load: `spec::SpecDecoder::loadFromYaml(path)` → parser; `loadBothFromYaml` → parser + `ISOSpec` (introspection). Field-only docs (top-level `field:`): `loadField*FromYaml`, `decodeField`.

## Where to find more (progressive disclosure)
1. **Search first**, then read: `zvec_grep_search` (concepts; always pass absolute `root`) or Grep/`zvec_grep_rg` (exact names). Search hits carry snippets — don't re-read whole files.
2. **Deep-dive files** in `.agents/` (verbatim old AGENTS.md sections; `§N` refs in other docs point here):

| Topic | File | Sections |
|---|---|---|
| Purpose, repo layout, version-string spots | `.agents/overview-layout.md` | §1, §2 |
| Build, CMake options, presets, tests, CI, clangd | `.agents/build-test-ci.md` | §3, §7, §9 |
| Object model, key type/ABI, Message API, threading, logging, encoding/EBCDIC pin, exceptions, global state | `.agents/concepts-encoding.md` | §4, §13 |
| YAML spec grammar, formats, TLV, `remaining`, `amount`, sandbox, `strict_length`; `src/` map | `.agents/yaml-spec.md` | §5, §6 |
| Runtime side effects + full pitfall list (45 items) | `.agents/pitfalls.md` | §12, §15 |
| Docs build, history, new header/field-type/option/test checklists, release procedure, vendored licenses | `.agents/process-release.md` | §8, §10, §11, §14 |

3. **Public API reference:** `include/iso8583/AGENTS.md` (German, canonical). **Normative spec schema:** `docs/internals/spec_schema.md`; also `yaml_format.md`, `encoding.md`.
4. **Project history / decisions / feature requests:** Obsidian vault `ai_connected` — folders `iso8583/` (FR-1…FR-5, `Übersicht`) and `iso8583-dev/` (implementation notes per feature), `chatrooms/iso8583.md`. Use `search_vault_simple` / `search_vault_smart`; plans also live in `docs/plans/`.

## Build & test (fast path)
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
# single suite: build/debug/bin/libiso8583_tests "[spec]"   (tags: e2e slow message tlv spec ber ebcdic …)
```
- Needs `VCPKG_ROOT`; vcpkg manifest mode. **MSVC+Ninja: run from a Developer Prompt/`vcvars64.bat`** (else `LNK1104 kernel32.lib`).
- New `tests/test_*.cc` must be registered in `tests/CMakeLists.txt`; `test_e2e_full_message.cc` stays **last**. `TEST_CASE` names **ASCII-only** (no umlauts).
- `ctest` counts early-returned (skipped) tests as Passed and excludes `[slow]`; scan output for skips.

## Hard rules (violations cause real bugs)
1. **C++20 only** (constexpr containers in `detail/_codec_impl.hh`). Never include `detail/` or private `src/_*.hh` from user code.
2. **DE key type is ABI** (`int16_t`; `int32_t` with `ISO8583_BERTLV`, a PUBLIC compile definition). Library and consumers must match.
3. Dependencies: `nlohmann-json`, `tsl-robin-map` PUBLIC; `fmt`, `ryml` PRIVATE; ICU build/CI tool only. **No runtime EBCDIC converter** (table-only codec, iconv removed).
4. `ISO_MAP` is fixed (`tsl::robin_map`). Bitmaps are auto-computed — never set them. Nested DEs via dot notation (`"48.72.1"`). `BinaryField` values are **uppercase hex strings**. `mti()` throws without MTI (`hasMTI()` first).
5. Use new type names (`Message`, `OpaqueField`, …), not deprecated `ISO*` aliases.
6. **EBCDIC tables are ICU-78.3-oracle-pinned**; after touching `kEbcdicToAscii/kAsciiToEbcdic/kEbcdicValid` run `verify-ebcdic-tables` and keep counts (85/84/171) in `tests/test_encoding_determinism.cc`; keep `examples/tcp_gateway/send_test.py` table identical. `strict_` must reach **every** codec call site in `src/_parser.hh`.
7. **Memory safety:** guard every `dynamic_bitset` index (`bmp.size() > n`); never convert `npos` to a key type; `description()/explanation()` are non-owning views (storage must outlive them).
8. **Loader code** throws *positioned* `std::runtime_error`/`SpecValidationError` (SourceMap) — never raw `std::stoi`-style exceptions. Any path containment check must be alias-aware (`isWithinRoot`). Do not weaken the include sandbox.
9. **PCI:** `sensitive: true` masks only `dump()`/`operator<<`; `to_json()/value()` are unmasked. Keep log level ≤ WARN in production.
10. **Threading:** each `Message` entry point takes the one recursive lock exactly once; no lock-free fast paths; parsers are immutable and shareable.
11. `.smap` sidecars are cache next to specs — never commit.
12. New public API: `TNG_EXPORT`, `///` Doxygen, register header in `CMakeLists.txt` + `iso8583.h` + docs (`sphinx -W` is enforced in CI). New value types need explicit instantiation in `src/_components.cc` (+ `extern template`).
13. Language: `docs/` and code comments German; this file and `.agents/` English — match the surrounding file.

## Process
- Commits: `<prefix>(<kind>) summary` with prefixes `[+](Added)`, `[-](Removed)`, `[#](Fixed)`, `[~](FIX|Updated|Changed)`, `[!](BREAKING)`, `[i](Info)`; one logical change per commit.
- Version lives in 4 places (`CMakeLists.txt`, `include/iso8583/config.h` `TNG_CORE_VERSION`, root `vcpkg.json`, `vcpkg-port/vcpkg.json`); `changelog.md` and `docs/changelog.md` must stay identical.
- **Release only after a green Docs run on `main`** (procedure: `.agents/process-release.md` §14.2). Releases are user-initiated.
- When you change behavior described in `.agents/*` or `include/iso8583/AGENTS.md`, update the affected file in the same commit.
