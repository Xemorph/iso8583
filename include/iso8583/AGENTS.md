# AGENTS.md — libiso8583 öffentliche API

Diese Datei ist die primäre Referenz für KI-Agenten und Code-Generierungs-
werkzeuge, die mit dem Verzeichnis `include/iso8583/` arbeiten. Lies sie,
bevor du Code generierst oder änderst, der libiso8583 nutzt.

---

## Was diese Bibliothek macht

libiso8583 ist eine C++20-Bibliothek zum **Parsen und Bauen von ISO-8583-
Finanznachrichten** — dem Protokoll von Visa, Mastercard und den meisten
Zahlungsverkehrssystemen.

Kern-Workflow:

```
YAML-Spec   ──► SpecDecoder::loadFromYaml()      ──► ISOParserPtrBase
           │
           └──► SpecDecoder::loadBothFromYaml()   ──► ISOParserPtrBase
                                                      └──► ISOSpec (Introspektion)

ISOParserPtrBase + rohe Wire-Bytes ──► Message::unparse() ──► Message (dekodiert)
Message                        ──► ISOParserPtrBase::parse() ──► Wire-Bytes
```

---

## Namespace und Schlüsseltyp

```cpp
// Alles lebt im Namespace iso8583::
using namespace iso8583;   // optionale Bequemlichkeit

// DE-Nummern (Data-Element-Keys) sind standardmäßig int16_t
TNG_KEY_TYPE key = 2;   // entspricht iso8583::key_type = int16_t
// Sonderwert -1 = die Wurzel-Message (kein Sub-Feld)
// Sonderwert -2 = intern reserviert für TLV-TCC-Felder
```

### Konfigurierbarer Schlüsseltyp (BER-TLV / EMV)

`TNG_KEY_TYPE` ist standardmäßig `int16_t` (-32768…32767). Reale
2-Byte-EMV-Tags mit erstem Byte `>= 0x80` (z. B. `9F26`, `5F24` — praktisch
alle `9Fxx`/`5Fxx`-Tags) ergeben als Big-Endian-Wert `> 32767` und passen
nicht hinein.

Für volle BER-TLV/EMV-Tag-Unterstützung `ISO8583_BERTLV` definieren (schaltet
automatisch auf `int32_t` um):

```cmake
# CMake
target_compile_definitions(your_target PUBLIC ISO8583_BERTLV)
# oder beim Konfigurieren:
cmake -DISO8583_BERTLV=ON ...
```

```cpp
// Manuell (ohne CMake-Option), VOR dem ersten iso8583-Include:
#define ISO8583_BERTLV
#include <iso8583/iso8583.h>
```

Wer einen noch größeren (oder anderen) Schlüsseltyp braucht, kann ihn frei
festlegen — das überstimmt auch `ISO8583_BERTLV`:

```cpp
#define ISO8583_KEY_TYPE int64_t
```

**Wichtig (ABI):** Der Schlüsseltyp fließt in die virtuelle Signatur von
`ISOComponentPtrBase::key()` ein. Wird libiso8583 als Shared Library gebaut,
muss JEDER Konsument mit demselben Wert übersetzt werden. Über CMake genügt
`target_link_libraries(... iso8583::iso8583)`, da `ISO8583_BERTLV` als
`PUBLIC`-Compile-Definition automatisch weitergereicht wird. Bei manueller
Einbindung ohne CMake-Target muss das Makro von Hand in Bibliothek UND allen
Konsumenten identisch gesetzt werden.

---

## Public-Header — was zu includieren ist

| Header | Wann verwenden |
|---|---|
| `<iso8583/iso8583.h>` | Immer: zieht alles darunter mit |
| `<iso8583/ISOMessage.hh>` | Arbeiten mit Nachrichten und Feldtypen (`Message`, `OpaqueField`, `BinaryField`, …) |
| `<iso8583/ISOSpec.hh>` | Laden einer YAML-Spec; Introspektion des geladenen Specs (`ISOSpec`, `SpecFieldInfo`, `SpecFieldFormat`) |
| `<iso8583/ISOLog.hh>` | Konfigurieren des Bibliothek-Loggings |
| `<iso8583/ISOUtils.hh>` | Hilfsfunktionen (`utils::makeBitmap()`, `flatten()`, `getOrThrow()`, …) |
| `<iso8583/POSDataCode.hh>` | `pos::POSDataCode` (Dekodierung der POS-Fähigkeiten, DE61) |
| `<iso8583/Currency.hh>` | `currency::Currency` (ISO-4217-Nachschlagentabelle) |
| `<iso8583/ISOParser.hh>` | Implementieren eines eigenen Parsers (fortgeschritten) |
| `<iso8583/_codec.hh>` | Codec-Enums/-Funktionen direkt verwenden (fortgeschritten) |

Header in `detail/` sind Implementierungsdetails — sie dürfen **nicht**
direkt eingebunden werden.

---

## Feldtypen

| C++-Typ | Wertetyp | Typischer Einsatz |
|---|---|---|
| `iso8583::OpaqueField` | `std::string` | Text, Nummern, EBCDIC/BCD als Zeichenkette |
| `iso8583::BinaryField` | `std::vector<uint8_t>` | PIN-Block, ICC/EMV-Daten, Kryptogramme |
| `iso8583::FastBinaryField` | `std::vector<std::byte>` | Binär mit `std::byte`-Speicher (intern) |
| `iso8583::Bitmap` | `dynamic_bitset<>` | Primäre/sekundäre Bitmap |
| `iso8583::CodeField` | `int32_t` | Numerische Antwortcodes |
| `iso8583::Message` | `ISO_MAP` (Feld-Map) | Composite / verschachtelte Sub-Nachricht |
| `iso8583::ISOTaggedField` | — | Dekodiertes TLV-SE (Tag + verwiesenes Feld) |

Alle Typen erben von `ISOComponentPtrBase` und werden immer in
`std::shared_ptr` gehalten.

> Die alten Namen `ISOOpaqueField`, `ISOBinaryField`, `ISOBitmap`,
> `ISOCodeField`, `ISOFastBinaryField` und `ISOMessage` sind deprecated
> Aliase (ein Release-Zyklus) — neuen Code immer mit den neuen Namen
> schreiben.

---

## Eine Nachricht dekodieren (unparse = Wire-Bytes → Felder)

```cpp
#include <iso8583/iso8583.h>

// 1. Spec einmal laden — zwischenspeichern, es ist teuer
auto parser = iso8583::spec::SpecDecoder::loadFromYaml("mastercard.yml");

// 2. Nachricht anlegen und Parser zuordnen
auto msg = std::make_shared<iso8583::Message>();
msg->parser(parser);

// 3. Dekodieren
std::vector<uint8_t> raw = get_from_network();
msg->unparse(msg, raw);

// 4. Felder lesen
if (auto pan = msg->tryGet<OpaqueField>(2))       // optional – Feld kann fehlen
    std::cout << (*pan)->value() << "\n";

auto amount = iso8583::utils::getOrDefault<OpaqueField>(*msg, 4, "000000000000");
auto mti_str = msg->mti();  // z. B. "0200"

// 5. MTI-Klassifikation
if (msg->isAuthorization() && msg->isRequest())  { /* 01xx */ }
if (msg->isFinancial()     && msg->isResponse()) { /* 021x */ }
// weitere Klassifikationen: isFileAction(), isReversal(), isChargeback(),
// isReconciliation(), isAdministrative(), isFeeCollection(),
// isNetworkManagement(), isRetransmission()
```

---

## Eine Nachricht bauen (parse = Felder → Wire-Bytes)

```cpp
auto msg = std::make_shared<iso8583::Message>("0200");  // MTI im Konstruktor
msg->parser(parser);

// Einfache Felder
msg->set(2,  "4111111111111111");   // PAN          – OpaqueField
msg->set(3,  "000000");             // Proc. code
msg->set(4,  "000000010000");       // Amount (Cents)
msg->set(11, "000001");             // STAN

// Binärfeld — Wert muss eine Hex-Zeichenkette in Großbuchstaben sein
msg->set(52, "0102030405060708");   // PIN-Block     – BinaryField

// Verschachtelte Felder über Punkt-Notation
msg->set("48.72.1", "ABC");         // DE48 → SE72 → Tag 1
msg->set("3.1",     "00");          // DE3, Sub-Feld 1

// In Wire-Bytes kodieren
std::vector<uint8_t> wire = parser->parse(msg);
```

---

## Felder lesen — den richtigen accessor wählen

```cpp
// get<T>()  — liefert nullptr, wenn das Feld fehlt oder der Typ abweicht
auto f = msg->get<OpaqueField>(2);
if (f) use(f->value());

// tryGet<T>() — liefert std::optional<shared_ptr<T>>
if (auto opt = msg->tryGet<OpaqueField>(35))
    use((*opt)->value());

// tryGetValue<T>() — liefert std::optional<ValueType> (Kopie)
if (auto val = msg->tryGetValue<OpaqueField>(11))
    use(*val);  // val ist std::optional<std::string>

// tryGetValueRef<T>() — liefert optional reference_wrapper (zero-copy)
if (auto ref = msg->tryGetValueRef<BinaryField>(55))
    use(ref->get());  // Zero-Copy-Zugriff auf std::vector<uint8_t>

// utils-Hilfsfunktionen
auto pan  = iso8583::utils::getOrThrow<OpaqueField>(*msg, 2);   // wirft, wenn fehlt
auto curr = iso8583::utils::getOrDefault<OpaqueField>(*msg, 49, "978");
iso8583::utils::ifPresent<OpaqueField>(*msg, 11, [](const std::string& stan) {
    log("STAN: {}", stan);
});
```

---

## Spec-Introspektion (ISOSpec)

`loadBothFromYaml` liefert sowohl einen Parser als auch ein `ISOSpec`-Objekt,
mit dem sich die Struktur der geladenen Spec zur Laufzeit abfragen lässt.

```cpp
#include <iso8583/iso8583.h>

auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mastercard.yml");

// Parser wie gewohnt an die Nachricht hängen
msg->parser(parser);

// --- Introspektion ---

// Name und globale Encoding aus den YAML-Keys "spec:" / "encoding:"
spec->name();      // z. B. "Mastercard GMC"
spec->encoding();  // z. B. "EBCDIC"

// Prüfen, ob ein DE definiert ist
spec->has(2);      // true, wenn DE002 existiert

// Ein einzelnes Feld abfragen
if (auto f = spec->field(2)) {
    f->description;            // "Primary Account Number"
    f->format.type;            // "CHAR"
    f->format.prefix_digits;   // 2  (= LL-Präfix)
    f->format.max_length;      // 19
    f->encoding;               // "EBCDIC"
    f->is_nested;              // false
    f->is_bitmap;              // false
}

// Verschachteltes Feld und seine Kinder abfragen
if (auto pos = spec->field(61)) {
    pos->is_nested;            // true
    pos->children.size();      // Anzahl der Sub-Felder
    pos->children[0].description;           // "POS Terminal Attendance"
    pos->children[0].format.prefix_digits;  // 0  (fix)
}

// Alle definierten DEs in Key-Reihenfolge durchlaufen
for (const auto& f : spec->fields())
    fmt::print("DE{:03d}  {:<30}  {}{}  max={}\n",
        f.key, f.description,
        f.format.prefix_digits > 0
            ? std::string(f.format.prefix_digits, 'L') : "FIX",
        f.format.type,
        f.format.max_length);
```

### SpecFieldFormat-Mitglieder

| Mitglied | Typ | Bedeutung |
|---|---|---|
| `type` | `std::string` | Basisformat: `"CHAR"`, `"NUMERIC"`, `"BINARY"`, `"BITMAP"`, `"NOP"`, `"REMAINING"` |
| `prefix_digits` | `int` | `0`=fix, `1`=L, `2`=LL, `3`=LLL, `4`=LLLL |
| `max_length` | `int` | Maximale Nutzdatenlänge in logischen Einheiten (Zeichen, Ziffern oder Bytes) |

### SpecFieldInfo-Mitglieder

| Mitglied | Typ | Bedeutung |
|---|---|---|
| `key` | `TNG_KEY_TYPE` | DE-Nummer |
| `description` | `std::string` | Menschlesbarer Name aus der Spec-YAML |
| `format` | `SpecFieldFormat` | Wire-Format (s. o.) |
| `encoding` | `std::string` | `"EBCDIC"`, `"ASCII"`, `"BCD"`, `"BINARY"`, `""` (neutral) |
| `is_nested` | `bool` | `true` für composite Sub-Nachrichten-DEs |
| `is_bitmap` | `bool` | `true` für das Bitmap-DE |
| `children` | `vector<SpecFieldInfo>` | Sub-Felder verschachtelter DEs (leer bei Blättern) |

### Wann loadFromYaml vs. loadBothFromYaml

| | `loadFromYaml` | `loadBothFromYaml` |
|---|---|---|
| Nachrichten parsen/bauen | ✓ | ✓ |
| Feldnamen/-formate zur Laufzeit abfragen | ✗ | ✓ |
| UI-Feldlisten, Validatoren, Dokumentation | ✗ | ✓ |
| Overhead | minimal | ein zusätzlicher Durchlauf über die Feld-Map |

### Caching (`loadFromYamlCached`, `loadBothFromYamlCached`)

Beide Cached-Varianten halten das Ergebnis prozessweit pro absolutem Pfad.
Ein wiederholter Aufruf für dieselbe (ungeänderte) Datei ist nur ein
sperrgeschützter Map-Lookup — kein YAML-Parsing, kein Preprocessing, kein
Rebuild des Feld-Parser-Baums. Bevorzugt `...Cached` überall dort, wo dieselbe
Spec während der Prozesslebensdauer mehr als einmal geladen wird.

- `CacheValidation::CheckEveryCall` (Default): prüft bei jedem Call die
  `last_write_time()` der Datei — erkennt Änderungen automatisch, kostet pro
  Call einen `stat()`-artigen Systemaufruf.
- `CacheValidation::TrustUntilInvalidated`: kein Dateisystem-Zugriff bei
  Cache-Treffer (~25 ns), erkennt Dateiänderungen aber **nicht** — selbst mit
  `SpecDecoder::invalidateCache(path)` invalidate, wenn die Datei sich geändert
  hat (z. B. über einen eigenen File-Watcher).
- `SpecDecoder::clearCache()` leert beide Caches vollständig.

```cpp
// Startup: Parser + Spec geladen und prozessweit wiederverwendet
static auto [parser, spec] =
    iso8583::spec::SpecDecoder::loadBothFromYamlCached("mastercard.yml");
```

### SpecLoadOptions & Include-Sandbox (0.3.0)

Die `...FromYaml{,Cached}`-Overloads mit `const SpecLoadOptions&` steuern das
Vertrauensmodell beim Laden. Specs können Third-Party-/Remote-Herkunft haben;
der Lade-Pfad ist **fail-closed** und beschränkt:

```cpp
auto [parser, spec] =
    iso8583::spec::SpecDecoder::loadBothFromYaml("third_party.yml",
        iso8583::spec::SpecLoadOptions{
            .trackSourceMap  = true,
            .sandbox         = true,   // !include_files außerhalb der roots → ablehnen
            .roots           = {"/opt/specs"}, // leer → Verzeichnis der Top-Level-Spec
            .allowSmapWrite  = true,   // .smap-Sidecar nur innerhalb der roots
            .maxSpecBytes    = 32u*1024u*1024u,  // pro Quell-Datei, beim Streamen erzwungen
            .maxIncludeFiles = 1024,             // distinct Dateien pro Load
            .maxSmapBytes    = 16u*1024u*1024u,  // übergroße Sidecars → verwerfen/regenerieren
        });
```

- **Include-Sandbox (Default an):** `!include_files`-Einträge, die außerhalb
der `roots` landen — `../`-Traversals, absolute/UNC-Pfade, Symlink-Escapes —
werden **abgelehnt** (lexikalisch und, falls die Datei existiert, über den
kanonisierten, Symlink-auflösenden Pfad). Leere `roots` = Verzeichnis der
Top-Level-Spec (Nutzer-Input, selbst nicht sandboxed).
- **`fields:` muss eine nicht-leere Map sein** — leere Maps/Sequenzen und
  nicht-numerische DE-Keys werfen positionsgenau (`SpecValidationError`), nie
ein roher `std::stoi`.
- **Migration (Betrifft dich, falls):** Top-Level-Specs, die `!include_files`
  *außerhalb* des eigenen Verzeichnisses nutzen, brauchen jetzt explizite
  `roots` (oder bewusst `sandbox=false` für voll vertrauenswürdige Spec-Bäume).
  Die alten `bool trackSourceMap`-Overloads bleiben source-kompatibel (bauen
  intern Default-Optionen).

---

## YAML-Spezifikationsformat

```yaml
spec:     "My Spec"
encoding: ebcdic          # global: ascii | bcd | ebcdic | binary
strict:   true            # (Default) fail-closed: Trunkatur/Übergröße/ungültiges EBCDIC → throw

definitions:              # wiederverwendbare Bausteine
  pan_field:
    type: scalar
    format: llchar
    length: 19

fields:
  "000":                  # MTI — immer Key 000
    type: scalar
    format: numeric
    length: 4
  "001":                  # Bitmap — immer Key 001
    type: scalar
    format: bitmap
    length: 8
  "002": !use pan_field   # Definition referenzieren
  "003":                  # explizites Feld
    type: scalar
    format: numeric
    length: 6
    encoding: bcd         # globale Encoding für dieses Feld überschreiben
  "055":                  # variable binäre Länge
    !merge
    - !template LLL(BINARY, 255)
    - description: "ICC Data"
  "056":                  # BER-TLV-Container (EMV ICC-Daten) — nur scalar
    format: lllbertlv     # LLL-Präfix + BER-TLV-Payload (ISO/IEC 8825-1)
    length: 999
    description: "ICC Data (BER-TLV)"
    # Kein 'type: nested', 'children' oder 'tlv:'-Block — BER-TLV-Tags sind
    # dynamisch (keine feste, vorab deklarierte SE-Liste), daher nicht nötig.
  "057":                  # BER-TLV mit Tag-Beschreibungen (optional)
    type: nested
    format: lllbinary
    length: 999
    description: "ICC Data with declared tags"
    tlv:
      ber: true
    children:             # Map = TLV-Modus; Keys sind HEX-Tags (ber: true)
      "9F26":              # reales EMV-Tag: Application Cryptogram
        format: binary
        length: 8
        description: "Application Cryptogram"
      "5A":                 # reales EMV-Tag: Application PAN
        format: binary
        length: 10
        description: "Application PAN"
    # Momentan wird nur 'description' an das dekodierte Feld übergeben (jedes
    # SE/Tag wird weiterhin als rohes BinaryField dekodiert — 'format'/'length'
    # hier sind nur Dokumentation und werden beim Decode noch nicht erzwungen).
    # Nicht deklarierte Tags fallen automatisch auf eine generische
    # "SE<n>“-Beschreibung zurück.
  "048":                  # Mastercard-artiges fixes TLV — SE-Keys DEZIMAL
    type: nested
    format: lllchar
    length: 999
    description: "Additional Data"
    tlv:
      tag_bytes: 2
      len_bytes: 2
    children:
      "26":                 # dezimale SE-Nummer (NICHT hex — hier kein 'ber: true')
        format: char
        length: 10
        description: "Some Subelement"
      "0x1A":               # explizites '0x'-Präfix erzwingt hier hex (== 26)
        format: char
        length: 5
        description: "Same Subelement, written in hex"
  "061":                  # verschachteltes Feld mit Kindern
    type: nested
    format: binary
    length: 26
    children:
      - format: numeric
        length: 1
        description: "POS Terminal Attendance"
      - format: remaining   # konsumiert alle restlichen Bytes — kein Längenpräfix
        description: "POS Postal Code"
```

**Direktiven:**
- `!include_files [a.yml, b.yml]` — externe Definitionsdateien laden
  (Root-Ebene). **Muss von einem `---`-Dokumenttrenner gefolgt werden**,
  bevor der Rest der Spec folgt (`spec:`, `encoding:`, `fields:`, …) —
  `!include_files` und der verbleibende Inhalt sind zwei separate YAML-
  Dokumente in derselben Datei. Weglassen von `---` ist ein Parse-Fehler
  (strikt YAML 1.2; frühere Versionen dieser Bibliothek tolerierten es,
  reale Spec-Dateien, die für diese Versionen geschrieben wurden, brauchen
  evtl. einmalig ein `---` nach dem `!include_files [...]`-Block). Beispiel:
  ```yaml
  !include_files
  - common_definitions.yml
  ---
  spec: "My Spec"
  encoding: ebcdic
  fields:
    "000": !use mti_field
  ```
- `!use <name>` — benannte Definition substituieren
- `!template P(F, N)` — Kurzform für variable Längen, z. B. `LL(CHAR, 19)` → `{ type: scalar, format: LLCHAR, length: 19 }`
- `!merge [...]` — Maps mergen, spätere Einträge überschreiben frühere
- `!include <name>` — **deprecated** Alias für `!use` (emittiert eine Warnung)

**Feld-/Spec-Attribute (0.3.0):**
- `strict: true|false` (Spec-Wurzel, Default **true**) — Strict/Fail-closed-Modus;
  `parser.strict(bool)` steuert ihn zur Laufzeit (siehe „Strict-Modus" unten).
- `sensitive: true` (Feld, `children`-Eintrag oder `definitions:`) — der
  **Wert** wird in `dump()`/`operator<<` und Log-Ausgaben als `***` maskiert
  (PCI); `value()`/`to_json()` bleiben unmaskiert (siehe „Logging"). Auf
  nested/TLV/BERTLV-Containern verbreitet es sich auf alle Children/Tags.

**Format/Encoding-Kombinationen:**
- `numeric`, `char`, `binary`, `bitmap`, `nop`
- `llchar`, `lllchar`, `llbinary`, `lllbinary`, `llllbinary`
- `remaining` — liest alle Bytes, die im Elternpuffer übrig sind
- `bertlv` (optional mit `l`/`ll`/`lll`/`llllbertlv`) — BER-TLV-Container
  (ISO/IEC 8825-1, EMV Book 3 Annex B); **nur scalar**, darf NICHT mit
  `type: nested`, `children` oder einem `tlv:`-Block kombiniert werden.
  Erzeugt zur Laufzeit eine verschachtelte `Message`, deren Kind-Keys die
  rohen BER-Tag-Werte sind (siehe `BERTLVParser` in `src/_tlv.hh`). Benötigt
  `ISO8583_BERTLV` (s. o.), wenn ein Tag außerhalb des `int16_t`-Bereichs
  liegt, z. B. reale 2-Byte-EMV-Tags wie `9F26`.

**TLV-`children`-Key-Notation:** Wenn `type: nested` mit einem expliziten
`tlv:`-Block und einer `children:`-**Map** (im Gegensatz zur `bertlv`-
Format-Kurzform oben, die gar keine `children` braucht) kombiniert wird,
benennt jeder Key eine SE-Nummer oder ein BER-Tag:
- `tlv: {ber: true}` → Keys sind **hexadezimal** (`"9F26"`, `"5A"`, `"1A"`),
  passend zur EMV-Book-3-/ISO-7816-Schreibweise.
- Fixformat-TLV (Mastercard/Visa, mit `tag_bytes`/`len_bytes`) → Keys sind
  **dezimale** SE-Nummern (`"26"`), unverändert zu früheren Versionen.
- Ein explizites `"0x"`-Präfix (z. B. `"0x1A"`) erzwingt Hexadezimal,
  unabhängig vom TLV-Modus.
- Momentan wird nur `description` an das dekodierte Feld übergeben (jedes
  SE/Tag wird weiterhin als rohes `BinaryField` dekodiert — `format`/`length`
  in `children` bleiben Dokumentation, noch nicht beim Decode erzwungen).
  Tags ohne einen deklarierten `children`-Eintrag fallen automatisch auf die
  generische `"SE<n>"`-Beschreibung zurück.

**Encodings:** `ascii`, `bcd`, `ebcdic`, `binary`

---

## Strict-Modus & Fail-closed-Verhalten (0.3.0)

Die Bibliothek ist im Default **strict** (Spec-Wurzel `strict: true`, Default
`true`; zur Laufzeit `parser.strict(bool)` / `strict() const`). Strict bedeutet
**fail-closed**: statt abgeschnittene Frames still zu clampen, überdimensionale
Felder zu verwerfen oder ungültige EBCDIC-Bytes auf `.` zu mappen, wirft der
Pfad einen **positionsgenauen** `std::runtime_error` mit dem `[ISO8583]`-
Präfix (Feld, Offset, Byte, Hexdump):

- Feld am Pufferende abgeschnitten (erwartet X Bytes, verbleiben Y)
- Serialisierung größer als Feld-Maximum (Frame würde fehlerhaft → Werfen)
- Längenpräfix am Pufferende abgeschnitten
- ungültiges EBCDIC-Byte (tabelle- und Orakelpfad)
- Bitmap-Byte am Pufferende; zu kurzer Wire-Header (WLP-FO 93 B / BASE1)
- TLV: explizite `offset+N`-Prechecks (`read_num`/BCD), `BerLength` > 8
  Längenbytes abgelehnt, `store_se` warnt/lehnt nicht-repräsentierbare
  BER-Tags ab (keine stille `static_cast`-Trunkierung)

**Escape-Hatch für tolerante Integratoren:** `strict: false` in der Spec oder
`parser.strict(false)` → altes clamp/WARN/`.`-Sentinel-Verhalten, aber nie
*still* (jeder Fall loggt mindestens WARN/ERROR).

---

## EBCDIC-Konvertierung & Determinismus (0.3.0)

EBCDIC↔ASCII ist **vollständig tabellen-getrieben** (IBM-1047) —
`kEbcdicToAscii`/`kAsciiToEbcdic`/`kEbcdicValid` in `include/iso8583/_codec.hh`
— ohne Laufzeit-Converter (kein libiconv, kein ICU). Die Tabellen sind gegen
ein exakt gepinntes **ICU-78.3**-Orakel verifiziert
(`tools/generate_ebcdic_tables/`, Build-/CI-only; ICU wird **nie** in die
Laufzeit-Targets verlinkt) und über eine Cross-Toolchain-Diff stabil gehalten:
gleiche Bytes + gleiche Spec liefern auf allen Toolchains/Plattformen das
gleiche Ergebnis. Der **Strict-Whitelist** ist bewusst *stärker* als ICU
(ICU 78.3 konvertiert alle 256 Bytes; Strict akzeptiert nur die 85
IBM-1047-Druck-/Ziffern-Bytes E2A bzw. 84 mappablen ASCII-Zeichen A2E).
`libiconv`/`ISO8583_ENABLE_ICONV` sind **deprigiert** (Removal 0.4) und
dienen nur noch als Übergangs-Fallback — der Standard-EBCDIC-Pfad nutzt sie
nicht.

---

## Logging

```cpp
#include <iso8583/ISOLog.hh>

// Option A — nur Level (Default ist WARN)
iso8583::log::setLevel(iso8583::log::Level::DEBUG);

// Option B — eigener Logger
class MyLogger : public iso8583::log::ISOLogger {
public:
    void log(iso8583::log::Level level, std::string_view file,
             int line, std::string_view message) override {
        fmt::print("[iso8583] {}\n", message);
    }
};
static MyLogger g_logger;
iso8583::log::setLogger(&g_logger);

// Option C — Quill-Bridge (wenn libiso8583 eine DLL ist)
// quill VOR ISOLog.hh includieren, um QuillBridge zu aktivieren:
#include <quill/LogMacros.h>
#include <iso8583/ISOLog.hh>
static iso8583::log::QuillBridge bridge(myQuillLogger);
iso8583::log::setLogger(&bridge);
iso8583::log::setLevel(iso8583::log::Level::DEBUG);
```

**Wichtig (DLL):** Nicht `setQuillLogger()` verwenden, wenn libiso8583 eine
DLL ist. Quills Prozess-spezifisches Singleton wird an der DLL-Grenze
geteilt — stattdessen `QuillBridge` verwenden, damit die Log-Makros in der
Quill-Instanz des hostenden EXE expandiert werden.

---

## Wire-Positions-Tracking

Nach `unparse()` trägt jedes Feld seine Position im Originalpuffer mit sich:

```cpp
auto de2 = msg->get<OpaqueField>(2);
de2->wire_offset();  // Byte-Offset im Original-Rohpuffer
de2->wire_length();  // verbrauchte Bytes (inkl. Längenpräfix)
```

Mit `iso8583::utils::flatten()` eine flache Map aller Blätterfelder erhalten:

```cpp
auto flat = iso8583::utils::flatten(*msg);
// flat["2"]       = "4111111111111111"
// flat["48.72.1"] = "ABC"
```

---

## Header

```cpp
// Vor dem Kodieren/Dekodieren anhängen
auto hdr = std::make_shared<iso8583::BASE1Header>("000001", "000002");
msg->header(hdr);

// Nach dem Dekodieren lesen
auto h = std::dynamic_pointer_cast<iso8583::BASE1Header>(msg->header());
if (h && h->isRejected())
    handle_reject(h->getRejectCode());
```

Verfügbare Header-Typen: `iso8583::BaseHeader`, `iso8583::BASE1Header`
(Visa), `iso8583::WLP_FOHeader` (Worldline).

`WLP_FOHeader::pack()` serialisiert den **vollen 93-Byte-Header** (4-Byte-
ASCII-Längenpräfix + EBCDIC-Bytes); `parse()`/`unparse()` prüfen das
gepackte Ergebnis und werfen fail-closed bei einem zu kurzen Wire-Header
(WLP < 93 B / BASE1 < erwartete Länge) — nie OOB-Zugriff.

---

## Typische Fehler, die zu vermeiden sind

| Fehler | Korrekte Vorgehensweise |
|---|---|
| `detail/`-Header direkt includieren | `<iso8583/iso8583.h>` verwenden |
| `msg->unparse()` ohne Parser | Zuerst `msg->parser(parser)` aufrufen |
| `int` oder `int32_t` als DE-Keys | `TNG_KEY_TYPE` (`int16_t`) verwenden |
| Deprecated Namen wie `ISOOpaqueField`/`ISOMessage` in neuem Code | `OpaqueField`/`Message` (Namespace `iso8583::`) verwenden |
| `setQuillLogger()` mit einem DLL-Build | `QuillBridge` + `setLogger()` verwenden |
| Ein `BITMAP`-Feld manuell setzen | Bitmaps werden automatisch berechnet |
| Ein `NESTED`-Feld mit einer simplen Zeichenkette setzen | Punkt-Notation: `msg->set("3.1", "00")` |
| Hex-Zeichenkette an ein nicht-BINARY-Feld | Nur `BinaryField` akzeptiert Hex-Eingabe |
| Rohe Bytes an `BinaryField` übergeben | Hex-Zeichenkette in Großbuchstaben, z. B. `"DEADBEEF"` |
| `msg->mti()` vor der Prüfung von `hasMTI()` | Wirft `std::logic_error`, wenn kein MTI gesetzt ist |

---

## Thread-Sicherheit (0.3.0)

Eine `ISOMessage` darf **sicher von mehreren Threads gleichzeitig** genutzt
werden. Alle öffentlichen Eintritte (`set`/`unset`/`has`/`get`/`tryGet`/
`tryGetValue`/`tryGetValueRef`/`reset`/`keys`/`size`/`to_json`/`dump`/`parser`/
`parse`/`unparse`/`header`/`direction`/`hasMTI`/`mti`/`isRequest`/…) nehmen
denselben **rekursiven Message-Lock** genau einmal; interne Aufrufketten
(z. B. `parse → recalcBitmap → set`) laufen unter dem bereits gehaltenen
Lock. Writer und Reader schließen sich aus (ein Lock, **kein** paralleler-
Reader-Modus). `to_json`/`dump` sichern den Feldsatz unter dem Lock und
formatieren außerhalb (kurze Lock-Besitzzeiten).

- **Parser-Objekte sind nach dem Laden immutable** → sicher teilbar über
  Threads und Messages; paralleles `parse`/`unparse` auf *verschiedenen*
  Messages mit demselben Parser ist sicher.
- Logger-Globalen (`setLevel`/`setLogger`/`currentLogger`/`getLevel`) sind
  atomar.
- **Restrisiken (dokumentiert in `ISOMessage.hh`):** `mti()` liefert ein
  `string_view` **in** den mutablen Feld-Speicher — vor threadübergreifender
  Nutzung kopieren: `std::string m = msg->mti();`. `tryGetValueRef` ist eine
  zero-copy-Referenz mit derselben Einschränkung.

---

## Wichtige numerische DE-Bereiche

| Bereich | Bedeutung |
|---|---|
| -1 | Wurzel-`Message` (kein Sub-Feld) |
| 0 | MTI-Slot |
| 1 | Primärer Bitmap-Slot |
| 2–64 | DEs der primären Bitmap |
| 65–128 | DEs der sekundären Bitmap |
| 129–192 | DEs der tertiären Bitmap |
| Sub-Feld-Keys starten bei 0 | Innerhalb der übergeordneten verschachtelten Nachricht indiziert |