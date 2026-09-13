# Plan: `remaining` encoding-aware + YAML-Spec-Schema für AI-Agents

> **Status:** in Umsetzung
> **Version:** nächstes Release (0.6.0-Kandidat — Breaking Change)
> **Verwandt:** `docs/internals/yaml_format.md`, `docs/internals/encoding.md`,
> `docs/plans/tlv-typed-children-plan.md` (0.5.0, Präzedenz für Breaking Changes)

## Ausgangslage

1. **`remaining` ist (fälschlich) encoding-neutral:** `isEncodingNeutral()` in
   `src/_spec.cc` zählt `REMAINING` zu den Formaten, die jede Encoding-
   Einstellung ignorieren. Ein `remaining`-Feld in einer EBCDIC-Spec wird
   deshalb immer als rohe Bytes dekodiert — die Parser-Tabelle enthält
   bereits `REMAINING|EBCDIC` (`IFE_REMAINING`), der Eintrag ist aber über
   YAML unerreichbar (dead code), weil `resolveEncoding()` für neutrale
   Formate immer `""` liefert.
2. **Quirk:** `remaining` ohne `length` dekodiert still 0 Bytes (der Clamp
   `l > de_l_ → l = de_l_` greift mit `de_l_ = 0`), während die Doku
   "konsumiert alle restlichen Bytes" verspricht.
3. **Kein formales Schema:** Für andere AI-Agents, die korrekte YAML-Specs
   erzeugen sollen, existiert keine kompakte, normative Referenz über das
   komplette Spec-Format (nur die narrativen Deep-Dives `yaml_format.md` /
   `encoding.md`).

## Entscheidungen (vom Nutzer bestätigt, 2026-09-13)

| # | Entscheidung |
|---|-------------|
| D1 | `remaining` ohne auflösbare Encoding (keine Feld- und keine globale `encoding`) bleibt **roh: `""`/`binary` → `BinaryField`** (Catch-all, minimale Brechung). |
| D2 | `remaining` akzeptiert **alle vier Encodings**: `ascii`, `ebcdic`, `bcd`, `binary` (gleiche Semantik-Tabelle wie jedes andere Format). Text-Encoding → `OpaqueField`, `binary`/`""` → `BinaryField`. |
| D3 | `remaining` **ohne `length` → positioniertes `SpecValidationError`** (Fail-closed, konsistent mit D5 der 0.5.0-Planung). `length` = Maximum (bestehender Clamp). Doku-Beispiele ohne `length` werden korrigiert. |
| D4 | Schema als **normative Markdown-Referenz** `docs/internals/spec_schema.md` (Deutsch, Toctree → Docs-Site). Kein JSON Schema (YAML-Tags `!use`/`!merge`/`!template` lassen sich damit nicht ausdrücken). |

## Zusatz-Fix (bei der Analyse entdeckt)

**BCD-Byte/Ziffern-Diskrepanz im `remaining`-Pfad:** `unparse()` in
`src/_parser.hh` berechnet bei `Length::UNKNOWN` die Länge als
`l = b.size() - o` (**Bytes**), aber `codec::as<string, BCD>` und
`required_sz_for_as<BCD>` erwarten **Ziffern** (1 Byte = 2 Ziffern).
Ohne Korrektur würde ein `remaining`-Feld mit `encoding: bcd` still die
halbe Pufferlänge dekodieren und ein falsches `consumed` melden.
→ Im `UNKNOWN`/`CONSUME`-Zweig bei `Encoder::BCD` `l *= 2` (nur relevant
für die neue `REMAINING|BCD`-Instanz; alle bestehenden Instanzen sind
ASCII/EBCDIC/BINARY).

## WP1 — Code: `remaining` encoding-aware (Breaking)

1. `src/_spec.cc`:
   - `isEncodingNeutral()`: Neutral-Set → `BINARY, BITMAP, NOP, UNUSED`
     (`REMAINING` entfernt).
   - `parserTable()`: + `{ "REMAINING|ASCII", IFA_REMAINING }`,
     + `{ "REMAINING|BCD", IFB_REMAINING }`; bestehende
     `REMAINING|` / `REMAINING|BINARY` → `IF_REMAINING` (roh),
     `REMAINING|EBCDIC` → `IFE_REMAINING` bleiben.
   - `validateSpecYaml()`: `format: remaining` ohne `length` →
     `SpecValidationError` (positioniert; ersetzt die bisherige
     Warning-Ausnahme).
   - Introspection (`_spec.cc`, `max_length`-Zuweisung): `REMAINING` wird
     nicht mehr auf 0 erzwungen — das deklarierte Maximum wird gemeldet
     (korrektheit, passt zur neuen Pflicht).
2. `src/fmt_types.hh`: + `IFA_REMAINING`
   (`ISORemainderFieldParser<std::string, Encoder::ASCII>`),
   + `IFB_REMAINING` (`…<std::string, Encoder::BCD>`) — Namenskonvention:
   `IFA_` = ASCII, `IFB_` = BCD, `IFE_` = EBCDIC, `IF_` = BINARY.
3. `src/_parser.hh`: BCD-Ziffern-Korrektur im `UNKNOWN`/`CONSUME`-Zweig
   (siehe Zusatz-Fix).
4. `include/iso8583/ISOSpec.hh`: Kommentare (`type`-Werteliste,
   `max_length`-Semantik für `REMAINING`).

**Breaking-Implikation:** In Specs mit globalem Text-Encoding
(`ascii`/`ebcdic`/`bcd`) wechseln `remaining`-Werte von `BinaryField` auf
`OpaqueField`. Konsumenten: `get<OpaqueField>(de)` verwenden oder
`encoding: binary` am Feld pinnen. (Muster wie 0.5.0 TLV-Kinder.)

## WP2 — Tests

`tests/test_remaining_field.cc` (neue TEST_CASEs, Tags `[remaining][spec]`
ggf. + `[roundtrip]`):
- **YAML-Matrix** (TempDir-Specs): globale Encoding
  `∅/ascii/ebcdic/bcd/binary` × `remaining` (mit `length`) → unparse →
  erwarteter Runtime-Typ (`BinaryField` bzw. `OpaqueField`) + Wert;
  EBCDIC-Payload mit `0xC8 0xC5 0xD3 0xD3 0xD6 0x40` ("HELLO "),
  BCD-Payload `{0x01,0x23,0x45,0x67,0x89,0xAB}` → `"0123456789AB"`.
- **Feld-Override:** global `ascii`, Feld `encoding: ebcdic` → EBCDIC-Typ.
- **Roundtrip** (parse→unparse) für `ascii` und `ebcdic`
  (Wert-Setzen via `set()`, Wire-Vergleich).
- **Strict:** `strict: true` + EBCDIC-Payload mit `0x9C` → throw;
  `strict: false` → Legacy-`'.'`-Mapping.
- **Validierung:** `remaining` ohne `length` → `SpecValidationError`
  (Try/Catch, Message-Teilerkennung); bestehender Test
  ("loads without error", mit `length: 10`) bleibt grün.
- Bestehende BMP_061-/IFE_REMAINING-Tests bleiben unverändert grün.

**Verifikation:** `debug` + `debug-bertlv` ctest vollständig grün.

## WP3 — Doku (Deutsch)

1. **Neu:** `docs/internals/spec_schema.md` — normatives Schema
   (Ziel: AI-Agents erzeugen daraus korrekte Specs). Aufbau:
   1. Dokumentgerüst (Root-Keys, `!include_files` + `---`, Typen/Defaults)
   2. Feld-Deklaration (alle Keys: `type`, `format`, `length`, `encoding`,
      `description`, `sensitive`, `children`, `tlv` + Direktiven
      `!use`/`!merge`)
   3. **Format×Encoding-Matrix** (1:1 zu `parserTable()`: erlaubte
      Encodings + resultierender Runtime-Typ pro Format; inkl. neuer
      `remaining`-Semantik)
   4. `remaining` (neue Semantik: encoding-aware, `length` = Pflicht/Max)
   5. Direktiven (`!use`, `!template`, `!merge`, `!include_files` +
      Sandbox-Regeln, `!include` deprecated)
   6. Nested & TLV (children Seq vs. Map, `tag_bytes`/`len_bytes` vs.
      `ber: true`, `...bertlv`-Kurzform + Hex-Map, Whitelist D5/0.5.0,
      typisierte Kinder, SE-Key-Notation)
   7. Encoding-Auflösung & -Vererbung (Feld > global > `""`, nur noch für
      echt neutrale Formate)
   8. Validierung / Fail-closed-Verhalten (vollständige
      Fehlerliste inkl. neuer `remaining`-Regel)
   9. Lauffeld-Verhalten (`wire_offset`/`wire_length`, `sensitive`,
      strict)
   10. Komplette Beispiele (ASCII; EBCDIC mit TLV + BERTLV-Kindern;
      `!use`/`!template`/`!include_files`)
   11. Häufige Fehler (Auto-Bitmap, Hex-Strings für BinaryField,
      L-Präfix vs. TLV, `remaining` in TLV, strict-Default, EBCDIC-
      Prefix-Nibbles)
2. `docs/index.rst`: Toctree "Internals" + `internals/spec_schema`.
3. `docs/internals/encoding.md`: Neutral-Liste (`REMAINING` entfernen),
   Tabelle/Hinweis: `remaining` ist jetzt encoding-aware.
4. `docs/internals/yaml_format.md`: `remaining`-Zeile in der Format-Tabelle
   (alle Encodings, `length` = Pflicht/Maximum), Beispielzeile 235 korrigieren
   (`length` ergänzen), Verweis: "Normative Referenz: spec_schema.md".
5. `AGENTS.md` (Root, Englisch): §4 Neutral-Liste + Encoding-Tabelle,
   YAML-Beispiel (`remaining` bekommt `length`), Format-Liste.
6. `include/iso8583/AGENTS.md` (Deutsch): `SpecFieldFormat`-Tabelle,
   Format-Auflistung, Beispiel, `remaining`-Beschreibung.
7. `ISOSpec.hh`-Kommentare (mit WP1).

## WP4 — Changelog & Release-Vorbereitung

- `changelog.md` + Spiegel `docs/changelog.md` (byte-identisch):
  neue `## Unreleased`-Sektion, eindeutige Subsections:
  - `[!]` BREAKING: `remaining` respektiert Encoding
    (BinaryField ⇒ OpaqueField in Text-Specs)
  - `[#]` `remaining` ohne `length` → Fail-closed-Validierung
  - `[~]` Introspection: `REMAINING` meldet deklariertes `max_length`
  - `[+]` Neues Schema-Dokument `spec_schema.md`
- **Kein Version-Bump jetzt** (nächstes Release = 0.6.0-Kandidat;
  Bump erst im Release-Flow §14.2).
- Kommit-Konvention: `[!](BREAKING) …` für WP1, `[~](Updated) …` für WP3,
  `[i](Info) …` für WP4.

## Risiko-Notizen

- **ABI:** keine Signaturen-/Layout-Änderungen (private Aliase +
  Tabelle; `SpecFieldInfo`/`ISO_MAP` unberührt). `SpecFieldInfo::max_length`
  für `REMAINING` liefert jetzt den deklarierten Wert (Behavior, kein Layout).
- **`ISOConsumer`** (`Length::CONSUME`) bleibt BINARY — von der BCD-Korrektur
  in `if constexpr (BCD == e_)` nicht betroffen (instanziiert nur mit BINARY).
- **TLV-Whitelist** unverändert: `remaining` bleibt in TLV-Kindern verboten
  (D5, 0.5.0) — gilt unabhängig von der Encoding-Änderung.
- **Doku-Beispiele** ohne `length` (AGENTS.md, yaml_format.md,
  include/AGENTS.md) werden in WP3 korrigiert, sonst würde das neue
  Schema eigene Beispiele als ungültig markieren.