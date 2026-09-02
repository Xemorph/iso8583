# Encoding-System

## Auflösungsreihenfolge

```
Feld-Encoding  >  globales YAML-Encoding  >  "" (nur für encoding-neutrale Formate)
```

Jedes Feld-Ergebnis entsteht in `resolveEncoding()`:
encoding-neutrale Formate liefern immer das leere Encoding, alle
anderen Formate übernehmen das optionale Feld-`encoding` oder das
globale `encoding` der Spec.

## Encoding-neutrale Formate

Die folgenden Formate lesen/schreiben immer Rohtext, unabhängig von
jeder Encoding-Einstellung. Sie verwenden weder das globale Encoding
noch ein encoding-awarees Längenpräfix:

- `BINARY` (fixe Länge, kein Präfix)
- `BITMAP`
- `NOP` / `UNUSED`
- `REMAINING`

**Hinweis:** `LBINARY`, `LLBINARY`, `LLLBINARY`, `LLLLBINARY` sind
**nicht** encoding-neutral, da ihr Längenpräfix das Spec-Encoding
(EBCDIC/BCD/ASCII) verwendet.

## Feldweise Override

```yaml
spec:     "Mixed Spec"
encoding: ebcdic        # global

fields:
  "002":
    format: numeric
    encoding: bcd        # überschreibt global nur für dieses Feld
  "052":
    format: binary       # encoding-neutral — ignoriert global
```

## Encoding-Werte

| Wert | Längenpräfix | Daten |
|---|---|---|
| `ascii` | ASCII-Ziffern `'0'`–`'9'` | ASCII-Text |
| `bcd` | BCD-Nibbles | BCD-kodierte Ziffern |
| `ebcdic` | EBCDIC-Ziffern `0xF0`–`0xF9` | EBCDIC-Text |
| `binary` | Big-Endian-Bytes | Rohe Bytes |

## Kinder-Vererbung

Encoding-neutrale Felder geben das **globale** Encoding an ihre
Kinder weiter; encoding-bewusste Felder geben ihr eigenes
aufgelöstes Encoding weiter. Dadurch bleibt eine EBCDIC- oder BCD-
Spec konsistent, auch wenn zwischengeschaltete Container (z. B. ein
`binary`-DE) encoding-neutral sind.

## EBCDIC: tabellenbasiert, orakelgepinnt (seit 0.3.0)

Die EBCDIC-Konvertierung (IBM-1047) ist **voll tabellenbasiert** und
verwendet zur Laufzeit **keinen** Converter mehr — weder `libiconv`
noch ICU. Die Tabellen `kEbcdicToAscii`, `kAsciiToEbcdic` und
`kEbcdicValid` in `include/iso8583/_codec.hh` sind die einzige
Konvertierungsquelle in Build und Runtime.

**Orakel-Pin (ICU 78.3):** Die Tabellenprovenanz wird über das
Build-/CI-Tool `tools/generate_ebcdic_tables/` an ein festes
ICU-78.3-Orakel gekoppelt:

- Das Tool (nur mit `ISO8583_BUILD_CODEC_TOOLS=ON` gebaut) erzeugt
  mit `ucnv_convertEx` Verdicts für alle 256 Bytes in beiden
  Richtungen (IBM-1047 ⇄ US-ASCII) und prüft sie im Modus
  `--expect` byte-genau gegen die gecheckten Dateien
  `tools/generate_ebcdic_tables/pinned/icu_verdicts_{e2a,a2e}.json`.
- `cmake --build <builddir> --target verify-ebcdic-tables` verifiziert,
  `update-ebcdic-tables` regeneriert (Ergebnis committen). Der
  Generator verweigert den Betrieb bei ICU-Hauptversion ≠ 78 (Exit 2),
  damit ein Baseline-Bump nicht stillschweigend andere Verdicts
  erzeugt. ICU wird **ausschließlich** an das Tool-Executable gelinkt,
  nie an `iso8583` oder Tests.
- `tests/test_encoding_determinism.cc` fegt alle 256 Bytes der
  Laufzeit-Codecs über die Tabellen und vergleicht die Ergebnisse
  (Whitelist-Bytes) sowie das strict-Verhalten mit den gepinnten
  Verdicts — ein Table-Drift bricht die Test-Suite, lange bevor er
  ins Produktionssystem gelangt.

**Whitelist vs. Orakel:** Die Library-Whitelist ist bewusst
**strenger** als ICU: ICU 78.3 konvertiert alle 256 EBCDIC-Bytes
(C1-Kontrollen, Binary-Bytes), während der strict-Modus nur die
85 IBM-1047-Druck-/Ziffern-Bytes akzeptiert (E2A). A2E sind es
84 darstellbare ASCII-Zeichen plus die dokumentierte `'?'`-
Ausnahme: `0x3F` hat kein Tabellen-Mapping (Fallback `0x6F`), wird
aber auch im strict-Modus niemals verworfen. Im Legacy-Modus
(Non-strict) bleibt das alte Verhalten erhalten: unmappbare E2A-Bytes
→ `.`-Sentinel (`0x2E`), unmappbare A2E-Zeichen → `0x6F`.

**Strict-Propagation:** Das `strict()`-Flag des Parsers wird an
alle vier Codec-Aufrufstellen in `src/_parser.hh` (encode/decode ×
string/binary) als `rejectInvalid` weitergereicht. Neue
Konversions-Aufrufstellen müssen `strict_` übergeben — andernfalls
degradieren strict-Specs stillschweigend auf das Legacy-Verhalten.

**Restriktionen (dokumentiert, bewusst so):**

- EBCDIC-**Längenpräfixe** werden als rohes Lower-Nibble gelesen
  (`b[i] & 0x0F` in `decode_length`) — `constexpr` kann nicht
  werfen; ein korruptes Präfix wird fail-closed von der
  nachgelagerten strict-Byteprüfung (bzw. den B1-Längenchecks)
  abgefangen.
- **Header-**Unpacking (`WLP_FOHeader`/`BASE1Header`) bleibt
  bewusst non-strict: Header-Klassen tragen keinen Strict-Zustand
  und konvertieren mit `rejectInvalid=false`.
- `ISO8583_ENABLE_ICONV` (und `src/_iconv_wrapper.cc`) ist seit
  0.3.0 **depräkariert** (Entfernung in 0.4): Die Wrapper-Funktion
  existiert noch als Übergangs-Fallback, liegt aber nicht mehr auf
  dem Runtime-Pfad. Configure warnt bei `ON`.