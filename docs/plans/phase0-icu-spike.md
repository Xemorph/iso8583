# Phase 0-Bericht: ICU-Spike + Sanitizer-/Fuzz-Tooling

**Status:** abgeschlossen. Die Phase-2-Korrektur (Q6/P3) wurde vom Maintainer
am 2026-07-22 bestätigt (Tabellen-Lookup als Laufzeit, ICU exakt 78.3 als
Build-/CI-Orakel, `libiconv`-Fallback bis 0.4). Phase 1 (fail-closed) ist
**unabhängig** davon.

- Datum: 2026-07-22
- Spike-Projekt (lokal, untracked): `issues/b/icu_spike/`
- Spike-Artefakte: `icu_verdicts_e2a.json`, `icu_verdicts_a2e.json`,
  `strict_e2a_table.h`, `strict_a2e_table.h`, `main.cc` (Sweep-Tool)

---

## 1. Setup

| Parameter | Wert |
|---|---|
| ICU | **78.3** (auflösbar über vcpkg-Baseline `1f5e0348089e…` aus `vcpkg.json`; ältere 74.2-Pakete im vcpkg-Tree sind Relikte einer früheren Baseline) |
| Compiler | MSVC 14.51 (VS 18), Ninja, `/O2 /MD` (Release), C++20 |
| API | `ucnv_open()` + **`ucnv_convertEx()`** (TARGET-Converter zuerst; `reset=true`, `flush=true`; Grow-Dance auf `U_BUFFER_OVERFLOW_ERROR`) |
| Richtungen | IBM-1047 → US-ASCII (E2A) und US-ASCII → IBM-1047 (A2E) |
| Sweep | alle 256 Bytes pro Richtung, 1 Byte pro Aufruf + Sequenztests |

ICU-78-API-Abweichungen, die der Spike erwischt hat (relevant für Phase 2):
`F_FAILURE` ist entfernt (→ `U_FAILURE()`), Statusnamen tragen `_ERROR`-Suffix
(`U_BUFFER_OVERFLOW_ERROR == 15`), `u_getVersionString` ist entfernt.

## 2. Befunde

### 2.1 ICU funktioniert

- Converter `IBM-1047` und `US-ASCII` öffnen ohne Fehler.
- Gültige Sequenzen exakt (MATCH):
  - `F5 F0 F6 F2` → `"5062"` (MTI-Fall aus `issues/a`)
  - `F0..F9` → `"0123456789"`
  - `F2 F0 F3 F0 F0 F0 F0 F3 F4` → `"203000034"` (DE004-Fall)
  - A2E: `"5062"` → `F5 F0 F6 F2`

### 2.2 Kernbefund: ICU 78 lehnt **kein** Byte ab

**E2A: 0/256 abgelehnt. A2E: 0/256 abgelehnt.** Es traten keine
Fehler-Statuscodes bei der Konvertierung auf.

- IBM-1047 ist bei ICU eine **vollständige Byte-Mapping-Tabelle**: auch die
  als „ungedefiniert" geltenden EBCDIC-Positionen werden *irgendwohin*
  gemappt (meist C1-Steuerzeichen wie `0x1A`, teils `0x20`/`0x09`/`0x7F`).
  Beispiel: `0x24 → 0x1A`, `0x10 → 0x10`, `0x05 → 0x09` (Tab), `0x07 → 0x7F` (DEL).
- Die zweite Stufe (Pivot → US-ASCII) **substituiert** Codepoints > `U+007F`
  durch `?` statt eines Fehlers zu melden.
- A2E: ASCII-Bytes > `0x7F` (also eigentlich „nicht-ASCII") durchlaufen
  US-ASCII→Pivot als Codepoints `U+0080…U+00FF` und werden von IBM-1047 auf
  `?` (`0x3F`) substituiert — ebenfalls ohne Fehler.
- **Die Gift-Sequence aus `issues/a` (DE006, `f8 10 24 85 9b 46 a2 3d e3 c6
  81 8b`) konvertiert ICU „erfolgreich"** zu `8??e??s?TFa?`. Das Byte `0x24`,
  das der vcpkg-libiconv-Debug-Build mit `EILSEQ` (errno 42) abgelehnt hatte,
  wird von ICU akzeptiert (→ `0x1A`).

**Konsequenz:** Die in Phase 2 geplante „ICU-zweistufige Konvertierung"
implementiert **keine Ablehnung** und kann strikte Kodierungs-Validierung
nicht tragen. Der ursprüngliche P3-Gleichheitsansatz
(*strict-table-reject ≡ ICU-two-stage-reject*) ist damit **falsifiziert**.

### 2.3 EBCDIC↔ASCII ist unter ICU reine 1:1-Tabellen-Konvertierung

Die gesamte Konvertierung ist zustandslos und pro Byte exakt 1:1 (kein
MehrbYTE-Sequenzen, kein State, kein Fehlerpfad). Damit wird die gesamte
Konvertierungslogik durch **zwei 256-Einträge-Tabellen** exakt erfasst —
genau das, was der bestehende statische Tabellen-Pfad schon tut. Die
Spike-Artefakte (`strict_*_table.h`, `icu_verdicts_*.json`) sind die
ICU-78-Referenzdaten für alle 512 Mappings.

## 3. Korrigierte Phase-2-Architektur (Vorschlag, abzustimmen)

| Aspekt | Ursprünglicher Plan | Korrigiert (nach Spike) |
|---|---|---|
| Laufzeit-Konvertierung | `thread_local` ICU-Wrapper (`ucnv_convert`) | **Tabellen-Lookup** (256-Einträge, deterministisch, kein externer Converter auf dem Hot-Pfad) |
| ICU-Rolle | Laufzeit-Abhängigkeit (ersetzt iconv) | **Build-/CI-zeitliches Orakel**: erzeugt die Tabellen (pinned ICU 78.3), CI-Diff als Determinitäts-Pin; optional später für echte Multi-Byte-Encodings |
| vcpkg `icu` | Laufzeit-Dep | Dev-/CI-Dep (Generator-Tool); `libiconv` bleibt bis 0.4 als deprivierter Fallback (Q6: iconv wird als *Laufzeit*-Abhängigkeit durch Tabellen ersetzt — die Abhängigkeit `icu` bleibt im Manifest als Orakel) |
| Strict-Modus (Q2/Q4) | Ablehnung via ICU-Fehlerstatus | **Tabellen-getrieben**: strikt = Ablehnung aller Bytes, die der Legacy-Tabelle als `.`-Sentinel markiert sind (Ergänzungsmenge der Whitelist); non-strict = Legacy-Verhalten (`.`-Mapping) unverändert |
| P3-Invariante | *strict-table ≡ ICU-two-stage* | **Neu:** *Tabellen ≡ pin-veröffentlichtem ICU-78-Orakel* (CI: Regeneration + Byte-für-Byte-Diff) + *Whitelist-Mappings ≡ Legacy-Tabelle* (Kreuzprüfung; bei Differenz gewinnt das ICU-Orakel, Pin dokumentiert die Diff) |
| Test-Auswirkung | — | `test_incompatible_input.cc`: die **Umgebungsguards entfallen** — da die Ablehnung jetzt deterministisch (Tabelle, kein iconv), lässt sich der Gift-Case auf jeder Maschine asserten; der Test wird schärfer |

**Warum das besser ist:** kein `thread_local`-Converter, kein `E2BIG`-Grow-Loop,
keine build-abhängigen Converter-Builds (exakt die Ursache des
`issues/a`-Vorstfalls), keine ICU-DLL zur Laufzeit — und die
Determinitäts-Pins (256-Byte-JSON) machen jede Tabellen-Änderung in CI
sichtbar.

**Keine Auswirkung auf Phase 1** (fail-closed-Bounds-Checks, strict-Default,
Header-Guards o.ä. sind codec-unabhängig) — Phase 1 kann sofort starten.

## 4. Tooling-Deliverate (Phase 0)

1. **`CMakePresets.json`:**
   - `debug-asan` — MSVC `/fsanitize=address /Zi`, statisches `iso8583`
     (`ISO8583_BUILD_SHARED=OFF`), statische CRT (`/MTd`),
     vcpkg-Triplet `x64-windows-static` → ASan-Runzeit wird statisch
     verlinkt, kein DLL-Pfad-Problem unter `ctest`.
   - `linux-tsan` — `-fsanitize=thread`, **nur Linux-CI mit clang**
     (Compiler in der CI setzen); auf Windows/MSVC nicht konfigurierbar.
   - zugehörige `buildPresets`/`testPresets` ergänzt; `cmake --list-presets`
     zeigt alle Presets.
2. **Fuzz-Skelett:** `tests/fuzz/` mit `fuzz_codec` (voll funktional:
   `codec::as<>` EBCDIC+ASCII, Invariante „nie crashen"), `fuzz_tlv`,
   `fuzz_parse`, `fuzz_unparse`, `fuzz_spec` (Skelett, Phase 4: echte
   Parser-Routen anbinden). Wächter in `tests/fuzz/CMakeLists.txt`:
   bei Nicht-clang-Compiler werden die Ziele mit Warning übersprungen →
   normale MSVC-Builds brechen nicht, selbst mit
   `ISO8583_BUILD_FUZZERS=ON`. Root `CMakeLists.txt`: neue Option
   `ISO8583_BUILD_FUZZERS` (default OFF).
3. **Spike** `issues/b/icu_spike/` (lokal): `main.cc` (Sweep-Tool,
   wiederverwendbar als Tabellen-Generator in Phase 2), JSON-Verdicts,
   C-Tabellen.

## 5. Exit-Kriterien

| Kriterium | Status |
|---|---|
| Spike: 256-Byte-Tabellen beider Richtungen + Sequenztests + Gift-Case | ✅ erledigt (Abschnitt 2) |
| „ICU funktioniert"-Entscheidung + Befund protokolliert | ✅ erledigt (Abschnitt 2.1/2.2) |
| ASan-Preset vorhanden, minimaler Test-Subset-Lauf | ⏳ Build läuft (vcpkg-`x64-windows-static`-Install am ersten Mal lang); Ergebnis folgt hier |
| TSan-Preset vorhanden (Linux-CI) | ✅ vorhanden (läuft nur unter Linux/clang) |
| Fuzz-Skelett kompilierbar, bricht normale Builds nicht | ✅ (MSVC-`debug`-Preset rekonfiguriert sauber nach CMakeLists-Änderung; Fuzz-Ziele unter MSVC per Wächter übersprungen) |
| Entscheidung Phase-2-Korrektur (Q6/P3) durch Maintainer | ✅ bestätigt 2026-07-22 (Tabellen-Lookup, ICU 78.3-Orakel, iconv-Fallback bis 0.4) |

## 6. Entscheidungen (2026-07-22 durch Maintainer bestätigt)

1. **Q6/P3-Korrektur angenommen:** Tabellen-Lookup als Laufzeit, ICU nur als
   Build-/CI-Orakel (empfohlene Option) ✅
2. **Pins:** `icu` exakt **78.3** in `vcpkg.json` pinnen ✅
3. **`libiconv`-Fallback:** bis 0.4 behalten (depriviert), dann entfernen ✅