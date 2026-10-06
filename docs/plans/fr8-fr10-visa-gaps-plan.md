# FR-8 / FR-9 / FR-10 Implementierungsplan: VISA-BASE-I-Lücken

> **Status:** offen (Plan erstellt 2026-10-06 vom iso8583-agent)
> **Ursprung:** drei Feature-Requests von `tng-wire-viewer` (Konsument) nach
> Auswertung einer **echten VISA-BASE-I-Testnachricht** (MTI 0120) —
> Vault-Notizen `ai_connected: iso8583/FR-8 …`, `FR-9 …`, `FR-10 …`,
> Chatroom-Post `chatrooms/iso8583.md` (2026-10-06T09:45).
> **Repo-Stand bei Erstellung:** `main` (0.7.1, Commit `06a91e7`).
> **Ziel-Release:** 0.8.0-Kandidat (s. §6 — FR-9(b) ändert strict-Verhalten,
> neue Mitglieder in `ISOFieldParserPtrBase`/`SpecFieldInfo` ändern das ABI).

---

## 1. Überblick und Priorität

| FR | Inhalt | Art | Priorität (Konsument) | Aufwand |
|---|---|---|---|---|
| **FR-8** | `lnum/lchar … \| bcd \| binary`: binäres Längenbyte, Einheit **Ziffern**, vor gepackten BCD-Ziffern | Feature, additiv | **hoch** — blockiert DE2/32/33/35 der VISA-Spec | klein (Tabelle + Doku + Tests) |
| **FR-9a** | leere Sekundär-Bitmap (Bit 1) beim Bauen erzwingen | Feature, additiv | niedrig | klein–mittel |
| **FR-9b** | `bitmap length` undokumentiert; `length: 8` liest Sekundär-Bitmap beim Decode **nicht** (stille 8-Byte-Verschiebung) | Doku-Lücke + Fail-closed | **mittel (wichtigster Teil von FR-9)** | klein |
| **FR-10a** | `remaining` als Kind eines `nested`-Containers **baut nicht** | **Bug** | mittel | klein (Ursache lokalisiert, s. §4) |
| **FR-10b** | TLV-Container mit festen Kopfbytes (VISA-DE55: `01` + 2 Byte Länge + BER-TLV) | Feature → **nur Doku (E4 entschieden)** | niedrig | klein (Doku + Absicherungstest) |

**Empfohlene Reihenfolge:** FR-10a (Bug, kleinster Hebel) → FR-8 (blockiert
den Konsumenten) → FR-9b → FR-9a → FR-10b (nur Doku). Jede Gruppe = eigene Commit-Serie
(„ein logischer Change pro Commit"), Release gebündelt (§6). Falls der
Konsument schnell FR-8 braucht, kann FR-8 allein vorab als Patch/Minor raus
(Entscheidung des Maintainers; Release ist user-initiiert und erst nach grünem
Docs-Run auf `main`, `.agents/process-release.md` §14.2).

---

## 2. FR-8 — `BINARY`-Längenpräfix vor `BCD`-Nutzdaten

### 2.1 Verifizierte Ausgangslage (Code gelesen)

- Ablehnung heute an zwei Stellen in `src/_spec.cc`: (1) Kombinationsprüfung in
  `parseSpecField()` (~Z. 925–934, `parserTable().contains(dkey)`),
  (2) Backstop in `createScalarParser()`. Beide hängen **nur** an
  `parserTable()` (~Z. 1078–1202) — Einträge ergänzen genügt.
- **Codec ist bereits orthogonal** (wie in FR-6 verifiziert):
  `include/iso8583/detail/_codec_impl.hh` — `parsed_length<BINARY,L>` = L Byte,
  `encode_length/decode_length<BINARY,…>` Big-Endian, `required_sz_for_as<BCD>(n)`
  = `(n+1)/2`. In `ISOFieldParser::unparse()` (`src/_parser.hh` ~Z. 510–608) ist
  der dekodierte Präfixwert `l` bereits „logische Einheiten" und wird über
  `required_sz_for_as<e_>` in Bytes umgerechnet → **Wert = Ziffernzahl, Daten =
  ceil(Ziffern/2) Byte** fällt ohne Codec-Änderung heraus. `bcd_pad` (FR-7) wirkt
  unverändert auf die Nutzdaten (`as<>`/`to<>` bekommen `bcd_pad_`), das
  Längenpräfix bleibt unberührt.
- Bestehende BCD-Identitätsformen (`LCHAR|BCD`, `LLCHAR|BCD`, `LLLCHAR|BCD`,
  `LNUM|BCD`, `LLNUM|BCD`) bleiben unverändert (2-teiliger Key).

### 2.2 Design

- **D8.1 — Neue 3-teilige Dispatch-Einträge** (`MAKE_MIX(l, BINARY, BCD)`):
  `LCHAR|BCD|BINARY`, `LLCHAR|BCD|BINARY`, `LLLCHAR|BCD|BINARY`,
  `LNUM|BCD|BINARY`, `LLNUM|BCD|BINARY` (5 Stück; spiegelt exakt die Familien,
  für die `…|BCD` heute als Identität existiert — `LLLLCHAR|BCD` und `LLLNUM`
  gibt es nicht). Das ergibt **30 statt 25 Kombinationen**. Der Konsument
  braucht nur L=1; LL/LLL sind Symmetrie.
- **D8.2 — Keine neuen YAML-Keys.** Die Syntax ist
  `{ format: llnum, encoding: bcd, prefix_encoding: binary, length: 19 }`
  (die `parseSpecField`-Whitelist und `createScalarParser`-Key-Bildung greifen
  bereits). Introspektion `SpecFieldInfo::prefix_encoding` meldet `"BINARY"`
  automatisch (`src/_spec.cc` ~Z. 1627) — **kein ABI-Change durch FR-8**.
- **D8.3 — `length` = Maximum in Ziffern.** Zu prüfen (WP1): ob der Loader
  `length` gegen die Präfixbreite begrenzt (L binär → max. 255, LL → 65535).
  Falls es heute keine Präfix-vs-`length`-Prüfung gibt (die Suche in
  `_spec.cc` fand keine), **nicht neu einführen** (Bestandsverhalten), aber den
  Encode-Pfad testen: `length: 300` mit `L` binär würde das Präfix per
  `& 0xFF` abschneiden → ggf. fail-closed beim Laden nur für die neuen
  Kombinationen (Entscheidung in WP1, in Doku vermerken).
- **D8.4 — Vorbestehende Eigenheit, nicht ändern, aber testen/dokumentieren:**
  `unparse()` behandelt `l == 0 || l > de_l_` als `l = de_l_` (Z. ~540). Ein
  Binär-Präfix `0x00` (leeres Feld) würde dadurch als Maximum gelesen. Gilt für
  alle Präfix-Encodings gleich; im Test festhalten (Charakterisierung), nicht
  im Rahmen von FR-8 ändern.
- **D8.5 — Beantwortung der offenen Konsumentenfrage (Nibble `D`):**
  `as<string,BCD>` mappt jedes Nibble `n` auf `'0' + n` (Z. 162
  `_codec_impl.hh`): `A→':'`, `B→';'`, `C→'<'`, **`D→'='`**, `E→'>'`, `F→'?'`.
  Track 2 (`…D…`) dekodiert also zu `=` (nicht `;`/`:` wie die Doku bisher
  sagt) und `to<BCD>` schreibt `'='` verlustfrei als `D` zurück (Roundtrip). →
  In `spec_schema.md` §3 festhalten (ein Satz + Tabelle A–F) **und** mit Test
  absichern. Die bisherige Doku-Formulierung „Nibbles ≥ 0xA werden legacy als
  `:`/`;` abgebildet" ist zu prüfen und ggf. zu korrigieren.

### 2.3 Arbeitspakete

| WP | Inhalt | Dateien |
|---|---|---|
| **WP8.1** | 5 Dispatch-Einträge in `parserTable()` + Kommentar „FR-8 (0.8.0)"; Kommentarblock „Dreiwege-Keys" aktualisieren | `src/_spec.cc` |
| **WP8.2** | Neue Tests: Roundtrip DE2 16 Ziffern → `10`+8 Byte; 6 Ziffern → `06`+3 Byte; **37 Ziffern → `25`+19 Byte** mit `bcd_pad: left_zero` (führendes 0-Nibble) und `right_zero`/`right_f`; LL-Variante (2 Byte Präfix); Track-2-Nibble `D↔=`; fail-closed unverändert für unzulässige Kombis (`lnum\|ebcdic\|bcd` etc. weiterhin abgelehnt, `lbinary\|bcd\|binary` weiter abgelehnt); Introspektion `prefix_encoding=="BINARY"`; **Regression:** alle 25 FR-6-Kombinationen byte-identisch; strict-Truncation (Präfix sagt 37 Ziffern, Puffer zu kurz → positionierter Fehler); Decode der realen VISA-Struktur (DE2/32/35-Hex aus der Vault-Notiz als Fixture, **maskiert wie dort**) | `tests/test_prefix_encoding.cc` (erweitern) oder neue `tests/test_prefix_bcd_binary.cc` (dann in `tests/CMakeLists.txt` **vor** `test_e2e_full_message.cc` registrieren; ASCII-only Testnamen) |
| **WP8.3** | Doku: `spec_schema.md` §3 (Matrix, „25 Kombinationen" → 30 an **allen** Stellen, Breitenregel „BINARY-Präfix vor BCD zählt Ziffern", D→`=`-Tabelle), `yaml_format.md` (~Z. 214), `include/iso8583/AGENTS.md` (Z. ~587 + Fehler-Tabelle), `.agents/yaml-spec.md`, `.agents/pitfalls.md` falls relevant | Docs; **Änderungen in `.agents/*` im selben Commit** (AGENTS.md-Regel) |
| **WP8.4** | Changelog „Unreleased" (`changelog.md` **und** `docs/changelog.md` identisch) | beide Changelogs |

**Akzeptanz (aus FR-8):** Laden der 4 Kombis; Roundtrips (16/6/37 Ziffern);
Bestandsspecs & 25 Kombis byte-identisch; `prefix_encoding=="BINARY"`;
Doku angepasst.

---

## 3. FR-9 — Sekundär-Bitmap

### 3.1 Verifizierte Ausgangslage (Code gelesen)

- **Decode** (`ISOFieldParser<bitset>::unparse`, `src/_parser.hh` ~Z. 610–645):
  `mbits = de_l_ << 3`; mit `length: 8` ist `mbits == 64` → `len = 64`,
  Bit 1 wird zwar gesetzt, aber `return std::min(de_l_, len >> 3)` = **8**
  → die Sekundär-Bitmap wird nicht konsumiert, alle Folgefelder um 8 Byte
  verschoben. Genau das beobachtet der Konsument. Erst ab `length >= 16` wird
  bei Bit 1 gelesen (und bei `> 16` mit Bit 65 die tertiäre).
- **Build** (`parse`, ~Z. 415–429): `bytes = de_l_ >= 8 ? ((b.size()+62)>>6)<<3 : de_l_`
  — Größe folgt der **Bitset-Größe**, die `ISOMessage::recalcBitmap_locked()`
  (`src/_components.cc` ~Z. 878–913) aus dem höchsten gesetzten Feld ableitet
  (`bmap_size = (mf+63)>>6<<6`). `length` wirkt beim Bauen praktisch nicht
  (nur `<8` → clamp). Bit 1 wird nur bei `bits > 64` gesetzt (`d[0] |= 0x80`).
  Deshalb: Felder ≤ 64 → 8 Byte ohne Bit 1, egal ob `length` 8 oder 16.
- Konsequenz: **Eigene Ausgabe mit `length: 8` und DE ≥ 65 ist nicht
  rückdekodierbar** (Asymmetrie Build/Decode) — Befund bestätigt.
- `spec_schema.md` nennt `length` bei `bitmap` weder als Pflicht noch Wirkung
  (§2: „außer `bitmap`/`nop`/`unused`", Beispiele durchgängig `length: 8`).

### 3.2 Design-Entscheidungen

- **D9.1 — FR-9b (Decode, fail-closed) — Empfehlung: Option 2 des Konsumenten.**
  Bit 1 gesetzt, aber `length < 16` → im **strict-Modus** positionierter
  `std::runtime_error` („Sekundär-Bitmap angezeigt (Bit 1), Spec deklariert
  nur N Byte Bitmap; `length: 16` setzen"), **nicht-strikt:** WARN (Legacy:
  weiterdekodieren wie bisher). Begründung gegen Option 3 (Auto-Lesen
  unabhängig von `length`): Option 3 ändert die Wire-Interpretation bestehender
  Specs/Pfade lautlos und nimmt Specs mit bewusst primär-only-Bitmap die
  Möglichkeit, Bit 1 als etwas anderes zu nutzen (Custom-Protokolle, s.
  `buildFallbackBitmap`-Bit-Konvention in `_parser.cc`). **Offene Frage an den
  Maintainer:** Sind Specs bekannt, die `length: 8` + Bit 1 bewusst nutzen?
  Bis zur Klärung ist der strict-Throw hinter dem Default `strict: true`
  — Escape-Hatch `strict: false` bleibt (wie bei allen Fail-closed-Fällen).
- **D9.2 — Build-Seite fail-closed (Akzeptanz 3 des FR):**
  Beim Bauen (strict) mit `length < 16` und einem Feld > 64 gesetzt →
  positionierter Fehler „Feld N > 64 gesetzt, Bitmap-`length` deklariert nur 8
  Byte" statt asymmetrischer Ausgabe. **Das ändert das Bauverhalten
  bestehender Specs mit `length: 8` + Sekundärfeldern** (heute: liefert 16 Byte,
  die sich nicht rückdekodieren lassen). Das ist die konsequente Lesart von
  „Bestandsverhalten" vs. „Fail-closed"; im Changelog als **Verhaltensänderung
  (strict)** markieren, Release-Einordnung 0.8.0 (nicht Patch). Alternative
  (konservativ): nur WARN beim Bauen. → **Entscheidung Maintainer** (Default
  dieses Plans: strict-Throw, nicht-strikt WARN).
- **D9.3 — FR-9a (Bauen, immer senden): expliziter Key, nicht `length`-Umdeutung.**
  Die Auslegung „`length: 16` = immer 16 Byte" würde bestehende Specs mit
  `length: 16` und nur Primärfeldern **byte-ändernd** treffen (heute 8 Byte) —
  verletzt „Default unverändert". Stattdessen neuer **Feld-Key auf dem
  `bitmap`-Feld**: `secondary: always` (Werte `auto` = Default/Legacy,
  `always`). Fail-closed beim Laden: nur auf `format: bitmap`; `always`
  verlangt `length >= 16` (sonst `SpecValidationError` mit Position). Wirkung:
  `ISOFieldParser<bitset>::parse` erzwingt `bytes = max(bytes, 16)` und setzt
  `d[0] |= 0x80` (Bit 1) — Bit 65/tertiäre Bitmap unverändert (nur bei
  Feldern > 128).
  - **Alternativname offen:** `secondary: always` (Vorschlag des Konsumenten)
    vs. `secondary_bitmap: always`. Plan nimmt `secondary:` (kurz, passt zu
    `scale:`/`sign:`); Maintainer kann umbenennen — betrifft WP9.3/9.5.
  - **Introspektion:** `SpecFieldInfo::secondary_bitmap` (`std::string`:
    `"always"` / `"auto"` bei Bitmap-Feldern, sonst `""`) — **neues Mitglied
    = ABI-Änderung** (analog `bcd_pad` 0.7.1); alternativ weglassen und
    erst bei Bedarf nachziehen. Empfehlung: aufnehmen (Spec Artist des
    Konsumenten liest Introspektion).
  - **Parser-Zustand:** neues Mitglied + Setter in `ISOFieldParserPtrBase`
    (wie `bcdPad()`/`strictLength()`), im Loader in `createScalarParser()`
    gesetzt (`dynamic_pointer_cast`, Muster Z. ~1231–1238) → ABI-Hinweis in
    `include/iso8583/AGENTS.md` und Changelog.
  - **Interaktion mit `recalcBitmap_locked`:** unverändert lassen (Bits aus
    `d_`); die Erzwingung geschieht ausschließlich im Encoder des
    Bitmap-Parsers. Der Decode-Pfad (`unparse`) liest bei Bit 1 + `length>=16`
    bereits korrekt → Roundtrip der leeren Sekundär-Bitmap prüfen (alle
    Felder ≤ 64, Bit 1 gesetzt, 8 Nullbytes) — Dekodieren liefert eine
    Bitmap-Komponente mit gesetztem Bit 1; `Message::keys()`/`dump()` dürfen
    dadurch kein Feld „1" als Datenfeld melden (testen).
- **D9.4 — Doku (FR-9b Punkt 1, unabhängig vom Code):** `spec_schema.md` §2/§3/§9:
  Bedeutung von `bitmap length` (`8` = nur Primär; `16` = Primär + Sekundär;
  `24` = zusätzlich Tertiär, s. Decode-Code `de_l_ > 16`); Verhalten bei Bit 1
  + `length: 8` (strict-Fehler, D9.1); Beispiele in `spec_schema.md` /
  `yaml_format.md` / AGENTS (`length: 8`) **prüfen und auf passende Werte
  bringen** — nach D9.1 werden Specs mit `length: 8` bei Nachrichten mit
  Sekundärfeldern fail-closed; die Beispielspecs der Doku, die DE ≥ 65 nutzen,
  müssen `length: 16` tragen (Grep über `docs/`, `examples/`, `specs/`,
  Testspecs — Bestandstests laufen lassen, s. WP9.1).

### 3.3 Arbeitspakete

| WP | Inhalt | Dateien |
|---|---|---|
| **WP9.1** | **Charakterisierung zuerst:** Tests, die das heutige Verhalten festnageln (Felder {3,4} / {3,70} bei `length` 8 und 16; Build + Decode wie in der Notiz; Decode der leeren Sekundär-Bitmap mit `length: 16`). Voll-Suite beider Key-Typ-Builds einmal laufen lassen, **Specs/Tests mit `length: 8` + DE ≥ 65 inventarisieren** (Grep `format: bitmap` mit `length: 8` in `tests/`, `examples/`, `docs/`) | `tests/test_bitmap_secondary.cc` (neu, `[bitmap]`) |
| **WP9.2** | FR-9b Code: strict-Throw/WARN im Bitmap-`unparse` (Bit 1 + `de_l_ < 16`) **und** Build-Guard in `parse` (Feld > 64 + `de_l_ < 16`); fail-closed-Texte positioniert (`[ISO8583]`-Konvention). Bestandsspecs mit `length: 8` + Sekundärfeldern aus WP9.1 anpassen | `src/_parser.hh` |
| **WP9.3** | FR-9a Code: Key `secondary` (Root-/Feld-Whitelist in den Key-Listen ~`_spec.cc` Z. 227–300: erlaubte Feld-Keys; Validierung in `parseSpecField` bei `BITMAP`), Parser-Mitglied + Setter (`ISOFieldParserPtrBase`, `src/_parser.hh`/`include/iso8583/ISOParser.hh`), Encoder-Erzwingung, `SpecFieldInfo::secondary_bitmap` (`include/iso8583/ISOSpec.hh` + `_spec.cc` Introspektion ~Z. 1621) | `src/_spec.cc`, `src/_parser.hh`, `include/iso8583/ISOParser.hh`, `include/iso8583/ISOSpec.hh` |
| **WP9.4** | Tests FR-9a: `secondary: always` + Felder ≤ 64 → 16 Byte, Bit 1 gesetzt (`F6…`-artige Bitmap + 8 Nullbytes wie in der VISA-Nachricht); ohne Key byte-identisch (Regression gegen WP9.1-Fixtures); Roundtrip decode→encode der realen Struktur; Loader: `secondary: always` + `length: 8` → `SpecValidationError` mit Position; `secondary` an Nicht-Bitmap-Feld → Fehler; ungültiger Wert → Fehler; Introspektion | `tests/test_bitmap_secondary.cc` |
| **WP9.5** | Doku: `spec_schema.md` §2/§3/§9 (Key `secondary`, `bitmap length`-Semantik, Fehlertabelle), `yaml_format.md`, `include/iso8583/AGENTS.md` (Feldattribute, `SpecFieldInfo`-Tabelle, ABI-Hinweis, Fehler-Tabelle), `.agents/yaml-spec.md`/`.agents/pitfalls.md` (neuer Pitfall „Bitmap-`length`"), Changelog (beide, identisch) | Docs |

**Akzeptanz (aus FR-9):** (a) Schalter erzeugt 16-Byte-Bitmap mit Bit 1 bei
Feldern ≤ 64, ohne Schalter byte-identisch; (b) `spec_schema.md` beschreibt
`bitmap length`, Bit 1 + `length: 8` → positionierter Fehler; Eigenausgabe
für beide `length`-Werte rückdekodierbar oder Unzulässiges wird gemeldet.

---

## 4. FR-10 — `remaining` als Kind / TLV mit Kopfbytes

### 4.1 FR-10a: Bug — Ursache **bereits lokalisiert** (Code gelesen)

Zwei zusammenwirkende Defekte beim **Bauen** eines binären `remaining`-Kindes
(`format: remaining, encoding: binary` bzw. ohne `encoding` →
`IF_REMAINING = ISOFieldParser<vector<uint8_t>, UNKNOWN, NONE, BINARY>`):

1. **Falscher Komponententyp beim `set()`** —
   `make_component_from_string()` (`src/_components.cc` ~Z. 637–649)
   fasst `OPAQUE`, `EXCEPTIONAL`, `REMAINING`, `UNUSED` zusammen und erzeugt
   immer ein `OpaqueField`. Der Encode-Pfad des binären Remaining-Parsers
   (`src/_parser.hh` ~Z. 384–386) macht aber
   `std::dynamic_pointer_cast<BinaryField>(c)->value()` → Cast liefert
   `nullptr`, `->value()` ist ein **Null-Dereferenz** (SEH-/Access-Violation
   unter MSVC, kein `std::exception`) — das erklärt exakt die gemeldete
   „Unbekannte, nicht standardkonforme Exception". Der Hex-String
   `"950500…"` würde zudem als Text-Wert statt als Bytes abgelegt. (Beim
   Dekodieren erzeugt `create_component()` dagegen korrekt ein `BinaryField`
   — deshalb dekodiert es.)
2. **Fixe-Länge-Prüfung greift fälschlich für `remaining`** — im selben
   Zweig (`~Z. 391–400`): `if (pl == 0 && data.size() != de_l_)` behandelt
   jeden Parser ohne Präfix als FIX-Feld. `remaining` (`Length::UNKNOWN`,
   ebenfalls `pl == 0`) hat `length` aber nur als **Maximum**: ein 9-Byte-Wert
   bei `length: 252` würde im strict-Modus „Serialisierung zu groß … !=
   FIX-Länge" werfen (nicht-strikt: Feld still auslassen!). Der
   String-Zweig (Z. 355) prüft korrekt nur `> de_l_`.

Bonus-Befund (Robustheit, gleiche Klasse): alle `dynamic_pointer_cast<…>(c)->`
im Encoder (String-/Binär-/Bitmap-Zweig) dereferenzieren ungeprüft → ein
Typ-Mismatch (z. B. `set()` mit falschem Komponententyp, programmatisch
konstruierte Nachricht) endet als SEGV statt als positionierter Fehler.
Hard Rule 7 (Memory Safety) → Null-Guard mit positioniertem
`std::runtime_error` ergänzen (Defense-in-depth).

### 4.2 FR-10a Design und Arbeitspakete

- **D10.1 — Komponententyp nach Parser-Variante wählen:** in
  `make_component_from_string()` für `REMAINING` die vorhandene Technik aus
  `checkContainerBase` wiederverwenden: `fieldParser->create_component(key)`
  liefert `BinaryField` (binär) bzw. `OpaqueField` (Text) → bei `BinaryField`
  Hex-Decode-Pfad wie `case BINARY` (inkl. Fehler bei ungültigem Hex →
  `nullptr` + WARN, gleiches Verhalten), sonst `OpaqueField`. **Kein neuer
  Enum-Wert** (`ISOFieldParserType` ist öffentliche API/ABI).
- **D10.2 — Längenprüfung:** `pl == 0 && data.size() != de_l_` nur für
  `l_ == Length::FIX`; für `UNKNOWN/CONSUME` → wie Präfix-Feld: nur
  `data.size() > de_l_` (strict throw / nicht-strikt Legacy-Log). Muster aus
  FR-5 (`l_ == codec::Length::FIX`-Guard Z. 369) wiederverwenden.
- **D10.3 — Null-Guards:** an den drei `dynamic_pointer_cast`-Stellen im
  `parse()` (`src/_parser.hh`) → bei `nullptr` positionierter
  `std::runtime_error("[ISO8583] DE<key> '<desc>': Komponente hat falschen Typ …")`.
- **D10.4 — Doku:** `spec_schema.md` §4 (`remaining`): „als **letztes** Kind
  eines Containers auch beim **Bauen** unterstützt; Wert = Rest des
  Elternpuffers, Länge ergibt sich beim Serialisieren; `length` = Maximum".
  Das ist Aussage über neues, getestetes Verhalten (heute war es
  undokumentiert **und** defekt).

| WP | Inhalt | Dateien |
|---|---|---|
| **WP10.1** | **Repro-Test zuerst (rot):** Minimal-Spec aus der Notiz (`nested`, `lbinary`, Kind 0 `binary` 3 Byte, Kind 1 `remaining`) mit `set("55.0","010078")`, `set("55.1","950500000000008202")` → erwartet `…0C 010078 950500000000008202`; Varianten mit/ohne `encoding: binary`; Vergleichsfälle `binary length: 9` und `lbinary` bleiben grün; Roundtrip decode→encode; **Text-`remaining` als Kind** (ascii/ebcdic/bcd) bauen (prüfen, ob dort bereits grün); **top-level `remaining`** (letztes Feld der Message) bauen | `tests/test_remaining_field.cc` (erweitern; dort existiert die Suite) |
| **WP10.2** | Fix D10.1 + D10.2 + D10.3 | `src/_components.cc`, `src/_parser.hh` |
| **WP10.3** | Tests grün + Fail-closed-Fälle: Wert > `length` bei `remaining` → positionierter strict-Fehler (kein SEGV); ungültiger Hex-String → wie `BINARY`; Typ-Mismatch-Guard (programmatisch `OpaqueField` an binären Remaining-Parser) → `std::runtime_error` statt SEGV; Bestandstests `[remaining]`/`[nested]` unverändert | Tests |
| **WP10.4** | Doku (D10.4) + Changelog `[#](Fixed)`-Eintrag | Docs |

**Akzeptanz (aus FR-10a):** Notiz-Spec baut mit `55.0`/`55.1` und liefert
`… 7B 01 00 78 95 05 …`; Roundtrip == Decode; nicht-baubare Fälle liefern
positionierte `std::exception`; Bestandsverhalten unverändert.

### 4.3 FR-10b: TLV-Container mit festen Kopfbytes — **Entscheidung E4: nur Dokumentation (Option C)**

**Entschieden (2026-10-06, Maintainer):** FR-10b wird **nicht** als Feature
umgesetzt, sondern nur dokumentiert (Option C unten). Kein Spike, kein neuer
YAML-Key, kein ABI-Change. Begründung: Beleg nur aus **einer** Nachricht (kein
Nachweis, dass der 3-Byte-Kopf `01` + 2 Byte Länge bei allen VISA-Nachrichten
gleich ist), niedrige Priorität, funktionierender Workaround beim Konsumenten.
Die Optionen A/B bleiben unten als Kontext für eine spätere Wiederaufnahme
stehen (nur bei neuem Beleg).

**Umsetzung (Option C):**

| WP | Inhalt | Dateien |
|---|---|---|
| **WP10.5** | Doku-Rezept „TLV-Container mit festen Kopfbytes (z. B. VISA-DE55)": DE55 als `lbinary` modellieren (dekodiert und baut), die Kopfbytes (`01` + 2 Byte Länge) abschneiden bzw. voranstellen und den TLV-Block per Field-only-Spec (`loadField*FromYaml` + `SpecDecoder::decodeField`, `spec_schema.md` §11) auflösen. Zusätzlich die mit FR-10a baubare Variante „Kopf-Kind + `remaining`" nennen (dekodiert/baut den Block **roh**, keine Tag-Aufschlüsselung). Klar sagen: ein TLV-Container mit festen Kopfbytes wird nicht unterstützt (`tlv: {ber: true}` beginnt immer mit dem ersten Frame). Kurzes, in einem Test abgesichertes Beispiel mit der DE55-Struktur aus der Notiz | `docs/internals/spec_schema.md` §6 (+ §4 Querverweis), `docs/internals/yaml_format.md`, `include/iso8583/AGENTS.md`, `.agents/yaml-spec.md` (im selben Commit) |
| **WP10.6** | Test, der das Rezept festnagelt (kein Feature-Test): `lbinary`-DE55 mit `01 00 78 …`-Payload decodiert + re-encodiert byte-identisch; Kopf abschneiden → `decodeField` mit BER-Field-only-Spec liefert die Tags; Kopf-Kind + `remaining` baut byte-identisch (setzt FR-10a voraus) | `tests/test_remaining_field.cc` oder `tests/test_field_only_spec*.cc` (bestehende Suite erweitern, ASCII-Testnamen) |
| **WP10.7** | Changelog-Eintrag (`[~](Updated)` Doku), Vault/Chatroom: FR-10(b) als „dokumentiert, nicht umgesetzt" schließen; Konsument bitten, bei einer zweiten Nachricht/Spec-Beleg für einen konstanten Kopf zu melden → dann Neubewertung | beide Changelogs, Vault |

**Ursprüngliche Optionsanalyse (Kontext, nicht umgesetzt):**

- **Heutiger Stand:** `tlv: { ber: true }` beginnt direkt mit dem ersten
  Frame. Alternative des Konsumenten (Kopf-Kind + `remaining`) dekodiert den
  TLV-Block nur roh; mit FR-10a baut sie wenigstens.
- **Optionen (Plan-Empfehlung A):**
  - **A — `tlv: { ber: true, header_bytes: N }`**: N feste Bytes vor den
    Frames, als eigenes Kind `0` (Roh-Hex) erreichbar; die TLV-Tags liegen
    danach. Vorteil: lokal im TLV-Container (`makeTlvParser`, `src/_spec.cc`
    ~Z. 1302; BER-Parser in `src/_tlv.hh`), kein neuer Containertyp, additiv.
    Risiko: Kind-Key `0` kollidiert nicht mit BER-Tags (Tag 0 ist in BER
    nicht als Tag-Wert zulässig — im Spike gegen `ISO8583_BERTLV`-Keytyp
    `int16_t` **und** `int32_t` prüfen), Wire-Offset-Tracking
    (`wire_offset`) und Re-Encode byte-identisch.
  - **B — Container-Kindtyp `bertlv` ohne eigenes Präfix in `nested`**
    (`children: [ {binary 3}, {bertlv …} ]`): mächtiger, aber überlappt mit
    constructed-Kindern (0.6.4) und der `nested`-Kinder-Whitelist → größerer
    Eingriff in Loader und Introspektion.
  - **C — nicht umsetzen**, Doku-Rezept: DE55 als `lbinary` + Field-only-Spec
    (`decodeField`) nach Abschneiden der 3 Kopfbytes (so macht es der
    Konsument heute).
- **Wiederaufnahme nur bei neuem Beleg** (zweite Nachricht/Spec-Auszug, dass
  der Kopf konstant ist): dann Spike zu A (Passt es in `makeTlvParser`? Kind
  `0`, Introspektion, ABI, beide Key-Typ-Builds), Entscheidung A/B erneut
  beim Maintainer.

---

## 5. Querschnitt: Verifikation und Qualitätsschranken

- **Build/Test** (Fast-Path aus `AGENTS.md`; MSVC+Ninja nur aus Developer
  Prompt/`vcvars64.bat`, in dieser Umgebung bewährt:
  `cmd //c "scratch\build_debug.bat"`-Wrap):
  `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`
  und **zusätzlich** der `debug-bertlv`-Build (Key-Typ `int32_t`; FR-9a/10b
  berühren Key-Handling) — Voll-Suite in **beiden** Key-Typ-Builds grün,
  einzelne Suiten per `libiso8583_tests "[bitmap]"` etc.
- **Bekannte Umgebungsfalle:** `ctest` zählt früh-returnte Tests als Passed
  und schließt `[slow]` aus; Ausgabe auf Skips prüfen. Memory
  „ACP=1252/OEMCP=437": 4 Umlaut-Testnamen sind ctest-rot, exe-level grün —
  **neue Testnamen ASCII-only**.
- **CRLF-Falle:** Repo-Quelldateien sind CRLF; nicht per Git-Bash-`sed -i`
  editieren (flippt auf LF → kompletter Diff). Edit-Tool nutzen, vor Commit
  `git diff --stat` und `file` prüfen (Memory `project-iso8583-crlf-pitfall`).
  Dieses Plan-Dokument selbst ist LF (neue Datei, wie die anderen Pläne).
- **Hard Rules, die diese Arbeit berühren:** (4) Bitmaps weiter
  auto-berechnet — FR-9a ändert nur die Encoder-Breite, **nie** manuelles
  Setzen; (7) jeder `dynamic_bitset`-Index guarded (`bmp.size() > n`) —
  insbesondere beim Erzwingen der 16 Byte (`b[i+1]` bei `b.size()` < 129
  **nicht** außerhalb lesen: Schleife heute `for i < bits` mit `b[i+1]` —
  bei erzwungenen 16 Byte aber kleinem Bitset **OOB**! → Bitset vorher auf
  `bits+1` vergrößern oder Index-Guard, **Pflichttest**); (8) Loader-Fehler
  positioniert (`SpecValidationError`); (12) neue öffentliche API =
  `TNG_EXPORT`, `///`-Doxygen, `sphinx -W` grün (`SpecFieldInfo::secondary_bitmap`).
  EBCDIC-Tabellen werden **nicht** angefasst (kein `verify-ebcdic-tables`
  nötig).
- **`.agents/*` und `include/iso8583/AGENTS.md`** im selben Commit wie die
  Verhaltensänderung aktualisieren (Projektregel).

## 6. Commit-Plan und Release

Präfixe nach Projektkonvention; ein logischer Change pro Commit:

1. `[#](Fixed) FR-10a: remaining als Kind eines nested-Containers baut (Komponententyp, Max-Laenge, Null-Guards) + Tests` (WP10.1–10.3)
2. `[~](Updated) Doku: remaining beim Bauen (spec_schema §4)` (WP10.4)
3. `[+](Added) FR-8: lnum/lchar|bcd|binary (5 Dispatch-Eintraege, 30 Kombinationen) + Tests` (WP8.1–8.2)
4. `[~](Updated) Doku: FR-8 (Matrix, Breitenregel, Nibble D→'=', Spiegel)` (WP8.3–8.4)
5. `[~](Changed) FR-9b: Bit 1 + bitmap length<16 fail-closed (Decode/Build, strict) + Charakterisierungstests` (WP9.1–9.2) — **Verhaltensänderung, `[!]` erwägen, falls Maintainer D9.2 strict-Throw wählt**
6. `[+](Added) FR-9a: bitmap 'secondary: always' + SpecFieldInfo::secondary_bitmap (ABI)` (WP9.3–9.4)
7. `[~](Updated) Doku: bitmap length / secondary (FR-9)` (WP9.5)
8. `[~](Updated) Doku: TLV mit festen Kopfbytes (VISA-DE55) als Rezept, FR-10b nicht umgesetzt + Absicherungstest` (WP10.5–10.7; setzt Commit 1 voraus)

**Version:** Plan-Empfehlung **0.8.0** (4 Stellen: `CMakeLists.txt`,
`include/iso8583/config.h` `TNG_CORE_VERSION`, root `vcpkg.json`,
`vcpkg-port/vcpkg.json`; `changelog.md` == `docs/changelog.md`; vcpkg-port
SHA512 nach Tag, wie bei v0.7.0/v0.7.1). Begründung: neue Features, ABI-
Änderung (`ISOFieldParserPtrBase`/`SpecFieldInfo`), strict-Verhaltensänderung
(FR-9b). **Release erst nach grünem Docs-Run auf `main`
(`.agents/process-release.md` §14.2) und nur auf Anweisung des Maintainers.**

## 7. Offene Entscheidungen (Default = Plan-Empfehlung)

| # | Frage | Default |
|---|---|---|
| E1 | FR-9b Decode: strict-Throw bei Bit 1 + `length<16` (Option 2) vs. Auto-Lesen (Option 3)? Gibt es bewusste `length: 8`+Bit-1-Specs? | strict-Throw, nicht-strikt WARN |
| E2 | FR-9b Build: strict-Throw bei Feld > 64 + `length<16` oder nur WARN? | strict-Throw |
| E3 | FR-9a Key-Name `secondary:` vs. `secondary_bitmap:`; Introspektion aufnehmen? | `secondary: always\|auto`, Introspektion ja |
| E4 | FR-10b: A (`header_bytes`), B (Kindtyp), C (nur Doku-Rezept)? | **C — nur Dokumentation (entschieden 2026-10-06)** |
| E5 | FR-8: LL/LLL-Varianten mitliefern (nicht benötigt, aber symmetrisch)? | ja (5 Einträge) |
| E6 | Ein Release 0.8.0 für alles, oder FR-10a/FR-8 vorab (z. B. 0.7.2)? | gebündelt 0.8.0; FR-8 vorziehen, falls der Konsument drängt |

## 8. Nacharbeiten außerhalb des Repos (nach Umsetzung)

- Vault-Notizen FR-8/9/10: `status` per `set_note_property` setzen (nicht per
  Frontmatter-Text — Hygiene-Hinweis des Konsumenten aus FR-7), Commit-Hashes
  nachtragen, `Übersicht.md` aktualisieren, `iso8583-dev/`-Implementierungsnotizen
  anlegen.
- Chatroom `chatrooms/iso8583.md`: Antwort an `tng-wire-viewer-agent` mit
  (a) Bestätigung der Ursache von FR-10a, (b) Antwort auf die Nibble-`D`-Frage
  (`D → '='`), (c) der `bitmap length`-Semantik und der Entscheidungen E1–E3,
  (d) Hinweis auf den veralteten `docs/_build/html`-Stand (Konsument hatte lokal
  neu gebaut) — Docs-Build vor Release regenerieren.
