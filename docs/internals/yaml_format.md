# YAML-Spezifikationsformat

> **Normative Referenz:** [spec_schema.md](spec_schema.md) — vollständiges
> Schema (Root-/Feld-Keys, Format×Encoding-Matrix, Direktiven, TLV-Regeln,
> Fehlersemantik, komplette Beispiele) für Menschen und KI-Generatoren.

## Minimale Spec

```yaml
spec:     "Meine Spec"
encoding: ebcdic       # global: ascii | bcd | ebcdic | binary

fields:
  "000":               # MTI — immer Slot 000
    type: scalar
    format: numeric
    length: 4
  "001":               # Primär-Bitmap — immer Slot 001
    type: scalar
    format: bitmap
    length: 8
  "002":               # Primäre Kontonummer
    type: scalar
    format: llchar
    length: 19
```

## Root-Keys

Keys auf der Top-Level-Ebene der Spec (neben `definitions:` und `fields:`;
in Field-only-Specs wird `fields:` durch `field:` ersetzt, 0.6.0):

| Key | Typ | Default | Wirkung |
|---|---|---|---|
| `spec` | String | `<unnamed>` | Menschenlesbarer Spec-Name (Introspektion: `ISOSpec::name()`) |
| `encoding` | String | `""` | Globales Encoding: `ascii` \| `bcd` \| `ebcdic` \| `binary`; kann pro Feld überschrieben werden |
| `strict` | Bool | `true` | Strikte Dekodierung: Bytes außerhalb der Encoding-Whitelist (z. B. EBCDIC-Steuer-/Binär-Bytes) werden mit positioniertem Fehler abgelehnt; `false` = Legacy-Mapping (`.`-bzw. `?`-Füllzeichen) |
| `strict_length` | Bool | `false` | Opt-in (0.6.2): Unterlängen-Prüfung bei fester Länge — zu kurze Werte werden beim Serialisieren im strict-Modus abgelehnt statt gepaddet; Root-Default, ein Feld-Key `strict_length` überschreibt pro Feld (Details: [spec_schema.md](spec_schema.md) §9) |
| `bcd_pad` | String | – (Key fehlt = `right_zero`) | Root-Default (0.8.0, FR-7) für das Padding bei gepacktem BCD mit ungerader Ziffernzahl: `right_zero` \| `right_f` \| `left_zero`; ein Feld-Key `bcd_pad` überschreibt. Details: [spec_schema.md](spec_schema.md) §3 „BCD-Padding" |
| `header` | Integer | – (Key fehlt) | Fester Netzwerk-Header von N Bytes **vor** dem ISO-8583-Nachrichtenbody auf der Wire (z. B. proprietärer Frame-Header). Key fehlt → kein Header. Introspektion: `ISOSpec::hasHeader()` (Key definiert?) und `ISOSpec::headerSize()` (Byte-Anzahl, 0 wenn Key fehlt). Hinweis: Der Parser behandelt die Größe `0` wie "kein Header" (keine Wire-Bytes). |
| `field` | Map | – (Key fehlt) | **Field-only-Form (0.6.0):** die einzige Feld-Deklaration des Dokuments (nicht-leere Map, Grammatik wie `fields:`-Einträge) — exklusiv mit `fields:`; in Field-only-Dokumenten sind `fields:` und `header:` verboten (Fail-closed). |

Beispiel:

```yaml
spec: "Worldline"
encoding: ebcdic
strict: true
strict_length: false  # optional (0.6.2): true = zu kurze Werte bei fester Länge ablehnen
header: 93        # 93-Byte-Netzwerk-Header vor dem Nachrichtenbody

fields:
  "000":
    type: scalar
    format: numeric
    length: 4
```

## Field-only-Specs (0.6.0)

Neben Message-Specs gibt es eine zweite Dokument-Form: eine **Field-only**-
Spec enthält genau einen `field:`-Block (eine einzelne Feld-Deklaration mit
derselben Grammatik wie die `fields:`-Einträge) statt einer `fields:`-Map
und liefert einen Parser, der auf dem **Payload dieses einen Feldes**
läuft — ohne MTI, ohne Bitmap, ohne Header. Typischer Use-Case: DE55 ICC
Data (Mastercard SE oder EMV-BER-TLV). In Field-only-Dokumenten werden
`fields:` und `header:` abgelehnt (Fail-closed); das Feld wird unter dem
synthetischen Key `0` geparst (`ISOSpec::hasHeader() == false`,
`ISOMessage::mti()` wirft). Öffentliche Einträge:
`SpecDecoder::loadField*FromYaml(...)` / `SpecDecoder::decodeField(...)`,
mit eigenem Loader-Cache (`invalidateFieldCache` / `clearFieldCache`).
Vollständige Form, beide Beispiele und Wire-Vertrag:
[spec_schema.md](spec_schema.md) §11 „Field-only-Specs (0.6.0)".

## Definitionen und Wiederverwendung

```yaml
definitions:
  pan_field:
    type: scalar
    format: llchar
    length: 19
    description: "Primäre Kontonummer"

fields:
  "002": !use pan_field
```

## Multi-Datei-Specs

```yaml
# mastercard.yml
!include_files
- schemes/base.yml
- schemes/gmc.yml

spec: "Mastercard GMC"
encoding: ebcdic

fields:
  "002": !use pan_field   # in base.yml definiert
```

### Sandbox und Ressourcenlimits beim Laden (seit 0.3.0)

`SpecDecoder::load*FromYaml(..., const SpecLoadOptions&)` steuert das
Vertrauensmodell beim Laden. Alle bisherigen Überladungen (ohne Options-
Struktur) liefern ein `SpecLoadOptions` mit **Default-Werten**:

| Option | Default | Wirkung |
|---|---|---|
| `sandbox` | `true` | `!include_files`-Pfade werden **fail-closed** geprüft: Einträge, die außerhalb der erlaubten Wurzeln auflösen (`../`-Traversals, absolute Pfade, UNC-Pfade, per Symlink nach außen), werden mit `[ISO8583] Sandbox: …` abgelehnt. |
| `roots` | leer → Verzeichnis der Top-Level-Spec | Erlaubte Wurzeln (werden kanonisiert). Die Top-Level-Datei selbst ist Wahl der Anwendung und wird NICHT gegen die Wurzeln geprüft; explizite `roots` **ersetzen** den Default. |
| `allowSmapWrite` | `true` | `.smap`-Sidecar wird nur geschrieben, wenn `true` **und** der Sidecar-Pfad innerhalb der Sandbox-Wurzeln liegt. Der Load selbst ist davon unberührt. |
| `maxSpecBytes` | 32 MiB | Größengrenze **pro Quelldatei** (Top-Level + jede Include); wird beim Einlesen (streamend) erzwungen. |
| `maxIncludeFiles` | 1024 | Max. Anzahl **distinkter** Dateien pro Load (Top-Level mitgezählt) – schützt vor verschachtelten Include-Graphen. |
| `maxSmapBytes` | 16 MiB | Sidecar-Dateien darüber hinaus werden beim Laden verworfen und neu erzeugt. |

Beispiel (Read-only-Deployment mit expliziter Wurzel):

```cpp
iso8583::spec::SpecLoadOptions opts;
opts.roots = { "/etc/iso8583/specs" };
opts.allowSmapWrite = false;   // Read-only-Verzeichnis: keine Sidecar-Erzeugung
auto parser = iso8583::spec::SpecDecoder::loadFromYaml("/etc/iso8583/specs/gmc.yml", opts);
```

Hinweis: `fields:` muss eine **nicht-leere Map** sein (leere Maps, Sequenzen
und Ziffern-Overflows wie `"99999999999"` erzeugen präzise, lokalisierte
Fehler statt roher Standard-Exceptions).

## Direktiven

| Direktive | Wirkung |
|---|---|
| `!include_files [a.yml, b.yml]` | Externe Dateien laden; deren `definitions` werden zusammengeführt |
| `!use <name>` | Benannte Definition substituieren |
| `!template P(F, N)` | Kurzschreibweise für variable Länge, z. B. `LL(CHAR, 19)` |
| `!merge [...]` | Maps zusammenführen; spätere Einträge überschreiben frühere |
| `!include <name>` | Veralteter Alias für `!use` |

## Format-Referenz

Die Tabelle spiegelt die Parser-Dispatch-Tabelle aus `src/_spec.cc`
(`parserTable()`). Formate sind groß-/kleinschreibungsunabhängig; der
Effekt des Format-Strings hängt vom aufgelösten Encoding ab
(Feld-Encoding > globales `encoding` > leer).

### Encoding-neutral

Diese Formate lesen/schreiben immer Rohtext, unabhängig von jeder
Encoding-Einstellung:

| Format | Parser | Beschreibung |
|---|---|---|
| `binary` (fixe Länge) | `IF_BINARY` | Rohe Bytes, keine Präfix-Logik |
| `bitmap` | `IFB_BITMAP` | Primäre oder sekundäre Bitmap |
| `nop` / `unused` | `IF_NOP` | Skip/Platzhalter, keine Bytes verbraucht |

> **Hinweis:** `LBINARY`, `LLBINARY`, `LLLBINARY` (und `LLLLBINARY`)
> sind **nicht** encoding-neutral, da ihr Längen-Präfix das
> Spec-Encoding (EBCDIC/BCD/ASCII) verwendet. Auch `REMAINING` ist
> seit **0.6.0 nicht** encoding-neutral: es folgt dem aufgelösten
> Encoding (`""`/`binary` → roh `BinaryField`; `ascii`/`ebcdic`/`bcd`
> → `OpaqueField`) und verlangt zwingend `length` (Maximum, Clamp).
> Parser: `IF_REMAINING` (roh) / `IFA_REMAINING` / `IFE_REMAINING` /
> `IFB_REMAINING`. Details: [spec_schema.md](spec_schema.md) §4.

### ASCII

| Format | Parser | Beschreibung |
|---|---|---|
| `numeric` | `IFA_NUMERIC` | ASCII-Ziffern |
| `amount` (0.6.0) | `IFA_AMOUNT` | jPOS-ISOAmount: Währungscode + Skala + 12-stelliger Betrag → `AmountField`; optionaler Key `scale: N` (nur bei `amount`, Ganzzahl ≥ 0) = Standard-ISO-8583-Form: `length` nackte Ziffern, Skala `N`, keine Währung im Feld; zusätzlich `sign: true` (nach 0.6.0, nur mit `scale`, nicht mit bcd) = führendes Vorzeichenzeichen `C`/`D`/`+`/`-`, `length` inkl. Vorzeichen |
| `char` | `IFA_CHAR` | ASCII-Zeichenkette |
| `nopad_char` | `IFA_NOPAD_CHAR` | ASCII-Zeichenkette ohne Padding |
| `lchar` … `llllchar` | `IFA_LCHAR` … | 1–4-stelliges ASCII-Längenpräfix + `char`-Daten |
| `lnum` / `llnum` | `IFA_LNUM` / `IFA_LLNUM` | 1/2-stelliges ASCII-Längenpräfix + Ziffern |
| `lbinary` … `lllbinary` | `IFA_LBINARY` … | ASCII-Längenpräfix + Binärdaten |

### BCD

| Format | Parser | Beschreibung |
|---|---|---|
| `numeric` | `IFB_NUMERIC` | BCD-Ziffern (2 Ziffern/Byte) |
| `amount` (0.6.0) | `IFB_AMOUNT` | jPOS-ISOAmount, BCD (2 Ziffern/Byte) → `AmountField` |
| `lchar` / `llchar` / `lllchar` | `IFB_LCHAR` … | BCD-Längenpräfix + BCD-Zeichendaten |
| `lnum` / `llnum` (0.7.0) | `IFB_LNUM` / `IFB_LLNUM` | BCD-Längenpräfix + gepackte BCD-Ziffern (VISA-BASE-I-DE2-artig; Identitätslücke `lnum\|bcd`/`llnum\|bcd`) |
| `lbinary` … `lllbinary` | `IFB_LBINARY` … | BCD-Längenpräfix + Binärdaten |

### EBCDIC

| Format | Parser | Beschreibung |
|---|---|---|
| `binary` / `lbinary` … `llllbinary` | `IFE_BINARY` … | EBCDIC-Längenpräfix + Binärdaten |
| `numeric` / `lnum` / `llnum` (0.7.0) | `IFE_NUMERIC` / `IFE_LNUM` / `IFE_LLNUM` | EBCDIC-Ziffern |
| `amount` (0.6.0) | `IFE_AMOUNT` | jPOS-ISOAmount, EBCDIC → `AmountField` |
| `char` / `nopad_char` | `IFE_CHAR` / `IFE_NOPAD_CHAR` | EBCDIC-Zeichenketten |
| `lchar` / `llchar` / `lllchar` | `IFE_LCHAR` … | EBCDIC-Längenpräfix + EBCDIC-Zeichendaten |

### TLV / BER-TLV

| Format | Beschreibung |
|---|---|
| `tlv` (über `tlv:`-Knoten) | Festes TLV mit `tag_bytes` (1–2), `len_bytes` (1–3, seit 0.6.3), `tcc` (Mastercard/Visa-SE) |
| `...bertlv` (z. B. `lllbertlv`) | Dynamischer BER-TLV/EMV-Tags; das Präfix verhält sich wie `...binary`. Seit 0.5.0 (FR-2) optional mit `children:`-**Map** (HEX-Tag-Keys) für deklarierte/typisierte Kinder; undeclared Tags bleiben dynamisch. Unzulässig bleiben `type: nested`, ein eigener `tlv:`-Block und `children` als Sequence |

Präfix-Zeichen: `L` (max. 9), `LL` (max. 99), `LLL` (max. 999),
`LLLL` (max. 9999).

**Längenpräfix-Encoding `prefix_encoding:` (0.7.0, FR-6):** Der
optionale Feld-Key trennt das Codec des Längenpräfixes vom Codec der
Nutzdaten (z. B. VISA BASE-I: `llnum|bcd` + BCD-Präfix, `lllchar|ascii`
+ BINARY-Präfix). Default = `encoding`; verfügbar sind 25 Kombinationen,
sonst Fail-closed (`SpecValidationError` beim Laden) — nur auf variablen
`*char`/`*num`-Formaten, nicht bei TLV-Kindern. Normative Details
(Breiten-/Zählregeln, Kombinationen, `!merge`):
[spec_schema.md](spec_schema.md) §3, Unterabschnitt
„Längenpräfix-Encoding".

**BCD-Padding `bcd_pad:` (0.8.0, FR-7):** Bei gepacktem BCD mit ungerader
Ziffernzahl legt der optionale Key (Feld oder Root-Default) fest, wo das
Padding-Nibble steht und womit es gefüllt wird (Wert `123`): `right_zero`
(Default) → `12 30`, `right_f` → `12 3F`, `left_zero` → `01 23`. Nur bei
BCD-Nutzdaten (`numeric`, `amount`, `*char`, `*num`, `remaining`, TLV-Kinder),
nie am Längenpräfix; sonst Fail-closed beim Laden. Normative Details:
[spec_schema.md](spec_schema.md) §3, Unterabschnitt „BCD-Padding".

#### Typisierte TLV-Kinder (seit 0.5.0)

Deklarierte `children` (sowohl `tlv:`-Block als auch `...bertlv`-Kurzform)
werden nicht mehr nur als Dokumentation gelesen, sondern **typisiert**
dekodiert und kodiert:

| Deklaration | Laufzeit-Typ | Encoding |
|---|---|---|
| `format: char` / `numeric` / `nopad_char` | `OpaqueField` (String via Codec) | `ascii`, `ebcdic` oder `bcd` — explizit deklariert ODER vererbt (Feld → globale Spec-`encoding`); bei `...bertlv`-Kindern muss das Encoding **explizit** gesetzt werden, weil dort nichts vererbt wird |
| `format: amount` (0.6.0) | `AmountField` (String via Codec, jPOS-ISOAmount-Wert; mit `scale: N` Standard-Form, nackte Ziffern) | `ascii`, `ebcdic` oder `bcd` — wie `numeric` |
| `format: binary` | `BinaryField` (Rohbytes) | beliebig aus `ascii`/`ebcdic`/`bcd`/`binary` (wird ignoriert) |
| undeclared Tag | `BinaryField` (Rohbytes) + generische `"SE<n>"`-Beschreibung | — |

Beispiel (BERTLV-Kurzform mit gemischten Kindern):

```yaml
"055":
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:
    "5A": { format: char,   length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 2, description: "PIN Block" }
# Tag 0x8A (undeclared) wird dynamisch als BinaryField dekodiert ("SE138").
```

**Whitelist (Fail-closed beim Laden, positionierte Fehlermeldung):**
- Erlaubte Kind-Formate: `binary`, `char`, `numeric`, `nopad_char`,
  `amount` (0.6.0).
  L-präfixierte Formate (`llchar`, …), `bitmap`, `remaining` und `nop`
  sind bei TLV-Kindern widersprüchlich (die Länge liegt im Length-Feld
  des Frames) und werden verworfen.
- Erlaubte deklarierte Kind-Encodings: `ascii`, `ebcdic`, `bcd`, `binary`;
  Text-Formate (`char`/`numeric`/`nopad_char`/`amount`) nur mit `ascii`/`ebcdic`/`bcd`.
- Kind-Deklarationen müssen Maps sein; ein Text-Kind, das nach der
  Encoding-Auflösung auf ein unbrauchbares Encoding landet (z. B. globale
  `encoding: binary`), wird verworfen.

`length` bleibt reine Dokumentation (die TLV-Länge steht im Length-Feld).
Der Strict-Modus wird an die Kind-Codecs propagiert (nicht-mappbare
EBCDIC-Bytes → positionierter Fehler; non-strict → Legacy-`.`-Mapping).
Deklarierte Kinder sind per `loadBothFromYaml` über
`SpecFieldInfo::tlv_children` introspektierbar (Key = voller Tag-Wert als
`int`, damit 2-Byte-EMV-Tags wie `0x9F26` auch in `int16_t`-Builds passen).

#### Constructed-TLV-Kinder (0.6.4)

Ein TLV-Kind mit einem **eigenen `tlv:`-Block** (`tlv: { ber: true }` oder
`tlv: { tag_bytes, len_bytes }`) ist ein *constructed*-Container
(ISO/IEC 8825-1, EMV Book 3 — z. B. Tag `69` *Transaction Status
Information* mit Tags wie `63`): sein Wert ist selbst eine Folge von TLVs.

```yaml
"057":
  format: lllbertlv
  length: 999
  children:
    "69":                                   # constructed-Kind
      tlv: { ber: true }
      description: "Transaction Status Information"
      children:                             # optional: deklarierte innere Tags
        "63": { format: binary, description: "Result of EMV Application" }
```

- **Decode:** rekursive Auflösung über den eigenen Sub-Parser in eine
  Sub-`Message` — die inneren TLVs sind per Punkt-Notation erreichbar
  (z. B. `57.69.63`); **Encode:** byte-identischer Re-Encode. Beide
  TLV-Formen (BER **und** fixes TLV) nutzen denselben Codepfad
  (Policy-agnostisch); Rekursionstiefe ≤ 200 Ebenen.
- Container-Kinder dürfen **kein** `format:`/`length:`/`encoding:`
  deklarieren (das äußere TLV-Frame trägt Tag + Länge) — sonst positioniertes
  `SpecValidationError` (Fail-closed). Der `tlv:`-Block benötigt
  `ber: true` **oder** `tag_bytes`/`len_bytes`.
- Ohne `children:` ist das Kind ein *dynamischer* Container (innere Tags
  werden dynamisch dekodiert); eigene `children:` (Enkel-Tags) werden mit
  denselben Whitelist-Regeln rekursiv validiert.
- Ein constructed-Tag **ohne** `tlv:`-Block bleibt ein dynamischer
  `BinaryField`-Blob (Rohbytes) — es gibt keine implizite Erkennung über
  das Constructed-Bit (`0x20`).
- `sensitive: true` verbreitet sich auf den Sub-Baum (PCI-Masking); der
  Strict-Modus propagiert auf den Kind-Sub-Parser.
- Introspektion: `is_nested = true`, `tlv_is_ber` nach dem eigenen
  `tlv:`-Block, `tlv_children` rekursiv gefüllt.

## Verschachtelte Felder

```yaml
"061":
  type: nested
  format: binary
  length: 26
  description: "POS Data"
  children:
    - format: nop
      length: 0
    - format: numeric
      length: 1
      description: "POS Terminal Attendance"
    - format: remaining
      length: 10            # Pflicht seit 0.6.0 (Maximum, Clamp)
      description: "POS Postal Code"
```

Verschachtelte Felder werden über Punkt-Notation adressiert:

```cpp
msg->set("61.1", "0");   // Unterfeld 1 von DE61
```

**Text-basierte Container (0.6.0):** Auch `type: nested`-Container mit
text-basierten Formaten (`lllchar`, `llchar`, `llllchar` (nur ascii), …)
laden und decodieren korrekt — der Loader normalisiert den Container-
Basis-Parser wire-neutral auf den binären Zwilling (Introspektion meldet
das deklarierte Format; vor 0.6.0: `SIGSEGV`). Normative Regeln:
`docs/internals/spec_schema.md` (Abschnitt
„Container-Basis-Parser-Normalisierung (0.6.0)").

## Template-Kurzschreibweise

```yaml
"002": !template LL(CHAR, 19)
# expandiert zu: { type: scalar, format: LLCHAR, length: 19 }

"055":
  !merge
  - !template LLL(BINARY, 255)
  - description: "ICC / EMV Data"
```

## Sensible Felder (PCI-Logging-Hygiene, seit 0.3.0)

`sensitive: true` markiert ein Feld (oder einzelne TLV-Tags), dessen Wert in
`dump()` (und damit in `operator<<` bzw. jedem Log-Sink, der den Dump
protokolliert) als `***` maskiert wird:

```yaml
fields:
  "002":
    type: scalar
    format: llchar
    length: 19
    description: "Primäre Kontonummer"
    sensitive: true            # Wert im dump()/Log → "***"

  "048":
    type: nested
    format: lllbinary
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "72": { format: binary, length: 8, sensitive: true }   # nur SE72 maskiert
```

Regeln:
- Das Attribut ist bei jeder Feldart gültig (scalar, nested, TLV, BERTLV)
  und auch in `definitions:` (wirkt über `!use` durch).
- Auf einem Container (nested/TLV/BERTLV) vererbt es sich auf **alle**
  Kinder/Tags; pro Tag kann es in `children` einzeln gesetzt werden.
- `dump()` maskiert **nur den Wert** (`***`); die `description` bleibt
  sichtbar. Strukturdaten (Bitmaps, MTI) sind davon nicht betroffen.
- `value()`, `tryGetValue<T>()` und `to_json()` liefern **bewusst weiterhin
  Klartext** — das ist die programmatische Daten-API, kein Logging-Pfad.
- In PCI-Umgebungen das Log-Level bei **WARN oder niedriger** halten:
  `INFO`/`DEBUG` protokollieren pro Feld Encoding-/Decoding-Details
  (Größen, Offsets, Beschreibungen — aber nie Rohwerte sensitive Felder).