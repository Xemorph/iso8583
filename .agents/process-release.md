# Docs, history, extension checklists, release

> Verbatim section(s) §8, §10, §11, §14 of the former monolithic AGENTS.md (section numbers are stable; other docs cite them). Entry point: `../AGENTS.md`.

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

## 10. Recent history (see `changelog.md`)

- **0.6.2** — Opt-in `strict_length: true` (Spec-Wurzel oder Feld-Key): zu kurze Werte bei Feldern fester Länge werden im strict-Modus beim Serialisieren abgelehnt statt still aufgefüllt (FR-5; Default unverändert = Padding; `ISOFieldParserPtrBase::strictLength`; ABI: Layout). Dokumentiert außerdem `is_nested` für TLV-Container seit 0.6.0.
- **0.6.1** — `AmountField`: optionales Vorzeichen `sign: true` in der Standardform (führendes `C`/`D`/`+`/`-`, nur mit `scale:`, nicht mit `bcd`; `hasSign()`/`isNegative()`, signed `minorUnits()`/`amount()`, `SpecFieldInfo::amount_signed`; ABI: Vtable + Layout). Intern: ASCII-only-Testnamen (Windows-ctest), nightly.yml-Heredoc-Fix.
- **0.6.0** — `remaining` encoding-aware: Format folgt dem aufgelösten Feld-/Global-Encoding (roh `BinaryField` nur bei `""`/`binary`; `ascii`/`ebcdic`/`bcd` → `OpaqueField`), `length` wird Pflicht als Maximum (Fail-closed `SpecValidationError`), Introspection `max_length` meldet das deklarierte Maximum. Neu: `AmountField`-Feldtyp `format: amount` (jPOS-`ISOAmount`-Konvention, Encodings `ascii`/`ebcdic`/`bcd` → `IFA/IFB/IFE_AMOUNT`, inkl. typisierter TLV-Kind-Formate); optionaler Key `scale: N` schaltet auf die Standard-ISO-8583-Betragsform (nackte Ziffern, keine Währung im Feld) um, `SpecFieldInfo::amount_scale` introspektierbar (ABI). FR-4: TLV-Modus (fixer SE vs. BER-TLV) ist über `SpecFieldInfo::tlv_is_ber` introspektierbar — `true` bei `tlv: {ber: true}` und der `...bertlv`-Kurzform, `false` sonst (ABI: weiteres Layout-Mitglied des per-Wert `SpecFieldInfo`). Normative Schema-Referenz: `docs/internals/spec_schema.md`.
- **0.5.0** — typed TLV/BER-TLV children (FR-1/FR-2): declared `children` are decoded/encoded per declared `format`/`encoding` via the codec (BREAKING: text children now yield `OpaqueField` instead of `BinaryField`), the `...bertlv` shorthand accepts a `children:` Map (undeclared tags stay dynamic), D5 child whitelist (fail-closed), `SpecFieldInfo::tlv_children` introspection (ABI: layout change), BCD digit-count fix. Consumer migration checklist in the changelog.
- **0.4.0** — iconv fallback removed (`ISO8583_ENABLE_ICONV` / `_iconv_wrapper`): the EBCDIC codec is fully table-based (no `thread_local` in the tree); `ISOSpec::hasHeader()`/`headerSize()` introspection; FAQ page; `isWithinRoot` Windows alias fix.
- **0.3.0** — security/robustness release: strict mode (default), EBCDIC tables pinned against the ICU-78.3 oracle, spec include sandbox (`SpecLoadOptions`), thread-safe `ISOMessage` (one message from N threads), PCI masking (`sensitive: true`), `.smap` sidecar contract, `QuillBridge`.
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

## 14. Process & release

### 14.1 Commit-message convention
`<prefix>(<kind>) <imperative summary>` — prefixes observed across the history (see `git log`):
`[+](Added)`, `[-](Removed)`, `[#](Fixed)`, `[~](FIX | Updated | Changed)`, `[!](BREAKING | WARNING | FIX)`, `[/](Activated)`, `[i](Info)`, `[R]`.
Use `[!](BREAKING)` for breaking changes. One commit per logical change.

### 14.2 Release procedure (ordered)
> **Release gate (mandatory):** a release (tag, GitHub release, vcpkg hash) may
> only be created from a commit whose **Docs workflow run (`docs.yml`, build-docs
> + deploy) on `main` finished successfully**. Push the release-candidate commit
> to `main` first, wait for the green docs run (`gh run list --workflow=docs.yml`),
> and only then tag and publish. A red docs build blocks the release — fix it and
> re-verify on the new commit. (Also run `sphinx-build -W` locally beforehand.)

1. Bump **all four** version spots: `project(VERSION …)` in `CMakeLists.txt`, `TNG_CORE_VERSION` in `include/iso8583/config.h`, `version` in root `vcpkg.json`, `version` in `vcpkg-port/vcpkg.json`.
2. Update `changelog.md` **and** its tracked mirror `docs/changelog.md` (both must end up identical).
3. Push, **wait for a green Docs run on that commit (release gate above)**, then create tag `vX.Y.Z` (the vcpkg portfile fetches `REF v${VERSION}` from `Xemorph/iso8583`).
4. **After the tag exists**: compute the tag's SHA512 (GitHub codeload tarball of the tag) → fill it into `vcpkg-port/portfile.cmake` (currently holds the v0.6.2 archive hash; each release replaces it).
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
