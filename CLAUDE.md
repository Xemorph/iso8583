# CLAUDE.md – libiso8583

Dieses Dokument ist das persistente Gedächtnis für KI-gestützte Entwicklung an libiso8583.
Bitte zu Beginn jeder neuen Konversation lesen.

---

## Projekt-Überblick

C++17-Bibliothek zum Parsen und Erzeugen von ISO 8583-Finanznachrichten (VISA, Mastercard, etc.).

**Kern-Konzept:**
- `ISOMessage` ist der Hauptarbeitsbehälter (Composite-Pattern)
- Ein `ISOBaseParser` (konfiguriert via YAML-Spec) liest Bytes → Felder (`unparse`) oder schreibt Felder → Bytes (`parse`)
- Felder: `ISOOpaqueField` (String), `ISOBinaryField` (Bytes), `ISOBitmap` (Bitset), `ISOCodeField` (int)

---

## Build & Toolchain

```powershell
# Konfigurieren (Windows, MSVC)
cmake -B build/msvc-debug --preset msvc-debug

# Bauen
cmake --build build/msvc-debug --config Debug

# Tests ausführen
ctest --preset msvc-debug --output-on-failure

# vcpkg-Baseline aktualisieren (nach vcpkg-Update)
vcpkg x-update-baseline
```

**Presets:** `debug`, `release`, `msvc-debug`, `msvc-release` (siehe `CMakePresets.json`)

**Tests aktivieren:** `-DISO8583_BUILD_TESTS=ON` (Standard: OFF)

**Abhängigkeiten via vcpkg:** `nlohmann-json`, `fmt`, `yaml-cpp`, `quill`, `robin-map`, `catch2`, `libiconv` (nicht Linux)

---

## Architektur: API-Ebenen

```
┌─────────────────────────────────────────────────────────────┐
│ Standardanwender                                            │
│   ISOMessage.hh   – ISOMessage, Feldtypen, Header-Klassen  │
│   ISOSpec.hh      – SpecDecoder::loadFromYaml()            │
├─────────────────────────────────────────────────────────────┤
│ Erfahrene Anwender (eigener Parser)                        │
│   ISOParser.hh    – nur ISOParserPtrBase (abstrakt)        │
│   _codec.hh       – Enums, Tabellen, Deklarationen         │
├─────────────────────────────────────────────────────────────┤
│ Intern – NICHT öffentliche API                             │
│   src/_parser.hh      – ISOBaseParser, ISOFieldParser<>    │
│   src/fmt_types.hh    – IFE_CHAR, IFB_NUMERIC, … Aliase   │
│   src/_padder.hh      – Padding-Implementierung            │
└─────────────────────────────────────────────────────────────┘
```

**Regel:** `src/`-Dateien binden public-Header mit `<iso8583/...>` ein, nie mit `"..."`.
**Regel:** `_prefixer.hh` und `_encoder.hh` sind nur noch Compat-Stubs → `_codec.hh` verwenden.

---

## Codec-Architektur (Option A: Explizite Instanziierungen)

Template-Implementierungen liegen in:
- `include/iso8583/_codec.hh` – Deklarationen + Enums (öffentlich)
- `include/iso8583/detail/_codec_impl.hh` – Template-Bodies (nicht direkt einbinden)
- `src/_codec.cc` – Explizite Instanziierungen aller verwendeten Kombinationen

**Neue Kombination hinzufügen:**
1. Typ-Alias in `src/fmt_types.hh` anlegen
2. Entsprechende `INST_*`-Zeilen in `src/_codec.cc` ergänzen

---

## Schlüsseltyp

```cpp
// config.h:
namespace tng { using key_type = int16_t; }
#define TNG_KEY_TYPE ::tng::key_type   // Makro für Rückwärtskompatibilität
```

**Warum `using` statt `#define int16_t`:** MSVC löst `#define`-Makros beim Vergleich
virtueller Override-Signaturen nicht korrekt auf → Compiler-Fehler C2555.

---

## ISO 8583 Längen-Constraints

```
L    = max.    9  (1 Stelle)
LL   = max.   99  (2 Stellen)
LLL  = max.  999  (3 Stellen)
LLLL = max. 9999  (4 Stellen)
```

255 ist kein gültiger LL-Wert. Testdaten müssen diese Grenzen einhalten.

---

## Bekannte Fallstricke & gelöste Bugs

### MSVC: Unsigned-Wraparound in rückwärts-laufenden Loops
```cpp
// FALSCH: size_t ist unsigned → i-- nach 0 = SIZE_MAX → Crash
for (auto i = len - 1; i >= 0; --i) { ... }

// RICHTIG:
for (std::ptrdiff_t i = (std::ptrdiff_t)len - 1; i >= 0; --i) {
    b[(std::size_t)i] = ...;
}
```

### CTest / Windows: Nicht-ASCII in Testnamen
`catch_discover_tests()` übergibt Testnamen als Filter-String durch die Windows-Konsolenkodepage.
**UTF-8-Zeichen (–, Umlaute) korrumpieren den Filter → Test wird nie gefunden → immer Failed.**
**Regel: Testnamen ausschließlich ASCII.**

### BCD-Truncation: Bytes vs. Ziffern
```cpp
// FALSCH: trunc ist Bytes, l ist BCD-Ziffern
if (trunc < l) l = trunc;   // 6 Bytes < 12 Ziffern → trunciert fälschlicherweise

// RICHTIG: Vergleich in Bytes
const std::size_t needed = required_sz_for_as<BCD>(l);
if (available_bytes < needed) l = available_bytes * 2;
```

### HEX_EBCDIC Off-by-one
```cpp
// FALSCH: 0xC0 als Basis → 'A'(0xC1) wird zu 11 statt 10
int h = hi < 0xF0u ? 10 + hi - 0xC0 : hi - 0xF0;

// RICHTIG: 0xC1 als Basis
int h = hi < 0xF0u ? 10 + hi - 0xC1 : hi - 0xF0;
```

### Logging-Makros: `__VA_ARGS__` trailing comma (GCC/Clang)
```cpp
// FALSCH: bei Aufruf ohne extra Argumente entsteht ein trailing comma
#define TNG_LOG_ERROR(fmt, ...) LOG_ERROR(logger, fmt, __VA_ARGS__)
// → LOG_ERROR(logger, "msg", )  ← Syntaxfehler auf GCC/Clang

// RICHTIG: ## entfernt das Komma wenn __VA_ARGS__ leer ist
#define TNG_LOG_ERROR(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)
```
`##__VA_ARGS__` ist eine GNU-Extension, wird aber von GCC, Clang und MSVC unterstützt.
`emplace` überschreibt keine vorhandenen Einträge (gibt `false` zurück).
**Gelöst:** `insert_or_assign` verwenden.

### `encode_length` Buffer-Sizing
`encode_length` schreibt per Index ohne Bounds-Check. Buffer muss vorher die richtige
Größe haben oder die Funktion muss ihn selbst sizen (`if (b.size() < len) b.resize(len, 0)`).
**Aktueller Stand:** Funktion setzt Buffer selbst auf korrekte Größe.

---

## Wire-Position Tracking

Jedes `ISOComponentPtrBase` hat:
- `wire_offset()` – absolute Byte-Position im Original-Buffer
- `wire_length()` – Byte-Länge inklusive Längen-Prefix

**Verantwortlichkeiten:**
- `ISOBaseParser` (`_parser.cc`) setzt `wire_offset(base_offset + consumed)` **vor** dem Unparse
- `ISOFieldParser` (`_parser.hh`) setzt `wire_length(total)` **nach** dem Unparse (kennt erst dann die tatsächliche Länge)
- Nested-Felder: `child_base_offset = o + parsed_length<pe_, l_>()` (Offset nach dem Längen-Prefix)

---

## Encoding-System

**Auflösungsreihenfolge:**
```
Feld-encoding  >  globales YAML-encoding  >  "" (nur für neutrale Formate)
```

**Encoding-neutrale Formate** (ignorieren globales und Feld-Encoding immer):
`BINARY`, `LBINARY`, `LLBINARY`, `LLLBINARY`, `LLLLBINARY`, `BITMAP`, `NOP`, `UNUSED`

**YAML-Beispiel:**
```yaml
spec: "Mastercard Spec"
encoding: ebcdic          # global – alle Felder ohne eigenes encoding erben dies

fields:
  "004":
    format: numeric
    encoding: bcd          # überschreibt globales ebcdic für dieses Feld
  "052":
    format: binary         # encoding-neutral – ignoriert globales encoding
```

---

## Offene Punkte / TODOs

### Kritisch
- **`parse()`-Pfad für `std::string` ist unvollständig** (`src/_parser.hh`, Zeile ~378):
  Der Buffer wird alloziert und der Längen-Prefix kodiert, aber die eigentlichen Nutzdaten
  werden **nie** in `b_img` geschrieben. `return b_img` gibt einen Null-gefüllten Buffer zurück.
  Felder → Bytes (Serialisierung) funktioniert damit nicht korrekt.

- **`bcd2str()` in `_components.cc` ist ein Stub** (gibt immer `""` zurück, Zeile ~508).
  Wird für BCD-Konvertierung in Header-Klassen benötigt.

### Mittelfristig
- `_tz.hh` / `_tz.cc` / `_tz_private.hh` fehlen (Zeitzone-Handling für Datums-/Zeitfelder).
  `_date.hh` und `_currency.hh` sind öffentlich, aber die Implementierung fehlt noch.

- `ISOComponentv2` / `ISOMessagev2` in `_components.hh` sind auskommentierter Legacy-Code.
  Entweder vollständig entfernen oder reaktivieren.

- Testabdeckung `parse()`-Pfad (Felder → Bytes) fehlt komplett sobald der Bug oben behoben ist.

- TLV/Tagged-Subfield-Format (`LLL | TCC | SubfieldID | SubLen | Daten`) ist mit der
  aktuellen Spezifikation nicht darstellbar – erfordert Parser-Erweiterung.

### Langfristig
- `ISOArena` (`_memory.hh`): Bump-Allocator vorhanden, aber nicht eingebunden.
  **Nicht** für `shared_ptr`/`ISO_MAP` geeignet (Inkompatibilität mit Referenzzählung
  und `tsl::robin_map`-Allocator-Interface). Sinnvoll als `thread_local` per-Thread-
  Parsing-Buffer für `std::vector<uint8_t>`-Zwischenbuffer, sobald Profiling einen
  Allokations-Bottleneck zeigt.

- `std::byte` statt `uint8_t`: **Nicht empfohlen.** `std::byte` erlaubt keine Arithmetik
  (kein `+`, `-`, kein impliziter Cast zu `int`). BCD/EBCDIC-Dekoder würden überall
  `std::to_integer<int>()` benötigen. Industrie-Standard für Protokoll-Libs ist `uint8_t`.

---

## Dateinamen-Konvention

| Präfix | Bedeutung |
|--------|-----------|
| `_` (underscore) | Interne/private Datei (nicht direkt einbinden) |
| `ISO` | Öffentliche Klasse (z.B. `ISOMessage`, `ISOSpec`) |
| `IFE_` | EBCDIC-Feldtyp-Alias |
| `IFA_` | ASCII-Feldtyp-Alias |
| `IFB_` | BCD/Binary-Feldtyp-Alias |
| `IF_`  | Encoding-neutraler Feldtyp-Alias |

---

## Tests

**Framework:** Catch2 v3 (`Catch2::Catch2WithMain`, kein eigenes `main.cc`)

**Dateien:**
- `test_prefixer_encoder.cc` – White-Box: `decode_length`, `encode_length`, `as<>`, ISO-Grenzwerte
- `test_field_parser.cc` – Black-Box: `ISOFieldParser<>` unparse, wire_position, Fehlerfälle
- `test_iso_message.cc` – `ISOMessage` CRUD, MTI-Klassifikation, `to_json`
- `test_full_unparse.cc` – End-to-End mit realen Byte-Buffern, wire_offset-Akkumulation
- `test_spec_loader.cc` – `SpecDecoder::loadFromYaml()`, Encoding-Override, RAII-TempYaml

**CI:** GitHub Actions (`.github/workflows/ci.yml`)
- Matrix: `windows-latest` + `ubuntu-latest` × `Debug` + `Release`
- Branch-Protection: `ci-success`-Job als Required Status Check eintragen
- `--no-tests=error`: schlägt fehl wenn keine Tests gefunden werden (verhindert false positives)
