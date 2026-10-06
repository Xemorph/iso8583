# Changelog

## Unreleased

### `[~](Changed)` Sekundär-Bitmap: `bitmap length` fail-closed + `secondary: always` (FR-9)

- **Verhaltensänderung (strict, Default) — FR-9b:** Mit `bitmap … length: 8`
  las der Decoder die Sekundär-Bitmap **nicht**, auch bei gesetztem Bit 1 —
  alle Folgefelder waren stillschweigend um 8 Byte verschoben (Fehler traten
  erst später und irreführend auf), und die eigene Ausgabe des Builders (der die
  Sekundär-Bitmap unabhängig von `length` schreibt) war nicht rückdekodierbar.
  Jetzt wirft der **Decoder** bei Bit 1 + `length < 16` und der **Builder** bei
  Feldern, die mehr Bitmap-Bytes erfordern als `length` (z. B. `length: 8` und
  DE 70), einen positionierten `std::runtime_error`
  (`Bitmap @ Offset N: Bit 1 (Sekundär-Bitmap) ist gesetzt … 'length: 16'
  setzen`). Nicht-strikt (`strict: false`) bleibt das Legacy-Verhalten mit
  Warnung. **Migration:** Specs mit Feldern > 64 bzw. Nachrichten mit Bit 1
  brauchen `length: 16`. Die IFB_BITMAP-Feldparser-Ebene ist unverändert.
  Fünf Test-Fixtures (`test_spec_loader.cc`, `test_incompatible_input.cc`)
  hatten Bit 1 versehentlich bei `length: 8` gesetzt und wurden korrigiert.
- **Neu — FR-9a:** Feld-Key `secondary: auto|always` am `bitmap`-Feld.
  `always` erzwingt beim Bauen die Sekundär-Bitmap (Bit 1 + 16 Byte), auch ohne
  Feld > 64 (VISA BASE I: Primär-Bitmap mit Bit 1 + leere Sekundär-Bitmap);
  erfordert `length >= 16`. Default `auto` = unverändert (byte-identisch zu
  0.7.1). Fail-closed beim Laden (positionierte `SpecValidationError`):
  ungültiger Wert, Key an Nicht-Bitmap-Feld, `always` mit `length < 16`.
  Der Bitset-Zugriff des Bitmap-Encoders ist gegen kleinere Bitsets
  abgesichert (erzwungene 16 Byte).
- **Introspektion/ABI:** `SpecFieldInfo::secondary_bitmap` (`"always"` |
  `"auto"` | `""`); neues Mitglied in `SpecFieldInfo` und
  `ISOFieldParserPtrBase` (`secondaryAlways`) — Layout-Änderung,
  Shared-Library-Consumer müssen neu kompiliert werden.
- Doku: `spec_schema.md` §2/§3 „Bitmap-Felder“/§8/§9/§12 (zuvor war die
  Bedeutung von `bitmap length` nirgends beschrieben), `yaml_format.md`,
  `include/iso8583/AGENTS.md`, `.agents/yaml-spec.md`, `.agents/pitfalls.md` (45).
- Tests: neue Suite `tests/test_bitmap_secondary.cc` (Tag `[fr9]`, 18 Cases).

### `[+](Added)` Binäres Längenpräfix (Einheit Ziffern) vor gepackten BCD-Ziffern (FR-8)

- Neue Kombinationen `lchar`/`llchar`/`lllchar`/`lnum`/`llnum` mit
  `encoding: bcd` + `prefix_encoding: binary` (5 zusätzliche
  Dispatch-Einträge, jetzt **30** statt 25 Kombinationen): das Längenpräfix
  ist ein Big-Endian-Binärwert (L=1 → 1 Byte, LL → 2, LLL → 3) in der Einheit
  der Nutzdaten, also **Ziffern**; die Daten belegen `ceil(Ziffern/2)` Byte,
  das Padding folgt `bcd_pad`. VISA BASE I: DE 2 `10` + 8 Byte = 16 Ziffern;
  DE 35 `25` = 37 Ziffern → 19 Byte mit `bcd_pad: left_zero`.
- Rein additiv: bisher abgelehnte Kombinationen werden akzeptiert; alle
  übrigen Kombinationen (inkl. der 25 aus 0.7.0) und Bestandsspecs bleiben
  byte-identisch. Weiterhin abgelehnt: `llllchar`/`lllnum` mit BCD, `*binary`,
  fixe Formate. Introspektion: `SpecFieldInfo::prefix_encoding == "BINARY"`;
  kein ABI-Change.
- Doku: BCD-Nibbles ≥ `A` dekodieren als `'0' + n` — `D` → `=`
  (Track-2-Trenner), `A`→`:`, `B`→`;`, `C`→`<`, `E`→`>`, `F`→`?`
  (verlustfreier Roundtrip); die bisherige Formulierung nannte nur `:`/`;`.
- Tests: neue Suite `tests/test_prefix_bcd_binary.cc` (Tag `[fr8]`, 20 Cases,
  reale VISA-DE2/32/35-Struktur).

### `[#](Fixed)` `remaining` als Kind eines `nested`-Containers baut jetzt (FR-10a)

- **Bug:** `format: remaining` (binär, mit oder ohne `encoding: binary`) als
  Kind eines `nested`-Containers (oder als Schlussfeld) ließ sich zwar
  dekodieren, aber nicht **bauen**: `set()` legte ein `OpaqueField` ab, der
  Encoder dereferenzierte den fehlgeschlagenen `BinaryField`-Cast (Null-Zugriff,
  bei Konsumenten als „nicht standardkonforme Exception“ sichtbar), und
  `length` wurde dort fälschlich als FIX-Länge geprüft.
- **Fix:** binäres `remaining` nimmt beim `set()` einen Hex-String
  (`BinaryField`, wie `binary`); `length` ist beim Serialisieren ein
  **Maximum** (strict: positionierter `std::runtime_error` darüber). Die
  Encoder haben zusätzlich einen Typ-Guard: eine Komponente des falschen Typs
  (`OpaqueField`/`BinaryField`/`Bitmap`) ergibt einen positionierten
  `std::runtime_error` statt eines Null-Zugriffs. Wire-Format und
  Bestandsverhalten unverändert (Dekodierung unberührt).
- Doku: `spec_schema.md` §4 Punkt 6 (Bauen von `remaining`).
- Tests: `tests/test_remaining_field.cc` (Tag `[fr10a]`, 6 Cases).

## 0.7.1

### `[+](Added)` Konfigurierbares Padding bei gepacktem BCD mit ungerader Ziffernzahl (FR-7: `bcd_pad:`)

- Neuer optionaler Key `bcd_pad:` (`right_zero` | `right_f` | `left_zero`,
  Feld-Key oder Root-Default, Feld überschreibt Root): legt fest, wo das
  übrige Nibble bei **ungerader** BCD-Ziffernzahl steht und womit es gefüllt
  wird (`123` → `12 30` / `12 3F` / `01 23`). Default `right_zero` = bisheriges
  Verhalten — ohne den Key bleibt die Wire byte-identisch zu 0.7.0.
- Scope: BCD-**Nutzdaten** (`numeric`, `amount`, `*char`, `*num`, `remaining`,
  typisierte TLV-Kinder); nie das Längenpräfix (`0010` bei LLL bleibt),
  `binary`-Formate oder TLV-Container. **Fail-closed** beim Laden
  (positionierte `SpecValidationError`): ungültiger Wert; Feld ohne
  BCD-Nutzdaten; TLV-Container/constructed-Kind.
- **Decode:** mit deklariertem `bcd_pad` wird das Padding-Nibble validiert
  (strict: positionierter `std::runtime_error`, nicht-strikt: Warnung); ohne
  Deklaration bleibt es wie bisher ungeprüft. TLV-Kinder mit ungerader
  deklarierter `length` (Ziffern) nutzen diese als Ziffernzahl.
- **Introspektion:** `SpecFieldInfo::bcd_pad` (`""` bei Nicht-BCD-Feldern).
  **ABI:** neue Mitglieder in `SpecFieldInfo` und `ISOFieldParserPtrBase` —
  Layout-Änderung, Shared-Library-Consumer müssen neu kompiliert werden.
  `codec::as<>`/`codec::to<>` erhalten das Default-Argument
  `codec::BcdPad pad = RIGHT_ZERO` (source-kompatibel).
- Tests: neue Suite `tests/test_bcd_pad.cc` (Tag `[bcdpad]`).

## 0.7.0

### `[+](Added)` Längenpräfix-Encoding unabhängig vom Nutzdaten-Encoding (FR-6: `prefix_encoding:`)

- Neuer optionaler Feld-Key `prefix_encoding:` (`ascii`/`bcd`/`ebcdic`/
  `binary`, Default = `encoding`): das Codec des **Längenpräfixes** wird
  vom Codec der **Nutzdaten** entkoppelt — VISA-BASE-I-Situation, z. B.
  DE 2 `{ format: llnum, encoding: bcd, prefix_encoding: bcd }` (BCD-
  Längenbyte + gepackte Ziffern) oder DE 48 `{ format: lllchar,
  encoding: ascii, prefix_encoding: binary }` (Binär-Längenbytes +
  ASCII-Text). Ohne den Key gilt `prefix_encoding = encoding` (rein
  additiv — Bestandsspecs ändern sich nicht); der Key ist feldlokal
  (kein Root-Default, keine Vererbung).
- 25 Dispatch-Kombinationen (3-teiler Dispatch-Key `Format|Encoding|
  PrefixEncoding`, additiv zur bestehenden 2-teiler-Tabelle):
  16× `l*char` (L/LL/LLL, ascii zusätzlich LLLL) × `ascii`/`ebcdic`-
  Nutzdaten × `bcd`/`binary`-Präfix, 8× `lnum`/`llnum` ×
  `ascii`/`ebcdic` × `bcd`/`binary`, plus 3 native Identitätslücken
  (`lnum|bcd`, `llnum|bcd` → `IFB_LNUM`/`IFB_LLNUM`, `llnum|ebcdic` →
  `IFE_LLNUM`).
- **Fail-closed** beim Laden (positionierte `SpecValidationError`):
  Value-Whitelist; nur variable `*char`/`*num`-Formate (bei
  `*binary`/`bertlv` bestimmt `encoding:` bereits das Präfix-Codec;
  `amount`/`remaining`/fixbreite Formate haben kein Längenpräfix);
  nicht verfügbare Kombinationen; TLV-Kinder (fix/BERTLV/constructed)
  und ein Root-Level-Key sind unzulässig.
- **Introspektion:** `SpecFieldInfo::prefix_encoding` (effektives
  Präfix-Encoding; Key weggelassen → `encoding`, encoding-neutrale
  Formate → `""`; rekursiv auf `children`/`tlv_children`).
  **ABI:** neues `SpecFieldInfo`-Mitglied — Layout-Änderung,
  Shared-Library-Consumer müssen gegen die neue Bibliothek neu
  kompiliert werden (0.7.0).
- **Decode-Korrektur (B6):** Ein am Pufferende abgeschnittenes
  Längenpräfix wirft jetzt in beiden Modi garantiert den
  positionierten Fehler `Längenpräfix am Pufferende abgeschnitten: …`
  **vor** der Präfix-Lesung (davor konnte hier eine rohe STL-Exception
  aus der Präfix-Lesung austreten).
- Tests: neue Suite `tests/test_prefix_encoding.cc` (Tag `[prefix]`:
  Codec-Roundtrips, Strict-Fehlerpfade, VISA-BASE-I-artige Voll-Spec
  mit handgebautem Frame, Fail-closed-Validierung, Field-only-Specs,
  `!merge`-Komposition).
- Doku: `spec_schema.md` §2/§3/§7/§8/§9/§12, `yaml_format.md`
  (Format-Referenz), `include/iso8583/AGENTS.md` (SpecFieldInfo-Mitglieder,
  ABI-Hinweis, Format/Encoding-Kombinationen, Typische Fehler),
  `.agents/yaml-spec.md` (Loader-Verhalten + `src/`-Map).

## 0.6.4

### `[+](Added)` BERTLV/Fix-TLV: constructed-TLV-Kinder rekursiv dekodieren/kodieren

- Ein TLV-Kind mit eigenem `tlv:`-Block (`tlv: { ber: true }` oder
  `tlv: { tag_bytes, len_bytes }`) ist jetzt ein *constructed*-Container
  (ISO/IEC 8825-1, EMV Book 3 — z. B. Tag `69` *Transaction Status
  Information* mit Tags wie `63`): sein Wert ist selbst eine Folge von
  TLVs und wird bei der Dekodierung rekursiv über einen eigenen
  Sub-Parser in eine Sub-`Message` aufgelöst; beim Re-Encode wird die
  Sub-`Message` byte-identisch zurückkodiert.
- Die inneren TLVs sind über die bestehende Punkt-Notation adressierbar
  (z. B. `57.69.63`: DE57 → Tag `69` → Tag `63`); der Sub-Parser wird an
  die Sub-Nachricht angehängt, sodass sie sich selbst re-serialisieren
  kann. Beide TLV-Formen (BER **und** fixes TLV) nutzen denselben
  Codepfad (Policy-agnostisch); die Rekursionstiefe ist durch die
  bestehende ≤ 200-Ebenen-Begrenzung gedeckt.
- Ohne eigene `children:` ist das Kind ein *dynamischer* Container
  (innere Tags werden dynamisch dekodiert); deklarierte `children:`
  (Enkel-Tags) werden mit denselben Whitelist-Regeln rekursiv validiert.
- Fail-closed beim Laden (positionierte `SpecValidationError`):
  - Container-Kinder dürfen kein `format:`/`length:`/`encoding:`
    deklarieren (das äußere TLV-Frame trägt Tag + Länge);
  - der `tlv:`-Block des Kinds benötigt `ber: true` **oder**
    `tag_bytes`/`len_bytes`.
- Ein constructed-Tag **ohne** `tlv:`-Block bleibt ein dynamischer
  `BinaryField`-Blob (Rohbytes) wie bisher — keine implizite Erkennung
  über das Constructed-Bit (`0x20`).
- PCI: `sensitive: true` auf dem Container-Kind verbreitet sich auf den
  Sub-Baum (Masking in `dump()`/`operator<<`); der Strict-Modus
  propagiert auf den Kind-Sub-Parser.
- Introspektion: constructed-Kinder melden `is_nested = true`,
  `tlv_is_ber` nach dem eigenen `tlv:`-Block, `tlv_children` rekursiv
  gefüllt. **Keine** öffentliche API-/ABI-Änderung (`TlvChildInfo` ist
  privat).
- Key-Typ: 2-Byte-Sub-Tags (≥ `0x8000`) unterliegen der bestehenden
  `TNG_KEY_TYPE`-Regel — im Default-Build (int16) Warnung + Überspringen
  (kein Fehlrouting), mit `ISO8583_BERTLV` (int32) voll unterstützt.
- Tests: Parser-Ebene + Full-Spec (BERTLV und fixes TLV) + Loader
  (Fail-closed) + Introspektion + Strict-Propagation + Key-Typ (beide
  Builds) + Field-only-Specs.
- Doku: `docs/internals/spec_schema.md` §6, `docs/internals/yaml_format.md`,
  `include/iso8583/AGENTS.md`, `include/iso8583/ISOSpec.hh`
  (`SpecFieldInfo::tlv_children`-Doxygen).

## 0.6.3

### `[+](Added)` Fix-TLV: `len_bytes` jetzt bis 3 (davor max. 2)

- `tlv: { tag_bytes, len_bytes }` (fixer TLV, z. B. Mastercard/Visa-SE)
  unterstützt jetzt `len_bytes` bis **3** (davor nur 1–2); `tag_bytes` bleibt
  1–2, jeweils für `ascii`/`ebcdic`/`bcd` mit/ohne `tcc`.
- Zuvor fiel jeder andere Wert (z. B. `len_bytes: 3`) per Warnung auf den
  Mastercard-Default (`tag_bytes`/`len_bytes` = 2/2, EBCDIC) zurück und
  dekodiert/serialisiert das Längenfeld falsch. In-Scope-Werte instanzieren
  jetzt den korrekten `FixedNumericLength<N>`-Parser; außerhalb des Supports
  (`len_bytes > 3`, `tag_bytes > 2`) bleibt Warnung + Default.
- Intern: die handverwaltete Dispatch-Tabelle in `makeTlvParser` durch eine
  kompakte `tag_bytes`×`len_bytes`-Auflösung (Policy-Templates) ersetzt; die
  Policy-Ebene (`_tlv_policy.hh`) unterstützte bereits N ≤ 4.
- Tests: E2E-Roundtrip mit 3-bytes EBCDIC-Länge (`test_e2e_full_message.cc`,
  Tag `[len3]`) + Loader-Introspection (`test_spec_loader.cc`).

## 0.6.2

### `[+](Added)` Opt-in `strict_length`: Unterlängen-Prüfung bei Feldern fester Länge (FR-5)

- Neuer optionaler Key `strict_length: true` — als Spec-Wurzel (Default für alle
  Felder) oder pro Feld (überschreibt die Wurzel). Default `false`: das bisherige
  Verhalten bleibt **unverändert** (zu kurze Werte werden beim Serialisieren
  aufgefüllt: `numeric`/`amount` links mit `0`, `char` rechts mit Leerzeichen).
- Aktiv: ein Wert, der kürzer als die feste Länge ist (kein L-Präfix), löst im
  strict-Modus einen `std::runtime_error` („Serialisierung zu kurz …") aus;
  nicht-strikt: Warnung + Padding. L-präfixierte Felder und `remaining`
  (Maximum) sind nie betroffen; `binary`-Felder fester Länge waren schon immer
  exakt-längenpflichtig.
- Laufzeit-API: `ISOFieldParserPtrBase::strictLength(bool)`/`strictLength()`.
- ABI: neues Mitglied in `ISOFieldParserPtrBase` (Layout) — Shared-Library-Consumer
  neu kompilieren.
- Hinweis (0.6.0-Interaktion): Seit 0.6.0 ist `SpecFieldInfo::is_nested` auch
  für TLV-Container `true` (unter 0.5.0 blieb es `false`); Konsumenten, die einen
  TLV-Katalog klassifizieren, werten `tlv_children`/`tlv_is_ber` **vor**
  `is_nested` aus.

## 0.6.1

### `[+](Added)` `AmountField`: optionales Vorzeichen (`sign: true`) in der Standardform

- Neuer optionaler Feld-Key `sign: true` für `format: amount` (nur zusammen
  mit `scale:`, nicht mit `encoding: bcd`; Fail-closed mit positioniertem
  `SpecValidationError` sonst, `sign: false` ist erlaubt). Der Wire-Wert
  beginnt mit einem Vorzeichenzeichen `C`/`+` (positiv) oder `D`/`-` (negativ),
  danach nackte Ziffern; `length` zählt das Vorzeichenzeichen mit (z. B.
  DE 28–31 „x+n 8“: `length: 9`). Ohne `sign:` bleibt alles unverändert.
- `AmountField`: neuer Konstruktor `(key, AmountForm, scale, signedWire)`,
  `hasSign()`, `isNegative()`; `minorUnits()`/`amount()` vorzeichenbehaftet,
  `readable_value()` mit führendem `-` (z. B. `"-0.05"`), `to_json()` ergänzt
  `negative`. Auch für typisierte TLV-Kinder.
- Neu: `SpecFieldInfo::amount_signed`.
- Intern: `TEST_CASE`-Namen ASCII-only (Windows-ctest-Registrierung), `nightly.yml`-Heredoc
  im Seed-Corpus-Schritt repariert (Fuzz-Soak lief nicht).
- ABI: neue virtuelle Funktion `ISOFieldParserPtrBase::setAmountSigned`
  (Vtable), neues Mitglied `signed_` in `AmountField`, neues Mitglied in
  `SpecFieldInfo` (Layout) — Shared-Library-Consumer neu kompilieren.

## 0.6.0

> 0.6.0-Kandidat (Plan `docs/plans/remaining-encoding-spec-schema.md`):
> `remaining` folgt ab jetzt dem aufgelösten Feld-/Global-Encoding
> (vorher encoding-neutral) und verlangt zwingend `length` (Maximum,
> Fail-closed). Neue normative Spec-Schema-Referenz für Menschen und
> KI-Generatoren: `docs/internals/spec_schema.md`.

### ⚠️ Breaking: `remaining` ist encoding-aware (0.6.0-Kandidat)

- `format: remaining` ist **nicht mehr encoding-neutral**: Es folgt der
  üblichen Encoding-Auflösung (Feld-`encoding` > globale `encoding` >
  `""`). `""`/`binary` → rohes `BinaryField` (Verhalten unverändert);
  `ascii`/`ebcdic`/`bcd` → `OpaqueField` (Text bzw. BCD-Ziffern) —
  d. h. in Specs mit globalem Text-Encoding dekodiert `remaining` ab
  jetzt `OpaqueField` statt `BinaryField` (0.5.0-TLV-Children-Muster,
  bewusst gewollt).
- `remaining`+`ebcdic` ist EBCDIC-Text (IBM-1047-Tabelle), **nicht**
  HEX_EBCDIC (das gilt nur für `format: binary` unter `ebcdic`).
- BCD: `length` zählt **Ziffern** (1 Byte = 2 Ziffern); dazu BCD-
  Korrektur im `UNKNOWN`/`CONSUME`-Zweig des Feld-Parsers
  (`src/_parser.hh`), damit `remaining`+`bcd` den kompletten
  Restpuffer decodiert und korrekt `consumed` meldet.
- Strict-Modus wird an die `remaining`-Konvertierung propagiert
  (EBCDIC-Whitelist-Throw vs. Legacy `'.'`/`'?'`-Mapping).
- Migration: Code, der `remaining`-Felder in Text-Specs als
  `BinaryField` liest, muss `OpaqueField`/`tryGetValue<std::string>`
  verwenden — oder das Feld lokal auf `encoding: binary` setzen.

### Fail-closed: `remaining` verlangt `length`

- `format: remaining` ohne `length` wird beim Laden mit einem
  positionierten `SpecValidationError` abgelehnt (Fail-closed; ohne
  Maximum würden 0 Bytes dekodiert). `length` gilt weiterhin als
  Maximum (Clamp); ein längerer Payload wird gekürzt (strict: Fehler
  „Unverbrauchte Bytes am Pufferende“).

### Introspection: `REMAINING` meldet deklariertes `max_length`

- `SpecFieldFormat::max_length` für `REMAINING` liefert das deklarierte
  `length` (vorher wurde 0 erzwungen). Verhalten, keine
  Layout-Änderung — keine ABI-Wirkung.

### Neu: normative Spec-Schema-Referenz (`spec_schema.md`)

- Neues Doku-Kapitel (Toctree *Internals*): Root-/Feld-Keys, komplette
  Format×Encoding-Matrix (1:1 zur Parser-Dispatch-Tabelle),
  `remaining`-Semantik, Direktiven, Nested/TLV/BERTLV-Regeln,
  Encoding-Auflösung/-Vererbung, Fail-closed-Fehlerliste, Laufzeit-
  Verhalten, komplette Beispiele und eine Generator-Checkliste — für
  Menschen und KI-Agenten, die Spec-Dateien erzeugen sollen.
- Synchron: `encoding.md` / `yaml_format.md` / beide `AGENTS.md`-
  Referenzen (Neutral-Set, Formattabellen, Beispiele mit `length`,
  Pitfall-/Fehlerlisten).
- Neue Catch2-Tests (Matrix aller vier Encodings, Feld-Override,
  Roundtrip, Strict/Legacy, Fail-closed ohne `length`, Clamp):
  `tests/test_remaining_field.cc` (+6 TEST_CASEs).

### `[#](Fixed)` SIGSEGV in text-basierten Nested-Containern (0.6.0-Kandidat)

- `type: nested` + text-basiertes Containerformat (`char`/`numeric`/`nopad_char`, L-Prefix `llchar`/`lllchar`/`llllchar` (ascii) sowie `remaining` + Text-Encoding) crashte in `unparse()`/`parse()` mit SIGSEGV: der Container-Basis-Parser (string-basiert) empfing den `BinaryField`-Scratch/-Wrapper des Nested-Zweigs (seit 0.3.0 Thread-Sicherheits-Pattern) → Null-Pointer-Dereferenz. Die AGENTS.md-Beispiele (DE48 `lllchar`+`tlv`) waren betroffen; v0.5.0 ebenso.
- Fix: Der Loader normalisiert den Container-Basis-Parser auf den binären Zwilling (wire-neutral: gleicher L-Zähler + Prefix-Encoding, Container-Daten bleiben Roh-Bytes für die Kinder; Introspektion meldet weiterhin das deklarierte Format). Neue Tabelle-Lücke geschlossen: `llllbinary|ascii` (`IFA_LLLLBINARY`).
- Fail-closed-Guard: manuell konstruierte `ISONestedFieldParser`-Instanzen mit nicht-binärer Container-Basis werfen jetzt ein positioniertes `std::runtime_error` statt SEGV.
- FR-3: Typisierte TLV-Kinder im fixen `tag_bytes`/`len_bytes`-Modus funktionieren damit auch in text-basierten Containern (in binären, z. B. `lllbinary`, bereits seit 0.5.0).
- Härtung (in der Implementierung neu gefunden): Container-Sub-Parser kennen kein MTI (Slot 0 = erstes Kind-Feld) — vorher serialisierten Encode- und Decode-Pfad bei genau einem Kind denselben Datenbereich doppelt (MTI-Block + Daten-Loop), was das Längen-Prefix verkrümmte; der Encode-Pfad wirft bei fehlender Bitmap-Komponente (direkter Parser-Aufruf an einer nie dekodierten Nachricht) jetzt ein positioniertes `std::runtime_error` statt `std::bad_optional_access` an ferner Stelle.
- Regressionstests: `tests/test_nested_text_container.cc` (11 TEST_CASEs, Tag [fr3nested]; Deckung: llchar/lllchar/llllchar|ascii, fixer + BERTLV-artiger TLV in text-basierten Containern, Encoding-Vererbung, Single-/Multi-Kind, Encode-first und Decode-first).

### ⚠️ Breaking: `pos::POSDataCode` — `OFFSET`-Pseudowerte entfernt, Typos-Korrektur (0.6.0-Kandidat)

- `POSDataCode` ist seit 0.2.0 veröffentliche API; die vier Flag-Enums
  (`ReadingMethod`, `VerificationMethod`, `POSEnvironment`,
  `SecurityCharacteristic`) verlieren die `OFFSET`-Pseudo-Enum-Member:
  Die Byte-Offsets (0/4/8/12) sind jetzt privates Implementationsdetail
  (`offsetOf()`). Nur Compile-Time — das Draht-Layout (4 × 4-Byte
  Little-Endian-Wörter, 16 Bytes) bleibt unverändert.
- `SecurityCharacteristic::PRIVAT_ALG_ENCRYPTION` →
  `PRIVATE_ALG_ENCRYPTION` (Typos). Bit-Position `1 << 7` unverändert →
  gepackte Bytes bleiben byte-kompatibel.
- Migration: Code, der `*::OFFSET` oder die alte Typos-Schreibweise
  referenziert, muss neu kompiliert werden; Laufzeit- und
  Draht-Verhalten ändert sich nicht.

### `[+](Added)` `pos::POSDataCode`: vollständiger Flag-Operatorsatz, Konstruktoren-Defaults

- Vollständiger Satz an Flag-Operatoren: `^` (symmetrische
  Differenz), `~` (Bit-Negation), `&=`, `^=` — ergänzend zu `|`, `&`,
  `|=` der Vorgängerversion; auf allen vier Enums, `constexpr`/`noexcept`.
- Der Vier-Argumente-Konstruktor hat Defaults (`= UNKNOWN`):
  `POSDataCode(read)`, `POSDataCode(read, verify)` usw. sind möglich.
- `has*(f)` ist jetzt explizit dokumentiert: true, wenn **alle** Bits
  von `f` gesetzt sind (leere Maske `f == 0` → vacuously true).
- Header dokumentiert das Draht-Layout und die Little-Endian-Konvention
  (ISO-8583-Elemente sind üblicherweise Big-Endian — die Konvention
  darf nicht ungeprüft auf andere DEs übertragen werden).

### `[~](Changed)` `pos::POSDataCode`: deterministische, vollständige `describe()`-Ausgabe

- Lookup-Tabellen `std::unordered_map` → `std::map`: `describe()` /
  `operator<<` listen die gesetzten Flags jetzt in deterministischer
  (steigender) Bit-Reihenfolge — die Ausgabe ist test- und
  vergleichbar.
- `describe()` labelt jetzt **immer** alle vier Kategorien
  (`"Reading:"`, `"Verification:"`, `"Environment:"`, `"Security:"`);
  eine leere Kategorie (keine gesetzten Bits) liefert `"<label>: none"`.
- Tests: `tests/test_pos_data_code.cc` neu geschrieben (9 TEST_CASEs,
  Tags `[pos]`, `[error]`, `[integration]` — inkl. Integration mit
  echtem `ISOMessage`/`BinaryField` über die übliche Feld-API).

### `[+](Added)` Field-only-Specs: Einzel-Feld-Specs (z. B. DE55/ICC) auf BinaryField-Payloads anwenden (0.6.0-Kandidat)

- Neue Dokument-Form: eine Spec mit einem einzelnen `field:`-Block
  (gleiche Feld-Grammatik wie `fields:`-Einträge) definiert die Semantik
  eines **einzelnen** Feldes — typischer Use-Case: DE55 ICC Data
  (Mastercard fix-TLV `lllbinary` + `tlv:`, oder EMV `lllbertlv`).
- Neue Public API (additive only): `SpecDecoder::loadFieldFromYaml` /
  `loadFieldBothFromYaml` (+ `Cached`-Varianten, `SpecLoadOptions` wird
  unverändert wiederverwendet). Die geladene Spec operiert auf exakt den
  Bytes, die ein `BinaryField` nach dem Vollnachrichten-Decode hält
  (ohne das Längenpräfix des DEs); Roundtrip `parse(...)` ist
  byte-identisch.
- `SpecDecoder::decodeField(parser, binaryField)`: Convenience —
  synthetische leere `ISOMessage` + Parser + `unparse` in einem Aufruf;
  TLV-Kinder werden per SE/Tag, Sequenz-Kinder pro Position adressiert
  (das einzelne Feld selbst wird als Key `0` introspektiert).
- Getrennter Loader-Cache (LRU ≤ 64, gleiches TOCTOU-/SHA-256-Protokoll
  wie der Message-Spec-Cache): `invalidateFieldCache(path)` /
  `clearFieldCache()`. Hinweis: Wer eine Datei in beiden Formen lädt,
  muss bei Änderung beide Caches invalidieren.
- Fail-closed (positionierte Fehler): `fields:` und `header:` in
  Field-only-Dokumenten sowie `field:` in Message-Specs werden
  abgelehnt.

### `[~](Changed)` `AmountField`: auf Standard-ISO-8583-Betragsfelder generalisiert (optionaler `scale:`-Key) (0.6.0-Kandidat)

- Neuer optionaler Feld-Key `scale: N` (nur bei `format: amount`, Ganzzahl
  ≥ 0; Fail-closed mit positioniertem `SpecValidationError` bei
  Nicht-`amount`-Feld, negativem oder nicht-numerischem Wert). Ohne `scale:`
  bleibt die jPOS-`ISOAmount`-Form **unverändert** (rückwärtskompatibel).
  Mit `scale: N` gilt die Standard-ISO-8583-Form (z. B. DE 4): `length`
  nackte Ziffern, Skala = `N`, Währung **nicht** im Feld (`currency()` =
  `nullptr`, `readable_value()` ohne Währungspräfix, `to_json()` ohne
  `currency`, mit `scale`).
- Auch für typisierte TLV-Kinder (`amount`-Kinder werden jetzt als
  `AmountField` statt `OpaqueField` dekodiert; `AmountField` ist eine
  `OpaqueField`-Ableitung, `get<OpaqueField>` funktioniert weiter).
- ABI: neue virtuelle Funktion `ISOFieldParserPtrBase::setAmountScale`
  (Vtable), neues Enum `AmountForm` sowie neue Mitglieder/neuer Konstruktor
  von `AmountField` (Layout) — Shared-Library-Consumer neu kompilieren.

### `[+](Added)` `SpecFieldInfo::amount_scale`: deklarierte `scale` introspektierbar (0.6.0-Kandidat)

- Neues Mitglied `std::optional<int> amount_scale` (`nullopt` = jPOS-Form).
  ABI: weiteres Layout-Mitglied des per-Wert `SpecFieldInfo`.

### `[+](Added)` `AmountField`: AMOUNT-Feldtyp — jPOS-`ISOAmount`-Konvention (Währungscode + Skala + Betrag) (0.6.0-Kandidat)

- Neuer Feldtyp `format: amount` (Encodings `ascii`/`ebcdic`/`bcd`;
  Parser-Aliase `IFA_AMOUNT`/`IFB_AMOUNT`/`IFE_AMOUNT`): dekodiert zur
  neuen `iso8583::AmountField`-Komponente (String-Komponente mit
  dünnem abgeleitetem `AmountFieldParser` — die String-
  Parse-/Unparse-Pfade bleiben unverändert, keine Kern-Parser-Änderung,
  keine neuen Template-Instantiierungen).
- Wire-Konvention (jPOS `org.jpos.iso.ISOAmount`):
  `zeropad3(Währungs-Ziffercode)` + 1-stellige Skala +
  `zeropad12(Betrag-Integer)` = 16 Zeichen, z. B. EUR 19.99 →
  `"978200000001999"` (978 = ISO 4217); `length: 16` üblich.
- `AmountField` nutzt die eigene `currency::Currency` (ISO-4217-Tabelle)
  statt jPOS `ISOCurrency`:
  - Konstruktor `AmountField(key, currencyCode, minorUnits)` baut den
    Wire-String (`std::invalid_argument` bei unbekanntem Währungscode);
  - typisierte Accessors (parsen den Wire-Wert on demand,
    `std::invalid_argument` bei Länge < 12): `currencyCode()` /
    `currencyCodeAsString()`, `currency()` (nullptr, wenn nicht in der
    Tabelle), `scale()`, `minorUnits()`, `amount()`,
    `legacyAmountString()` (12-stellig, ungeskalter Integer) und
    `readable_value()` (z. B. `"978/19.99"`);
  - jPOS-Parität: „rounding problem" — Wire-Skala > Währungs-
    Decimalstellen und nicht durch 10^(Skala−Decimalstellen) teilbare
    Ziffern → `minorUnits()`/`amount()` werfen
    `std::invalid_argument`; unbekannte Währungen laufen mit
    Decimalstellen = 0 weiter;
  - `to_json()` ergänzt `currency` (Alpha-Code), `amount` (double) und
    `minor_units` (long long) zum Basis-JSON; `dump()` inkl.
    Sensitive-Maskierung wird geerbt, kein Override.
- Spec-Grammatik: `amount` in den Format-Validierungslisten (inkl.
  typisierter TLV-Kind-Formate, Fail-closed), Parser-Tabelleinträge
  `AMOUNT|ASCII/BCD/EBCDIC`, `SpecFieldFormat::type` meldet `"AMOUNT"`;
  als typisiertes TLV-Kind erlaubt (Encodings wie `numeric`).
- Doku: `docs/internals/spec_schema.md` (Matrix + Semantik),
  `docs/internals/yaml_format.md` (Format-Tabellen + Whitelist), beide
  `AGENTS.md` (Loader-Verhalten + Format-Liste + `SpecFieldFormat`-
  Tabelle).
- Tests: `tests/test_amount.cc` (11 TEST_CASEs, Tag `[amount]`:
  Konstruktoren inkl. JPY (0 Decimals) / JOD (3 Decimals) / unbekannter
  Code, Accessors, Reskalierung, Rounding-Problem, Länge < 12, unbekannte
  Währung, YAML-Roundtrips `format: amount` + `encoding: ascii` (inkl.
  Sensitive-Maskierung im dump) bzw. `encoding: bcd`, `to_json`).

### `[+](Added)` `SpecFieldInfo::tlv_is_ber`: TLV-Modus (fixer SE vs. BER-TLV) introspektierbar (FR-4, 0.6.0-Kandidat)

- Neues `SpecFieldInfo`-Mitglied `bool tlv_is_ber = false` (per-Wert
  zurückgegeben durch `ISOSpec::field(de)`): `true`, wenn das Feld ein
  TLV-Container im **BER-TLV-Modus** ist — beide Schreibweisen,
  `tlv: {ber: true}` (SE-Keys in Hex) **und** die `...bertlv`-
  Kurzform (`format: lllbertlv` etc.), setzen das Flag identisch —,
  `false` im fixen SE-Modus (`tlv: {tag_bytes, len_bytes, tcc}`) und bei
  allen Nicht-TLV-Feldern (Default).
- Pure Introspection: kein Decode-/Encode-Verhalten ändert sich;
  `SpecDecoder::makeSpecFieldInfo()` setzt das Flag aus
  `SpecField::tlv->ber`. Auch für BER-Container **ohne** deklarierte
  `children` wird `true` gemeldet.
- Motivation (tng-wire-viewer): Compose-Tabs sollen zwei Typ-Badges
  („TLV" vs. „BER-TLV") statt eines generischen „NESTED" anzeigen —
  eine Heuristik über `SpecFieldFormat::type` ist nachweislich falsch,
  da fixer SE- und BER-Container dasselbe `format.type` tragen können
  (z. B. `lllbinary` + `tlv: {ber: true}` vs. `lllbinary` +
  `tlv: {tag_bytes, len_bytes}`).
- **ABI:** wie `tlv_children` (0.5.0) ändert das neue Mitglied das
  Layout des per-Wert zurückgegebenen `SpecFieldInfo` —
  Shared-Library-Consumer müssen gegen die neue Bibliothek neu
  kompiliert werden (0.x).
- Doku: beide `AGENTS.md` (Spez-Schema-/Introspektions-Tabellen +
  ABI-Hinweis 0.6.0), `docs/internals/spec_schema.md`
  (Signatur-Listing).
- Tests: `tests/test_spec_loader.cc` — `tlv_is_ber`-Checks in den
  FR-2-Tests (bertlv-Kurzform `true`, fixer SE-Block `false`) plus
  neuer Test für `tlv: {ber: true}`-Block-Schreibweise (true) inkl.
  Nicht-TLV-Default (false).

## 0.5.0

> 0.5.0 (FR-1/FR-2, Plan `docs/plans/tlv-typed-children-plan.md`):
> Deklarierte TLV-/BERTLV-Kinder werden gemäß ihrer deklarierten
> `format`/`encoding` **typisiert** dekodiert und kodiert — gewollt
> **breaking** für Text-Kinder (früher rohes `BinaryField`) — und die
> `...bertlv`-Kurzform erlaubt erstmals eine optionale `children:`-Map.
> Das `SpecFieldInfo`-Layout ändert sich (`tlv_children`): Shared-
> Library-Konsumenten kompilieren neu gegen die geändete Public Header.

### Neu: typisierte TLV-/BERTLV-Kinder (FR-1/FR-2), `children`-Map bei `bertlv`, `tlv_children`

- **FR-1: Typisierte TLV-/BERTLV-Kinder** (gleiche Regeln für beide TLV-
  Formen): deklarierte `children` mit `format: char`/`numeric`/
  `nopad_char` dekodieren zu `OpaqueField` via Codec (Encoding
  `ascii`/`ebcdic`/`bcd`, explizit deklariert oder vererbt — bei
  `...bertlv`-Kindern muss es explizit gesetzt werden, da dort nichts
  vererbt wird) und kodieren zurück (Byte-für-Byte-Roundtrip);
  `format: binary` und undeclared Tags bleiben rohes `BinaryField`
  (inkl. generischer `"SE<n>"`-Beschreibung). Der Strict-Modus wird an
  die Kind-Codecs propagiert. `length` bleibt reine Dokumentation (die
  TLV-Länge liegt im Length-Feld des Frames).
- **FR-2: `...bertlv`-Kurzform mit `children:`-Map** (Hex-Tag-Keys,
  z. B. `"9F26": { format: binary, length: 8 }`): bekannte/erwartete
  EMV-Tags können deklariert und typisiert werden; undeclared Tags
  werden weiterhin dynamisch dekodiert. `type: nested`, ein eigener
  `tlv:`-Block und `children` als Sequence bleiben unzulässig — jetzt
  mit präzisen, separaten Fail-closed-Fehlern statt einer
  Sammelmeldung.
- **Kind-Whitelist (D5, Fail-closed beim Laden, positionierte Fehler):**
  erlaubte Kind-Formate sind `binary`, `char`, `numeric`, `nopad_char`
  (L-präfixierte Formate, `bitmap`, `remaining`, `nop` sind widersprüchlich
  und werden verworfen); erlaubte deklarierte Kind-Encodings `ascii`,
  `ebcdic`, `bcd`, `binary`, Text-Formate nur mit `ascii`/`ebcdic`/`bcd`;
  Kind-Deklarationen müssen Maps sein; ein Text-Kind, das nach der
  Encoding-Auflösung (Feld → globale Spec-`encoding`) auf ein
  unbrauchbares Encoding landet, wird verworfen.
- **`SpecFieldInfo::tlv_children`** (`std::map<int, SpecFieldInfo>`):
  deklarierte TLV-/BERTLV-Kinder per `loadBothFromYaml` introspektierbar
  (beide TLV-Formen; Key = voller Tag-Wert als `int`, damit 2-Byte-EMV-
  Tags wie `0x9F26` auch in `int16_t`-Builds passen).

### Changed (BREAKING)

- **Text-Kinder von TLV-/BERTLV-Containern** (`char`/`numeric`/
  `nopad_char`) sind jetzt typisierte `OpaqueField` statt rohem
  `BinaryField` (Codec-Konvertierung; im Strict-Modus werfen nicht-
  mappbare Bytes statt `.` zu mappen). Specs, die das alte Verhalten
  (Rohbytes) brauchen, deklarieren die betroffenen Kinder als
  `format: binary`.
- Ungültige BERTLV-Kombinationen (`type: nested` + `...bertlv`, eigener
  `tlv:`-Block + `...bertlv`, `children` als Sequence + `...bertlv`)
  erzeugen jetzt jeweils eine eigene, präzise Fehlermeldung.

### Bugfixes

- **BCD-TLV-Kinder: halbierte Ziffernanzahl beim Decode** —
  `child_as_string` übergab die Byte-Länge an `codec::as<...,BCD>`,
  das Ziffern zählt (2 pro Byte); jetzt `length × 2` (Decode/Encode
  symmetrisch, Roundtrip bytegenau).
- **CI: `nightly.yml` trug noch die 0.4.0 entfernte iconv-Konfiguration**
  (Folgefehler von `e07dbe4`, das nur `ci.yml` anfasste):
  `-DISO8583_ENABLE_ICONV=ON` (in 0.4.0 entfernte CMake-Option, wurde
  still ignoriert) und das apt-Paket `libiconv-hook-dev` (Iconv-Linkage
  ist aus dem Build entfernt) sind aus dem Fuzz-Soak-Job entfernt.
- **Docs: `docs/conf.py`-Release-String** von `0.2.0` auf `0.5.0`
  aktualisiert (seit dem 0.3.0-Release veraltet; fließt in die
  Sphinx-Footer-/Metadaten der gebauten Doku-Seiten ein).

### ABI-Hinweis (0.5.0)

- `SpecFieldInfo` ist per Wert zurückgegeben und enthält jetzt das neue
  Mitglied `tlv_children` — das Layout ändert sich; Shared-Library-
  Konsumenten müssen gegen die neue Bibliothek neu kompiliert werden.

### Migration 0.4.0 → 0.5.0 (Konsumenten-Checkliste)

Kurze Checkliste für Bibliotheks-Konsumenten (z. B. Tauri-/GUI-Backends):

1. **Neu kompilieren** gegen die geänderten Public Headers (`SpecFieldInfo`
   hat das neue Mitglied `tlv_children` — Layout-Change) und gegen die neue
   Bibliothek (Shared-Library: neu linken, Import-Lib/DLL austauschen).
2. **Key-Typ prüfen:** wer `...bertlv` mit 2-Byte-EMV-Tags (z. B. `9F26`)
   nutzt, kompiliert Bibliothek **und** Konsument mit `ISO8583_BERTLV`
   (CMake-Option bzw. `ISO8583_KEY_TYPE` — ABI-kritisch, beide Seiten
   identisch); ohne diese Definition sind nur Tags ≤ 32767 darstellbar.
3. **Typisierte TLV-Kinder:** deklarierte `char`/`numeric`/`nopad_char`-
   Kinder sind jetzt `OpaqueField` (vorher `BinaryField`) →
   `get<BinaryField>(tag)`-Aufrufe auf solchen Tags auf
   `get<OpaqueField>(tag)` umstellen (sonst `nullptr`); wer weiterhin
   Rohbytes will, deklariert das Kind als `format: binary`.
4. **BERTLV-Text-Kinder:** bei der `...bertlv`-Kurzform wird an Kinder
   nichts vererbt — ein `encoding:` (`ascii`/`ebcdic`/`bcd`) ist
   verpflichtend, sonst verwirft der Loader die Spec (Fail-closed, mit
   positionierter Fehlermeldung).
5. **Kind-Whitelist:** erlaubte Kind-Formate `binary`/`char`/`numeric`/
   `nopad_char`; deklarierte Kind-Encodings `ascii`/`ebcdic`/`bcd`/
   `binary` (Text-Formate nur `ascii`/`ebcdic`/`bcd`). L-präfixierte
   Formate, `bitmap`, `remaining`, `nop` werden beim Laden verworfen.
6. **Introspektion:** `spec->field(de).tlv_children`
   (`std::map<int, SpecFieldInfo>`) ist für **beide** TLV-Formen über
   denselben Codepfad befüllt (BERTLV/`ber:true`: Map-Key = voller HEX-
   Tag-Wert; Fixformat-TLV: Map-Key = dezimale SE-Nummer). Den Map-Key
   immer als Wahrheit nutzen — `child.key` ist nur die `key_type`-Sicht
   desselben Werts (in `int16_t`-Builds bei Tags > 32767 eingekürzt).
   Undeklarierte Tags tauchen dort nicht auf (bleiben dynamisch).
   `child.encoding` kann bei BERTLV-Kindern `""` sein (binäre Kinder,
   nichts vererbt) — UI-/Introspektions-Code sollte das tolerieren.
7. **Strict-Modus:** wird an typisierte Kind-Codecs propagiert —
   nicht-mappbare EBCDIC-Bytes in deklarierten Text-Kindern werfen jetzt
   (vorher gab es keine Kind-Typisierung, Bytes kamen roh zurück).
8. **Unverändert:** `value()`/`to_json()` bleiben unmaskiert,
   `sensitive:` wirkt nur auf dem dump-/Log-Pfad; undeclared Tags bleiben
   dynamisch (`BinaryField`, generische `"SE<n>"`-Beschreibung); `length`
   in `children` ist reine Dokumentation (die Länge liegt im Length-Feld).

## 0.4.0

> 0.4.0 entfernt den seit 0.3.0 deprivierten libiconv-Fallback
> (`ISO8583_ENABLE_ICONV`, `src/_iconv_wrapper.{cc,hh}`) — der EBCDIC-Codec
> ist voll tabellenbasiert (ICU-78.3-Orakel-Pin) und der Baum enthält keine
> `thread_local`-Deskriptoren mehr. Die Public API ist ansonsten additiv:
> `ISOSpec` erhält `hasHeader()`/`headerSize()` (Introspektion des
> YAML-Root-Keys `header:`; bestehende 3-Arg-Konstruktion bleibt
> source-kompatibel, Shared-Library-Konsumenten kompilieren neu gegen den
> Header).

### Added

- **`ISOSpec`-Introspektion für Netzwerk-Header**: `spec->hasHeader()`
  (true, wenn die YAML-Root-Key `header:` definiert ist) und
  `spec->headerSize()` (Byte-Anzahl des Headers; `0`, wenn die Key fehlt).
  Damit ist die im Parser bereits verdrahtete `header:`-Größe (z. B.
  proprietärer Frame-Header vor dem ISO-8583-Body) ohne Zugriff auf die
  privaten Parser-Interna abfragbar. `docs/internals/yaml_format.md`
  dokumentiert die Root-Keys (`spec:`, `encoding:`, `strict:`, `header:`).

- **FAQ-Seite (`docs/faq.md`)**: Neue Doku-Seite für typische
  Fehlerszenarien und deren Triage. Erster Eintrag:
  `std::system_error: Resource deadlock would occur` nach `unparse()`
  (Windows/MSVC) — Mechanismus (MSVC-STL wirft genau diese Meldung bei
  Re-Sperr einer **nicht-rekursiven** Sperre durch den haltenden Thread;
  `call_once`/`once_flag` ausgeschlossen, auf Linux/GCC stillsteht das
  Muster statt zu werfen), Audit-Ergebnis (alle libiso8583-Sperren auf dem
  Decode-Pfad sind rekursiv oder RAII-gescoped — die Meldung kann nicht aus
  der Bibliothek stammen), typische Host-Code-Verursacher (gelockte/
  verschachtelte `std::mutex`, re-entrante Logger-Callbacks, `join()` auf
  sich selbst) und Differenzialtests.

### Removed

- **iconv-Fallback & `ISO8583_ENABLE_ICONV`** (geplanter Removal aus 0.3.0):
  Der deprivierte libiconv-Fallback für EBCDIC ist vollständig entfernt —
  CMake-Option, `src/_iconv_wrapper.{cc,hh}`, die `ENABLE_ICONV`-
  Compile-Definition, die `Iconv::Iconv`-Linkage, `libiconv` in
  `vcpkg.json`/`vcpkg-port/vcpkg.json`, die Preset-Einträge, die
  CI-Flags (inkl. `libiconv-hook-dev` auf Linux) und die beiden
  deprivierten Exporte `codec::ebcdic_to_ascii_cached()`/
  `codec::ascii_to_ebcdic_cached()` (public header). Der EBCDIC-Codec ist
  voll tabellenbasiert (ICU-78.3-Orakel-Pin, s. 0.3.0) und hat den Fallback
  auf dem Runtime-Pfad nie mehr genutzt; der Baum enthält jetzt keine
  `thread_local`-Deskriptoren mehr. **Breaking (Build):**
  `-DISO8583_ENABLE_ICONV=…` wird nicht mehr erkannt (CMake ignoriert
  unbekannte Optionen still); auf Nicht-Linux-Plattformen entfällt die
  libiconv-Abhängigkeit. Für Code, der nur die Public API der 0.3.x nutzt,
  gibt es keine ABI-Veränderung (außer den zwei deprivierten Fallback-
  Symbolen, die kein Codec-Pfad je aufrief).

### Fixed

- **Windows-CI: Sandbox/Sidecar-Fehlpositiv "außerhalb der erlaubten Wurzel"**
  (12 Test-Failures in den Windows-Jobs): `isWithinRoot()` verglich den
  Kandidatenpfad (`fs::absolute`, nicht kanonisiert) rein lexikalisch mit der
  kanonisierten Sandbox-Wurzel. Windows-8.3-Kurznamen (GitHub-Windows-Runner
  setzen `TEMP=C:\Users\RUNNER~1\AppData\Local\Temp`; `fs::canonical` liefert
  `C:\Users\runneradmin\...`) und Symlinks/Junctions (macOS `/tmp` →
  `/private/tmp`) sind lexikalisch verschieden, aber physikalisch identisch —
  in-Wurzel-`!include_files` und `.smap`-Schreibzugriffe wurden fälschlich
  verworfen. Fix: nach negativem lexikalischem Vergleich Fallback auf
  `fs::weakly_canonical` des Kandidaten (Fail-closed bleibt erhalten: wirklich
  außerhalb liegende Pfade kanonisieren auf außerhalb liegende Formen).
- **Test-Härtung**: Die `TempDir`/`TempYaml`-Helfer benennen ihre
  Temp-Verzeichnisse/Dateien jetzt nach PID+Zähler statt Thread-ID-Hash. Unter
  `ctest -j` kollidierten mehrere Test-Prozesse prozessübergreifend über
  gleiche Haupt-Thread-IDs auf denselben Verzeichnisnamen (Rest-Dateien,
  `exists()`-Rennbedingungen zwischen parallelen Tests).
- **TOCTOU-Test (Windows-Flake)**: Der alte Diskriminator (exists()-Check im
  Fehlermoment) hatte selbst ein Mikrosekunden-TOCTOU-Fenster (Datei fehlt
  beim `open`-Fehler, ist beim `exists()`-Check schon wieder da) → seltene
  Fehl-Failures. Jetzt zählen nur IO-ebene-Ladefehler des Loaders
  (`Datei nicht lesbar`) als transient; echte Load-/Cache-Fehler (Sandbox,
  Parsing, Validierung) werden unverändert sofort weitergeworfen. Anhaltende
  IO-Fehler deckt die Race-Grenze ab.

## 0.3.0

> **Wichtig:** 0.3.0 ist ein Sicherheits-/Robustheits-Release für den
> produktiven Einsatz im Finanzumfeld (PCI). Mehrere Verhaltensänderungen sind
> bewusst **breaking** (0.x); Details unten. Die Public-API ist ansonsten
> stabil (nur additive Zugänge) — aber `ISOBaseParser` bekommt ein neues
> Mitglied (`strict_`), daher müssen **Shared-Library-Konsumenten neu
> kompiliert** werden (siehe ABI-Hinweis unten).

### Neu: Strict-Modus als Standard (fail-closed)

Die Bibliothek verhält sich jetzt im Default **strict**: statt abge-
schnittene Frames still zu clampen, überdimensionale Felder zu verwerfen
oder ungültige EBCDIC-Bytes auf `.` zu mappen, wirft der Dekodier-/Encoder-Pfad
einen **positionsgenauen** `std::runtime_error` mit dem `[ISO8583]`-Präfix
(Feld, Offset, Byte, Hexdump).

- Neues Spec-Rot-Attribut `strict: true|false` (Default **`true`**) und
  Laufzeit-`ISOBaseParser::strict(bool)` / `strict() const`.
- Betroffene Fälle: Pufferende-Trunkatur (Feld am Ende abgeschnitten),
  Serialisierung größer als Feld-Maximum (Frame würde fehlerhaft),
  Längenpräfix am Pufferende abgeschnitten, ungültiges EBCDIC-Byte
  (tabelle- und Orakelpfad), Bitmap-Byte am Pufferende, überlanger
  Wire-Header (WLP-FO 93 B / BASE1), TLV-`offset+N`-Prechecks.
- **Escape-Hatch für tolerante Integratoren:** `strict: false` in der Spec
  oder `parser.strict(false)` zur Laufzeit → altes (clamp/WARN/`.`-Sentinel)
  Verhalten, aber nie mehr *still* (jeder Fall loggt mindestens WARN/ERROR).

### ⚠️ Breaking: WLP-FO-Header wird jetzt vollständig (93 Byte) serialisiert

`WLP_FOHeader::pack()` erzeugte bisher nur 89 Byte und ließ das 4-Byte-ASCII-
Längenpräfix sowie Bytes 4..93 weg — ausgehende WLP-Frames waren dadurch
korrupt (Längenpräfix verloren, Rest verschoben). `pack()` liefert jetzt den
**vollen 93-Byte-Header**, und `parse()` prüft das **gepackte** Ergebnis
(früher: nur das gespeicherte Header-Objekt). Ein zu kurzer Wire-Header wirft
fail-closed statt zu OOB-Zugriff.

**Betrifft dich, falls** du WLP-FO-Nachrichten (Worldline) *erzeugst*: Die
ausgehenden Frame-Bytes ändern sich (Korrektur). Bei reinem Empfang
(`unparse`) ändert sich nichts.

### Neu: EBCDIC-Konvertierung tabellen-getrieben + ICU-78.3-Orakel-Pin

Die EBCDIC↔ASCII-Konvertierung ist jetzt **vollständig tabellen-getrieben**
(IBM-1047) und läuft **ohne** Laufzeit-Converter (kein libiconv, kein ICU zur
Laufzeit):

- `kEbcdicToAscii` / `kAsciiToEbcdic` / `kEbcdicValid` in
  `include/iso8583/_codec.hh` werden aus einem exakt gepinnten **ICU-78.3**-
  Orakel erzeugt (`tools/generate_ebcdic_tables/`, Build-/CI-only — ICU wird
  nie in die Laufzeit-Targets verlinkt).
- **Determinismus:** Die 256-Byte-Verdicts beider Richtungen sind gecheckt
  und über eine Cross-Toolchain-Diff in der CI abgeglichen (MSVC/clang/GCC
  liefern byte-identische Tabellen). Die historische libiconv-Debug-/
  Release-Divergenz ist damit eliminiert.
- **Strict-Whitelist** ist bewusst *stärker* als ICU: ICU 78.3 konvertiert
  alle 256 Bytes (C1-Steuerzeichen, Binär-Bytes), der Strict-Modus akzeptiert
  nur die 85 IBM-1047-Druck-/Ziffern-Bytes (E2A) bzw. die 84 mappablen
  ASCII-Zeichen (A2E). `tests/test_encoding_determinism.cc` pinnt beide
  Zählungen.
- `libiconv` / `ISO8583_ENABLE_ICONV` sind **deprigiert (Removal in 0.4)**
  und dienen nur noch als Übergangs-Fallback; der Standard-EBCDIC-Pfad nutzt
  sie nicht. Das Configure warnt bei `ISO8583_ENABLE_ICONV=ON`.

### Neu: Spec-Sandbox + Lade-Limits

Specs können Third-Party-/Remote-Herkunft haben; ein bösartiger oder
korrupter Spec darf den Host nicht kompromittieren. Der Lade-Pfad ist deshalb
**fail-closed** und beschränkt:

- **Include-Sandbox (Default an):** `!include_files`-Einträge, die außerhalb
der `roots` (leer → Verzeichnis der Top-Level-Spec) landen —
`../`-Traversals, absolute/UNC-Pfade, Symlink-Escapes — werden **abgelehnt**
(lexikalisch und, falls die Datei existiert, über den kanonisierten,
Symlink-auflösenden Pfad).
- **`SpecLoadOptions`** (neues public API, `ISOSpec.hh`) steuert:
  `sandbox`, `roots`, `allowSmapWrite` (Sidecar nur innerhalb `roots`),
  `maxSpecBytes` (32 MiB/Datei, beim Streamen erzwungen), `maxIncludeFiles`
  (1024), `maxSmapBytes` (16 MiB; übergroße Sidecars werden verworfen).
- **Leere `fields:`** → sauberer, positionsgenauer Fehler (statt der früheren
  `rbegin()==rend()`-UB); nicht-numerische DE-Keys und `std::stoi`-Entwürfe
  werden sauber validiert.
- Die alten `loadFromYaml(path, bool trackSourceMap)`-Overloads bleiben
  source-kompatibel (bauen interne Default-Optionen).

**Migration (Betrifft dich, falls):** Top-Level-Specs, die `!include_files`
außerhalb des eigenen Verzeichnisses nutzen, brauchen jetzt explizite `roots`
(`SpecLoadOptions::roots`) oder bewusst `sandbox=false`.

### Neu: `ISOMessage` ist thread-sicher teilbar

Eine `ISOMessage` kann jetzt **sicher von mehreren Threads gleichzeitig**
genutzt werden: alle öffentlichen Eintritte
(`set`/`unset`/`has`/`get`/`tryGet`/`tryGetValue`/`tryGetValueRef`/`reset`/
`keys`/`size`/`to_json`/`dump`/`parser`/`parse`/`unparse`/`header`/
`direction`/`hasMTI`/`mti`/`isRequest`/…) nehmen denselben **rekursiven
Message-Lock** genau einmal; interne Aufrufketten (z. B. `parse →
recalcBitmap → set`) laufen unter dem bereits gehaltenen Lock. Writer und
Reader schließen sich aus (ein Lock, kein paralleler-Reader-Modus).
`to_json`/`dump` sichern den Feldsatz unter dem Lock und formatieren außerhalb
(kurze Lock-Besitzzeiten).

- Parser-Objekte sind nach dem Laden **immutable** → sicher teilbar über
  Threads und Messages (paralleles `parse`/`unparse` auf *verschiedenen*
  Messages mit demselben Parser ist sicher).
- Logger-Globalen (`setLevel`/`setLogger`/`currentLogger`/`getLevel`) sind
  atomar.
- **Restrisiko (dokumentiert in `ISOMessage.hh`):** `mti()` liefert ein
  `string_view` *in* den mutablen Feld-Speicher — vor threadübergreifender
  Nutzung kopieren (`std::string m = msg->mti();`). `tryGetValueRef` ist eine
  zero-copy-Referenz mit derselben Einschränkung.

### Neu: PCI-Logging-Hygiene (`sensitive:`)

Neues Feld-Attribut `sensitive: true` (auch für TLV-`children`-Einträge und
`definitions:`): Der **Wert** wird in `dump()`/`operator<<` und in
Log-Ausgaben als `***` maskiert (die Beschreibung bleibt sichtbar). Auf
nested/TLV-BERTLV-Containern verbreitet es sich auf alle Children/Tags.
`value()`/`to_json()` bleiben bewusst **unmaskiert** (programmatische
Daten-API) — nie `to_json()` in einen Log-Sink für sensible Daten.
Produktiv-Loglevel: **WARN** oder niedriger.

### Spec-Cache-Härtung

- Cache-Eintrag trägt jetzt `{parser, spec, mtime, contentHash}`;
  **Publish-then-Verify**: Ein Parser wird nur unter exakt dem
  Dateisnapshot publiziert, aus dem er gebaut wurde → eine zur Laufzeit
  ausgetauschte Spec kann nie einen „gemischten" Parser liefern (TOCTOU am
  Publish-Punkt geschlossen).
- **LRU-Cap: 64 Einträge** (evictiert least-recent) — schließt das
  unbeschränkte Wachstum.
- `CacheValidation::TrustUntilInvalidated` bleibt, ist aber **als unsicher
  für rollende Spec-Änderungen** dokumentiert (Prozess bei Spec-Wechsel neu
  starten, `invalidateCache(path)` manuell aufrufen, oder `CheckEveryCall`
  verwenden).

### Memory-Sicherheit (Fail-closed statt Absturz)

- **A1:** Bitmap-Puffer-Prüfungen vor jedem Byte-Zugriff (positionsgenaues
  Throw statt OOB-Read); expliziter `bmp.size() > 65`-Guard.
- **A3:** Alle Header-Getter/Setter (WLP-FO, BASE1) rufen `require(n)` auf
  und werfen bei zu kurzem Header; Konstruktoren aus User-Vektoren validieren
  sofort (fail-closed, keine OOB-Write).
- **A5:** `ISOMessage::parser(p)` wirft bei falschem Parser-Typ.
- **A4 (Audit-Regel, Root-`AGENTS.md`):** `dynamic_bitset::operator[]`
  prüft nur per `assert()` (in Release aus) — **niemals** `bmp[n]` ohne
  vorherigen `bmp.size() > n`-Guard indexieren.
- **Iconv-Fallback (so lange gebaut):** E2BIG-Grow-Loop ist jetzt
  no-progress-erkennend und hard-capped (EBCDIC↔ASCII ist 1:1).
- **TLV-Härtung:** `read_num`/BCD-Policy mit expliziten `offset+N <=
  buf.size()`-Prechecks; `BerLength` lehnt `num_bytes > 8` ab (keine
  Shift-Overflow); `store_se` lehnt/warnt, wenn das BER-Tag nicht in
  `TNG_KEY_TYPE` passt (keine stille `static_cast`-Trunkierung → keine
  SE-Misrouting).

### Fuzzing + Sanitizer-CI

- **libFuzzer-Targets** (`tests/fuzz/`, nur mit `ISO8583_BUILD_FUZZERS=ON`):
  `fuzz_unparse` (F1), `fuzz_spec` (F2), `fuzz_serialize` (F3),
  `fuzz_header` (F4), `fuzz_tlv` (F5) + `fuzz_codec`.
- **CI-Matrix:** pro PR `debug` + `debug-asan` (MSVC & Linux/clang) + `tsan`
  (Linux/clang); **nightly** Fuzz-Soak (alle Targets) + Cross-Toolchain-
  Verdict-Diff (EBCDIC-Tabellen).
- **MSVC-ASan:** `parserTable()` ist jetzt ein bewusst ge-leaktes
  Prozess-Lebenszeit-Singleton — MSVC-ASan (Debug CRT) faultet sonst beim
  STL-`unordered_map`-`atexit`-Teardown; die Tabelle wird nie dealloziert
  (OS räumt beim Prozessende auf), die Logik bleibt unverändert.

### Sonstige Bugfixes

- **WLP-FO-Timestamp-Breite:** `getFormattedTimestamp()` erzeugt jetzt eine
  fixe 26-Zeichen-Timestamp (Subsekunden via `duration_cast<microseconds>` +
  `setw(6)`); rohe 100-ns-Clock-Ticks + `setw(4)` lieferten auf Windows
  24–29 Zeichen und einen ~10 % `Timestamp format error`-Flake.
- **`ISOMessage`-Sicherheitsnetz:** alle Standard-Ausnahmen ohne
  `[ISO8583]`-Präfix aus `parse`/`unparse` werden mit
  `[ISO8583] ISOMessage::parse|unparse: …` neu geworfen (keine rohen
  `std::system_error`/`std::stoi` entweichen).

### ABI-Hinweis

`ISOBaseParser` (public) bekommt das neue Mitglied `strict_` →
**Shared-Library-Konsumenten müssen neu kompiliert** werden. Es werden keine
bestehenden Symbole entfernt oder umbenannt; alle neuen Zugänge sind
additive.

## 0.2.1

### Bugfix: `!merge`-Tag bei Sequenz-Definitionen ging beim Vorverarbeiten verloren

Felder, die über `!use` auf eine `!merge`-Definition referenzieren, deren Wert
eine **Sequenz** ist (z. B. `bmp_35: !merge` → Liste aus `!template`-/
`scalar`-Einträgen), wurden nicht mehr expandiert. Die Definitions-Extraktion
verwendet eine gleichbaum-interne `merge_with()` des rapidyaml-Parsers
(v0.15.2), die anschließend das `!merge`-Val-Tag des Zielknotens leerstellt —
obwohl `has_val_tag()` weiter `true` meldet. Map-basierte Definitionen waren
nicht betroffen, deshalb fiel der Fehler nur bei sequenziellen `!merge`-
Definitionen auf. Die ungeexpandierte Sequenz erreichte den SpecDecoder,
deren erstes Element ohne `format` (leer) blieb: fünf `<dummy>`-WARNs gefolgt
von `ERR Unbekannte Format/Encoding-Kombination`.

Behoben, indem das Val-Tag nach der Extraktion aus dem (weiterhin lesbaren)
Quellknoten wiederhergestellt wird, bevor der Self-Merge es leeren kann.
Ergänzend ein Regressionstest, der das reale Muster (`!merge`-Sequenz-
Definition in einer `!include_files`-Datei, per `!use` referenziert)
abdeckt. Keine Änderung an bestehenden Specs nötig.

## 0.2.0

### Neu: hexadezimale Tag-Notation für BER-TLV `children`

Bei `tlv: {ber: true}` werden `children`-Schlüssel jetzt standardmäßig als
**hexadezimal** interpretiert (`"9F26"`, `"5A"`, `"1A"`), passend zur in
EMV Book 3 / ISO 7816 üblichen Schreibweise. Fix-Format-TLV (Mastercard/
Visa, `tag_bytes`/`len_bytes` gegeben) bleibt unverändert **dezimal**
(SE-Nummern, z.B. `"26"`) — keine Breaking Change für bestehende Specs.
Ein explizites `"0x"`-Präfix (`"0x1A"`) erzwingt hexadezimal unabhängig vom
TLV-Modus. Ungültige Schlüssel werfen jetzt eine klare, positionsgenaue
Fehlermeldung statt einer rohen `std::stoi`-Exception.

### Neu: `children`-Beschreibungen tatsächlich wirksam

Bisher wurden `format`/`description`/`encoding`-Angaben in einem TLV-
`children`-Block vollständig ignoriert — jedes SE/Tag wurde beim Dekodieren
immer als `BinaryField` mit generischer Beschreibung `"SE<n>"` gespeichert,
unabhängig davon, was in der Spec deklariert war. Ab jetzt wird zumindest
`description` korrekt übernommen. `format`/Typisierung pro Tag bleiben
bewusst zurückgestellt (größerer Eingriff, separate Entscheidung) — jedes
SE/Tag wird weiterhin als `BinaryField` dekodiert.

### Bugfix: dangelnde `string_view` bei TLV-Beschreibungen

Beim Umsetzen der description-Propagierung wurde ein **vorbestehender**
Speicherfehler gefunden und behoben: `ISOComponent::description()` speichert
nur eine `nonstd::string_view` (keine eigene Kopie) — der bisherige
generische `"SE" + se_num`-Fallback übergab dafür ein temporäres
`std::string`, das sofort nach dem Aufruf zerstört wurde. In der Praxis
meist unauffällig (kurze Strings landen typischerweise in der Small-String-
Optimization und werden nicht sofort überschrieben), aber echtes
Undefined Behavior — mit AddressSanitizer verifiziert behoben. `ISOTLVParser`
hält jetzt selbst langlebigen Speicher für sowohl deklarierte als auch
generierte Fallback-Beschreibungen vor.

### Unter 0.2.0: yaml-cpp → rapidyaml-Migration

Der komplette YAML-Spec-Ladepfad (`SpecDecoder::loadFromYaml` und verwandte
Funktionen) wurde von [yaml-cpp](https://github.com/jbeder/yaml-cpp) auf
[rapidyaml](https://github.com/biojppm/rapidyaml) (`ryml`) umgestellt.
Gründe: 100%ige Abdeckung des offiziellen YAML-Testsuites, keine bekannten
Schwachstellen, spürbar bessere Ladeperformance (siehe unten).

### ⚠️ Breaking Change: `!include_files` benötigt jetzt einen `---`-Trenner

yaml-cpp tolerierte stillschweigend mehrere YAML-Dokumente in einer Datei
**ohne** `---`-Trenner dazwischen (z.B. eine `!include_files`-Sequenz direkt
gefolgt vom eigentlichen Spec-Inhalt). Das ist nach YAML 1.2 spezifikations-
widrig, und rapidyaml (spezifikationskonform) akzeptiert es zu Recht nicht
mehr.

**Betrifft dich, falls** eine deiner Spec-Dateien `!include_files` am
Dateianfang nutzt. Migration: `---` zwischen die `!include_files`-Sequenz und
den restlichen Inhalt einfügen.

Vorher (funktionierte nur mit yaml-cpp):
```yaml
!include_files
- common_definitions.yml
spec: "My Spec"
encoding: ebcdic
fields:
  "000": !use mti_field
```

Nachher (spezifikationskonform, funktioniert mit beiden Bibliotheken):
```yaml
!include_files
- common_definitions.yml
---
spec: "My Spec"
encoding: ebcdic
fields:
  "000": !use mti_field
```

Ein einmaliger `grep -rl '^!include_files$' *.yml` über den eigenen
Spec-Bestand findet alle betroffenen Dateien.

### Neu: globale ryml-Fehlerbehandlung

ryml ruft bei Parse-/Validierungsfehlern standardmäßig `std::abort()` auf,
nicht etwa eine C++-Exception. Damit `SpecDecoder::loadFromYaml()` weiterhin
zuverlässig einen fangbaren `std::runtime_error` wirft (wie dokumentiert und
vom Rest der Bibliothek erwartet), installiert `libiso8583` beim ersten
Preprocessing-Aufruf **einmalig, für den gesamten Prozess** eigene
ryml-Fehler-Callbacks (`ryml::set_callbacks`).

**Betrifft dich, falls** deine Anwendung *zusätzlich, unabhängig von
libiso8583* direkt mit rapidyaml arbeitet und dabei eigene
Fehler-Callbacks setzt: `libiso8583` überschreibt diese beim ersten eigenen
Ladevorgang. Für die meisten Fälle unkritisch (Exceptions statt `abort()`
sind für eingebettete Bibliotheken i.d.R. ohnehin die gewünschte Wahl), aber
gut zu wissen, falls es zu unerwartetem Verhalten in eurer eigenen
ryml-Nutzung kommt.

### Neu: Rekursionstiefenschutz + Erkennung zirkulärer `!use`-Referenzen

Vorher konnte eine extrem tief verschachtelte oder versehentlich zirkuläre
(`!use`-Kette, die auf sich selbst zurückverweist) Spec-Datei zu einem
Stack Overflow (hartem Absturz) statt einer sauberen Fehlermeldung führen.
Beides wirft jetzt einen `std::runtime_error` mit klarer Beschreibung.

### Performance

Gemessen an einer ~65-Felder-Spec (Release-Build, gleiche Methodik vorher/nachher):

| Modus | vorher (yaml-cpp) | nachher (ryml) |
|---|---|---|
| `loadFromYaml` (mit Positions-Tracking) | ~1,28 ms | ~750–800 µs (**~1,6–1,7×**) |
| `loadFromYaml` (ohne Positions-Tracking) | ~1,34 ms | ~580–680 µs (**~2×**) |
| `loadFromYamlCached`-Treffer (Standard) | ~1,45 µs | ~1,0–1,2 µs (**~1,3×**) |
| `loadFromYamlCached`-Treffer (`TrustUntilInvalidated`) | ~0,35 µs | ~0,25–0,27 µs (**~1,3×**) |

### SourceMap: Zeilennummern → Knoten-Identität

Fehlerpositionen (z.B. "welche Datei/Zeile enthält das ungültige `length:
-5`?") werden jetzt über eine tree-interne Knoten-Identität statt über
Zeilennummern im prozessierten Dokument nachverfolgt. Das ist präziser als
vorher — insbesondere bei Fehlern innerhalb einer über `!use` aus einer
*anderen* Datei referenzierten Definition, wo die alte, zeilennummer-basierte
Zuordnung ungenau werden konnte.

**Betrifft dich, falls** du `.smap`-Sidecar-Dateien einer *älteren*
`libiso8583`-Version im Dateisystem liegen hast: Diese werden automatisch als
veraltet erkannt (über ein `format_version`-Feld) und beim nächsten Laden
transparent neu erzeugt — keine manuelle Aktion nötig, aber der erste
Ladevorgang nach dem Upgrade ist für betroffene Dateien einmalig etwas
langsamer (baut die Sidecar neu auf).

### Sonstiges

- `yaml-cpp` ist keine Abhängigkeit mehr (weder Build noch vcpkg-Manifest).
- Neue Abhängigkeit: `ryml` (rapidyaml), Version ≥ 0.15.2.
- `SpecPreProcessor::preprocessFile()` (unbenutzte, nicht-öffentliche
  Altlast) wurde entfernt.
