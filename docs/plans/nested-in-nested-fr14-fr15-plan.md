# Plan: Container in Container (FR-14 + FR-15, inkl. Header)

Ziel: ein `nested`-Kind darf selbst Container sein (positionell oder Bitmap-Kind, auch als `header:`-Kind), beliebig tief bis zur Rekursionsgrenze. Rein additiv, kein ABI-Change.
Quelle: Obsidian `iso8583/FR-14 ...`, `iso8583/FR-15 ...` (inkl. Nachtrag 2026-10-14 zum Header).

## Ursache

Eine fehlende Rekursion, kein dreifaches Problem:

- `buildNestedSubParser` (`src/_spec.cc`) baut jedes Kind mit `createScalarParser` - sowohl im Bitmap-Zweig (FR-14) als auch in der positionellen Schleife (FR-15). Ein Kind mit `children` wird dadurch still zum Blatt.
- Die Rekursion existiert nur in `buildFieldParser` (`case NESTED`: `ISONestedFieldParser` um `containerBaseField` + `subParser(...)`).
- `ISOMessage::set_recursive_locked` loest Dot-Pfade beliebig tief auf, sobald der Parser stimmt (Vermutung, in M3 zu belegen).
- `parseHeaderField` verbietet `nested` bewusst; der Header laeuft ueber `buildNestedSubParser` und erbt den Fix.

Entscheidung: echte Rekursion statt Ladefehler.

## Meilensteine (je ein Commit)

- **M1 Tests (rot):** `tests/test_nested_in_nested.cc` (in `tests/CMakeLists.txt` vor `test_e2e_full_message.cc` registrieren): FR-15 Repro 1+2, FR-14 Repro, Round-Trip, abgeschnittene innere Container, zwei innere Container (88+22 Byte), Bitmap als Eltern-/Kind-Container, Header-Repro, Tiefenlimit.
- **M2 Rekursion im Builder:** gemeinsamer Helfer fuer das Kind-Parser-Objekt; `case NESTED` aus `buildFieldParser` extrahieren und von beiden Zweigen in `buildNestedSubParser` nutzen. `sensitive` rekursiv vererben, `strict_` an jedem Codec-Aufruf.
- **M3 Runtime pruefen:** Dekodier-/Bau-Schleife in `src/_parser.cc` und `set_recursive_locked`; Bit des Bitmap-Kindes nur bei gesetzten Unterkindern (FR-14 AK 2); nur nachziehen, was die Tests rot zeigen.
- **M4 Header:** `parseHeaderField` erlaubt `nested` (ohne `bitmap`/`tlv`/`pack`), verbietet `remaining`/`bitmap`/`nop`/`unused` rekursiv, positionierte Fehler. Konsumenten-Fixture `error--header-fields-invalid--nested-child.yml` kippt (Konsumentenseite).
- **M5 Fail-closed + Tiefe:** `pack: nibble` mit `nested`-Kind bleibt Ladefehler (Test). `checkDepth` (Limit 200, `MAX_RECURSION_DEPTH`) wirft kuenftig `SpecValidationError` mit Position (Hard-Rule 8); Test mit 201 Ebenen erwartet den Fehler. Kein neues, niedrigeres Limit (Verhaltensaenderung, nicht Teil von FR-14/15).
- **M6 Doku + Version 0.11.0:** `spec_schema.md` §1 (Header-Kinder) und §6 (nested in nested, Bitmap-Kind, Tiefe max. 200); Changelog (Root), `.agents/*`, `include/iso8583/AGENTS.md`; Version an 4 Stellen; Vault FR-14/FR-15 schliessen. Release bleibt maintainer-initiiert nach gruener Docs-Run auf `main`.

## Verifikation

`cmake --preset debug && cmake --build --preset debug && ctest --preset debug`, zusaetzlich Build mit `ISO8583_BERTLV`; `[spec]`/`[e2e]` gruen; Test-Exe direkt starten (ctest-Umlaut-Anomalie); `sphinx -W`.

## Risiken

- Dekodier-/Bau-Schleife fuer tiefere Ebenen vorab nicht gelesen (M3).
- Bitmap-Container als Kind eines Containers (FR-15 AK 4) ungeprueft.
