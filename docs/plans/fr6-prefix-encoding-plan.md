# FR-6 Implementierungsplan: Längenpräfix-Encoding unabhängig vom Nutzdaten-Encoding

> **Status:** offen (Plan erstellt 2026-10-05 vom iso8583-agent)
> **Ursprung:** Feature-Request von `tng-wire-viewer` (Tauri-App, Konsument von
> libiso8583) — Vault-Notiz `ai_connected: iso8583/FR-6 Laengenpraefix-Encoding
> unabhaengig vom Nutzdaten-Encoding.md`, Chatroom-Post
> `ai_connected: chatrooms/iso8583.md` (2026-10-05T10:04).
> **Repo-Stand bei Erstellung:** `main` (0.6.4, Commit `76a4836`).
> **Ziel-Release:** 0.7.0-Kandidat (ABI-Änderung durch neues
> `SpecFieldInfo`-Mitglied, s. WP5).

---

## 1. Kontext und Problem

Klassische VISA-BASE-I-/VIP-Nachrichten kodieren Zahlenfelder als gepacktes
BCD, alphanumerische Felder als EBCDIC/ASCII und stellen variablen Feldern ein
**BCD- bzw. binäres Längenbyte** voran. Das Encoding des Längenpräfixes
unterscheidet sich dabei vom Encoding der Nutzdaten. Die Spec-Syntax kennt pro
Feld aber nur EIN `encoding:`-Key, der Präfix **und** Nutzdaten bestimmt.
Fehlende Kombinationen in `parserTable()` (Stand v0.6.3/v0.6.4,
`src/_spec.cc`):

| Lücke | Beispiel (BASE I) | Aktueller Workaround des Konsumenten |
|---|---|---|
| `lnum`/`llnum` mit BCD (Präfix **und** Nutzdaten) | DE2 PAN, DE32/33 | `llchar, encoding: bcd` (BCD-Präfix + BCD-Ziffern — wire-identisch, aber semantisch CHAR statt NUMERIC in der Introspektion) |
| `llnum\|ebcdic` | — | — |
| BCD-/Binär-Präfix + ASCII/EBCDIC-Text | DE35 Track 2, DE44/45/48/54/104/123–125 | EBCDIC-Ziffernpräfix unterstellt + `[?]`-Markierung (gegen echten Host vermutlich falsch) |
| Binär-Präfix + Text/Zahlen | DE48 | `lbinary`-Umweg taugt nicht: dort zählt das Präfix **Bytes**, das Wire-Format zählt **Ziffern** |

Der Konsument hat einen dokumentierten Workaround, ist nicht dringend —
verlangt aber die exakte Abbildung des Wire-Formats.

## 2. Wichtige Architektur-Tatsache (vorab verifiziert)

**Die Codec-Ebene ist bereits vollständig orthogonal.** Präfix-Codec und
Nutzdaten-Codec sind getrennte Template-Parameter:

- `src/_parser.hh:218` — `ISOFieldParser<T, l_, pe_, e_, p_>`:
  `pe_` (`PrefixEncoder`) und `e_` (`Encoder`) sind unverbunden; die
  `static_assert`s (Z. 223–227) beschränken nur FIX/UNKNOWN/CONSUME.
- `src/_parser.hh:656–661` — `ISOOpaqueFieldParser`/`ISOBinaryFieldParser`
  sind Template-Aliase über `(Length, PrefixEncoder, Encoder, Padder)`.
- `include/iso8583/detail/_codec_impl.hh` — `parsed_length<pe,l>`,
  `encode_length`/`decode_length` (reine Präfix-Codecs) und
  `required_sz_for_as<e>` (logische Einheiten → Bytes: BCD = n/2,
  ASCII/EBCDIC/BINARY = n) sind unabhängig voneinander komponierbar.
  Der geparste Präfixwert `l` ist in **logischen Einheiten** (Ziffern für BCD,
  Zeichen/Bytes für ASCII/EBCDIC/BINARY); `unparse()`/`parse()` in
  `src/_parser.hh` (Z. ~379–382 / ~521–556) rechnen `l` über
  `required_sz_for_as<e_>` in Bytes um.
- **Praxis-Beweis:** gemischte Kombinationen shippen bereits:
  `LBINARY|BCD` → `IFB_LBINARY` (BCD-Präfix + rohe Bytes) und
  `LBINARY|EBCDIC` → `IFE_LBINARY` (EBCDIC-Präfix + rohe Bytes) sind in
  `parserTable()` (`src/_spec.cc:946–960`) enthalten.

⇒ **Es sind KEINE Codec-Änderungen nötig** (EBCDIC-Tabellen, `strict_`-
Propagation, `_codec_impl.hh` bleiben unangetastet). Der ganze Aufwand liegt in
vier Schichten: Dispatch-Tabelle, YAML-Key + Validierung, Introspektion,
Doku/Tests.

## 3. Design-Entscheidungen

- **D1 — Neuer optionales Feld-Key `prefix_encoding:`** (Werte
  `ascii` | `ebcdic` | `bcd` | `binary`, case-insensitive wie `encoding:`).
  **Default = das aufgelöste `encoding:` des Feldes** → Specs ohne den Key
  verhalten sich byte-identisch wie heute (rein additiv, kein Breaking
  Change). Kein Root-Level-Key (das Präfix-Encoding ist immer feldspezifisch;
  ein Default ergäbe nur Konfusion).
- **D2 — Dispatch:** `createScalarParser()` (`src/_spec.cc:977–994`) baut
  heute `key = format + "|" + encoding` (2-teil) und fällt auf `format + "|"`
  zurück. Neu: wenn `prefix_encoding` deklariert **und ≠ encoding**, wird der
  **3-teile Key** `FORMAT|ENCODING|PREFIX` verwendet; andernfalls unverändert
  2-teil. Identitätsgap-Kombinationen (`lnum|bcd` u. a.) werden als normale
  2-teile Einträge in die bestehende Tabelle aufgenommen.
- **D3 — Aliase:** Die drei fehlenden Familien-Aliase werden in
  `src/fmt_types.hh` ergänzt (`IFB_LNUM`, `IFB_LLNUM`, `IFE_LLNUM`). Die 22
  gemischten Kombinationen erhalten **keine** benannten Aliase, sondern
  inline Template-Argumente in der Dispatch-Tabelle (private Datei, keine
  öffentliche API — keine CMake/Header-Registrierung nötig).
- **D4 — Scope der neuen Kombinationen:** BCD- und BINARY-Präfix ×
  ASCII/EBCDIC-Nutzdaten (char und num) plus die drei Identitätslücken.
  ASCII-/EBCDIC-Präfix × fremde Text-Payload (z. B. EBCDIC-Ziffernpräfix +
  ASCII-Text) werden **bewusst nicht** ausgeliefert — die Mechanik
  unterstützte sie, die Dispatch-Tabelle nicht; ein späterer Dialekt kann sie
  per 2 Zeilen (Alias/Tabellenzeile) nachliefern. Alles Weitere fällt unter
  Fail-closed.
- **D5 — Introspektion:** `SpecFieldInfo` gewinnt `std::string
  prefix_encoding` = **effektives** Präfix-Encoding (deklariert, sonst =
  `encoding`; bei encoding-neutralen Formaten `""`). `SpecFieldFormat` bleibt
  unverändert (Präfix-*Breite* via `prefix_digits` unverändert, das neue
  Mitglied trägt die Präfix-*Codierung*).
- **D6 — Fail-closed-Regeln** (positionierte `SpecValidationError`, nie rohe
  std-Exceptions — Hard Rule 8):
  1. `prefix_encoding` nur auf **variablen Skalar-Formaten** (Format beginnt
     mit `L`: `lchar`…`llllchar`, `lnum`, `llnum`). Auf fixen Formaten,
     `amount`, `remaining`, `bitmap`, `nop`/`unused` und `*bertlv` ablehnen
     (Begründung in den Fehlermeldungen: bei `*binary`/`bertlv` bestimmt
     `encoding:` bereits das Präfix-Codec; bei fixen Formaten existiert kein
     Präfix).
  2. Value-Whitelist `ascii|ebcdic|bcd|binary` — alles andere wird abgelehnt.
  3. Die Kombination `(format, encoding, prefix_encoding)` muss in der
     Dispatch-Tabelle existieren (Prüfbar über die gleichen Keys wie
     `createScalarParser` — beide Funktionen sind statics im selben
     Übersetzungseinheit `src/_spec.cc`).
  4. TLV-Kinder (beide TLV-Formen, Map-`children:`): `prefix_encoding` ist
     ein widersprüchlicher Key (die TLV-Länge liegt im Length-Feld des Frames)
     → in die `badKey`-Listen aufnehmen (siehe WP3).
- **D7 — Längenzählung (normativ, in die Doku):** Bei **Zahlen**feldern zählt
  das Präfix **Ziffern** (auch bei gepacktem BCD-Nutzdaten: 19 Ziffern →
  Präfix `0x13`, Nutzdaten 10 Bytes), bei **Text**feldern Zeichen (= Bytes bei
  ASCII/EBCDIC), bei `binary`-Nutzdaten Bytes. Präfix-Breiten (s.
  `parsed_length`): BCD: L/LL = 1 Byte, LLL/LLLL = 2 Byte; BINARY: L/LL =
  1 Byte, LLL/LLLL = 2 Byte (Big-Endian, unsigned); ASCII/EBCDIC: L/LL/LLL/
  LLLL = 1/2/3/4 Bytes (Ziffern).

## 4. Vollständige Liste neuer Dispatch-Einträge (25)

### 4.1 Neue 2-teile Einträge (Identitätslücken, `parserTable()`)

| Key | Typ | Alias (`src/fmt_types.hh`) |
|---|---|---|
| `LNUM\|BCD` | `ISOOpaqueFieldParser<L, BCD, BCD>` | `IFB_LNUM` (neu) |
| `LLNUM\|BCD` | `ISOOpaqueFieldParser<LL, BCD, BCD>` | `IFB_LLNUM` (neu) |
| `LLNUM\|EBCDIC` | `ISOOpaqueFieldParser<LL, EBCDIC, EBCDIC>` | `IFE_LLNUM` (neu) |

### 4.2 Neue 3-teile Einträge (gemischte Präfix-/Nutzdaten-Encoding)

Alle als `ISOOpaqueFieldParser<Length, PrefixEncoder, Encoder>`
(Padder-Default `NONE`; variable Felder werden nicht gepaddet):

**CHAR, Nutzdaten ASCII** (4 L-Stufen × 2 Präfixe = 8):

| Key | Typ |
|---|---|
| `LCHAR\|ASCII\|BCD` | `<L, BCD, ASCII>` |
| `LLCHAR\|ASCII\|BCD` | `<LL, BCD, ASCII>` |
| `LLLCHAR\|ASCII\|BCD` | `<LLL, BCD, ASCII>` |
| `LLLLCHAR\|ASCII\|BCD` | `<LLLL, BCD, ASCII>` |
| `LCHAR\|ASCII\|BINARY` | `<L, BINARY, ASCII>` |
| `LLCHAR\|ASCII\|BINARY` | `<LL, BINARY, ASCII>` |
| `LLLCHAR\|ASCII\|BINARY` | `<LLL, BINARY, ASCII>` |
| `LLLLCHAR\|ASCII\|BINARY` | `<LLLL, BINARY, ASCII>` |

**CHAR, Nutzdaten EBCDIC** (3 L-Stufen × 2 Präfixe = 6):

| Key | Typ |
|---|---|
| `LCHAR\|EBCDIC\|BCD` | `<L, BCD, EBCDIC>` |
| `LLCHAR\|EBCDIC\|BCD` | `<LL, BCD, EBCDIC>` |
| `LLLCHAR\|EBCDIC\|BCD` | `<LLL, BCD, EBCDIC>` |
| `LCHAR\|EBCDIC\|BINARY` | `<L, BINARY, EBCDIC>` |
| `LLCHAR\|EBCDIC\|BINARY` | `<LL, BINARY, EBCDIC>` |
| `LLLCHAR\|EBCDIC\|BINARY` | `<LLL, BINARY, EBCDIC>` |

**NUM, Nutzdaten ASCII** (2 L-Stufen × 2 Präfixe = 4):

| Key | Typ |
|---|---|
| `LNUM\|ASCII\|BCD` | `<L, BCD, ASCII>` |
| `LLNUM\|ASCII\|BCD` | `<LL, BCD, ASCII>` |
| `LNUM\|ASCII\|BINARY` | `<L, BINARY, ASCII>` |
| `LLNUM\|ASCII\|BINARY` | `<LL, BINARY, ASCII>` |

**NUM, Nutzdaten EBCDIC** (2 L-Stufen × 2 Präfixe = 4):

| Key | Typ |
|---|---|
| `LNUM\|EBCDIC\|BCD` | `<L, BCD, EBCDIC>` |
| `LLNUM\|EBCDIC\|BCD` | `<LL, BCD, EBCDIC>` |
| `LNUM\|EBCDIC\|BINARY` | `<L, BINARY, EBCDIC>` |
| `LLNUM\|EBCDIC\|BINARY` | `<LL, BINARY, EBCDIC>` |

**Summe: 3 + 8 + 6 + 4 + 4 = 25 neue Einträge.**

## 5. Arbeitspakete

### WP1 — Dispatch-Tabelle + Aliase (Code)

**Dateien:** `src/_spec.cc` (`parserTable()` Z. 895–975), `src/fmt_types.hh`

1. `src/fmt_types.hh`: drei Aliase ergänzen — `IFB_LNUM`/`IFB_LLNUM` im
   BCD-Block (neben `IFB_LCHAR` u. a.), `IFE_LLNUM` im EBCDIC-Block (neben
   `IFE_LNUM`), gleicher Stil/Kommentar wie die Nachbarn.
2. `parserTable()`: die 25 Einträge aus §4 einhängen — neue Sektionen
   `── Identitätslücken (FR-6) ──` (2-teile) und
   `── FR-6: gemischte Präfix-/Nutzdaten-Encoding (3-teile Keys) ──`
   (3-teile), alphabetisch/BLock-weise gruppiert wie oben. Tabellenkommentar
   (Z. 900–908, „Key = Format|Encoding") um die 3-teile-Form ergänzen.
   **Leaky-Singleton-Hinweis beachten:** kein neues Deallozieren, Tabelle
   bleibt immutable.
3. `createScalarParser()` (Z. 980–994): Key-Bildung um die Präfix-Komponente
   erweitern:
   ```cpp
   std::string key = f.format + "|" + f.encoding;
   if (!f.prefix_encoding.empty() && f.prefix_encoding != f.encoding)
       key += "|" + f.prefix_encoding;
   ```
   Fallback-Logik (`format + "|"`) und die `strict_length`/`amount`-Zweige
   bleiben unverändert. Fehlermeldung am Ende (Z. ~1013) um die Präfix-
   Information ergänzen, wenn gesetzt (Backstop — primär wirft WP3 beim
   Laden positioniert).

**Checkpoint:** Build grün (beide Key-Typ-Builds, s. §8); keine Verhaltens-
änderung bei Specs ohne `prefix_encoding` (alle bestehenden Tests grün).

### WP2 — `SpecField` + YAML-Key + Validierung (Code)

**Datei:** `src/_spec.cc`

1. `struct SpecField` (Z. 125–151): Mitglied ergänzen
   ```cpp
   // FR-6 (0.7.0): optionales 'prefix_encoding' (Feld-Key) — Encoding des
   // Längenpräfixes, unabhängig vom Nutzdaten-Encoding. Leer = nicht
   // deklariert (effektiv = encoding).
   std::string prefix_encoding;
   ```
2. `validateFieldKeys` (Z. 241–254): `"prefix_encoding"` in die
   `allowed`-Menge aufnehmen (gilt automatisch auch für `definitions:`-
   Blöcke und Field-only-`field:`-Blöcke, da dieselbe Funktion läuft).
3. `parseSpecField` (Z. 622 ff.): **nach** dem `sign`-Block (Z. ~768), vor
   der length-0-Warnung (Z. ~770), neuen Block nach dem
   `scale`/`sign`-Muster (Z. 715–768):
   ```text
   if (hasKey(node, "prefix_encoding")):
       const auto pe = toUpper(getStr(node, "prefix_encoding"));
       (a) pe muss in {ASCII, EBCDIC, BCD, BINARY} liegen
           → sonst SpecValidationError ("ungültiger Wert ... erlaubt: ascii, ebcdic, bcd, binary"),
              Position node["prefix_encoding"].id()
       (b) Format muss variable Skalar-Form sein: beginnt mit 'L' und Basis
           in {CHAR, NUM} (nach BERTLV-Rewrite prüfen — 'bertlv' ist zu
           diesem Punkt schon '...BINARY' → Basis 'BINARY' fällt raus)
           → sonst SpecValidationError mit kontextsensitiver Begründung:
              - fixe Formate/NOP/BITMAP/REMAINING: "kein Längenpräfix vorhanden"
              - *binary/bertlv: "'encoding' bestimmt bei ...binary bereits das Präfix-Codec"
              - amount: "amount hat kein Längenpräfix"
           Position node["prefix_encoding"].id()
       (c) Kombinations-Check: der (endgültige) Dispatch-Key —
           3-teil wenn pe != f.encoding, sonst 2-teil — muss in parserTable()
           existieren → sonst SpecValidationError
           ("Kombination format=... encoding=... prefix_encoding=... ist nicht verfügbar",
           s. Format×Encoding-Matrix in spec_schema.md §3)
       f.prefix_encoding = pe;
   ```
   Hinweis für den Implementierer: `parserTable()` ist im selben TU static —
   der Check ist ein `parserTable().contains(key)`. Die BERTLV-Kurzform
   (`f.format`-Rewrite Z. 641–652) läuft bereits **vor** dieser Stelle.
4. TLV-Kinder: `prefix_encoding` in die `badKey`-Listen aufnehmen:
   - Container-Kinder (eigener `tlv`-Block): Z. ~378
     `{ "format", "length", "encoding" }` → `{ "format", "length", "encoding", "prefix_encoding" }`
     (Fehlermeldung generiert den Key-Namen automatisch aus `badKey`).
   - Nicht-Container-TLV-Kinder: analoge Prüfung in `validateTlvChildMap`
     (Z. ~401–437 prüfen `format`/`encoding` explizit) — `prefix_encoding`
     auf einem TLV-Kind ist ebenfalls widersprüchlich → verwerfen.
5. Root-Level: `prefix_encoding` ist **kein** Root-Key — bei Auftreten auf
   Root-Ebene greift die bestehende Root-Key-Validierung in
   `validateSpecYaml` (unbekannter Root-Key → bereits abgelehnt; falls Root-
   Keys nicht whitelisted sind, ist kein Code nötig — einmal verifizieren).

**Checkpoint:** neue Loader-Tests (WP4) grün; alle bestehenden
`[spec]`/`[loader]`-Tests grün (Whitelist-Erweiterung ist für alte Specs
no-op).

### WP3 — Introspektion (öffentliche API, ABI)

**Dateien:** `include/iso8583/ISOSpec.hh` (Z. 88–184), `src/_spec.cc`
(`makeSpecFieldInfo`, Z. 1365–1395), `include/iso8583/AGENTS.md`

1. `SpecFieldInfo`: neues Mitglied **nach** `amount_signed` (Ende der
   Struktur — Layout-Append, gleiche Konvention wie `tlv_is_ber`/
   `amount_scale`):
   ```cpp
   /// @brief Effective length-prefix encoding of a variable-length field (FR-6, 0.7.0).
   ///
   /// `"ASCII"` | `"EBCDIC"` | `"BCD"` | `"BINARY"` | `""` (keine/encoding-
   /// neutrale Formate).  Equals `encoding` when the YAML `prefix_encoding:`
   /// key is absent (the default); carries the declared value otherwise.
   ///
   /// @note ABI: adding this member changes the `SpecFieldInfo`
   ///       layout — shared-library consumers must be rebuilt (0.7.0).
   std::string prefix_encoding;
   ```
2. `makeSpecFieldInfo`:
   ```cpp
   info.prefix_encoding = f.prefix_encoding.empty() ? f.encoding : f.prefix_encoding;
   ```
   (rekursiv automatisch auf `children`/`tlv_children` übertragen).
3. `include/iso8583/AGENTS.md`: Tabelle „SpecFieldInfo-Mitglieder“ um die
   Zeile `prefix_encoding` ergänzen + ABI-Hinweis-Absatz (Stil wie
   `amount_signed`) + „Typische Fehler“-Tabelle: neue Zeile
   *„`prefix_encoding` an fixen Formaten / `*binary` / TLV-Kindern (0.7.0)"
   → „nur variable `*char`/`*num`-Formate; bei `*binary`/`bertlv` bestimmt
   `encoding:` bereits das Präfix — sonst `SpecValidationError` beim Laden"*.

**Checkpoint:** Doxygen-Kommentar vollständig (`sphinx -W` grün, s. WP6);
Introspektions-Test (WP4) zeigt effektive Werte (auch Default = `encoding`).

### WP4 — Tests

**Neue Datei:** `tests/test_prefix_encoding.cc` (Pattern-Vorbilder:
`tests/test_strict_length.cc` für Loader-/Validierungstests,
`tests/test_field_parser.cc` für Codec-Roundtrips). **Muss in
`tests/CMakeLists.txt` registriert werden — vor `test_e2e_full_message.cc`**
(die bleibt letzte). Alle `TEST_CASE`-Namen **ASCII-only** (keine Umlaute),
Tag `[prefix]` (+ `[spec]` bzw. `[field]`-Tags passend).

**A) Codec-Roundtrips** (Parser-Typen direkt, wie in `test_field_parser.cc`;
`unparse` → Wert prüfen → `parse` → wire-Bytes byte-identisch):

| Fall | Wert | Erwartetes Wire-Bild |
|---|---|---|
| `IFB_LLNUM` (llnum\|bcd) | `"4111111111111111"` (16 Ziffern) | `0x10` + `41 11 11 11 11 11 11 11` |
| `LLCHAR\|ASCII\|BCD` | `"HELLO"` | `0x05` + `48 45 4C 4C 4F` |
| `LLCHAR\|EBCDIC\|BCD` | `"AB"` | `0x02` + `C1 C2` |
| `LLLCHAR\|ASCII\|BINARY` (2-Byte-BE-Präfix) | `"ABC"` | `00 03` + `41 42 43` |
| `LLNUM\|ASCII\|BCD` (Ziffern-Präfix zählt ZIFFERN, ASCII-Ziffern als Nutzdaten) | `"123456"` | `0x06` + `31 32 33 34 35 36` |

**B) Fehlerpfade:**
- Trunkation: Präfix komplett, Nutzdaten abgeschnitten → strict:
  `runtime_error` „Feld am Pufferende abgeschnitten" (generisch, kein
  kombinierungspezifischer Code — reicht einer der obigen Fälle).
- Prefix am Pufferende abgeschnitten (B6-Guard) bei 2-Byte-BINARY-Präfix.

**C) Loader-/Validierungstests** (Spec-YAML in Tempdatei laden, wie in
`test_strict_length.cc`/`test_spec_loader.cc`):

1. Voll-Spec mit gemischten Feldern (aus der FR-6-Notiz, minimal auf DE0/DE1
   + drei Felder reduziert):
   ```yaml
   spec: "FR-6 prefix test"
   encoding: ebcdic
   fields:
     "000": { format: numeric, length: 4 }
     "001": { format: bitmap,  length: 8 }
     "002": { format: llnum,   encoding: bcd,    prefix_encoding: bcd,    length: 19, description: "Pan" }
     "035": { format: llchar,  encoding: ebcdic, prefix_encoding: bcd,    length: 37, description: "Track2" }
     "048": { format: lllchar, encoding: ascii,  prefix_encoding: binary, length: 255, description: "AddData" }
   ```
   → Load erfolgreich; `loadBothFromYaml`: `spec->field(2)->prefix_encoding
   == "BCD"`, `field(48)->prefix_encoding == "BINARY"`; Dekodieren eines
   handgebastelten Frames + Re-Encode byte-identisch.
2. Explizite Identität: `llnum + encoding: bcd + prefix_encoding: bcd`
   (2-teiler Key) → Load + Roundtrip.
3. Default: Feld ohne `prefix_encoding` → `SpecFieldInfo::prefix_encoding ==
   encoding` (z. B. `"EBCDIC"` bei globalem EBCDIC); encoding-neutrales
   Feld (`binary` fix) → `""`.
4. Fail-closed (je ein `REQUIRE_THROWS_AS(..., SpecValidationError)` bzw.
   `std::runtime_error`, je nach Pfad + Message-Teile prüfen):
   - `prefix_encoding` auf `numeric`/`char` (fix), `amount`, `remaining`,
     `bitmap`, `nop`, `lllbinary`/`lllbertlv`
   - ungültiger Wert `prefix_encoding: hex`
   - verfügbare-Kombinations-Lücke: `llchar|ascii + prefix_encoding: ebcdic`
     (nicht in der Tabelle → abgelehnt)
   - `prefix_encoding` in TLV-Kindern (tlv:-Block und bertlv-Form) und bei
     Container-Kindern
   - `prefix_encoding` auf Root-Ebene (falls WP2.5 zeigt, dass Root-Keys
     whitelisted sind, sonst weglassen)
5. Field-only-Dokument (FE-1-Pfad): `field:`-Block mit
   `prefix_encoding` → `loadFieldFromYaml` + `decodeField` funktioniert.
6. `!merge`-Komposition: `!merge [!template L(CHAR, 37),
   { encoding: ebcdic, prefix_encoding: bcd }]` → wie explizite Schreibweise.

**Checkpoint:** `ctest --preset debug` vollständig grün (beide Key-Typ-Builds);
neue Suite via `libiso8583_tests "[prefix]"` einzeln ausführbar.

### WP5 — Doku

**Dateien** (alle im selben Commit wie die zugehörige Code-Änderung, s.
„Commits"):

1. `docs/internals/spec_schema.md`:
   - §2 (Feld-Deklaration, Key-Tabelle Z. ~67–79): Zeile
     `prefix_encoding` | `ascii|bcd|ebcdic|binary` | nein |
     „Encoding des Längenpräfixes, unabhängig vom `encoding` (Nutzdaten).
     Default = `encoding`. Nur auf variablen `*char`/`*num`-Formaten;
     Breiten-/Zählregeln s. §3"
   - §3: Matrix-Zeilen `l*`/`lnum` um die neuen Präfix-Spalten ergänzen bzw.
     neuen Unterabschnitt „Längenpräfix-Encoding (`prefix_encoding`, 0.7.0)“:
     Breitenregel (D7), Zählregel, Liste der 25 Kombinationen (aus §4),
     Fail-closed-Regeln (D6), Hinweis `!template`/`!merge`
   - §7 (Encoding-Auflösung): `prefix_encoding` ist feldlokal, wird nicht
     geerbt/vererbt (klarschreiben)
   - §8 (Validierung): neue Fehlerfälle aufnehmen
   - §12 (Checkliste für Generatoren): eine Zeile
2. `include/iso8583/AGENTS.md`: s. WP3.3 (+ „Format/Encoding-
   Kombinationen"-Auflistung um `prefix_encoding`-Hinweis ergänzen)
3. `.agents/yaml-spec.md` (§5/§6): Grammatik-Ergänzung + `src/`-Map-Hinweis
   (englisch, wie die übrige Datei)
4. `changelog.md` **und** `docs/changelog.md` (müssen identisch bleiben):
   `[+](Added)`-Eintrag FR-6 (0.7.0-Kandidat): YAML-Key, 25 Kombinationen,
   Identitätslücken `lnum`/`llnum`-BCD/`llnum`-EBCDIC,
   `SpecFieldInfo::prefix_encoding` (ABI: Layout, Shared-Library-Consumer
   neu kompilieren), Fail-closed-Regeln.
5. `docs/index.rst`: Toctree-Zeile `plans/fr6-prefix-encoding-plan` (diese
   Plan-Doku) — bereits bei der Planungseinstellung gemacht.

**Checkpoint:** `sphinx -W` (Docs-Build) grün; beide Changelogs
byte-identisch (`git diff` nach der Synchronisation leer).

### WP6 — Abschluss, Kommunikation, Release-Vorbereitung

1. **Vault-Abschluss** (vom Maintainer/Agent nach grünem Build): FR-6-Notiz
   `ai_connected: iso8583/FR-6 ...md` auf `status: erledigt` +
   `gelöst_in: v0.7.0`-Hinweis, `Übersicht.md`-Abschnitt „Offen" aktualisieren,
   Chatroom-Post an `tng-wire-viewer-agent`: Umsetzung bestätigt,
   Workaround (`llchar|bcd` für Ziffernfelder, EBCDIC-Präfix-Annahme mit
   `[?]` in `specs/visa_base1.yml`) kann nach v0.7.0 auf exakte
   `prefix_encoding`-Deklarationen umgestellt werden; **Um eine echte
   BASE-I-Testnachricht gebeten** (hat der Konsument laut Chatroom noch
   nicht) zur finalen Byte-Verifikation.
2. **Version:** `0.7.0` an den 4 Version-Stellen (`CMakeLists.txt`,
   `include/iso8583/config.h` `TNG_CORE_VERSION`, root `vcpkg.json`,
   `vcpkg-port/vcpkg.json`) — erst im Release-Commit (maintainer-initiiert,
   nur nach grünem Docs-Run auf `main`, s. `.agents/process-release.md` §14.2).
   Bis dahin bleibt die FR-6-Änderung ein 0.7.0-Kandidat auf `main`.

## 6. Bewusst NICHT in Scope

- ASCII-/EBCDIC-Präfix × fremde Text-Nutzdaten (18 weitere Kombinationen;
  Mechanik vorhanden, Nachrüsten trivial — s. D4).
- Root-Level-`prefix_encoding`-Default (D1).
- `!template`-Direktive mit Präfix-Argument (bleibt 2-arg; `!merge` genügt).
- `prefix_encoding` bei TLV-Kindern oder `*binary`/`bertlv` (widersprüchlich,
  D6.1).
- Neue öffentliche Parser-Konstruktoren/Header (Aliase in `src/fmt_types.hh`
  sind privat; die neuen Kombinationen sind über YAML erreichbar).

## 7. Hard Rules, die der Implementierer beachten muss

1. **C++20**, keine `detail/`-/`src/_*.hh`-Includes aus „Nutzer"-Code;
   neue Kombinationen rein über Template-Aliase (keine neuen Klassen).
2. **Keine ABI-/API-Seitenwirkungen außer** dem einen
   `SpecFieldInfo`-Mitglied (WP3) — das muss im Changelog + AGENTS.md als
   ABI-Notiz stehen (Shared-Library-Consumer neu kompilieren). Kein
   Vtable-Change, keine neuen public Headers, keine CMake-Header-Registrierung,
   keine explizite Template-Instanzierung in `src/_components.cc` (die
   `ISOOpaqueFieldParser`-Instanziierungen leben wie alle anderen in
   `src/_spec.cc` via Header).
3. **EBCDIC-Tabellen unverändert** — keine Codec-Änderung; `verify-ebcdic-
   tables` nicht nötig (Tabellen unangetastet), aber der bestehende Test
   `test_encoding_determinism.cc` muss grün bleiben.
4. **`strict_`-Propagation:** es wird **kein** neuer Codec-Call-Site
   eingeführt (die gemischten Parser-Instanziierungen laufen durch die
   bestehenden, bereits `strict_`-awareen Pfade in `src/_parser.hh`) — nichts
   zu tun, aber beim Review verifizieren.
5. **Loader-Fehler** ausschließlich als positionierte
   `SpecValidationError`/`std::runtime_error` mit SourceMap (Hard Rule 8) —
   nie `std::stoi`-Artiges; Fehlermeldungen auf Deutsch (Code-Kommentare/
   Doku: deutsch, wie umgebende Dateien).
6. **Kein Commit von `.smap`-Sidecars** (Cache neben Specs).
7. `test_e2e_full_message.cc` bleibt **letzte** Suite in
   `tests/CMakeLists.txt`; neue `TEST_CASE`-Namen ASCII-only.
8. Wenn Verhalten geändert wird, das in `.agents/*` oder
   `include/iso8583/AGENTS.md` beschrieben ist: betroffene Datei im
   **selben Commit** aktualisieren (WP5-Liste dazu).

## 8. Build & Verifikation

```bash
# MSVC: aus Developer Prompt / vcvars64.bat heraus starten (sonst LNK1104)
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
# neue Suite isoliert:
build/debug/bin/libiso8583_tests "[prefix]"
# BERTLV-Build (int32-Keys) ebenfalls komplett:
cmake --preset debug-bertlv && cmake --build --preset debug-bertlv && ctest --preset debug-bertlv
# Doku (CI erzwingt -W):
#   sphinx-Build laut .agents/build-test-ci.md — muss grün sein
```
Hinweis: `ctest` zählt früh-ende (geskippte) Tests als Passed und schließt
`[slow]` aus — Ausgabe auf Skips sichten. Bekannte Maschinen-Anomalie
(ACP=1252/OEMCP=437, 4 Umlaut-Testnamen rot auf exe-Ebene): nicht als
FR-6-Regression interpretieren.

**Definition of Done:**
- beide Key-Typ-Builds: `ctest` vollständig grün, neue `[prefix]`-Suite grün
- `sphinx -W` grün
- Akzeptanzkriterien aus §9 nachgewiesen
- Doku-Sync-Checkliste (WP5) vollständig abgehakt, Changelogs identisch
- (vom Maintainer) Release v0.7.0 erst nach grünem Docs-Run auf `main`

## 9. Akzeptanzkriterien (aus FR-6 + Chatroom)

1. **Lücke 1 geschlossen:** `llnum`/`lnum` mit BCD-Länge + gepackten Ziffern
   (DE2-artig) laden, dekodieren und byte-identisch re-encodieren;
   Introspektion meldet `format.type == "NUMERIC"` (nicht mehr die CHAR-
   Semantik des `llchar|bcd`-Workarounds).
2. **Lücke 2/3 geschlossen:** BCD- und Binär-Längenpräfix mit
   ASCII/EBCDIC-Text (DE35/DE48-artig) und mit Zahlen-Nutzdaten; Zählregel:
   Ziffern/Zeichen/Bytes je Typ (D7), Breitenregel L/LL/LLL/LLLL wie heute.
3. **Reine Additivität:** jede existierende Spec ohne `prefix_encoding`
   verhält sich byte-identisch (gesamte Bestands-Testsuite als Nachweis).
4. **Fail-closed:** alle Regeln aus D6 werfen positionierte Fehler beim
   Laden — nie Stille-Auswahl, nie rohe Exceptions.
5. **Introspektion:** `SpecFieldInfo::prefix_encoding` liefert das effektive
   Präfix-Encoding (Default = `encoding`) für alle Feld-Ebenen (inkl.
   `children`/`tlv_children`-Einträge, wo definiert).
6. **Konsumenten-Handover:** nach Release kann `tng-wire-viewer` die
   `[?]`-markierten BASE-I-Felder in `specs/visa_base1.yml` exakt deklarieren
   (vcpkg-Pin von v0.6.3 auf v0.7.0 heben); finale Byte-Verifikation mit
   einer echten BASE-I-Testnachricht, sobald der Konsument sie liefert.
