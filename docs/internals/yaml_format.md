# YAML-Spezifikationsformat

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
| `remaining` | `IF_REMAINING` / `IFE_REMAINING` | Alle restlichen Bytes des Eltern-Buffers |

> **Hinweis:** `LBINARY`, `LLBINARY`, `LLLBINARY` (und `LLLLBINARY`)
> sind **nicht** encoding-neutral, da ihr Längen-Präfix das
> Spec-Encoding (EBCDIC/BCD/ASCII) verwendet.

### ASCII

| Format | Parser | Beschreibung |
|---|---|---|
| `numeric` | `IFA_NUMERIC` | ASCII-Ziffern |
| `char` | `IFA_CHAR` | ASCII-Zeichenkette |
| `nopad_char` | `IFA_NOPAD_CHAR` | ASCII-Zeichenkette ohne Padding |
| `lchar` … `llllchar` | `IFA_LCHAR` … | 1–4-stelliges ASCII-Längenpräfix + `char`-Daten |
| `lnum` / `llnum` | `IFA_LNUM` / `IFA_LLNUM` | 1/2-stelliges ASCII-Längenpräfix + Ziffern |
| `lbinary` … `lllbinary` | `IFA_LBINARY` … | ASCII-Längenpräfix + Binärdaten |

### BCD

| Format | Parser | Beschreibung |
|---|---|---|
| `numeric` | `IFB_NUMERIC` | BCD-Ziffern (2 Ziffern/Byte) |
| `lchar` / `llchar` / `lllchar` | `IFB_LCHAR` … | BCD-Längenpräfix + BCD-Zeichendaten |
| `lbinary` … `lllbinary` | `IFB_LBINARY` … | BCD-Längenpräfix + Binärdaten |

### EBCDIC

| Format | Parser | Beschreibung |
|---|---|---|
| `binary` / `lbinary` … `llllbinary` | `IFE_BINARY` … | EBCDIC-Längenpräfix + Binärdaten |
| `numeric` / `lnum` | `IFE_NUMERIC` / `IFE_LNUM` | EBCDIC-Ziffern |
| `char` / `nopad_char` | `IFE_CHAR` / `IFE_NOPAD_CHAR` | EBCDIC-Zeichenketten |
| `lchar` / `llchar` / `lllchar` | `IFE_LCHAR` … | EBCDIC-Längenpräfix + EBCDIC-Zeichendaten |

### TLV / BER-TLV

| Format | Beschreibung |
|---|---|
| `tlv` (über `tlv:`-Knoten) | Festes TLV mit `tag_bytes`, `len_bytes`, `tcc` (Mastercard/Visa-SE) |
| `...bertlv` (z. B. `lllbertlv`) | Dynamischer BER-TLV/EMV-Tags; die Kinderliste entfällt, das Präfix verhält sich wie `...binary` |

Präfix-Zeichen: `L` (max. 9), `LL` (max. 99), `LLL` (max. 999),
`LLLL` (max. 9999).

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
    - format: remaining          # optionales Schlussfeld — kein Längenpräfix
      description: "POS Postal Code"
```

Verschachtelte Felder werden über Punkt-Notation adressiert:

```cpp
msg->set("61.1", "0");   // Unterfeld 1 von DE61
```

## Template-Kurzschreibweise

```yaml
"002": !template LL(CHAR, 19)
# expandiert zu: { type: scalar, format: LLCHAR, length: 19 }

"055":
  !merge
  - !template LLL(BINARY, 255)
  - description: "ICC / EMV Data"
```