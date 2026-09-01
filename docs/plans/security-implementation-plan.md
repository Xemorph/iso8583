# Security Implementation Plan — `libiso8583` (production financial use)

Status: **DRAFT — requires maintainer approval before implementation.**
Basis: security audit (risk register A1–A5, B1–B6, C1–C3, D1–D3, E1–E3, F1–F4) + maintainer decisions Q1–Q6.
Continues the Issue-A crash work (commit `959780e`, 288/288 tests green).

---

## 0. Decisions (locked by maintainer)

| # | Decision | Consequence in this plan |
|---|----------|--------------------------|
| Q1 | Specs can be third-party/remote | **Include sandbox API is mandatory** (E1 → High). Default: sandbox ON. |
| Q2 | **Strict by default** | Truncation / oversized-field / invalid-encoding → throw (positioned `[ISO8583]`), unless `strict: false` is set in the spec or via API. |
| Q3 | Sharing one `ISOMessage` across threads is **supported** | Full locking fix (F1), not documentation-only. TSan CI. |
| Q4 | Keep the static-table codec path; `.`-mapping for invalid EBCDIC is **opt-in**: strict → reject, non-strict → legacy `.` | Table fallback takes a `rejectInvalid` flag driven by the strict mode. |
| Q5 | WLP-FO is in **active production use** | A2 (pack/UB + wire corruption) moves into Phase 1, top priority. |
| Q6 | **iconv is replaced by ICU** — corrected & confirmed after Phase 0 (2026-07-22): ICU as build/CI oracle (exactly **78.3**), runtime = generated tables; `libiconv` stays as deprecated fallback until 0.4 | Phase 2 (corrected), see `docs/plans/phase0-icu-spike.md` |

**Safety invariants the end state must guarantee** (acceptance criteria, see §9):
- **P1 No crash:** any byte input → success or positioned `std::runtime_error`. Never UB / SIGSEGV / raw STL exception.
- **P2 No silent corruption:** never report success with truncated/dropped fields; never serialize a structurally wrong frame.
- **P3 Determinism:** same bytes + same spec → same verdict across builds/CRTs/platforms.
- **P4 Bounded resources:** CPU/memory bounded in input size; recursion and allocation amplified only linearly.

**Release:** 0.3.0. 0.x version → breaking changes are allowed and are documented (§8). Existing symbols are not removed or renamed; new members are added (`ISOBaseParser` gains `strict_` → consumers of the DLL must rebuild).

---

## Phase 0 — ICU spike & tooling prerequisites (≈2 d)

Goal: de-risk Q6 (ICU) before committing the Phase 2 migration, and make sanitizer CI work.

1. **ICU EBCDIC spike** (throwaway target `issues/b/`-style, local):
   - vcpkg manifest trial: add `"icu"` (pick version, later pinned in Phase 2), build, `find_package(ICU COMPONENTS uc data)`.
   - Verify vcpkg's ICU ships the EBCDIC converter tables (`IBM-1047`) — required for `UnicodeString(const char8_t*, len, "IBM-1047")` to work.
   - Behavior sweep: all 256 bytes, `IBM-1047 → UTF-8 → US-ASCII` and reverse; record the exact accept/reject set per byte.
   - Verify the known poison bytes `f8 10 24 85 9b 46 a2 3d e3 c6 81 8b` are **rejected** (ICU must not reproduce the vcpkg-libiconv debug/release divergence).
   - Save the 256-byte verdict table as a test-corpus file → becomes the determinism pin (D1/D2).
2. **Sanitizer tooling:**
   - Preset `debug-asan` (MSVC `/fsanitize=address` locally; clang/LLVM on CI) + `tsan` (clang) in `CMakePresets.json`.
   - Verify the full 288-test suite builds and runs under ASan (baseline: current code **will** report A1/A2-class findings — expected, tracked as pre-fix baseline).
3. Fuzzer harness skeleton: `tests/fuzz/` with libFuzzer main stubs (built only with `ISO8583_BUILD_FUZZERS=ON`, clang, Linux CI) — targets land in Phase 4, but the CMake wiring is validated now.

**Exit criteria:** spike report with the 256-byte table; ASan preset green on a *minimal* subset; decision "ICU works" recorded.

### Phase 0 — Ergebnis-Addendum (2026-07-22, **korrigiert Phase 2**)

Der Spike (`issues/b/icu_spike/`, Bericht: `docs/plans/phase0-icu-spike.md`) hat
ein Ergebnis geliefert, das die Annahmen 1./Q6/P3 dieses Plans **korrigiert**:

- ICU **78.3** funktioniert (Converter öffnen, gültige Sequenzen exakt,
  Roundtrips MATCH) — aber die IBM-1047-Tabellen sind **vollständig**: kein
  einziges der 256 Bytes wird in *beiden* Richtungen abgelehnt (0/256 + 0/256).
  Ungedefinierte EBCDIC-Positionen werden auf C1-Steuerzeichen gemappt,
  Codepoints > `U+007F` werden in der ASCII-Stufe mit `?` *substituiert*.
  Die Gift-Sequence aus `issues/a` (DE006) konvertiert ICU „erfolgreich"
  (→ `8??e??s?TFa?`) — **sie wird NICHT abgelehnt** (im Gegensatz zur
  Erwartung oben und zum vcpkg-libiconv-Debug-Build mit EILSEQ).
- Da EBCDIC↔ASCII unter ICU reine 1:1-zustandslose Tabellen-Konvertierung
  ist, wird Phase 2 **korrigiert**: Laufzeit = deterministischer
  **Tabellen-Lookup** (kein `thread_local`-Converter mehr); ICU wird zum
  **Build-/CI-Orakel** (generiert die 256-Byte-Tabellen, pin = ICU 78.3 via
  vcpkg-Baseline, CI-Regenerations-Diff). Strict-Modus wird
  **tabellen-getrieben** definiert (Ablehnung = `.`-Sentinel-Ergänzungsmenge
  der Legacy-Whitelist); non-strict bleibt Legacy-Verhalten. Damit entfallen
  die Umgebungsguards in `tests/test_incompatible_input.cc` (Ablehnung wird
  deterministisch → Test lässt sich überall asserten).
- **Nicht betroffen:** Phase 1 (fail-closed) ist codec-unabhängig und kann
  unverzüglich starten. **Korrigierung bestätigt (2026-07-22):** Tabellen-Lookup
  als Laufzeit, ICU exakt 78.3 als Build-/CI-Orakel, `libiconv`-Fallback bis 0.4. Details + offene Punkte: `docs/plans/phase0-icu-spike.md` §3/§6.

---

## Phase 1 — Fail-closed hardening (≈5–6 d)

All fixes keep the public API stable (except the additive `strict` knob). Every new failure is a positioned `std::runtime_error` with the existing `[ISO8583]` prefix; the existing `ISOMessage::parse/unparse` safety net stays as backstop.

### 1.1 Strict mode plumbing (enables B1, B2, B6, Q4-table)

- New YAML root key `strict: true|false` (default **true**). Parsed in `_spec.cc` alongside `encoding`/`header`; allow-list of accepted keys extended.
- `ISOBaseParser` (public class, `detail/_interfaces.hh`) gains `bool strict_` (default `true`) + `void strict(bool)` / `bool strict() const`. Set by `buildParser` from the YAML; overridable programmatically. *(ABI note: new member — rebuild required, documented.)*
- Strict semantics:
  - **B1** (`src/_parser.hh:370–377`): `available_bytes < needed_bytes` → **strict:** throw `"[ISO8583] DE%03d '…' @ Offset N: Feld am Pufferende abgeschnitten (erwartet X, verbleiben Y logische Einheiten)"`. **non-strict:** keep the clamp **plus** `TNG_LOG_WARN` (no longer silent).
  - **B2** (`src/_parser.hh:260`): serialized field > `de_l_` → **strict:** throw `… Serialisierung zu groß (N > Maximum M) — Wert wird verworfen, um fehlerhafte Frames zu verhindern`. **non-strict:** `TNG_LOG_ERROR` + legacy `{}` (field omitted) — but never silent.
  - **B6** (`detail/_codec_impl.hh:80`, `decode_length` via `b.at`): pre-check `o + prefix_len <= b.size()` in `ISOFieldParser::unparse` before decoding the length prefix → positioned `"[ISO8583] DE%03d … @ Offset N: Längenprfix am Pufferende abgeschnitten"` instead of raw `std::out_of_range`.
  - **Q4 table path** (`include/iso8583/_codec.hh` `#else` branch, `e2a_n`/`a2e_n`): pass `rejectInvalid = strict_` through the codec entry points (`ebcdic_to_ascii_cached` / `ascii_to_ebcdic_cached` and the template `as/to` EBCDIC paths). **strict:** invalid EBCDIC → positioned `std::runtime_error` (same message family as the iconv/ICU path). **non-strict:** legacy `.` (0x2E) mapping.

### 1.2 Memory-safety fixes (A1, A2, A3, A5)

- **A1 bitmap OOB read** (`src/_parser.hh:401,410`): before `b[o]`: `if (o >= b.size()) throw … "Bitmap-Byte am Pufferende abgeschnitten"`. After the extended-bitmap test (`b[o] & 0x80`): `if (o + 9 > b.size()) throw …`. Replace the `bmp.size()==129`-dependent `bmp[65]` access with an explicit `bmp.size() > 65 && bmp[65]` guard (removes reliance on `&&` short-circuiting, A4 hygiene).
- **A2 WLP-FO pack/UB (Q5: production-critical)**:
  - `WLP_FOHeader::pack()` (`src/_components.cc:875`): rewrite to return the **full 93-byte header**: bytes `0..4` (ASCII length prefix) verbatim from the stored header + EBCDIC conversion of bytes `4..end()`. Result size == stored header size (93).
  - `ISOBaseParser::parse` (`src/_parser.cc:85`): the current guard `hdr->size() >= hdr_sz_` checks the **stored** header, not the packed output — it passes while `pack()` returns 89 bytes, then `out.insert(begin, begin+93)` overruns. Replace with a guard on the **packed result**: `if (hdr_bytes.size() < hdr_sz_) throw "[ISO8583] Header-Serialisierung inkonsistent: gepackte Bytes X < erwartet Y"`.
  - Tests: `pack(unpack(x)) == x` for a canonical 93-byte WLP header; full WLP message serialize → first 93 bytes == original header bytes (no garbage, no length-prefix loss); `length(n)` setter reflected in packed bytes 0–3.
- **A3 header fixed-offset OOB** (WLP getters/setters, `BASE1Header::getHLen/source/setLen`):
  - `BaseHeader` gains `void require(std::size_t n) const` → throws `"[ISO8583] <HeaderType>: Header zu kurz (X < benötigte Y Bytes)"`.
  - Every getter/setter that touches fixed offsets calls `require(max_offset+1)` first (WLP: `length` 4, `sysId` 14, `record` 24, `mti` 28, `creationTs` 54, `version` 55, `uuid` 75, `reference` 91, `payment` 93, …; BASE1: `getHLen` 1, `source` 14, `setLen`/len 5, …).
  - Constructors from user vectors validate immediately (WLP: `size() >= 93` → else throw; BASE1: `size() >= 1` plus `size() >= header[0]` consistency where the layout implies it).
  - `ISOMessage::header(const std::vector<uint8_t>&)` (`src/_components.cc:602`) and `ISOMessage::header(ptr)`: validate before/after `unpack`; short wire header → positioned throw, never an OOB **write**.
- **A5 null-deref in `ISOMessage::parser(p)`** (`src/_components.cc:534`): `auto* base = dynamic_cast<ISOBaseParser*>(p_.get()); if (!base) throw std::runtime_error("[ISO8583] ISOMessage::parser: übergebener Parser ist kein ISOBaseParser")`.
- **A4** (`dynamic_bitset::operator[]` assert-only): no change to vendored code; audit rule codified in root `AGENTS.md` (English section "Memory-safety rules"): *never index `dynamic_bitset` beyond its constructed size; guard every `bmp[n]` with `bmp.size() > n`.*

### 1.3 Integrity fixes (B3, B4)

- **B3** (`src/_parser.cc:335`): `m->set(de)` return value checked → on `false`: `throw std::runtime_error("[ISO8583] DE%03d: ISOMessage::set fehlgeschlagen (Speicherfehler) — Feld wird nicht still verschluckt")`. (Decode path becomes allocation-failure-fatal.)
- **B4** (`ISOBaseParser::parse`): the swallow-returns-`{}` branches (null/`!is_composite`, empty `l_`, non-message component, and the field-loop exception handler) become: `TNG_LOG_ERROR` + throw (or, for the "not configured" precondition, throw `"[ISO8583] Parser nicht konfiguriert"`). An empty frame can no longer be produced silently. `ISOMessage::parse`'s return path keeps its safety net.

### 1.4 Resource bound (C1, interim)

- `_iconv_wrapper.cc:119` E2BIG growth loop: add (a) **no-progress detection** — if after a grow `pin`/`pout` did not advance (`inleft` and produced-count unchanged), throw `std::runtime_error("iconv: kein Fortschritt (Converter-Zyklus)")`; (b) **hard cap** `out.size() <= in.size() * 4 + 1024` → throw. (Final home of this logic is the ICU wrapper in Phase 2; the iconv wrapper gets the same guard now, still needed as transitional fallback.)
- **C2 interim:** `finalize()` in `src/_preprocessor.cc` gets the same depth parameter/cap (200) its sibling passes already have.

### 1.5 Phase 1 tests

- New `tests/test_strict_parsing.cc`:
  - **Truncation battery:** for the E2E fixture and the `issues/a` 383-byte frame (fixture-guarded like `test_incompatible_input.cc`), cut the buffer at every offset 1..N-1 → REQUIRE: throws `[ISO8583]` runtime_error (positioned) OR clean success only where the cut is inside `REMAINING`-free padding. No crash under ASan.
  - Bitmap short cases: header+MTI+{0,1,8,9,15,16} bitmap bytes → clean positioned throw (A1 regression).
  - Oversized value: 13 chars into a 12-char fixed field → strict throw (B2); `strict(false)` → no throw + field omitted.
  - `strict: false` spec variant of the EBCDIC-poison case → table/iconv legacy behavior preserved.
- Extend `test_wlp_fo_header.cc`: pack/unpack round-trip, 93-byte invariant, length-prefix bytes (A2).
- Short/shorter wire header → clean throw (A3) — extends existing test #3 in `test_incompatible_input.cc` matrix (WLP + BASE1).
- ASan run of the whole suite (new preset) — must be green **after** A1–A5 (pre-fix baseline failures are fixed by this phase).

**Exit criteria:** `ctest --preset debug-asan` green; repro app (Issue A) emits the same clean DE006 error; no finding of class A/B remains open.

---

## Phase 2 — Codec-Determinismus: Tabellen + ICU-Orakel (≈5–7 d)

> **Korrigiert nach Phase 0** (2026-07-22, vom Maintainer bestätigt;
> Details in `docs/plans/phase0-icu-spike.md`): Der Spike zeigte, dass
> ICU 78 für IBM-1047↔US-ASCII **kein** der 256 Bytes ablehnt
> (Substitution durch `?`/C1-Steuerzeichen statt Fehler). Die Laufzeit
> konvertiert daher **nicht** mit ICU, sondern über **generierte
> 1:1-Tabellen** (EBCDIC↔ASCII ist zustandslos und 1:1). ICU wird
> zum **Build-/CI-Orakel** herabgestuft (Tabellen-Generierung + Pin,
> exakt gepinnte Version **78.3** in `vcpkg.json`). `libiconv` bleibt
> als deprivierter Fallback bis 0.4.

### 2.1 Tabellen-getriebener EBCDIC-Codec (Q4, Q6-korrigiert; schließt D1, D3)

- `kEbcdicToAscii[256]`/`kAsciiToEbcdic[256]` werden aus dem gepinnten
  ICU-78.3-Orakel neu generiert (Spike-Tool `issues/b/icu_spike/main.cc`
  → wiederverwendbarer Generator in `tools/`):
  - **strict-Tabelle** = Mappings der Whitelist-Bytes (Buchstaben/Ziffern/
    Zeichensatz-Whitelist = Legacy-Menge); alle anderen Bytes → Reject.
  - Kreuzprüfung gegen die Legacy-Tabelle (iconv-geleitet): Die
    Whitelist-Mappings müssen byte-genau übereinstimmen (Orakel gewinnt;
    Abweichungen im Pin dokumentiert).
- Laufzeit: Tabellen-Lookup in `ebcdic_to_ascii_cached`/`ascii_to_ebcdic_cached`
  (kein `thread_local`-Converter, kein Grow-Loop, kein E2BIG). Strict-Modus
  (Default, Q2) → positioniertes `[ISO8583]`-`std::runtime_error` mit
  Byte/Offset/hexdump (gleiche Fehlerfamilie wie heute, inkl.
  „Byte 0xXX an Position N ist nicht konvertierbar"); non-strict →
  Legacy-`.`-Sentinel (A2E-Fallback `?`) unverändert.
- Iconv-Wrapper + `ISO8583_ENABLE_ICONV` bleiben als deprivierter
  Übergangs-Fallback (Configure-Warning) bis 0.4 für Integratoren mit
  iconv-Builds; der Default-EBCDIC-Pfad nutzt ihn nicht mehr. Die C1-Guards
  (No-Progress + Hard-Cap, §1.4) bleiben im Iconv-Wrapper für den Fallback.
- `vcpkg.json`: `"icu": "78.3"` (exakt; Orakel-/CI-Abhängigkeit — nicht in
  die Laufzeit-Link-Ziele verknüpft).

### 2.2 Determinism-Pins (D1, D2)

- `tools/`-Generator (aus dem Spike) regeneriert Tabellen + 256-Byte-Verdict-JSON
  aus ICU 78.3; CI-Schritt: Regenerieren → Byte-für-Byte-Diff gegen die
  gecheckten Tabellen/JSON → jeder Orakel-Drift bricht die CI.
- `tests/test_encoding_determinism.cc`: 256-Byte-Sweep (E2A/A2E) gegen die
  Laufzeit-Tabelle inkl. strict/non-strict-Verdicts; Verdict-JSON gecheckt
  ein → P3 mechanisch erzwungen. CI-Matrix (MSVC/clang/GCC) läuft dieselben
  Tests und difft die JSONs.
- Solange der iconv-Fallback gebaut wird: loggter Diff-Lauf (dokumentiert
  den iconv-Rückstufungsgrund).

### 2.3 Test-Auswirkungen

- `test_incompatible_input.cc`: Die **Umgebungsguards entfallen**
  (`codec::as`-Probe) — die Ablehnung ist jetzt deterministisch (Tabelle),
  der Gift-Case wird auf jeder Maschine assertet (strict-Default). Der
  Fixture-Guard (lokal-only `issues/a`) bleibt.

**Exit criteria:** EBCDIC-Pfad ohne Laufzeit-Converter-Abhängigkeit; Tabellen +
Verdict-JSON gepinnt und über 3 Toolchains identisch; 288+ Tests unter ASan
grün; Issue-A-Repro: sauberer positionierter Fehler (gleiche Offset/Byte-
Angabe); Root-`AGENTS.md`-Sektion „Encoding & determinism" aktualisiert
(Englisch); `icu` exakt 78.3 im Manifest gepinnt.

---

## Phase 3 — Trust (spec loading) & concurrency (≈5–6 d)

### 3.1 Include sandbox (E1, High per Q1)

New additive public API in `include/iso8583/ISOSpec.hh`:

```cpp
struct TNG_EXPORT SpecLoadOptions {
    bool trackSourceMap   = true;
    bool sandbox          = true;   // Q1: specs können third-party sein -> an
    std::vector<std::string> roots; // leer -> {top-level-Spec-Verzeichnis}
    bool allowSmapWrite   = true;   // Sidecar-Schreiben nur innerhalb roots (F4)
    std::size_t maxSpecBytes    = 32u * 1024u * 1024u; // C3
    std::size_t maxIncludeFiles = 1024;                // C3
};
// additive Overloads (bestehende Signatur bleiben):
static std::shared_ptr<ISOParserPtrBase> loadFromYaml(const std::string& path, const SpecLoadOptions& opts);
static std::tuple<..., std::shared_ptr<ISOSpec>> loadBothFromYaml(const std::string& path, const SpecLoadOptions& opts);
```

- `_preprocessor.cc` include resolution (`!include_files`, ~line 575): after `fs::absolute`, canonicalize (`weakly_canonical`) and **sandbox check**: candidate must lie inside one of the (normalized) roots, else `throw std::runtime_error("[ISO8583] SpecLoader: Sandbox-Verletzung – Include '…' liegt außerhalb der erlaubten Verzeichnisse")`. Escape via `../../`, absolute paths, and UNC all fail closed.
- `visitedFiles`/cycle guard stays; **C3** caps enforced: total loaded bytes > `maxSpecBytes` or include count > `maxIncludeFiles` → clean throw at load time.
- **F4:** `.smap` sidecar written only when the sidecar path is inside a root AND `allowSmapWrite` (else warn-only, as today). Sidecar **load** capped (e.g. 16 MB) → clean throw otherwise.
- Legacy `loadFromYaml(path, bool)` overloads keep working = `SpecLoadOptions{trackSourceMap=bool}` (sandbox ON, roots = spec's dir). Breaking edge: top-level specs that include from *outside* their own directory now need explicit `roots` — called out in changelog; `sandbox=false` remains the explicit escape hatch.
- **E3** (`_spec.cc` `buildParser`): `loaded.fields.empty()` → throw `"[ISO8583] SpecLoader: Spec hat keine 'fields' – kann keinen Parser bauen"` (kills the `rbegin()==rend()` UB). `std::stoi` on keys stays guarded by `validateSpecYaml` (keep both, add regression test with a non-numeric key → clean validation error).

### 3.2 Spec cache hardening (E2)

- Cache entry becomes `{parser, spec, mtime, contentHash}`; **publish-then-verify** protocol: load content → hash → lock → compare (mtime, hash) with cached entry → publish only on consistent match, else rebuild. A hot-swapped spec file can no longer yield a stale parser (TOCTOU closed at publish point).
- `TrustUntilInvalidated` kept as explicit policy for the cached overloads but **documented as unsafe for rolling spec changes** (root `AGENTS.md` + `ISOSpec.hh` docs: "restart on spec change or use Verify"). Default policy for financial deployments: verify.
- LRU cap: 64 entries (configurable), evict least-recent — bounds the unbounded-growth note.
- Test: two threads, one loading while the file is swapped between → resulting parser always matches one complete file version (hash-checked), never a mix.

### 3.3 Threading model (F1, Q3: supported usage)

- `ISOMessage`: **all** public entry points take `d_lock_` (switched to `std::recursive_mutex`): `set/unset/has/reset/keys/size/to_json/dump/parse/unparse/mti/isRequest/header()/header(bytes)/header(ptr)/parser(p)`. Internal `_locked` variants for the call chain (`set → recalcBitmap`) avoid double-locking.
- `p_`, `hf_`, `recalc_`, `hdr_` are only touched under the lock (fixes `size()` :528, `to_json` :558, `recalcBitmap`, `dump`, `parser()`-assignment).
- `to_json`/`dump`: snapshot field values under the lock, then format outside it (bounds lock hold time).
- Shared-state docs: parser objects are immutable after construction → shareable; **one `ISOMessage` from N threads: supported**. Known residual hazard, documented in `ISOMessage.hh`: `mti()` returns a `string_view` into mutable storage — copy it (`std::string m = msg->mti();`) before cross-thread use; `isRequest()` is made safe by copying internally (fixes the `mti().at(2)` raw `std::out_of_range`, B5).
- **F3** (`src/_logger.cc`): `g_level` → `std::atomic<Level>`; `g_external_logger` → atomic pointer load/store (or `std::atomic<std::shared_ptr<ISOLogger>>`); `TNG_LOG` hot path = one atomic level read (unchanged perf).

### 3.4 PCI logging hygiene (F2)

- New YAML field key `sensitive: true` (added to the accept list); `ISOFieldParser` (src-internal template — no public ABI impact) stores the flag.
- All `TNG_LOG_*` sites that print **field values** (parser field dumps, codec errors beyond the minimal failing-byte hex, `dump()`) emit `***` for sensitive DEs; descriptions may still be logged.
- Docs (root `AGENTS.md` + `README.md`): production logging level = WARN; DEBUG prints field values — never to shared/long-lived sinks in PCI scope. The Phase-1 EILSEQ error message keeps only the 12-byte failing-field hexdump (no PAN echo) — documented.
- Test: `sensitive: true` on DE002 → DEBUG log of a set/unparse cycle contains `***`, not the PAN.

**Exit criteria:** TSan run of suite + new concurrent-sharing test green; sandbox escape tests green (in-sandbox OK, `../../` + absolute + UNC rejected); cache TOCTOU test green; `TrustUntilInvalidated` docs in place.

---

## Phase 4 — Fuzzing, CI, docs, release 0.3.0 (≈4–5 d incl. soak)

### 4.1 Fuzz targets (libFuzzer, clang, Linux CI; harness skeleton from Phase 0)

| Target | Entry point | Invariant checked |
|---|---|---|
| F1 `fuzz_unparse` | `ISOMessage::unparse` over fixed specs (gmc_dmsa, minimal, BERTLV) | P1: only clean exceptions; strict: truncated frames never "succeed" |
| F2 `fuzz_spec` | `SpecDecoder::loadFromYaml` on generated YAML (incl. `!include_files`, deep nesting) | P1 + sandbox + C2/C3 caps |
| F3 `fuzz_serialize` | `ISOMessage::parse` with random/oversized field values | P2: outgoing frame never claims (bitmap) a missing/omitted field |
| F4 `fuzz_header` | `ISOMessage::header(<random vector>)` (WLP + BASE1) | P1 (A3 closed) |
| F5 `fuzz_tlv` | TLV unparse, BERTLV tag/length policies | no shift overflow (BerLength long-form > 8 length bytes), no OOB (read_num bounds) |

Also fix the two TLV latent items found in the audit as part of F5 hardening: `read_num`/BCD policy get explicit `offset + N <= buf.size()` pre-checks (positioned throw); `BerLength` rejects `num_bytes > 8` (saturation + throw); `store_se` rejects/warns when the BER tag doesn't fit `TNG_KEY_TYPE` (no silent `static_cast` truncation → no SE misrouting).

- Soak: 24 h per target on CI before the release tag; zero P1/P2 (crash/UB/strict-violation) findings allowed, P3 (robustness) findings triaged.
- Corpus: `issues/a/test_message.txt`, all test fixtures, generated truncations from Phase 1, Phase-0 256-byte table.

### 4.2 CI matrix

- Per PR: `debug` + `debug-asan` (ctest full suite) on MSVC x64 & Linux (clang); `tsan` on Linux.
- Nightly: fuzz soak + cross-toolchain verdict-table diff (P3).

### 4.3 Documentation & release

- `changelog.md` (German): 0.3.0 entry — strict default (Q2) with `strict: false` escape hatch; WLP-FO `pack()` now emits the full 93-byte header (fixes wire corruption, Q5); iconv → ICU (Q6) + ICU version pin; include sandbox (Q1) with migration note for out-of-dir includes; thread-safety of `ISOMessage` (Q3); `sensitive:` logging key; ABI note (rebuild required).
- `README.md` (German): short "Security" subsection (threat model summary, strict default, sandbox, ICU pin, logging guidance).
- Root `AGENTS.md` (English, maintained per existing convention): new sections "Security invariants (P1–P4)", "Strict mode", "Spec sandbox", "Threading model", "Encoding & determinism (ICU pin)", "Memory-safety rules" (A4 rule).
- `issues/a/README.md` (local only, not pushed): mark the open libiconv-build item as **closed by ICU migration** (cross-reference Phase 2).
- Tag `v0.3.0` after all exit criteria green.

---

## Finding → fix mapping (complete)

| ID | Sev | Fix | Phase |
|----|-----|-----|-------|
| A1 | C | bitmap size pre-checks + positioned throw; explicit `bmp.size()>65` guard | 1 |
| A2 | C | `WLP_FOHeader::pack()` returns full 93-byte header; `parse()` guards the **packed** result | 1 (Q5) |
| A3 | H | `BaseHeader::require(n)` in all header getters/setctors + ctor validation; `ISOMessage::header(…)` validation | 1 |
| A4 | M | audit rule + guards at call sites (vendored bitset untouched) | 1 (docs: 4) |
| A5 | M | null-check + throw in `ISOMessage::parser(p)` | 1 |
| B1 | H | strict-mode truncation throw / WARN (non-strict) | 1 |
| B2 | H | strict-mode oversized throw / ERR-log+omit (non-strict) | 1 |
| B3 | H | `set()` return checked in decode path → throw | 1 |
| B4 | H | `parse()` no longer swallows; throws on failure/misconfiguration | 1 |
| B5 | M | `isRequest()` copies; `mti()` hazard documented (API stable) | 1 (docs: 4) |
| B6 | I | length-prefix pre-check → positioned error | 1 |
| C1 | M | no-progress detection + hard output cap (iconv now, ICU final) | 1/2 |
| C2 | M | recursion depth caps (parse/unparse 64; `finalize` 200) | 1/2 |
| C3 | L | spec/include size + count caps via `SpecLoadOptions` | 3 |
| D1 | H | ICU replaces iconv; 256-byte verdict table pinned; cross-toolchain CI diff | 2 |
| D2 | M | table fallback: strict→reject / non-strict→`.` (Q4); parity requirement vs ICU | 2 |
| D3 | L | ICU pinned exactly in vcpkg manifest + docs | 2 |
| E1 | H (Q1) | `SpecLoadOptions` include sandbox (roots, fail-closed), sidecar write gating | 3 |
| E2 | M | cache entry w/ content hash, publish-verify protocol, LRU cap, `TrustUntilInvalidated` documented | 3 |
| E3 | L | empty-`fields` → clean throw; validation regression test | 3 |
| F1 | M→H (Q3) | full `recursive_mutex` locking of all shared state; TSan test | 3 |
| F2 | M | `sensitive:` YAML key + value masking in logs; production logging guidance | 3 |
| F3 | L | atomic logger level/pointer | 3 |
| F4 | L | sidecar write restricted to roots + `allowSmapWrite`; load size cap | 3 |

---

## Public API surface (additive only)

| Addition | Where |
|---|---|
| `SpecLoadOptions` + `loadFromYaml(path, opts)` / `loadBothFromYaml(path, opts)` | `ISOSpec.hh` |
| `ISOBaseParser::strict(bool)` / `strict() const` (+ new member → ABI: rebuild) | `detail/_interfaces.hh` |
| YAML keys: `strict:` (root), `sensitive:` (field) | spec format |
| (no signature changes; no removals; `mti()` keeps `string_view`) | — |

## Behavior changes (0.3.0, all maintainer-approved)

1. **Strict by default:** truncated frames / oversized fields / invalid EBCDIC (both ICU and table path) now **throw** instead of silently clamping/dropping/mapping to `.`. Escape hatch: `strict: false` in the spec or `parser.strict(false)`.
2. **WLP-FO serialization:** `pack()`/`header()` emit the full 93-byte header (previously 89 bytes + garbage) — outgoing WLP frames change bytes; this is the corruption fix (Q5).
3. **Spec sandbox on by default:** includes outside the spec directory (or explicit `roots`) fail at load time.
4. **iconv → ICU:** EBCDIC verdicts follow the pinned ICU version (divergence from vcpkg-libiconv debug/release builds eliminated, Q6).
5. **`ISOMessage` is now thread-safe for shared use** (Q3); `parser()` throws on wrong parser type.

## Risks & mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | vcpkg ICU lacks EBCDIC tables / huge dep (~10–20 MB, slow builds) | Phase-0 spike gates Phase 2; fallback: static ICU or keep transitional iconv fallback (`ISO8583_ENABLE_ICONV`) |
| R2 | Strict default breaks lenient integrators | `strict: false` escape hatch + prominent changelog (Q2 accepted) |
| R3 | libiconv removal breaks `ISO8583_ENABLE_ICONV` consumers | keep as deprecated transitional fallback through 0.3.x; removal no earlier than 0.4 |
| R4 | `recursive_mutex` overhead / lock-ordering deadlock | single lock, `_locked` internal variants, TSan+lock-order audit in Phase 3; batch workload → negligible |
| R5 | ICU UnicodeString per-call allocation | 1:1 byte conversions, negligible vs. network I/O; benchmark only if Phase-2 tests show it |
| R6 | Out-of-dir includes break existing spec layouts (sandbox default) | document; explicit `roots` API; `sandbox=false` escape hatch |

## Verification gates (release checklist)

1. `ctest --preset debug` **and** `debug-asan` green (full suite, all phases).
2. `tsan` (Linux/clang) green incl. new concurrent-sharing test.
3. Fuzz soak 24 h × 5 targets: zero P1/P2.
4. Cross-toolchain (MSVC, clang, GCC) verdict-table diff identical (P3).
5. Issue-A repro app: clean positioned DE006 error under ICU; no SEH/terminate.
6. WLP production check: serialize a real WLP-FO frame → byte-identical header vs. partner-captured sample (maintainer to provide sample if available).
7. Docs updated (§4.3); changelog complete; tag `v0.3.0`.

## Effort

Phase 0: ~2 d · Phase 1: ~5–6 d · Phase 2: ~5–7 d · Phase 3: ~5–6 d · Phase 4: ~4–5 d (incl. 24 h soak + triage). **Total ≈ 4–5 weeks** for one engineer; phases are independently releasable (1 and 2 ship as 0.2.x pre-releases if desired, 3+4 as 0.3.0).