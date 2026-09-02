# tools/generate_ebcdic_tables — EBCDIC-Orakel-Tool (ICU 78.3)

Reines **Build-/CI-Tool**: es generiert und verifiziert die ICU-78.3-
Orakel-Verdicts, gegen die die gecheckten EBCDIC-(IBM-1047)-Tabellen in
[`include/iso8583/_codec.hh`](../../include/iso8583/_codec.hh)
(`kEbcdicToAscii` / `kAsciiToEbcdic` / `kEbcdicValid`) deterministisch
geprüft werden (s. `tests/test_encoding_determinism.cc`).

**ICU wird ausschließlich an das Tool-Executable gelinkt — niemals an
Runtime-Targets der Bibliothek.** Die Laufzeit-Konvertierung nutzt
ausschliesslich die gecheckten Tabellen (kein iconv, kein ICU im
Default-Build).

## Warum ein Orakel-Pin?

Die Tabellen ersetzen frühere Laufzeit-Converter (libiconv/ICU), deren
Verhalten je nach Plattform-Build und Version variieren konnte. Durch den
exakten Pin auf **ICU 78.3** (vcpkg-Abhängigkeit in der Wurzel
[`vcpkg.json`](../../vcpkg.json)) ist die Tabellenprovenanz reproduzierbar:

- `pinned/icu_verdicts_e2a.json` / `pinned/icu_verdicts_a2e.json` sind die
  byte-stabilen Orakel-Verdicts aller 256 Bytes beider Richtungen
  (`ucnv_convertEx`, IBM-1047 ↔ US-ASCII), erzeugt mit ICU 78.3.
- Das Tool bricht mit Exit-Code 2 ab, wenn ICU **nicht** major 78 ist —
  andere ICU-Versionen können andere IBM-1047-Zuordnungen liefern
  (C1-Steuerbyte-Gebiet) und würden die Pin-Dateien verfälschen.
- Die Whitelist (`kEbcdicValid`, 85 gültige E2A-Bytes; A2E-Validität über
  `kAsciiToEbcdic[c] != 0x6F`, 84 gültige ASCII-Bytes) ist bewusst
  **strenger** als ICU: ICU konvertiert alle 256 Bytes (inkl. C1-Steuer-
  und Binärbytes), die Bibliothek akzeptiert im strict-Modus nur die
  IBM-1047-Druckzeichen-/Ziffern-Whitelist. Der Determinism-Test prüft
  daher: für alle whitelist-akzeptierten Bytes stimmen Tabellenspiegelung
  und Orakel überein; abgelehnte Bytes sind dokumentierte, bewusste
  Abweichungen.

## Nutzung

Voraussetzung: `ISO8583_BUILD_CODEC_TOOLS=ON` beim Configure plus ICU 78.3
(vcpkg, Pin in der Wurzel `vcpkg.json`). Fehlt ICU, wird das Sub-Project
übersprungen (Status-Message) — die Bibliothek ist davon nicht betroffen.

```bash
cmake --preset debug -DISO8583_BUILD_CODEC_TOOLS=ON
# Prüfen, ob die gecheckten Verdicts noch zum ICU-78.3-Orakel passen:
cmake --build --preset debug --target verify-ebcdic-tables
# Tabellen aktualisieren (ergibt neue pinned/*.json; Commit nicht vergessen):
cmake --build --preset debug --target update-ebcdic-tables
```

## Datei-Verzeichnis

| Datei | Zweck |
|---|---|
| `main.cc` | Generator/Verifizierer (`--out <dir>` / `--expect <dir>`), ICU-major-78-Assert. |
| `CMakeLists.txt` | `iso8583_ebcdic_table_gen` (EXCLUDE_FROM_ALL) + Targets `update-ebcdic-tables` / `verify-ebcdic-tables`. |
| `pinned/icu_verdicts_e2a.json` | Orakel-Verdicts IBM-1047 → US-ASCII (256 Bytes), ICU 78.3. |
| `pinned/icu_verdicts_a2e.json` | Orakel-Verdicts US-ASCII → IBM-1047 (256 Bytes), ICU 78.3. |

Das JSON-Format (`converter` + `bytes[]` mit `ok`/`out` bzw.
`status`/`in_consumed`/`detail`) ist Teil des Orakel-Pins und darf nicht
geändert werden, ohne die `pinned/`-Dateien zu regenerieren und
`tests/test_encoding_determinism.cc` zu aktualisieren.