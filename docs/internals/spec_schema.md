# Normative Spec-Schema-Referenz (YAML)

> **Zweck dieses Dokuments:** Diese Seite ist die *normative* Referenz für
> die YAML-Spezifikationsdateien, die `iso8583::spec::SpecDecoder` lädt.
> Sie richtet sich an Menschen **und** an KI-Agenten, die Spec-Dateien
> generieren: Wer alle Regeln hier einhält, erhält eine ladefähige Spec;
> wer eine der fail-closed-Regeln verletzt, bekommt einen positionierten
> `SpecValidationError` (nie ein abstrakter Standard-Exception).
>
> Vertiefende Hintergrundtexte: [yaml_format.md](yaml_format.md)
> (Dokumentation aller Direktiven/Features) und [encoding.md](encoding.md)
> (Encoding-System und EBCDIC-Orakel-Pin).

## 1. Dokumentgerüst (Root)

Eine Spec-Datei ist eine YAML-Dokumentensammlung. Das **letzte** Dokument
enthält die eigentliche Spec; vorherige Dokumente dürfen
`!include_files` sein.

```yaml
!include_files          # optional; MUSS das erste Dokument sein
- common_definitions.yml
---                     # Dokumenttrenner ist PFLICHT nach !include_files
spec: "My Network"      # PFLICHT, String – Spec-Name
encoding: ascii         # optional: ascii | bcd | ebcdic | binary
strict: true            # optional, Default true (false = Legacy-Mapping)
strict_length: false    # optional, Default false (Opt-in: Unterlängen-Prüfung, s. §9)
header: 93              # optional, int – N-Byte-Netz-Header vor dem Body
definitions:            # optional – wiederverwendbare Bausteine (-> !use)
  pan_field: { type: scalar, format: llchar, length: 19 }
fields:                 # PFLICHT, nicht-leere Map
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": !use pan_field
```

**Root-Schlüssel:**

| Schlüssel | Typ | Pflicht | Bedeutung |
|---|---|---|---|
| `spec` | string | ja | Name der Spec (Introspection: `ISOSpec::name()`) |
| `encoding` | `ascii` \| `bcd` \| `ebcdic` \| `binary` | nein | globales Encoding (Auflösung s. §7) |
| `strict` | bool | nein | Default `true`; `false` = Legacy-`'.'`/`'?'`-Mapping statt positioned Throw (s. §8) |
| `strict_length` | bool | nein (Default `false`) | Opt-in (0.6.2): Root-Default für die Unterlängen-Prüfung bei fester Länge — Felder ohne eigenen `strict_length`-Key erben diesen Wert (s. §9) |
| `bcd_pad` | `right_zero` \| `right_f` \| `left_zero` | nein | (0.7.1, FR-7): Root-Default für das BCD-Padding bei ungerader Ziffernzahl — gilt für alle Felder mit BCD-Nutzdaten ohne eigenen `bcd_pad`-Key (§3 „BCD-Padding"); andere Felder bleiben unberührt |
| `header` | int | nein | N-Byte-Netz-Header vor dem Nachrichtenkörper; `0`/fehlt = kein Header (`ISOSpec::hasHeader()`/`headerSize()`) |
| `definitions` | map | nein | benannte Feld-Bausteine für `!use` |
| `fields` | map | ja | **nicht-leere** Map `DE-Schlüssel → Feld-Deklaration` |

**Root-Regeln (fail-closed):**

- `!include_files` (wenn vorhanden) muss das **erste** Dokument sein und
  wird vom Loader **vor** dem `---`-Trenner erwartet. Fehlt der Trenner:
  positionierter `SpecValidationError`.
- `fields` muss eine **nicht-leere Map** sein. Leere Maps, Sequenzen
  oder nicht-numerische DE-Schlüssel werden verworfen.
- Jeder DE-Schlüssel ist eine (optional mit führenden Nullen versehene)
  Zahl: `"000"` = MTI, `"001"` = Primär-Bitmap, `"002".."192"` = Daten-
  Elemente (DE 65–128 Sekundär-Bitmap, 129–192 Tertiär-Bitmap).
- Include-Sandbox (Default): `!include_files`-Einträge, die außerhalb
  des Verzeichnisses der Top-Level-Spec auflösen (`../`, absolute/UNC-
  Pfade, Symlink-Escape), werden **abgelehnt** (fail-closed).

## 2. Feld-Deklaration

Jeder Wert in `fields` ist eine Map (oder `!use`/`!merge`):

| Schlüssel | Typ | Pflicht | Bedeutung |
|---|---|---|---|
| `type` | `scalar` \| `nested` | nein (Default `scalar`) | `nested` = Sub-Nachricht (`Message`), benötigt `children` |
| `format` | string | ja (außer `nested` ohne `children`… siehe §6) | eine der Formate aus §3 |
| `length` | int | ja, **außer** `bitmap`/`nop`/`unused`; bei `remaining` **stets** Pflicht (0.6.0) | fixe Länge **oder** Maximum (variablen Formate/`remaining`). **BCD-Felder: `length` = Ziffernzahl** (1 Byte = 2 Ziffern). **`bitmap`: `length` = Bitmap-Größe in Bytes** (`8` = nur Primär, `16` = Primär + Sekundär, `24` = + Tertiär) — sie bestimmt, ob die Sekundär-Bitmap gelesen wird, s. §3 „Bitmap-Felder" (0.8.0, FR-9) |
| `secondary` | `auto` \| `always` | nein (Default `auto`) | (0.8.0, FR-9a): nur `format: bitmap` — `always` schreibt beim Bauen **immer** die Sekundär-Bitmap (Bit 1 + 16 Byte), auch ohne Feld > 64; erfordert `length >= 16`. Details: §3 „Bitmap-Felder" |
| `encoding` | `ascii` \| `bcd` \| `ebcdic` \| `binary` | nein | feldweises Override über das globale Encoding (§7) |
| `prefix_encoding` | `ascii` \| `bcd` \| `ebcdic` \| `binary` | nein | (0.7.0, FR-6): Encoding des **Längenpräfixes**, unabhängig vom `encoding` (Nutzdaten). Default = `encoding`. Nur auf variablen `*char`/`*num`-Formaten (L-/LL-/LLL-/LLLL-Präfix); Breiten-/Zählregeln und die 25 verfügbaren Kombinationen s. §3 |
| `bcd_pad` | `right_zero` \| `right_f` \| `left_zero` | nein | (0.7.1, FR-7): Padding-Nibble bei gepacktem BCD mit **ungerader** Ziffernzahl; nur bei BCD-**Nutzdaten** (`numeric`, `amount`, `*char`, `*num`, `remaining`, TLV-Kinder), nie am Längenpräfix. Default `right_zero` (Legacy); überschreibt den Root-Default. Details: §3 „BCD-Padding" |
| `description` | string | nein | Beschreibung (Introspection + Dump); bei `sensitive`-Feldern die einzige sichtbare Info im Dump |
| `sensitive` | bool | nein (Default `false`) | PCI-Masking: Wert wird in `dump()`/`operator<<` als `***` gerendert; `value()`/`to_json()` bleiben unmasked. Bei Containern: auf alle Kinder/Tags erbt |
| `scale` | int ≥ 0 | nein | nur `format: amount`: Standard-ISO-8583-Form (nackte Ziffern, deklarierte Skala, keine Währung im Feld); ohne Key jPOS-Form (§3) |
| `sign` | bool | nein (Default `false`) | nur `format: amount` mit `scale`, nicht `bcd`: führendes Vorzeichenzeichen `C`/`D`/`+`/`-`; `length` zählt es mit |
| `strict_length` | bool | nein | Opt-in (0.6.2): zu kurzer Wert bei fester Länge wird beim Serialisieren abgelehnt (§9); überschreibt den Root-Default |
| `tlv` | map | nein | `tag_bytes`/`len_bytes` (fester TLV) oder `ber: true` (EMV-BER-TLV), nur mit `type: nested` (§6) |
| `bitmap` | map `{ length: N }` | nein | (0.9.0, FR-12): nur `type: nested` (Nicht-TLV): Container mit **Bitmap-Kopf** von `N` Byte (1..16); `children` ist dann eine Map **Bit-Nummer → Kind**, Bit 1 ist ein normales Kind. Details: §6 |
| `pack` | `nibble` | nein | (0.9.0, FR-13): nur `type: nested` (Nicht-TLV) mit `children`-Liste aus `numeric`/`bcd`-Kindern fester Länge: die Kinder bilden einen dichten **Ziffern-Strom** (mehrere Kinder pro Byte); Padding nach `bcd_pad` am Container. Details: §6 |
| `children` | list \| map | ja bei `type: nested` | **Liste** = feste Subfelder (Positionsreihenfolge); **Map** = TLV-Modus (Schlüssel = SE-Nummer bzw. Hex-Tag) oder mit `bitmap:` Bit-Nummer → Kind |

**Minimales Feld:** `"003": { format: numeric, length: 6 }` — alles
andere ist optional; `description` wird für nachvollziehbare Specs
empfohlen.

## 3. Formate und die Format×Encoding-Matrix

Formate (YAML-Schreibweise) und ihre Dispatch-Einträge. Die Matrix
entspricht exakt der Dispatch-Tabelle in `src/_spec.cc`
(`parserTable()`); ein **fehrender Eintrag = Ladefehler**
(`Unbekannte Format/Encoding-Kombination …`), nicht Stille-
Default-Auswahl.

**Zuordnung der Laufzeit-Typen (Introspection `SpecFieldFormat::type`):**

- Text-Formate (`numeric`/`char`/`nopad_char` + `remaining` mit Text-
  Encoding) → `OpaqueField` (`std::string`)
- `amount` mit Text-Encoding → `AmountField` (`std::string`, Wert = jPOS-
  ISOAmount-Wire-String; s. Hinweis unten)
- `binary`-Formate + `remaining` ohne Encoding/`binary` → `BinaryField`
  (`std::vector<uint8_t>`; set via **großgeschriebene Hex-Zeichenkette**)
- `bitmap` → `Bitmap` (auto-berechnet, nie manuell setzen)
- `nop`/`unused` → Platzhalter, verbraucht keine Bytes
- `type: nested` → `Message`

| Format | `ascii` | `bcd` | `ebcdic` | `binary`/keine |
|---|---|---|---|---|
| `numeric` | ✔ (IFA_NUMERIC) | ✔ (IFB_NUMERIC) | ✔ (IFE_NUMERIC) | ✘ |
| `amount` (0.6.0) | ✔ (IFA_AMOUNT) | ✔ (IFB_AMOUNT) | ✔ (IFE_AMOUNT) | ✘ |
| `char` | ✔ | ✘ | ✔ | ✘ |
| `nopad_char` | ✔ | ✘ | ✔ | ✘ |
| `l*`-Varianten (`lchar`, `llchar`, `lllchar`; `ascii` zusätzlich `llllchar`) | `lchar`–`llllchar` ✔ | `lchar`–`lllchar` ✔ | `lchar`–`lllchar` ✔ | ✘ (s. Hinweis unten) |
| `lnum` / `llnum` | ✔ | ✔ (0.7.0: `IFB_LNUM`/`IFB_LLNUM`) | ✔ (`IFE_LNUM`/`IFE_LLNUM`) | ✘ |
| `binary` (fix) | ✘ | ✘ | ✔ (HEX_EBCDIC, s. Hinweis) | ✔ (roh) |
| `lbinary`/`llbinary`/`lllbinary` | ✔ (Prefix ASCII-Ziffern) | ✔ (Prefix BCD) | ✔ (Prefix EBCDIC) | ✔ (Prefix Big-Endian-Bytes, **nicht** neutral!) |
| `llllbinary` | ✔ (0.6.0, FR-3: `IFA_LLLLBINARY`) | ✘ | ✔ (0.6.0: `IFE_LLLLBINARY`) | ✘ |
| `bitmap` | encoding-neutral (roh) | — | — | — |
| `nop` / `unused` | encoding-neutral (roh) | — | — | — |
| `remaining` | ✔ (0.6.0, OpaqueField) | ✔ (0.6.0, OpaqueField) | ✔ (0.6.0, OpaqueField, Text!) | ✔ (roh, BinaryField) — s. §4 |
| `bertlv` (`l`/`ll`/`lll`/`llll` + `bertlv`) | encoding-neutral (BER-TLV) | — | — | — |

**Wichtige Nuancen:**

- **`format: binary` unter `ebcdic`** ist **kein** Text: der Parser
  verwendet `HEX_EBCDIC` (2 EBCDIC-Hex-Zeichen pro Byte →
  `BinaryField`). Text in einer EBCDIC-Spec ist immer `char`/`numeric`.
- **`remaining` + `ebcdic`** dagegen **ist** Text (`OpaqueField`):
  `remaining` folgt der *Daten*-Codec-Tabelle wie `char`/`numeric`
  (IBM-1047, orakelgepinnt).
- **`l*binary` ohne/`binary`-Encoding**: Die Längenpräfix-Bytes werden
  als **Big-Endian-Bytes** gelesen (Encoding des Präfix = `binary`),
  die Daten bleiben roh. `lbinary` u. a. sind **nicht** encoding-neutral.
- **`format: amount` (0.6.0)** → `AmountField` (jPOS-`ISOAmount`-
  Konvention): Der Wire-Wert ist `zeropad3(Währungs-Ziffercode)` +
  1-stellige Skala + `zeropad12(Betrag)`, z. B. EUR 19.99 →
  `"978200000001999"`. `length` ist die Zeichenzahl (üblich: 16).
  Typisierte Accessors (`currency()`, `minorUnits()`, `amount()`)
  finden sich auf `iso8583::AmountField`.
  **Optionaler Key `scale: N`** (nur auf `format: amount` gültig, Ganzzahl
  ≥ 0; sonst positionierter `SpecValidationError`): Ohne `scale:` gilt die
  jPOS-Form (Default, unverändert). Mit `scale: N` gilt die **Standard-
  ISO-8583-Form** (z. B. DE 4): Der Wire-Wert besteht aus `length` nackten
  Ziffern, die Skala ist das deklarierte `N`, die Währung steht **nicht** im
  Feld (`currency()` = `nullptr`; sie liegt netzwerkseitig in DE 49).
  Beispiel: `"004": { format: amount, length: 12, scale: 2 }`,
  Wire `"000000019990"` → `minorUnits() == 19990`, `readable_value() ==
  "199.90"`. Introspektion: `SpecFieldInfo::amount_scale`.
  **Optionaler Key `sign: true`** (nach 0.6.0; nur zusammen mit `scale:`, nicht
  mit `encoding: bcd`, nur auf `format: amount`; sonst positionierter
  `SpecValidationError`): Der Wire-Wert beginnt mit einem Vorzeichenzeichen
  `C`/`+` (positiv) oder `D`/`-` (negativ), danach nackte Ziffern; `length`
  zählt das Vorzeichenzeichen mit (DE 28–31 „x+n 8“ → `length: 9`). Beispiel:
  Wire `"D00000150"` bei `scale: 2` → `minorUnits() == -150`,
  `readable_value() == "-1.50"`, `isNegative() == true`. Introspektion:
  `SpecFieldInfo::amount_signed`.
- **BCD-Semantik:** Bei allen BCD-Formaten ist `length` die
  **Ziffernzahl** (Präfixe ebenso: ein `ll`-Präfix in BCD trägt die
  *Ziffernzahl*, nicht die Bytezahl). Nibbles ≥ 0xA werden legacy als
  `'0' + n` abgebildet (nicht validiert, auch nicht im strict-Modus —
  BCD-Daten sollten nur Ziffern enthalten): `A`→`:`, `B`→`;`, `C`→`<`,
  **`D`→`=`**, `E`→`>`, `F`→`?`. Das Kodieren bildet diese Zeichen
  verlustfrei auf dasselbe Nibble zurück (Roundtrip) — so liefert ein
  Track-2-Trennzeichen `D` ein `=` (z. B. `4524720000000000=4912…`). Bei **ungerader** Ziffernzahl
  wird das letzte, ungenutzte Low-Nibble des abschließenden Bytes mit **`0`**
  gefüllt (nicht `F`): der Wert `123` wird zu BCD `12 30`; beim Decode mappt
  dieses Nibble wieder auf `0`.
- `type: scalar` + `bertlv`-Format erzeugt zur Laufzeit eine
  `Message`, deren Kind-Schlüssel die rohen BER-Tag-Werte sind
  (z. B. `0x9F26` → Key `9F26` bei int32-Keys).

### Längenpräfix-Encoding (`prefix_encoding`, 0.7.0, FR-6)

Der optionale Feld-Key `prefix_encoding:` entkoppelt das Codec des
**Längenpräfixes** vom Codec der **Nutzdaten** (`encoding:`).
VISA-BASE-I-Situation: BCD-/Binär-Längenbytes vor ASCII-/EBCDIC-
Nutzdaten (z. B. DE 2: `{ format: llnum, encoding: bcd,
prefix_encoding: bcd }`; DE 48: `{ format: lllchar, encoding: ascii,
prefix_encoding: binary }`).

- **Default = `encoding`:** ohne den Key ist das Präfix im Encoding
  der Nutzdaten codiert (reine Additivität — Bestandsspecs ändern
  sich nicht). Bei encoding-neutralen Formaten ist das Präfix-Encoding
  `""`.
- **Breiten-/Zählregeln je Präfix-Encoding:**

  | Präfix-Encoding | Breite | Zählung |
  |---|---|---|
  | `ascii` | `L`-Zahl = 1 Byte (L=1, LL=2, LLL=3, LLLL=4) | ASCII-Ziffern = Bytes |
  | `bcd` | **1 Byte für L/LL, 2 Byte für LLL/LLLL** (je 2 BCD-Ziffern/Byte, aufgerundet: `parsed_length = (L + 1) >> 1`) | *Ziffern* (1 Byte = 2 Ziffern); BCD-Zeichen sind Dezimalziffern: `0x16` = sechzehn, `0x10` = zehn |
  | `ebcdic` | `L`-Zahl = Bytes | EBCDIC-Ziffern (IBM-1047, orakelgepinnt) |
  | `binary` | `L`-Zahl = **Bytes** (L=1, LL=2, LLL=3, LLLL=4), Big-Endian | Bytes — **außer vor BCD-Nutzdaten** (0.8.0, FR-8): dort **Ziffern** (Einheit der Nutzdaten, s. unten) |

- **Verfügbare Kombinationen (30, Fail-closed beim Laden):**
  - 16× `l*char`: L/LL/LLL(/LLLL)/× `ascii`/`ebcdic`-Nutzdaten ×
    `bcd`/`binary`-Präfix (LLLL nur bei `ascii`)
  - 8× `lnum`/`llnum` × `ascii`/`ebcdic`-Nutzdaten × `bcd`/`binary`-Präfix
  - 3× **Identitätslücken** (Präfix == Nutzdaten-Encoding, 2-teiler
    Dispatch-Key): `lnum`/`llnum` mit `bcd` (`IFB_LNUM`/`IFB_LLNUM`)
    und `llnum` mit `ebcdic` (`IFE_LLNUM`)
  - 5× **BINARY-Präfix vor BCD-Nutzdaten** (0.8.0, FR-8): `lchar`/`llchar`/
    `lllchar`/`lnum`/`llnum` × `encoding: bcd` × `prefix_encoding: binary`
    (3-teiliger Dispatch-Key `FORMAT|BCD|BINARY`). `llllchar` und `lllnum`
    gibt es mit BCD nicht (keine BCD-Identität) und bleiben abgelehnt.
- **Binärpräfix vor BCD-Nutzdaten (0.8.0, FR-8):** Das Präfix ist ein
  Big-Endian-**Binärwert** (L=1 → 1 Byte, LL → 2, LLL → 3) in der Einheit
  der Nutzdaten, also **Ziffern** — die Daten belegen `ceil(Ziffern / 2)`
  Byte, das Padding bei ungerader Ziffernzahl folgt `bcd_pad` (das Präfix
  bleibt davon unberührt). `length` ist das Maximum in Ziffern. Beispiel
  VISA BASE I (L=1, `bcd_pad: left_zero`):

  | Feld | Wire (Hex) | Deutung |
  |---|---|---|
  | DE 2 | `10` `45 24 72 00 00 00 00 00` | `0x10` = **16 Ziffern** → 8 Byte |
  | DE 32 | `06` `40 90 23` | 6 Ziffern → 3 Byte |
  | DE 35 | `25` `04 52 47 20 …` (19 Byte) | `0x25` = **37 Ziffern** → 19 Byte, führendes `0`-Nibble als Padding |

  ```yaml
  "002": { format: lnum,  encoding: bcd, prefix_encoding: binary, bcd_pad: left_zero, length: 19 }
  "035": { format: lchar, encoding: bcd, prefix_encoding: binary, bcd_pad: left_zero, length: 37 }
  ```

  Vorbestehende Eigenheit (alle Präfix-Encodings, nicht geändert): ein
  Präfixwert `0` wird beim Decode wie das deklarierte Maximum gelesen.
- **Fail-closed-Regeln** (positionierte `SpecValidationError`, s. §8):
  - Value-Whitelist `ascii`/`ebcdic`/`bcd`/`binary`
  - nur variable `*char`/`*num`-Formate; bei `*binary`/`bertlv`
    bestimmt `encoding:` bereits das Präfix-Codec, `amount`/`remaining`/
    fixbreit Formate haben kein Längenpräfix
  - die (format, encoding, prefix_encoding)-Kombination muss in der
    Dispatch-Tabelle existieren (z. B. `llchar|ascii` + `ebcdic`-Präfix
    ist bewusst nicht verfügbar)
  - **nicht** bei TLV-Kindern (fix, BERTLV oder constructed) und nicht
    als Root-Key
- **`!template` bleibt 2-argumentig** (`P(F, N)`); ein anderes
  Präfix-Encoding wird über `!merge` gesetzt:
  `{ !merge [ !template LL(CHAR, 37), { encoding: ebcdic,
  prefix_encoding: bcd } ] }`.
- **Introspektion:** `SpecFieldInfo::prefix_encoding` (effektives
  Präfix-Encoding; Key weggelassen → `encoding`, encoding-neutral →
  `""`). **ABI:** Layout-Änderung von `SpecFieldInfo` —
  Shared-Library-Consumer müssen neu kompiliert werden (0.7.0).

### BCD-Padding (`bcd_pad`, 0.7.1, FR-7)

Bei gepacktem BCD mit **ungerader** Ziffernzahl bleibt ein Nibble übrig.
Der optionale Key `bcd_pad:` legt fest, wo es steht und womit es gefüllt
wird (Beispielwert `123`):

| Wert | Wire | Bedeutung |
|---|---|---|
| `right_zero` (**Default**, Legacy) | `12 30` | Ziffern vorn, ungenutztes Low-Nibble `0` |
| `right_f` | `12 3F` | Ziffern vorn, ungenutztes Low-Nibble `F` |
| `left_zero` | `01 23` | führende `0`, Ziffern rechtsbündig |

- **Scope:** nur Felder mit BCD-**Nutzdaten**: `numeric`, `amount`,
  `l*char`, `l*num`, `remaining` mit `encoding: bcd` sowie typisierte
  TLV-Kinder (`numeric`/`char`/`amount`/`nopad_char` mit BCD). Das
  **Längenpräfix** ist nie betroffen (es bleibt BCD mit führender `0`,
  z. B. `0010` bei LLL); `binary`-Formate und TLV-Container haben kein
  BCD-Padding.
- **Root-Default:** `bcd_pad:` an der Spec-Wurzel (Message- und
  Field-only-Spec) gilt für alle Felder mit BCD-Nutzdaten ohne eigenen
  Key; ein Feld-Key überschreibt. Felder ohne BCD-Nutzdaten ignorieren den
  Root-Default (kein Fehler).
- **Default unverändert:** ohne `bcd_pad` (Feld und Root) ist die Wire
  byte-identisch zu 0.7.0 und das Padding-Nibble wird beim Decode **nicht**
  geprüft (ein `F`-Padding wird wie bisher toleriert).
- **Decode-Validierung (nur bei deklariertem `bcd_pad`):** ein vom
  konfigurierten Padding abweichendes Nibble (z. B. `F` bei `right_zero`)
  wirft im strict-Modus einen positionierten `std::runtime_error`
  (`BCD-Padding-Nibble weicht von 'bcd_pad' ab …`), nicht-strikt eine
  Warnung. Gerade Ziffernzahl hat kein Padding.
- **TLV-Kinder:** die TLV-Länge zählt Bytes. Mit deklariertem `bcd_pad` und
  **ungerader** `length` (Ziffern), die zur Byte-Länge passt
  (`(length + 1) / 2`), wird `length` als Ziffernzahl benutzt — nur so ist
  das Padding-Nibble von einer Ziffer unterscheidbar. Ohne passende
  `length` gilt 2 Ziffern/Byte (Legacy). `remaining|bcd` kennt die
  Ziffernzahl nie (immer gerade): das Padding-Nibble erscheint dort als
  Ziffer `0` (`right_zero`/`left_zero`) bzw. `?`-Nibble `F` (`right_f`).
- **Fail-closed** (positionierte `SpecValidationError`): ungültiger Wert
  (Whitelist `right_zero`/`right_f`/`left_zero`, case-insensitive);
  `bcd_pad` an einem Feld ohne BCD-Nutzdaten (ASCII/EBCDIC, `binary`,
  `bcd`-Präfix-only wie `lllbinary`, `nop`, `bitmap`); an einem
  TLV-Container oder constructed-Kind.
- **Introspektion:** `SpecFieldInfo::bcd_pad` (`"right_zero"` |
  `"right_f"` | `"left_zero"` bei BCD-Nutzdaten, sonst `""`).
  **ABI:** Layout-Änderung von `SpecFieldInfo` und
  `ISOFieldParserPtrBase` (neue Mitglieder) — Shared-Library-Consumer
  müssen neu kompiliert werden (0.7.1).
- **Codec-API (fortgeschritten):** `codec::BcdPad`; `codec::as<>`/`codec::to<>`
  haben ein zusätzliches Default-Argument `BcdPad pad = RIGHT_ZERO`
  (source-kompatibel).

### Bitmap-Felder (`length`, `secondary`, 0.8.0, FR-9)

Das Bitmap-Feld (`"001": { format: bitmap, length: N }`) ist
encoding-neutral (roh) und wird **nie** manuell gesetzt — die Bibliothek
berechnet es aus den gesetzten Feldern. Seine `length` ist die **Größe der
Bitmap in Bytes** und legt fest, welche Bitmap-Teile der **Decoder** liest:

| `length` | Decoder liest | Typischer Einsatz |
|---|---|---|
| `8` | nur die **Primär-Bitmap** (DE 1–64) | Nachrichten ohne Felder > 64 |
| `16` | Primär-Bitmap + (bei **Bit 1**) die **Sekundär-Bitmap** (DE 65–128) | Standardfall mit Sekundärfeldern |
| `24` | zusätzlich die Tertiär-Bitmap (DE 129–192) | selten |

- **Bit 1 + `length: 8` ist Fail-closed (strict, Default).** Zeigt Bit 1 eine
  Sekundär-Bitmap an, das Bitmap-Feld deklariert aber nur 8 Byte, werden die
  Folgefelder sonst um 8 Byte verschoben und fallen erst später mit
  irreführenden Fehlern auf (z. B. „BCD-Padding-Nibble weicht ab“,
  „Unverbrauchte Bytes am Pufferende“). Seit 0.8.0 wirft der Decoder
  stattdessen sofort einen positionierten `std::runtime_error`
  (`Bitmap @ Offset N: Bit 1 (Sekundär-Bitmap) ist gesetzt, … 'length: 16'
  setzen`); nicht-strikt (`strict: false`) bleibt die Legacy-Dekodierung mit
  Warnung.
- **Bauen:** die Bitmap-Größe folgt den gesetzten Feldern, **nicht** `length`
  (Felder ≤ 64 → 8 Byte ohne Bit 1; ein Feld > 64 → 16 Byte mit Bit 1). Ist
  `length` kleiner als das Gebaute (z. B. `length: 8` und DE 70 gesetzt), ist
  die Ausgabe mit derselben Spec nicht rückdekodierbar — seit 0.8.0 wirft
  `parse()` dafür im strict-Modus einen positionierten `std::runtime_error`
  (nicht-strikt: Warnung, Legacy-Ausgabe). **Regel:** `length` so wählen,
  dass sie alle Felder der Spec abdeckt (`16` sobald Felder > 64 vorkommen).
- **`secondary: always`** (nur auf `format: bitmap`, 0.8.0, FR-9a): erzwingt
  beim **Bauen** die Sekundär-Bitmap — Bit 1 gesetzt und 8 weitere Bytes,
  auch wenn **kein** Feld > 64 gesetzt ist (VISA BASE I: Primär-Bitmap mit
  Bit 1 + **leere** Sekundär-Bitmap `00 00 00 00 00 00 00 00`). Default
  `auto` = bisheriges Verhalten (byte-identisch zu 0.7.1; `length: 16`
  allein erzwingt **nichts**). Fail-closed beim Laden (positionierte
  `SpecValidationError`): ungültiger Wert, Key an einem Nicht-Bitmap-Feld,
  `always` mit `length < 16`.

  ```yaml
  "001": { format: bitmap, length: 16, secondary: always, description: "Bitmap (VISA)" }
  ```

- **Zusammenspiel mit dem Decode-Roundtrip:** Ein dekodiertes Message-Objekt
  trägt die Bitmap aus der Wire. `parser->parse(msg)` (Expert-API) schreibt
  sie 1:1 zurück; `msg->parse(msg)` **rekalkuliert** sie aus den gesetzten
  Feldern — eine leere Sekundär-Bitmap bleibt dabei nur mit
  `secondary: always` erhalten.
- Introspektion: `SpecFieldInfo::secondary_bitmap` (`"always"` | `"auto"` bei
  Bitmap-Feldern, sonst `""`). **ABI:** neues Mitglied in `SpecFieldInfo` und
  `ISOFieldParserPtrBase` (Layout-Änderung) — Shared-Library-Consumer müssen
  neu kompiliert werden (0.8.0).

## 4. `remaining` (0.6.0: encoding-aware)

`remaining` liest **alle restlichen Bytes des Eltern-Buffers** — kein
eigenes Längenpräfix. Seit **0.6.0** ist `remaining` **nicht mehr**
encoding-neutral:

1. **`length` ist Pflicht.** Fehlt `length`, wirft der Loader einen
   positionierten `SpecValidationError`
   (`'format: remaining' benötigt 'length' …`). Ohne Maximum würden
   sonst 0 Bytes dekodiert (Fail-closed).
2. `length` ist ein **Maximum (Clamp)**: längerer Payload wird
   gekürzt; übrige Bytes bleiben unkonsumiert (strict: Lade-/
   Decode-Fehler „Unverbrauchte Bytes", legacy: Warnung).
3. **Encoding-Auflösung wie bei allen anderen Formaten**
   (Feld-`encoding` > globales `encoding` > `""`):

   | aufgelöstes Encoding | Laufzeit-Typ | Verhalten |
   |---|---|---|
   | `""` oder `binary` | `BinaryField` | rohe Bytes (wie vor 0.6.0) |
   | `ascii` | `OpaqueField` | ASCII-Text |
   | `ebcdic` | `OpaqueField` | IBM-1047-Text (strict: Whitelist-Throw; legacy: `'.'`) |
   | `bcd` | `OpaqueField` | gepackte Ziffern (`length` = Ziffern!) |

4. **Strict-Propagation:** Das Parser-`strict`-Flag wirkt auch auf
   `remaining`-Konvertierungen (EBCDIC-Whitelist, s. encoding.md).
5. `remaining` gehört in der Praxis an die **letzte Position** des
   Eltern-Buffers (typisch: Schlussfeld eines `type: nested` Containers,
   z. B. BMP_061-Subfeld 15/POS-Postleitzahl). In **TLV-Kindern** ist
   `remaining` verboten (Whitelist, §6).
6. **Bauen (Encode) — seit 0.8.0 (FR-10a) auch als Kind eines `nested`-
   Containers und als Schlussfeld der Nachricht unterstützt.** Der Wert ist
   der Rest des Eltern-Puffers; die Länge ergibt sich beim Serialisieren
   (kein Präfix). Der Setter-Typ folgt dem aufgelösten Encoding (Tabelle
   oben): bei `""`/`binary` nimmt `set("55.1", "950500…")` einen
   **Hex-String in Großbuchstaben** (`BinaryField`), bei Text-Encodings
   einen Text (`OpaqueField`). `length` ist auch hier ein **Maximum**: ein
   längerer Wert wird im strict-Modus mit einem positionierten
   `std::runtime_error` (`Serialisierung zu groß … > Maximum …`) abgelehnt
   (nicht-strikt: Fehler-Log, Feld entfällt). Vor 0.8.0 war das Bauen eines
   binären `remaining`-Kindes defekt (Null-Zugriff statt Fehler).

```yaml
# Beispiel: EBCDIC-Spec mit remaining-Schlussfeld (0.6.0)
spec: "Rem Example"
encoding: ebcdic
fields:
  "000": { format: numeric,  length: 4 }
  "001": { format: bitmap,   length: 8 }
  "061":
    type: nested
    format: binary
    length: 26
    children:
      - { format: numeric, length: 1 }
      - { format: remaining, length: 10, description: "POS Postal Code" }
```

## 5. Direktiven

| Direktive | Verwendung | Regeln |
|---|---|---|
| `!include_files [a.yml, b.yml]` | Root, erstes Dokument | muss von `---` gefolgt werden; Sandbox s. §1 |
| `!use <name>` | Feldwert oder `definitions`-Referenz | referenziert einen Eintrag aus `definitions:`; Zyklen und unendliche Rekursion werden erkannt und verworfen |
| `!template P(F, N)` | z. B. `!template LL(CHAR, 19)` | erzeugt ein `llchar`-Format mit Länge 19; zulässige `F`: `CHAR`, `NUMERIC`, `BINARY`, … |
| `!merge [a, b]` | Feldwert: `{ !merge [ !template LLL(BINARY, 255), description: "ICC Data" ] }` | fusioniert Map-Einträge; Sequenz-Definitionen sind erlaubt (0.2.1-Fix) |
| `!include` | **deprecated** Alias von `!use` | erzeugt eine Warnung; neu `!use` schreiben |

## 6. Nested, TLV und BERTLV

**Plain nested (Liste = feste Positionsreihenfolge):**

```yaml
"061":
  type: nested
  format: binary          # Container-Format (Daten roh)
  length: 26
  children:
    - { format: numeric, length: 1 }
    - { format: remaining, length: 10, description: "POS Postal Code" }
```

Die Kind-Schlüssel sind die **Positionen** ab `0` (`"61.0"`, `"61.1"`); `nop`
als Kind hält eine Position frei. Jedes Kind belegt **ganze Bytes**.

**Bitmap-gesteuerter Container (`bitmap:`, 0.9.0, FR-12/FR-11):**

Bitmap-Unterfelder (VISA DE62/DE63/DE126: Bitmap + genau die Unterfelder, deren
Bit gesetzt ist) werden mit einem `bitmap:`-Block und `children` als **Map**
(Bit-Nummer → Kind) deklariert:

```yaml
"062":
  type: nested
  format: lbinary
  encoding: binary
  length: 255
  description: "Custom Payment Service Fields"
  bitmap: { length: 8 }              # Bitmap-Kopf (62.0): 8 Byte, Bits 1..64
  children:                          # Map: Bit-Nummer -> Kind
    "1": { format: char,    encoding: ebcdic, length: 1,  description: "62.1 ACI" }
    "2": { format: numeric, encoding: bcd,    length: 16, description: "62.2 Transaction Identifier" }
    "7": { format: char,    encoding: ebcdic, length: 26, description: "62.7 Purchase Identifier" }
```

- **Wire:** `Container-Präfix | Bitmap (length Byte) | gesetzte Kinder in Bit-Reihenfolge`.
  Bit `n` (1-indiziert, MSB des ersten Bytes = Bit 1) gehört zum Kind `n`.
- **Bit 1 ist ein normales Kind** — es gibt im Container **keine**
  Sekundär-Bitmap-Semantik (anders als beim Top-Level-`bitmap`-Feld, §3).
  Mehrstufige Bitmaps im Container (Sekundär-/Tertiär-Bitmap) werden nicht
  unterstützt. Damit ist auch VISA-62.1 (Authorization Characteristics
  Indicator) abbildbar (FR-11); ein eigenes `secondary: never` für die
  Top-Level-Bitmap gibt es nicht.
- **Adressierung:** Punkt-Notation `"62.7"`; der Kind-Key ist die Bit-Nummer.
  Die Bitmap-Komponente selbst liegt (wie bei Nachrichten) unter dem
  Sonderschlüssel `-1`, der Kind-Key `1` bleibt frei.
- **Decode:** nur Kinder mit gesetztem Bit werden dekodiert. Ein gesetztes Bit
  **ohne** Kind-Deklaration → strict: positionierter `std::runtime_error`
  (die Länge der Folgebytes ist unbekannt); nicht-strikt: Warnung und
  Abbruch der Container-Dekodierung. Abgeschnittene Kinder und Rest-Bytes
  sind strict ebenfalls Fehler.
- **Build:** die Bitmap wird aus den gesetzten Kindern berechnet (nie setzen).
  Ein gesetztes Kind ohne Bit-Deklaration → strict: Fehler (sonst Warnung, Kind
  wird ausgelassen). Die Bitmap hat immer genau `length` Byte (nicht aufgerundet).
- **Fail-closed beim Laden:** `bitmap` an einem Nicht-`nested`-Feld oder zusammen
  mit `tlv:`/`...bertlv`/`pack`; `length` fehlt oder nicht `1..16`; unbekannter
  Schlüssel im `bitmap:`-Block (z. B. `secondary`); `children` keine nicht-leere
  Map; Bit-Nummer nicht dezimal, `< 1` oder `> 8*length`; doppelte Bit-Nummer
  (`"1"` und `"01"`); Kind-Format `bitmap`/`nop`/`unused`.
- **Introspektion:** `SpecFieldInfo::container_bitmap_bytes` (`0` = kein
  Bitmap-Container), `children[i].key` = Bit-Nummer (aufsteigend sortiert).

**Nibble-Packing (`pack: nibble`, 0.9.0, FR-13):**

VISA-DE60 (Additional POS Information) besteht aus einzelnen **Ziffern**
(Nibbles), die sich Bytes teilen. Mit `pack: nibble` bilden die BCD-Kinder
einen **dichten Ziffern-Strom** ohne Byte-Grenzen dazwischen:

```yaml
"060":
  type: nested
  format: lbinary                    # Länge in BYTES
  encoding: binary
  length: 255
  pack: nibble
  # bcd_pad: right_zero              # optional (Container-Key oder Root-Default)
  children:                          # Liste; Position = Kind-Schlüssel
    - { format: nop }                # Platzhalter -> die Unterfelder heißen 60.1 ...
    - { format: numeric, encoding: bcd, length: 1, description: "60.1 Terminal Type" }
    - { format: numeric, encoding: bcd, length: 1, description: "60.2 Terminal Entry Capability" }
    # ... 60.3 .. 60.7 ...
    - { format: numeric, encoding: bcd, length: 2, description: "60.8 MOTO/ECI Indicator" }
```

- **Wire:** der Container ist `ceil(Summe der Ziffern / 2)` Byte lang;
  `12 34 05` = Ziffern `1,2,3,4,0,5`. Bei **ungerader** Gesamtziffernzahl des
  vollständigen Containers sitzt das Padding-Nibble nach `bcd_pad` (`right_zero`
  Default: `123` → `12 30`, `right_f`: `12 3F`, `left_zero`: `01 23`); mit
  deklariertem `bcd_pad` wird es beim Decode validiert (strict: Fehler).
- **Kürzere Container sind erlaubt:** fehlen am Ende Bytes, bleiben die
  restlichen Kinder ungesetzt (VISA-DE60 mit 5 statt 6 Datenbytes). Ein Kind
  darf aber nicht **mittendrin** abgeschnitten sein (strict: Fehler).
- **Build:** die gesetzten Kinder müssen eine **lückenlose Folge ab dem ersten
  Kind** bilden (Lücke → strict: Fehler); jeder Wert hat genau `length` Ziffern
  (`0`–`9`). Ein verkürzter Container mit **ungerader** Ziffernzahl wird beim
  Bauen abgewiesen (das Padding-Nibble wäre beim Decode nicht von einer Ziffer
  des Folgekinds unterscheidbar).
- **`wire_offset`/`wire_length`** der Kinder sind eine Byte-Näherung: Offset =
  Byte, in dem das Kind beginnt; Länge = Anzahl der berührten Bytes (zwei Kinder
  in einem Byte haben denselben Offset).
- **Kinder:** nur `format: numeric` + `encoding: bcd` + feste `length`
  (Ziffern `>= 1`) oder `format: nop` (Platzhalter ohne Ziffern); kein `bcd_pad`
  am Kind (es gehört an den Container). Das **Container**-`format` bleibt ein
  Byte-Format (`lbinary` o. Ä.).
- **Fail-closed beim Laden:** `pack` an einem Nicht-`nested`-Feld, mit `tlv:`/
  `...bertlv`/`bitmap`; Wert ungleich `nibble`; `children` keine nicht-leere
  Liste; Kind mit anderem Format/Encoding/ohne `length`; `bcd_pad` am Kind.
  Ohne `pack` bleibt jedes Kind ein ganzes Byte (`numeric|bcd, length: 1` →
  Byte mit Padding-Nibble) — unverändert.
- **Introspektion:** `SpecFieldInfo::pack` (`"nibble"` | `""`); `bcd_pad` meldet
  beim Container die effektive Variante.

**Fixer TLV (MC/Visa-Style), `tag_bytes`/`len_bytes`:**

```yaml
"048":
  type: nested
  format: lllchar
  length: 999
  tlv: { tag_bytes: 2, len_bytes: 2 }
  children:
    "26": { format: char, length: 10, description: "…" }   # dezimale SE-Nummern
```

`tag_bytes` akzeptiert 1–2, `len_bytes` 1–3 (seit 0.6.3, davor max. 2) —
jeweils für `ascii`/`ebcdic`/`bcd` (via `tlv.encoding` oder vererbt) und
mit/ohne `tcc`. Werte außerhalb (z. B. `len_bytes: 4`) werden zur Laufzeit
mit Warnung auf den Mastercard-Default (`tag_bytes`/`len_bytes` = 2/2, EBCDIC)
abgeleitet.

**BER-TLV (EMV), `tlv: { ber: true }`:**

```yaml
"057":
  type: nested
  format: lllbinary
  length: 999
  tlv: { ber: true }
  children:
    "9F26": { format: binary, length: 8,  description: "Application Cryptogram" }  # Hex-Keys
    "5A":   { format: binary, length: 10, description: "Application PAN" }
```

**BERTLV als Scalar-Format** (`056`-Style): `format: lllbertlv`
**ohne** `type: nested`, **ohne** `tlv:`, **ohne** `children` —
jede auftretende BER-Tag wird dekodiert (ISO/IEC 8825-1);
Kind-Schlüssel = rohe Tag-Werte (z. B. `9F26` als int32-Key).

**Container-Basis-Parser-Normalisierung (0.6.0, normativ):**

`type: nested`-Container sind mit **allen** Containerformaten nutzbar —
auch mit text-basierten (`char`/`numeric`/`nopad_char`, L-Präfix
`llchar`/`lllchar`/`llllchar` (ascii) sowie `remaining` + Text-Encoding).
Beim Parser-Bau normalisiert der Loader den Container-**Basis-Parser** auf
den binären Zwilling (wire-neutral: gleicher L-Zähler + Präfix-Encoding wie
deklariert; die Container-Daten bleiben Roh-Bytes und jedes Kind löst sein
eigenes Encoding auf):

1. `L* + CHAR/NUMERIC/NOPAD_CHAR` → `L*BINARY` (L-Zähler + Präfix-Encoding
   erhalten, z. B. `llllchar|ascii` → `llllbinary|ascii`, neu in 0.6.0).
2. `FIX + CHAR/NUMERIC/NOPAD_CHAR` → `BINARY|` (Roh-Bytes; Encoding
   explizit geleert — `BINARY|EBCDIC` würde die `HEX_EBCDIC`-
   Sondersemantik der skalaren EBCDIC-`binary`-Formate auslösen).
3. `REMAINING + Text-Encoding` → `REMAINING|` (Roh-Bytes).

Skalare Textfelder (nicht `nested`) bleiben string-basiert (Verhalten
unverändert). Die Introspektion (`ISOSpec::field`) meldet das
**deklerierte** Format (`format.type`, `prefix_digits` unverändert).
Vor 0.6.0 crashten solche Container mit `SIGSEGV` in `unparse()`/`parse()`
(string-basierter Basis-Parser empfing den `BinaryField`-Scratch des
Nested-Zweigs); manuell konstruierte nicht-binäre Container-Basen
(`ISONestedFieldParser`) werfen jetzt ein positioniertes
`std::runtime_error` (Fail-closed) statt SEGV.

**TLV-Kind-Whitelist (fail-closed beim Laden):**

- Erlaubte Kind-Formate: `binary`, `char`, `numeric`, `nopad_char`.
  Verboten: L-präfixierte Formate, `bitmap`, `remaining`, `nop`
  (die Länge liegt im Length-Feld des Frames — Widerspruch).
- Erlaubte Kind-Encodings: `ascii`, `ebcdic`, `bcd`, `binary`;
  Text-Formate (`char`/`numeric`/`nopad_char`) nur mit
  `ascii`/`ebcdic`/`bcd`.
- `children`-Keys: `ber: true` → **Hex** (`"9F26"`, `"5A"`);
  fixer TLV → **dezimale** SE-Nummern (`"26"`); ein `"0x1A"`-Präfix
  erzwingt in beiden Modi Hex.
- Undeklarierte Tags/SEs werden dekodiert (Fallback-Beschreibung
  `SE<n>` bzw. generischer Tag-Name), nie verworfen.

**Constructed-TLV-Kinder (0.6.4, normativ):**

Ein TLV-Kind, das einen **eigenen `tlv:`-Block** trägt (`tlv: { ber: true }`
oder `tlv: { tag_bytes, len_bytes }`), ist ein **constructed-Container**
(BER-Constructed-Bit `0x20`, ISO/IEC 8825-1): sein Wert ist selbst eine
Folge von TLVs — z. B. EMV-Tag `69` *Transaction Status Information*, das
Tags wie `63` *Result of EMV Application* enthält. Verhalten:

- **Decode:** das Kind wird rekursiv über seinen eigenen Sub-Parser in eine
  Sub-`Message` aufgeschlüsselt (keine flache Komponente). Die inneren TLVs
  sind über die bestehende Punkt-Notation adressierbar
  (z. B. `57.69.63`: DE57 → Tag `69` → Tag `63`).
- **Encode:** die Sub-`Message` wird über ihren (beim Decode angehängten)
  Parser zurückkodiert — Roundtrip byte-identisch. Fehlt das Kind (oder
  seine Parser-Zuordnung), gilt die übliche „SE fehlt“-Semantik
  (Warnung + Frame weglassen); ein Typ-Fehlmatch (z. B. `BinaryField`
  statt `Message`) wirft fail-closed.
- **Scope:** beide TLV-Formen (`ber: true` **und** fixer
  `tag_bytes`/`len_bytes`) nutzen denselben Codepfad (Policy-agnostisch);
  die Rekursionstiefe unterliegt der bestehenden ≤ 200-Ebene-Begrenzung.

```yaml
"057":
  format: lllbertlv
  length: 999
  children:
    "69":                                   # constructed-Kind (eigener tlv-Block)
      tlv: { ber: true }
      description: "Transaction Status Information"
      children:                             # optional: deklarierte innere Tags
        "63": { format: binary }
```

Regeln (Fail-closed beim Laden, positioniertes `SpecValidationError`):

- Ein constructed-Kind darf **keine** `format:`, `length:` oder `encoding:`
  deklarieren — das äußere TLV-Frame trägt Tag + Länge, diese Keys wären
  widersprüchlich.
- Der `tlv:`-Block des Kinds benötigt `ber: true` **oder**
  `tag_bytes`/`len_bytes`.
- Ohne `children:` ist das Kind ein *dynamischer* Container: die inneren
  TLVs werden dynamisch dekodiert (wie undeclarierter BERTLV-Container).
- Die eigenen `children:` des Kinds (Enkel-Tags) werden mit denselben
  Whitelist-Regeln **rekursiv** validiert.
- Ein constructed-Tag **ohne** `tlv:`-Block bleibt ein dynamischer
  `BinaryField`-Blob (Rohbytes) wie bisher — es gibt **keine** implizite
  Erkennung über das Constructed-Bit; nur explizit erklärte Container-Kinder
  werden rekursiv aufgeschlüsselt.
- `sensitive: true` auf dem Container-Kind verbreitet sich auf den
  Sub-Baum (PCI-Masking); der Strict-Modus propagiert auf den
  Kind-Sub-Parser.
- **Introspektion:** ein constructed-Kind meldet `is_nested = true`,
  `tlv_is_ber` nach seinem eigenen `tlv:`-Block, und seine
  `tlv_children` sind mit den deklarierten Enkel-Tags gefüllt.
- **Key-Typ:** 2-Byte-Sub-Tags (≥ `0x8000`) unterliegen der bestehenden
  `TNG_KEY_TYPE`-Regel — im Default-Build (int16) werden sie gewarnt und
  übersprungen (kein Fehlrouting), mit `ISO8583_BERTLV` (int32) werden sie
  voll unterstützt.

**TLV mit festen Kopfbytes vor den Frames (z. B. VISA-DE55) — Rezept, kein
Feature (0.8.0, FR-10b):**

Ein TLV-Container beginnt **immer** mit dem ersten TLV-Frame
(`tlv: { ber: true }` bzw. `...bertlv`). Felder, die feste Bytes **vor**
den Frames tragen — VISA BASE I DE55: `01` (Version) + 2 Byte binäre
TLV-Länge + BER-TLV —, sind als TLV-Container **nicht** modellierbar (es gibt
kein `header_bytes:` o. Ä.; der Beleg beschränkt sich auf eine Nachricht, ob
der Kopf bei allen Nachrichten konstant ist, ist offen). Drei getestete
Wege (`tests/test_field_only_spec.cc`, Tag `[fr10b]`):

1. **Roh als `lbinary`** (dekodiert und baut byte-genau):
   `"055": { format: lbinary, encoding: binary, length: 255 }` — der Wert
   ist Kopf + TLV-Block als `BinaryField` (Setzen per Hex-Zeichenkette).
2. **Kopf abschneiden und den TLV-Block per Field-only-Spec auflösen**
   (§11; die Tags werden typisiert/beschrieben dekodiert): die ersten 3 Bytes
   des `BinaryField`-Werts abtrennen, den Rest mit einer Field-only-Spec
   (`field: { format: lllbertlv, length: 999, children: { "95": … } }`)
   über `SpecDecoder::decodeField(parser, binaryField)` dekodieren. Der
   Re-Encode (`msg->parse(msg)`) liefert den TLV-Block byte-identisch;
   Kopf voranstellen ergibt das ursprüngliche DE55. Die Kopf-Längenbytes
   (hier `00 0F`) nennen die Länge des TLV-Blocks und müssen beim Bauen vom
   Aufrufer berechnet werden.
3. **Kopf-Kind + `remaining`** (roh, seit 0.8.0 auch **baubar**, FR-10a):

   ```yaml
   "055":
     type: nested
     format: lbinary
     encoding: binary
     length: 255
     children:
       - { format: binary, length: 3, description: "Head (01 + TLV length)" }
       - { format: remaining, encoding: binary, length: 252, description: "TLV block (raw)" }
   ```

   Der Kopf ist `55.0`, der TLV-Block `55.1` (jeweils `BinaryField`, Hex-
   Zeichenkette in Großbuchstaben); der Block wird **nicht** in Tags
   aufgeschlüsselt (dafür Weg 2).

Wird ein konstanter Kopf über mehrere Nachrichten belegt, kann ein
`tlv: { ber: true, header_bytes: N }` neu bewertet werden (nicht geplant).

## 7. Encoding-Auflösung und -Vererbung

```
Feld-Encoding  >  globales YAML-Encoding  >  "" (nur encoding-neutrale Formate)
```

- **Encoding-neutral** (lesen/schreiben immer Rohtext, ignorieren
  jede Encoding-Einstellung): `BINARY` (fix), `BITMAP`, `NOP`,
  `UNUSED`, `BERTLV`. **`REMAINING` ist seit 0.6.0 nicht mehr neutral**
  (s. §4).
- **Kindervererbung:** encoding-neutrale Felder geben das
  **globale** Encoding an ihre Kinder weiter; encoding-bewusste
  Felder ihr eigenes aufgelöstes Encoding. So bleibt eine EBCDIC-Spec
  mit `binary`-Containern in der Mitte konsistent.
- **`prefix_encoding` ist feldlokal (0.7.0):** Der Key wirkt nur auf
  das eigene Feld — es gibt **keinen** Root-Level-Default und keine
  Vererbung von/auf Kinder (nur `encoding` an sich nimmt am
  Ererbungsmodell teil). Fehlt der Key, gilt `prefix_encoding = encoding`.
- **`bcd_pad` (0.7.1)** folgt dem Ererbungsmodell *nicht* über Container:
  es gibt einen Root-Default (alle BCD-Nutzdaten-Felder, auch TLV-Kinder),
  den ein Feld-Key überschreibt; Container selbst tragen keinen Key.
- Details, EBCDIC-Orakel-Pin und Strict-Regeln: [encoding.md](encoding.md).

## 8. Validierung und Fehlersemantik (fail-closed)

Alle Loader-/Validierungsfehler sind **positionierte**
`std::runtime_error` (Subtyp `SpecValidationError` mit
`file:line:col`), nie rohe STL-Exceptions:

| Auslöser | Fehler |
|---|---|
| `remaining` ohne `length` | `Feld …: 'format: remaining' benötigt 'length' …` |
| `fields` leer / keine Map / nicht-numerischer DE-Key | positionierter `SpecValidationError` |
| Format/Encoding-Kombination ohne Dispatch-Eintrag (§3) | `Unbekannte Format/Encoding-Kombination …` |
| `!include_files` ohne `---`-Trenner | positionierter `SpecValidationError` |
| `!include_files` außerhalb der Sandbox-Roots | `[ISO8583] Sandbox: …` (fail-closed) |
| zirkuläres `!use` / Rekursionstiefe > 200 | `std::runtime_error` (kein Stack-Overflow) |
| TLV-Kind außerhalb der Whitelist (§6) | `TLV-Kind '…' (Format …) … verworfen` |
| `prefix_encoding` mit unzulässigem Wert (0.7.0) | `Feld … hat ungültiges prefix_encoding='…' (erlaubt: ascii, ebcdic, bcd, binary)` |
| `prefix_encoding` an fixbreiten Formaten / `*binary`/`bertlv`/`amount`/`remaining`/`bitmap`/`nop`/`unused` (0.7.0) | `…'prefix_encoding' ist nur für variablen *char/*num-Formate gültig (format=…: …)` — kontextsensitive Begründung je Format-Familie |
| `prefix_encoding`-Kombination ohne Dispatch-Eintrag (0.7.0) | `Kombination format=…, encoding=…, prefix_encoding=… ist nicht verfügbar (s. Format×Encoding-Matrix in spec_schema.md §3)` |
| `prefix_encoding` bei TLV-Kindern / constructed-Kindern / Root-Level (0.7.0) | `…'prefix_encoding' ist bei TLV-Kindern unzulässig (die TLV-Länge liegt im Length-Feld des Frames)` bzw. `…darf 'prefix_encoding' nicht deklarieren…` |
| `bcd_pad` mit unzulässigem Wert (0.7.1) | `… hat ungültiges bcd_pad='…' (erlaubt: right_zero, right_f, left_zero)` |
| `bcd_pad` an Feld ohne BCD-Nutzdaten / TLV-Container / constructed-Kind (0.7.1) | `…'bcd_pad' ist nur für Felder mit BCD-Nutzdaten gültig (…)` bzw. `…darf 'bcd_pad' nicht deklarieren…` |
| `secondary` mit unzulässigem Wert / an Nicht-Bitmap-Feld / `always` mit `length < 16` (0.8.0) | `… hat ungültiges secondary='…' (erlaubt: auto, always)` bzw. `…'secondary' ist nur für 'format: bitmap' gültig…` bzw. `…'secondary: always' erfordert 'length: 16' (oder mehr)…` |
| Decode: Bit 1 gesetzt, aber `bitmap length` < 16 (0.8.0, strict) | `[ISO8583] Bitmap @ Offset N: Bit 1 (Sekundär-Bitmap) ist gesetzt, die Spec deklariert … nur 8 Byte … 'length: 16' setzen (FR-9)` |
| Bauen: gesetzte Felder erfordern mehr Bitmap-Bytes als `length` (0.8.0, strict) | `[ISO8583] Bitmap: gesetzte Felder erfordern 16 Byte Bitmap, die Spec deklariert aber nur 'length: 8' …` |
| Datei > `maxSpecBytes` (Default 32 MiB) / > 1024 Includes / oversized Sidecar | positionierter Fehler bzw. Discard+Regenerierung |
| rapidyaml-Parsefehler | via prozessweit installierten `ryml`-Callbacks in positionierte Exceptions übersetzt (Default wäre `std::abort()`) |

**Strict vs. Legacy (nur Decodierung, nur Daten-Bytes):**
`strict: true` (Default) wirft bei unmappbaren Bytes positioniert
(EBCDIC: 85-Byte-IBM-1047-Whitelist; A2E: 84 Zeichen + `'?'`-
Ausnahme). `strict: false` = Legacy: E2A → `'.'` (`0x2E`),
A2E → `'?'` (`0x6F`). Längenpräfixe werden immer roh gelesen
(`constexpr` kann nicht werfen) — korrupte Präfixwerte fallen an den
nachgelagerten Checks auf. Ein am Pufferende **abgeschnittenes**
Präfix (nicht alle Präfix-Bytes vorhanden) wirft dagegen **vor**
der Präfis-Lesung in beiden Modi einen positionierten Fehler
(`Längenpräfix am Pufferende abgeschnitten: …`), seit 0.7.0
garantiert (davor konnte hier eine rohe STL-Exception durch die
Präfix-Lesung austreten).

## 9. Laufzeitverhalten (für die Interpretation von Specs)

- `unparse()` = **Decode** (Wire → Felder), `parse()` = **Encode**
  (Felder → Wire). Diese Umkehrung ist bewusst.
- **Bitmap-Felder werden nie manuell gesetzt** — der Parser
  berechnet sie (bei `msg->parse(msg)` automatisch via
  `recalcBitmap_locked()`; die Expert-API `parser->parse(msg)`
  erwartet eine vorhandene Bitmap). `bitmap length` bestimmt, ob der
  Decoder die Sekundär-Bitmap liest (§3 „Bitmap-Felder“).
- DE-Zugriff per Punkt-Notation (`"48.72.1"`); `BinaryField`-Werte
  werden als **großgeschriebene Hex-Zeichenketten** gesetzt
  (`msg->set(52, "0102030405060708")`).
- **BCD-Padding (0.7.1, FR-7):** `bcd_pad: right_zero|right_f|left_zero`
  (Root-Default oder Feld-Key) konfiguriert Seite und Füll-Nibble bei
  ungerader BCD-Ziffernzahl (§3 „BCD-Padding").
- `msg->mti()` wirft `std::logic_error`, wenn kein MTI
  (`hasMTI()` zuerst prüfen); `mti()` setzt ein `OpaqueField`
  voraus (binary-MTIs: nur `hasMTI()`).
- **Padding und `strict_length` (0.6.2, FR-5):** Ein zu kurzer Wert bei
  **fester Länge** (kein L-Präfix) wird standardmäßig beim Serialisieren
  aufgefüllt (`numeric`/`amount` links mit `0`, `char` rechts mit
  Leerzeichen) — Legacy, unverändert. Mit dem Opt-in `strict_length: true`
  (Root-Key = Default für alle Felder oder Feld-Key, der den Root
  überschreibt) wird stattdessen im strict-Modus ein `std::runtime_error`
  ("Serialisierung zu kurz …") geworfen; nicht-strikt: Warnung + Padding.
  Nie betroffen: L-präfixierte Felder und `remaining` (Maximum);
  `binary`-Felder fester Länge sind ohnehin immer exakt-längenpflichtig.
- `sensitive: true` maskiert **nur** die Dump-/Log-Oberfläche
  (`dump()`, `operator<<` → `***`); `value()`/`to_json()`
  sind bewusst unmasked.
- `.smap`-Sidecar (Fehlerpositionen): Cache, neben der Spec-Datei,
  SHA-256-quellengeprüft, jederzeit löschbar; bei read-only-
  Deployment `SpecLoadOptions::allowSmapWrite=false`.
- Introspection: `ISOSpec::field(de)` liefert
  `SpecFieldInfo{key, description, format{type, prefix_digits,
  max_length}, encoding, prefix_encoding (0.7.0), bcd_pad (0.7.1), is_nested,
  is_bitmap, children, tlv_children (0.5.0), tlv_is_ber (0.6.0)}` —
  bei `remaining` ist `max_length` das deklarierte Maximum
  (0.6.0; vorher immer 0), bei `nop`/`unused` 0. `tlv_is_ber`
  ist `true` im BER-TLV-Modus (beide Schreibweisen) und `false`
  im fixen SE-Modus bzw. bei Nicht-TLV-Feldern.

## 10. Komplette Beispiele

**Minimal (ASCII):**

```yaml
spec: "Minimal ASCII"
encoding: ascii
fields:
  "000": { format: numeric, length: 4, description: "MTI" }
  "001": { format: bitmap,  length: 8 }
  "003": { format: numeric, length: 6, description: "Processing Code" }
  "011": { format: numeric, length: 6, description: "STAN" }
  "039": { format: numeric, length: 2, description: "Response Code" }
```

**EBCDIC mit TLV und remaining:**

```yaml
spec: "EBCDIC Gateway"
encoding: ebcdic
strict: true
header: 93
definitions:
  pan: { type: scalar, format: llchar, length: 19, sensitive: true }
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "002": !use pan
  "003": { format: numeric, length: 6, encoding: bcd }   # Feld-Override
  "048":
    type: nested
    format: lllchar
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "26": { format: char, length: 10, description: "Auth Code" }
  "057":
    type: nested
    format: lllbinary
    length: 999
    tlv: { ber: true }
    children:
      "9F26": { format: binary, length: 8, description: "Application Cryptogram" }
      "5A":   { format: binary, length: 10, description: "Application PAN", sensitive: true }
  "061":
    type: nested
    format: binary
    length: 26
    children:
      - { format: numeric, length: 1 }
      - { format: remaining, length: 10, description: "POS Postal Code" }
```

**BCD-spezifisch:**

```yaml
spec: "BCD Network"
encoding: bcd
fields:
  "000": { format: numeric, length: 4 }                  # 4 Ziffern = 2 Bytes
  "001": { format: bitmap,  length: 8 }
  "004": { format: numeric, length: 12, description: "Amount" }   # 12 Ziffern = 6 Bytes
  "061":
    type: nested
    format: binary
    length: 26
    children:
      - { format: remaining, length: 10, encoding: ascii, description: "ASCII im BCD-Container" }
```

## 11. Field-only-Specs (0.6.0)

Seit 0.6.0 gibt es neben der Message-Form (§1) eine zweite Dokument-Form:
eine **Field-only-Spec** definiert die Semantik eines *einzelnen* Feldes —
typischer Use-Case: DE55 ICC Data (Mastercard fixer TLV bzw. EMV BER-TLV) —
und liefert einen Parser, der exakt auf dem **Payload dieses einen
Feldes** läuft. Es gibt keinen MTI, keine Bitmap und keinen Header
(`ISOSpec::hasHeader() == false`, `ISOMessage::hasMTI() == false`); das
eine Feld wird unter dem synthetischen **Key `0`** geparst
(s. o., „Synthetische Key-`0`-Semantik").

**Dokument-Form:**

```yaml
spec: "DE55 ICC (Mastercard SE)"   # optional, Default "<unnamed>"
encoding: ebcdic                    # optional, §7
strict: true                        # optional, §8
field:                              # PFLICHT: eine nicht-leere Map (EIN Feld)
  format: lllbinary
  length: 255
  tlv: { tag_bytes: 2, len_bytes: 2 }
  children:
    "64": { format: char, length: 4, description: "Application PAN" }
    "71": { format: char, length: 3, description: "Terminal Capabilities" }
```

**Root-Schlüssel (Field-only-Dokument):**

| Schlüssel | Typ | Pflicht | Bedeutung |
|---|---|---|---|
| `spec` | string | nein | Name der Spec (Default `<unnamed>`; Introspection `ISOSpec::name()`) |
| `encoding` | `ascii` \| `bcd` \| `ebcdic` \| `binary` | nein | globales Encoding (Auflösung s. §7) |
| `strict` | bool | nein | Default `true` (§8) |
| `strict_length` | bool | nein (Default `false`) | wie §1: Root-Default für die Unterlängen-Prüfung bei fester Länge; ein Feld-Key `strict_length` überschreibt pro Feld (s. §9) |
| `definitions` | map | nein | wie §1 — mit allen Direktiven (`!include_files`, `!use`, `!template`, `!merge`) |
| `field` | map | ja | **nicht-leere** Map: die *einzige* Feld-Deklaration des Dokuments (Grammatik wie `fields:`-Einträge, §2) |
| `fields` | — | — | **verboten** → Fail-closed-Fehler |
| `header` | — | — | **verboten** → Fail-closed-Fehler (s. o., „Warum `header:` abgelehnt wird") |

**Root-Regeln (fail-closed, positionierte `SpecValidationError`s):**

| Auslöser | Fehlermeldung |
|---|---|
| `field` fehlt (leeres Dokument) | `Fehlender Abschnitt 'field' in YAML.` |
| `field` ist keine nicht-leere Map | `Abschnitt 'field' muss eine nicht-leere Map sein (eine einzelne Felddefinition, z. B. 'format: lllbinary')` |
| `fields:` in Field-only-Dokument | `Konflikt: field-only-Dokumente dürfen keine fields:-Map enthalten (fields: gehört in Message-Specs, field: hier)` |
| `header:` in Field-only-Dokument | `Konflikt: header: ist in Field-only-Dokumenten unzulässig (ein isoliertes Feld hat keinen MTI/Bitmap-Header)` |
| `field:` in Message-Dokument | `Konflikt: Message-Specs verwenden fields:, field: ist nur in Field-only-Dokumenten erlaubt` |

**Feld-Regeln:** Die `field:`-Deklaration folgt exakt der Grammatik der
`fields:`-Einträge (§2): Key-Whitelist `type`/`format`/`encoding`/`length`/
`description`/`children`/`tlv`/`sensitive`/`scale`/`sign`/`strict_length`/
`prefix_encoding`/`bcd_pad` (`validateFieldKeys`, DE-Key synthetisch `0`), Formate und Encoding-Matrix
(§3), `remaining` benötigt `length` (§4), TLV-Kind-Whitelist und
Container-Basis-Parser-Normalisierung (§6). Die `header`-Defaults bleiben
in Kraft, haben aber ohne `header:`-Block keine Wirkung — ein
Field-only-Dokument trägt per Definition keinen Header.

**Beispiele** (aus der Regressionstestsuite):

```yaml
# Mastercard-SE: fixer TLV (EBCDIC), SE64/SE71 deklariert,
# nicht deklarierte Tags (z. B. SE72) → OpaqueField.
spec: "DE55 ICC (Mastercard SE)"
encoding: ebcdic
field:
  format: lllbinary
  length: 255
  tlv: { tag_bytes: 2, len_bytes: 2 }
  children:
    "64": { format: char, length: 4, description: "Application PAN" }
    "71": { format: char, length: 3, description: "Terminal Capabilities" }
```

```yaml
# EMV: BER-TLV (ASCII), Hex-Tags als Map-Keys,
# nicht deklarierte Tags (z. B. 0x8A) → OpaqueField "SE138".
spec: "DE55 ICC (EMV)"
encoding: ascii
field:
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:
    "5A": { format: char, length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
```

**Wire-Vertrag:** Der Field-only-Parser arbeitet auf **exakt den Bytes,
die ein `BinaryField` für das Element hält** (z. B. aus
`msg->get<BinaryField>(55)` eines Voll-Nachrichten-Decode):

- **TLV/BERTLV**: die Kinderframes **ohne das eigene LLL-Präfix** des DEs
  (das Präfix wurde beim Voll-Decode konsumiert); jedes Frame trägt
  selbstverständlich sein eigenes Tag/Length-Präfix.
- **Nested (Sequenz)**: die Kinderframes **ohne das äußere Präfix**
  (z. B. `llllchar`).
- **Skalar**: die eigenen Wire-Bytes des Elements (Präfix + Payload).

Roundtrip-Garantie: `parse(decodeField(parser, payload))` reproduziert die
übergebenen Bytes **byte-identisch**; die Source-`BinaryField` bleibt
unangetastet.

**Anwendungsmuster:**

```cpp
// 1. Convenience (empfohlen):
const auto parser = spec::SpecDecoder::loadFieldFromYaml("de55_emv.yml");
const auto de55 = fullMsg->get<BinaryField>(55);   // aus Voll-Nachrichten-Decode
const auto m = spec::SpecDecoder::decodeField(parser, *de55);
const auto pan = m->get<OpaqueField>(0x5A);        // TLV-Kinder: Tag/SE-Key
// (NESTED-Sequenz-Kinder: Positions-Key 0, 1, …)

// 2. Äquivalenter manueller Pattern (identisches Ergebnis):
auto m2 = std::make_shared<ISOMessage>();          // synthetisch leer
m2->parser(parser);
m2->unparse(m2, de55->value());                    // unparse() = Decode (§9)

// 3. Introspektion:
const auto [p, spec] =
    spec::SpecDecoder::loadFieldBothFromYaml("de55_emv.yml");
// spec->fields() → genau ein SpecFieldInfo (key 0); hasHeader() == false
```

**Synthetische Key-`0`-Semantik:** Bewusst wird das eine Feld unter Key
`0` — dem MTI-Key — geparst, damit die gesamte bestehende
Parser-/Message-Maschinerie (Feld-Slot-Adressierung, `get<T>(key)`,
Introspektion) unverändert funktioniert. Konsequenzen:

- In einer Field-only-Message ist Key `0` **kein MTI**:
  `ISOMessage::mti()` wirft (`hasMTI() == false`); der Wert bei Key `0`
  ist das isolierte Feld selbst.
- **Nie** einen Field-only-Parser und einen Message-Parser an *derselben*
  `ISOMessage` mischen (eine Message trägt genau einen Parser) und nie
  dieselbe Spec-Datei in beiden Formen interpretieren — die Dokument-
  Form ist Fail-closed getrennt (s. o., „Root-Regeln").

**Getrennter Loader-Cache:** Field-only-Specs haben ihren **eigenen**
In-Process-Cache, getrennt vom Message-Spec-Cache (gleiche Mechanik:
absoluter Pfad als Key, LRU ≤ 64, Publish-then-Verify mit SHA-256
Content-Snapshots — s. `loadFromYamlCached` in `ISOSpec.hh`):

- `invalidateFieldCache(path)` / `clearFieldCache()` verwalten **nur**
  den Field-only-Cache.
- `invalidateCache(path)` / `clearCache()` verwalten **nur** die
  Message-Spec-Caches.
- Wer **dieselbe Datei in beiden Formen** lädt (`loadFromYamlCached` und
  `loadFieldFromYamlCached`), muss bei jeder Änderung **beide** Caches
  invalidieren (bei `TrustUntilInvalidated`; bei Default-`CheckEveryCall`
  erkennen beide Caches Änderungen automatisch).

**Warum `header:` abgelehnt wird (bewusste Schärfe):** Ein isoliertes
Feld hat per Definition weder MTI noch Bitmap — ein N-Byte-
Netzwerk-Header vor dem Payload widerspricht der Form. Der Loader wirft
daher, statt die Angabe still zu ignorieren (Fail-closed;
Entscheidung c im FE-1-Plan). **Erweiterungspunkt:** Sollte je ein
Use-Case auftauchen, der vor einem Feld-Payload einen festen
Präfix-Frame erwartet (z. B. ein Sub-Transport-Rahmen), wäre `header:`
das naheliegende Wort dafür — heute ist es bewusst gesperrt, um den
Wire-Vertrag (s. o.) eindeutig zu halten.

## 12. Häufige Fehler (Checkliste für Generatoren)

0. **Zu kurze Werte bei fester Länge** werden still aufgefüllt (Default) —
   für exakte Kontrolle `strict_length: true` setzen (§9).
1. **`remaining` ohne `length`** → Ladefehler (0.6.0, Fail-closed).
   Immer `length` (Maximum) mit angeben.
2. **`remaining`/`char`/`numeric` in TLV-Kindern** → Whitelist-Fehler.
   TLV-Kinder: nur `binary`/`char`/`numeric`/`nopad_char`.
3. **`!include_files` ohne `---`** → Ladefehler (0.2.0-Breaking).
4. **BCD-`length` in Bytes statt Ziffern** → Feld dekodiert die halbe
   Länge. `length: 12` = 12 Ziffern = 6 Bytes.
5. **Text als `binary` in EBCDIC-Spec** → `HEX_EBCDIC` (kein Text!).
   Text in EBCDIC-Specs: `char`/`numeric` (encoding vererbt `ebcdic`).
6. **`bitmap`/`nop`/`unused` mit `length > 0` bzw. `remaining` als
   TLV-Kind** → semantischer Widerspruch (Validierung/Warnung).
   *Ausnahme `bitmap`:* dort ist `length` die Bitmap-Größe in Bytes
   (`8` nur Primär, `16` mit Sekundär) und **muss** zu den Feldern der Spec
   passen — mit `length: 8` und Bit 1 bzw. Feldern > 64 wirft die Bibliothek
   seit 0.8.0 (strict) einen positionierten Fehler (§3 „Bitmap-Felder“).
7. **Nicht-numerische DE-Keys** (z. B. `pan:` statt `"002":`) →
   Ladefehler.
8. **Zirkuläres `!use`** → Ladefehler (Rekursionsschutz).
9. **`bertlv` kombinieren mit `type: nested`/`tlv:`/`children`** →
   verboten (BERTLV ist Scalar-only).
10. **`l*binary` ohne Encoding ≠ roh** → Präfix ist Big-Endian-Byte;
    für roh *mit* Präfix in ASCII-Specs bewusst `encoding: ascii`
    setzen (Präfix = ASCII-Ziffern).
11. **Include-Pfade außerhalb des Spec-Verzeichnisses** →
    Sandbox-Fehler; `SpecLoadOptions::roots` erweitern (nicht
    `sandbox=false`, außer bei vollständig vertrauenswürdigen Trees).
12. **Kommazeichen/Unicode-Dash in YAML-Strings** sind unproblematisch;
    Strings mit `:` müssen nicht quotiert werden, *schlüsselartige*
    DE-Keys („000") aber immer.
13. **BCD-Längenpräfix als Binärbyte interpretieren (0.7.0):** Ein
    BCD-Präfixbyte trägt zwei *Dezimalziffern* — `0x16` = sechzehn,
    `0x10` = zehn (nicht 16). Generatoren müssen die gewünschte
    Ziffernzahl als BCD packen; BINARY-Präfixe sind dagegen
    Big-Endian-Bytes mit Breite = L-Zahl (L=1, LL=2, LLL=3, LLLL=4)
    (§3).
14. **`prefix_encoding` an fixbreiten Formaten oder TLV-Kindern
    (0.7.0)** → `SpecValidationError` beim Laden (Fail-closed); der
    Key ist nur auf variablen `*char`/`*num`-Formaten gültig
    (§3, §8).
15. **`bcd_pad` an Nicht-BCD-Feldern oder Containern (0.7.1)** →
    `SpecValidationError` beim Laden (Fail-closed); der Key wirkt nur auf
    BCD-**Nutzdaten** bei ungerader Ziffernzahl, nie auf das
    Längenpräfix. Ohne Deklaration bleibt `right_zero` (Legacy) und das
    Padding-Nibble wird beim Decode nicht geprüft (§3 „BCD-Padding").
    *Ausnahme (0.9.0):* ein `nested`-Container mit `pack: nibble` trägt das
    Padding seines Ziffern-Stroms selbst (§6).
16. **Bitmap-Unterfelder als positionelle `children`-Liste modellieren
    (0.9.0)** → eine `bitmap`-Zeile als erstes Kind steuert nichts; die Kinder
    werden **starr nach Position** gelesen und passen nur für genau eine
    Bitmap (stille Fehlinterpretation). Richtig: `bitmap: { length: N }` +
    `children` als Bit-Nummer-Map (§6).
17. **Nibble-Unterfelder als ein Kind pro Byte oder `length: 1`-Kinder ohne
    `pack` (0.9.0)** → jedes Kind belegt ganze Bytes (`length: 1` ergibt ein Byte
    mit Padding-Nibble, zwei 1-stellige Kinder also **zwei** Bytes). Für
    Ziffern, die sich ein Byte teilen: `pack: nibble` am Container (§6). Kinder
    dort nur `numeric`+`bcd`+feste `length` (oder `nop`-Platzhalter).