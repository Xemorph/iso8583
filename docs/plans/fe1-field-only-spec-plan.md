# Plan: FE-1 — Field-only-Specs: Einzel-Feld-Specs (z. B. DE55/ICC) laden und auf `BinaryField`-Payloads anwenden (0.6.0-Kandidat)

Status: **Freigegeben** (2026-09-21, Konzept im Chat bestätigt, Entscheidungen (a)–(c) wie empfohlen) — Ausführung folgt
Reko-Basis: `main` = `f8db10b` (0.6.0-Kandidat; uncommittetes `pos::POSDataCode`-WIP liegt unabhängig darüber und wird vor FE-1 finalisiert)
Anforderung: „Der Nutzer soll die Möglichkeit haben, eine field-only-Spec zu laden, z. B. eine Feld-Spec zum Parsen von DE55 (ICC Data). Die geladene Spec kann dann auf ein `BinaryField` angewendet werden, das die Daten hält.“

## 0. Entscheidungen (vom Maintainer bestätigt, 2026-09-21)

| # | Entscheidung |
|---|---|
| a | **Getrennter Cache:** Field-only-Specs erhalten einen **eigenen, separaten Loader-Cache** (gleiche LRU ≤ 64, gleiches Publish-then-Verify-/TOCTOU-Protokoll) — analog der bestehenden Getrennt-Cache-Konvention; keine Zusammenlegung mit dem Message-Spec-Cache. |
| b | **`decodeField`-Convenience:** `SpecDecoder::decodeField(parser, binaryField)` wird mitgeliefert — „Spec auf ein `BinaryField` anwenden“ wird zur Einzeiler-API (intern: synthetische leere `ISOMessage` + Parser + `unparse`). |
| c | **`header:` in Field-only-Dokumenten ablehnen:** fail-closed wie alles andere im Loader (positionierter `std::runtime_error`), nicht still ignorieren. |

---

## 1. Kontext & Befunde

### 1.1 Anforderung & Motiv

ISO-Nachrichten enthalten Container-DEs (klassisch DE55 ICC Data: Mastercard fix-TLV `lllbinary` + `tlv: {tag_bytes: 2, len_bytes: 2}`, EMV `lllbertlv`), deren Inhalt aus der Vollnachricht in ein `BinaryField` gezogen und **unabhängig** interpretiert werden soll — ohne die komplette Nachrichtenspec zu laden (z. B. ein Viewer/Tool, das einen ICC-Block aus dem Wire-Dump herausdekodiert und mit einer kleinen, wartbaren DE55-Spec typisiert).

### 1.2 Befunde (Stand `f8db10b`)

1. **Public Loader nur auf Nachrichtenebene:** Alle 8 öffentlichen `SpecDecoder`-Eintritte (`include/iso8583/ISOSpec.hh`) laufen über `static LoadedBundle loadBundle(path, opts, wantSpec)` (`src/_spec.cc:1276`) und verlangen eine nicht-leere `fields:`-Map (fail-closed: `if (!hasKey(root, "fields"))`, `src/_spec.cc:248`). Eine Feld-gebundene Spec ist heute nicht ladbar.
2. **Die Field-Block-Mechanik existiert bereits vollständig** (0.6.0, aus dem SIGSEGV-Fix): `container_`-Modus (`src/_parser.hh:49` Setter `:75`, `emit_bitmap()` `:95-104`, `first_field()` `:130-138` — Slot 0 = erstes Kind-Feld, ohne MTI/Bitmap-Vorspann), `containerBaseField()`-Loader-Normalisierung (`src/_spec.cc:828-846`), `checkContainerBase()`-Fail-closed-Guard (`src/_parser.hh:190-216`), `buildFieldParser()` mit `sub->container(true)` (`src/_spec.cc:848-948`) und `makeTlvParser()` (`src/_spec.cc:751-798`). In `ISOBaseParser::unparse()` (`src/_parser.cc:206ff`) wird der MTI-Block über `container_ ? nullptr : l_.at(MTI_KEY)` (`:252`) übersprungen, das Bitmap über `emit_bitmap()` deaktiviert, der Datenloop decodiert die Kinder ab Slot 0 und `set()` sie auf die Nachricht.
3. **Harte ISOMessage-Sperre (Bewusst):** `unparse()`/`parse()` fail-closen auf `c == nullptr || !c->is_composite()` **und** `dynamic_pointer_cast<ISOMessage>(c) == nullptr` („Parser: Komponente ist kein ISOMessage“, `src/_parser.cc:223-232`). Da Container-Mode MTI und Bitmap überspringt, genügt eine **synthetisch leere `ISOMessage`** — die bestehenden `unparse`/`parse`-Eintritte funktionieren unverändert; die Kinder-Komponenten werden pro Parser angelegt und `set()` auf die leere Nachricht. Ein `BinaryField` ist keine `ISOMessage`: „auf ein BinaryField anwenden“ heißt, `field.value()` (den `vector<uint8_t>`-Payload) in eine leere Nachricht zu decodieren.
4. **Wire-Vertrag (aus dem NESTED-Muster, `src/_parser.hh`):** Ein Container-Parser konsumiert den äußeren Längenpräfix-Frame und reicht den **Payload ohne das eigene Präfix** an die Kinder weiter (unparse: `scratch` → `consumed_outer = n_->unparse(...)` → `payload = scratch->value()`; parse: umgekehrt mit `BinaryField`-Wrapper). Daraus folgt für FE-1: Der Field-only-Parser operiert exakt auf den Bytes, die ein `BinaryField` nach dem Vollnachrichten-Decode hält — **ohne das Längenpräfix des DEs selbst**.
5. **`_preprocessor.cc` ist formunabhängig:** `!include_files`, `definitions:`/`!use`, `!template`, `!merge` wirken auf beliebige Knotenpositionen — Field-only-Dokumente erben Multi-Datei-Support, Sandbox und `.smap`-Sidecar ohne Änderung.

### 1.3 Reko-Ergebnisse (exakte Anknüpfungspunkte, Stand `f8db10b`)

| Baustein | Ort |
|---|---|
| `validateSpecYaml` (Fork-Punkt: `fields:`-Pflicht `:248`; `remaining`-`length`-Pflicht `:300-305`) | `src/_spec.cc:244` |
| `validateFieldKeys` (erlaubte Feld-Keys: `type`/`format`/`encoding`/`length`/`description`/`children`/`tlv`/`sensitive`) | `src/_spec.cc:229-242` |
| `isEncodingNeutral` / `resolveEncoding` | `src/_spec.cc:209-214` / `:217-223` |
| `parseSpecField` (Feldblock → `SpecField`, TLV-Kind-Whitelist, BERTLV-Kinder) | `src/_spec.cc:468-638` |
| `makeTlvParser` (fix: `MAKE_FIXED_TLV`-Dispatch; BER: `BERTLVParser`; MC-Default-Fallback + `TNG_LOG_WARN`) | `src/_spec.cc:751-798` |
| `containerBaseField` (0.6.0-Normalisierung) | `src/_spec.cc:828-846` |
| `buildFieldParser` (NESTED-Zweig: `sub->container(true)`; TLV-Zweig) | `src/_spec.cc:848-948` |
| Ergebnis-Assembly (`desc` ← `spec`, `hdr_sz`, `headerKey` ← Key-Präsenz, `strict`, `defaultEncoding` ← `encoding`) | `src/_spec.cc:1032-1036` |
| `LoadedBundle` / `loadBundle` / `publishCacheEntry` / `loadCachedBundle` / 8 Public-Wrappers / `invalidateCache`/`clearCache` | `src/_spec.cc:1270-1498` |
| `container_` / `emit_bitmap()` / `first_field()` / `checkContainerBase` | `src/_parser.hh:49` / `:95-104` / `:130-138` / `:190-216` |
| `ISOBaseParser::unparse` (MTI-Skip bei `container_` `:252`; Bitmap via `emit_bitmap()`) | `src/_parser.cc:206ff` |
| ISOMessage-Fail-closed-Guard (harte Sperre) | `src/_parser.cc:223-232` |
| NESTED-Scratch/-Wrapper-Muster (Wire-Vertrag: parse ~`:322-339`, unparse ~`:436-481`) | `src/_parser.hh` |
| `ISOSpec` / `SpecLoadOptions` / `SpecDecoder` (Public) | `include/iso8583/ISOSpec.hh` |

---

## 2. Ziellage (Verhaltens-Spezifikation)

1. **Neue Dokument-Form:** Eine Field-only-Spec hat dieselben Root-Keys wie eine Message-Spec (`spec`, `encoding`, `strict`, `definitions:`, `!include_files`, alle Direktiven), ersetzt aber die pflichtige `fields:`-Map durch einen einzelnen `field:`-Block. Der `field:`-Block verwendet **exakt dieselbe Grammatik** wie ein `fields:`-Eintrag (gleiche Key-Whitelist, gleiche fail-closed-Regeln) — keine neue Feld-Syntax.
2. **Dokumenten-Regeln (fail-closed, positionierte Fehler):** `field:` vorhanden und nicht-leere Map; `fields:` in einem Field-only-Dokument abgelehnt; `field:` in einem Message-Dokument abgelehnt (explizite Dokument-Form; der Modus ergibt sich aus dem aufgerufenen Loader); `header:` in Field-only-Dokumenten abgelehnt (Entscheidung c). Kein Key-Konzept: das einzige Feld wird als Key `0` introspektiert.
3. **Wire-Vertrag:** Der Field-only-Parser decodiert exakt die Bytes, die ein `BinaryField` nach dem Vollnachrichten-Decode hält — Kinder-Bytes/TLV-Frames **ohne** das eigene Längenpräfix des DEs (Befund 1.2/4). Roundtrip-Vertrag: `decodeField(parser, payload)` ≡ Komponenten aus dem Vollnachrichten-Decode; `parse(decodeField(...))` ≡ Payload (byte-identisch).
4. **Kein neuer Decode-Pfad:** Container-Mode überspringt MTI + Bitmap; die bestehenden `unparse`/`parse`-Eintritte in `src/_parser.cc` bleiben unverändert (harte ISOMessage-Sperre durch synthetisch leere `ISOMessage` erfüllt, Befund 1.2/3). Das Quell-`BinaryField` wird nie mutiert. TLV-Kinder werden per SE/Tag adressiert, Sequenz-Kinder (nested) pro Position.
5. **Introspektion:** `ISOSpec` wiederverwendet, unverändert: `fields()` = ein `SpecFieldInfo` (Key `0`, inkl. `tlv_children`), `has()`, `name()`, `headerSize()`; `hasHeader() == false`.
6. **Getrennter Cache** (Entscheidung a): eigenständiger `fieldSpecCache` mit identischem Protokoll (LRU ≤ 64, mtime-Pre-Filter ~1 µs + SHA-256-Neuprüfung der Top-Level- und aller Include-Dateien, Publish-then-Verify gegen TOCTOU; `TrustUntilInvalidated` + `invalidateFieldCache(path)`).
7. **`SpecLoadOptions` unverändert wiederverwendet** (sandbox, roots, Limits, smap) — Field-only-Specs unterstützen `!include_files` und `.smap` identisch.
8. **Additive only:** neue Public-Declarations in `ISOSpec.hh`; keine ABI-Änderung, keine Änderung von `src/_parser.cc`/`src/_parser.hh`/`src/_preprocessor.cc`.

---

## 3. YAML-Schema (neue Dokument-Form)

### 3.1 Form & Regeln

```yaml
# de55_emv.yml — Field-only-Spec (BERTLV/EMV)
spec: "DE55 ICC (EMV)"
field:
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:
    "5A": { format: char,   length: 4, encoding: ascii, description: "Application PAN" }
    "95": { format: binary, length: 5,               description: "Transaction Type" }
# Tag 0x8A (undeclared) wird dynamisch als BinaryField dekodiert ("SE138").
```

```yaml
# de55_mc.yml — Field-only-Spec (Mastercard fix-TLV)
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

| Regel | Verhalten |
|---|---|
| `field:` | Pflicht, nicht-leere Map (exakt ein Feld). Gleiche Feld-Keys/Grammatik wie `fields:`-Einträge (`type`, `format`, `encoding`, `length`, `description`, `children`, `tlv`, `sensitive`) — inkl. TLV-Kind-Whitelist, `remaining`-`length`-Pflicht, BERTLV-Explicit-Encoding-Regel. |
| `fields:` in Field-only-Doku | **abgelehnt** (fail-closed, positioniert) |
| `field:` in Message-Doku | **abgelehnt** (fail-closed, positioniert) |
| `header:` in Field-only-Doku | **abgelehnt** (Entscheidung c; zukünftiger Erweiterungspunkt, s. §8) |
| `spec`/`encoding`/`strict`/`definitions:`/`!include_files`/`!use`/`!template`/`!merge` | Identisch wie bei Message-Specs; `!use` funktioniert auch in `field:`-Blöcken (`_preprocessor.cc` formunabhängig, Befund 1.2/5). |
| Keys | Kein Key-Konzept: das einzige Feld wird als Key `0` introspektiert. (Key `0` = MTI in Message-Specs — bewusst; Field-only- und Message-Parsers nie auf derselben Nachricht mischen.) |

### 3.2 Wire-Vertrag & Anwendung

```cpp
// Muster (vollständig):
auto [parser, spec] = iso8583::spec::SpecDecoder::loadFieldBothFromYaml("de55_emv.yml");
auto msg = std::make_shared<iso8583::ISOMessage>();   // synthetisch, leer — ohne MTI
msg->parser(parser);
msg->unparse(msg, bf->value());                       // → Kinder-Komponenten auf msg
auto pan = msg->tryGet<iso8583::OpaqueField>(0x5A);   // typisiert (char-Kind)

// oder als Einzeiler (Entscheidung b):
auto msg = iso8583::spec::SpecDecoder::decodeField(parser, *bf);
```

Runde-Reise: `auto payload2 = msg->parse(msg);` liefert die Payload **byte-identisch** zurück (ohne das Längenpräfix des DEs — exakt das, was `decodeField` erhalten hat). Das Quell-`BinaryField` `*bf` bleibt unverändert.

---

## 4. Public API (additive only)

Neue Member von `iso8583::spec::SpecDecoder` in `include/iso8583/ISOSpec.hh` (Doku-Kommentare wie die bestehenden Entry-Points):

```cpp
class SpecDecoder {
    // … bestehende 8 Message-Level-Entry-Points unverändert …

    // Field-only-Specs (FE-1, 0.6.0): laden eine Spec mit einem einzelnen
    // field:-Block und liefern einen Container-Parser, der auf dem Payload
    // eines BinaryFields (ohne das Längenpräfix des DEs) operiert.
    static ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        loadFieldFromYaml(const std::filesystem::path& path);
    static ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        loadFieldFromYaml(const std::filesystem::path& path, const SpecLoadOptions& opts);
    static std::pair<ISOParserPtrBase::ISOParserPtrBaseSmartPtr, ISOSpec::SmartPtr>
        loadFieldBothFromYaml(const std::filesystem::path& path);
    static std::pair<ISOParserPtrBase::ISOParserPtrBaseSmartPtr, ISOSpec::SmartPtr>
        loadFieldBothFromYaml(const std::filesystem::path& path, const SpecLoadOptions& opts);

    // Getrennter Cache (Entscheidung a): identisches Protokoll wie
    // load*FromYamlCached (LRU ≤ 64, mtime-Pre-Filter + SHA-256, Publish-
    // then-Verify); Cache-Validierung analog (CheckEveryCall / TrustUntilInvalidated).
    static ISOParserPtrBase::ISOParserPtrBaseSmartPtr
        loadFieldFromYamlCached(const std::filesystem::path& path,
                                const SpecLoadOptions& opts = {},
                                CacheValidation validation = CacheValidation::CheckEveryCall);
    static std::pair<ISOParserPtrBase::ISOParserPtrBaseSmartPtr, ISOSpec::SmartPtr>
        loadFieldBothFromYamlCached(const std::filesystem::path& path,
                                    const SpecLoadOptions& opts = {},
                                    CacheValidation validation = CacheValidation::CheckEveryCall);
    static void invalidateFieldCache(const std::filesystem::path& path);
    static void clearFieldCache();

    // Convenience (Entscheidung b): „Spec auf ein BinaryField anwenden“ —
    // intern: synthetische leere ISOMessage + Parser + unparse(field.value()).
    static ISOMessage::SmartPtr decodeField(
        const ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p,
        const BinaryField& field);
};
```

Hinweise:

- **`SpecLoadOptions` unverändert** wiederverwendet (sandbox, roots, Limits, smap).
- **Zwei Caches für denselben Pfad:** Wer eine Datei sowohl als Message- als auch als Field-only-Spec lädt, muss bei Änderung **beide** Caches invalidieren (`invalidateCache(path)` + `invalidateFieldCache(path)`). In der Doku (WP5) explizit benannt.
- **Introspektion:** `ISOSpec` unverändert; `fields()` = ein `SpecFieldInfo` (Key `0`, vollständiger `tlv_children`-Map), `hasHeader() == false`, `headerSize() == 0` (Konstruktion über den Public-Ktor mit `headerSize = nullopt`).
- **BERTLV-Builds:** Kind-Keys über `TlvChildMap` (size_t-gekeyt in beiden Build-Modi); `TNG_KEY_TYPE` je Build (int16_t / int32_t unter `ISO8583_BERTLV`) — API-Unterschrift ist build-unabhängig.
- **Kein neuer Decode-Pfad:** `decodeField` setzt das dokumentierte `msg->parser(p); msg->unparse(msg, …);`-Muster (aus `ISOParser.hh`-Header-Doku) in eine Funktion um.

---

## 5. Arbeitspakete (umsetzungsreife Reihenfolge, TDD)

> Build/Verifikation (jedes WP, vgl. AGENTS.md §7):
> `cmake --preset debug` (VCPKG_ROOT gesetzt; auf dieser Maschine via
> `scratch/build_local.bat <cfg|build|ctest|bin>`) →
> `cmake --build --preset debug` → gezielt:
> `build/debug/tests/libiso8583_tests "[fe1]"` → zusätzlich derselbe
> Durchgang mit `--preset debug-bertlv` (BERTLV ist für ICC-Daten
> relevant) → am Ende `ctest --preset debug` + `ctest --preset
> debug-bertlv` + `libiso8583_tests "[e2e]"`; ctest-Ausgabe auf
> Skip-Warnungen sichten (A4.33).

### WP1 — `validateFieldSpecYaml` (`src/_spec.cc`)

1. **TDD-RED zuerst:** neue `tests/test_field_only_spec.cc` (erstellt in WP4,
   Fehler-Case-Teile hier) mit `REQUIRE_THROWS_AS(… std::runtime_error)`:
   - `fields:` in Field-only-Dokument,
   - fehlendes `field:` (leeres Dokument bzw. nur Root-Keys),
   - `header:` in Field-only-Dokument (Entscheidung c),
   - `field:` in Message-Dokument (erwischt über die Message-Validierung).
   Vor der Implementierung: die Field-only-Loader-Eintritte existieren noch
   nicht → Cases gegen `validateFieldSpecYaml` direkt (file-scope-Testzugriff
   wie in `test_spec_loader.cc`).
2. **Neuer Static-Helper** direkt neben `validateSpecYaml` (`src/_spec.cc:244`),
   mit deutschem Kommentarblock (Dokumenten-Form, Fail-closed-Begründung):

   ```cpp
   // (0.6.0, FE-1) Validierung für Field-only-Dokumente.
   // Gleiche Root-Keys wie Message-Specs (spec/encoding/strict/definitions/
   // !include_files/Direktiven), aber: field: (einzelner Block, Pflicht,
   // nicht-leere Map) statt fields:. fields: und header: werden abgelehnt
   // (explizite Dokument-Form; header: ist für ein isoliertes Feld
   // widersprüchlich — Entscheidung c). Feld-Level-Checks (validateFieldKeys,
   // remaining-length, TLV-Whitelist) delegiert an die bestehende Maschinerie.
   static void validateFieldSpecYaml(const yaml::Node& root);
   ```

   Prüf-Reihenfolge: (1) `field:`-Präsenz + nicht-leere Map (E3-Audit-Kommentar
   analog `:264`), (2) `fields:`-Präsenz → positionierter Fehler
   („Konflikt: field-only-Dokumente dürfen keine fields:-Map enthalten"),
   (3) `header:`-Präsenz → positionierter Fehler, (4) `validateFieldKeys`
   (`:229-242`) auf dem `field:`-Block, (5) `remaining`-`length`-Pflicht
   (`:300-305`-Regel) auf dem Block bzw. seinen Kindern.

3. **Message-Seite:** `validateSpecYaml` erhält einen Check: `field:`-Präsenz →
   positionierter Fehler („Konflikt: Message-Specs verwenden fields:, field:
   ist nur in Field-only-Dokumenten erlaubt"). Verhalten aller bestehenden
   Message-Specs unverändert (keine existierende Spec nutzt `field:`).
4. **Verifikation:** Build grün; RED-Cases von (1) grün.

### WP2 — `buildFieldBlockParser` (`src/_spec.cc`)

1. **Refactoring (behavior-invariant):** Die internen Bausteine von
   `buildFieldParser` (`src/_spec.cc:848-948`) extrahieren:
   - `buildTlvFieldParser(f)` — TLV-/BERTLV-Zweig (`makeTlvParser`-Dispatch
     `:751-798`, inkl. `TlvChildMap`-Ausbau und MC-Default-Fallback),
   - `buildNestedSubParser(f, defaultEncoding)` — NESTED-Zweig (Sub-
     `ISOBaseParser` + Kinder + `containerBaseField`-Normalisierung
     `:828-846` + `sub->container(true)`).
   `buildFieldParser` ruft die Helfer auf (Message-Pfad byte-identisch —
   Regression über die unveränderten, bestehenden Message-Tests).
2. **Neuer Static-Helfer** (deutscher Kommentarblock, FE-1):

   ```cpp
   // (0.6.0, FE-1) Liefert den PAYLOAD-Parser für ein einzelnes
   // Field-only-Feld: das, was ein Container-Parser seinem Kind
   // weiterreicht — OHNE den äußeren Längenpräfix-Frame des DEs selbst.
   //   tlv/bertlv  → buildTlvFieldParser  (TLV-Frames = Payload)
   //   nested      → buildNestedSubParser (Sequenz-Kinder)
   //   skalar      → createScalarParser (einzelner Kind-Parser)
   // Der Top-Parser ist eine ISOBaseParser mit container(true), die diesen
   // Block-Parser als (einziges) Kind hält → unparse/parse überspringen
   // MTI + Bitmap (src/_parser.cc) und der Datenloop decodiert ab Slot 0.
   // checkContainerBase-Guard gilt automatisch (gleicher Pfad).
   static ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr
       buildFieldBlockParser(const SpecField& f, const std::string& defaultEncoding);
   ```

3. **Verifikation:** Build grün (echter Proof in WP4).

### WP3 — `loadFieldBundle` + getrennter Cache + Public-Wrappers

1. **`loadFieldBundle`** (neu, parallel zu `loadBundle` `src/_spec.cc:1276`,
   identischer Aufbau): `_preprocessor.cc` **unverändert** →
   `validateFieldSpecYaml` → exakt ein `parseSpecField` (`:468-638`) →
   `buildFieldBlockParser` → Top-`ISOBaseParser` mit `container(true)` →
   `ISOSpec`-Konstruktion über den Public-Ktor (Name ← `spec`, Encoding ←
   `encoding`, ein `SpecFieldInfo` Key `0`, `headerSize = nullopt`).
   `LoadedBundle` unverändert (Parser + optionale Spec).
2. **`fieldSpecCache`** (neu, eigener Static-Bestandteil): exakt dieselbe
   Form wie `specCache` (LRU ≤ 64, Key = kanonisierter absoluter Pfad,
   Eintrag = Bundle + SHA-256 Top-Level + aller Includes + mtime).
3. **`loadCachedBundle` parametrisieren:** die TOCTOU-Logik
   (mtime-Pre-Filter, SHA-256-Neuprüfung, Publish-then-Verify,
   `TrustUntilInvalidated`-Pfad) ist heute an `specCache`/`loadBundle`
   gebunden (`src/_spec.cc:1296-1385`). Vor dem Call des Bundle-Loaders
   auf einen Loader-Parameter (`function_ref` oder `bool fieldOnly` +
   Cache-Selektor) verallgemeinern, sodass **beide Pfade denselben
   verifizierten Code** teilen. Regression-Risiko auf dem Message-Pfad
   durch die unveränderten Cache-Tests abdecken (AC6).
4. **Public-Wrappers** in `ISOSpec.hh` (Doku-Kommentare, Sprache wie
   bestehende Entry-Points) + Implementierung in `src/_spec.cc`: die 6
   `loadField*`-Funktionen + `invalidateFieldCache`/`clearFieldCache`
   (spiegeln `:1407-1498` 1:1).
5. **Verifikation:** Build grün; WP1-Fehler-Cases + Lade-Tests
   (introspektiv, s. WP4) grün.

Commit (WP1–WP3): `[+](Added) Spec-Lader: Field-only-Specs (loadField*FromYaml, geteilter Cache, FE-1)`

### WP4 — `decodeField` + Regressionstest `tests/test_field_only_spec.cc`

1. **`decodeField`** (neu in `SpecDecoder`, Implementierung in `src/_spec.cc`):
   ```cpp
   ISOMessage::SmartPtr SpecDecoder::decodeField(
       const ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p,
       const BinaryField& field) {
       auto msg = std::make_shared<ISOMessage>();
       msg->parser(p);
       msg->unparse(msg, field.value());
       return msg;
   }
   ```
2. **`tests/CMakeLists.txt`:** `test_field_only_spec.cc` eintragen,
   **vor** `test_e2e_full_message.cc` (bleibt letzter, AGENTS.md §7/§15.11).
   Tags: `[fe1]` + Domain-Tag `[tlv]` bzw. `[nested]`.
3. **Cases** (Payloads aus bekannten ICC-Strukturen; `TempYaml`-Helper wie
   in `test_nested_text_container.cc`; **beide Presets** `debug` +
   `debug-bertlv`):
   - **A** fix-TLV (Mastercard-Variante §3.1: `lllbinary` + `ebcdic` +
     `tlv: {tag_bytes:2, len_bytes:2}`): Decode gegen bekannten Payload →
     Kind-Typisierung (`char` → `OpaqueField`, `binary` → `BinaryField`),
     undeclared Tag → dynamisch `BinaryField` + „SE<n>"-Beschreibung;
     Roundtrip `msg->parse(msg) == payload` byte-identisch;
     **Introspektion** via `loadFieldBothFromYaml`: `spec->fields()` Größe 1,
     Key `0`, `format.type == "binary"`, `prefix_digits == 3`,
     `tlv_children` befüllt, `hasHeader() == false`, `name()` korrekt.
   - **B** BERTLV (`lllbertlv`, EMV-Variante §3.1: `char`/`ascii`-Kind
     „5A", `binary`-Kind „95", undeclared 0x8A → „SE138"): Decode +
     Typisierung + Roundtrip. 2-Byte-Tags (z. B. 0x9F26) nur in BERTLV-
     Guard-Tests (Stil wie `test_tlv_parser.cc`); alle übrigen Tags
     int16-sicher (in beiden Presets lauffähig).
   - **C** nested-Sequenz (Field-only `type: nested`, `format: binary`,
     `children`-Sequenz): Kinder pro Position (`OpaqueField`/`BinaryField`),
     Roundtrip byte-identisch.
   - **D** `decodeField`-Äquivalenz: `decodeField(parser, *bf)` ergibt dieselben
     Komponenten/Werte wie das manuelle `parser` + `unparse`-Muster (doppelte
     Deklaration aus §3.2).
   - **E** **Integration (Kernfall der Anforderung):** Message-Spec laden,
     vollständigen DE55-tragenden Frame decodieren → `get<BinaryField>(55)`
     extrahieren → `decodeField(fieldParser, *bf55)` → Kinder-Komponenten
     (Werte **und** Typen) identisch zu denen aus dem Vollnachrichten-Decode;
     `parse(decodeField(...))` == `bf55->value()` (Payload, ohne DE-Präfix).
   - **F** Roundtrip-Kombi aus B/C: `parse`-Ergebnis byte-identisch zur
     Eingabe-Payload.
   - **G** Fehler-Cases (`REQUIRE_THROWS_AS(… std::runtime_error)`,
     positionierte Botschaft): `fields:` in Field-only-Doku; fehlendes
     `field:`; `header:` in Field-only-Doku (Entscheidung c); `field:` in
     Message-Doku; ungültiger Feld-Key im `field:`-Block (z. B.
     `format: blubb` → bestehende `validateFieldKeys`-Meldung).
4. **Volle Verifikation:** Build + `"[fe1]"` grün in **beiden Presets** +
   `ctest --preset debug` + `ctest --preset debug-bertlv` +
   `libiso8583_tests "[e2e]"`; Skip-Warnungen sichten.

Commit: `[+](Added) SpecDecoder::decodeField + Regressionstests Field-only-Specs (FE-1)`

### WP5 — Doku

1. **`docs/internals/spec_schema.md`** (normativ): neuer Abschnitt
   **„Field-only-Specs (0.6.0)"**: Dokument-Form + Regeln-Tabelle (wie
   §3.1), beide Beispiele (EMV/Mastercard), Wire-Vertrag + Anwendungs-
   muster (Parser + `unparse` bzw. `decodeField`), getrennter Cache
   (`invalidateFieldCache`/`clearFieldCache`, beide Caches bei
   Pfad-Änderung invalidieren), Key-`0`-Semantik (Unterschied MTI),
   `header:`-Ablehnung als bewusste Schärfe + Erweiterungspunkt.
2. **`docs/internals/yaml_format.md`:** kurze Sektion am `fields:`-Dokuort
   (2–6 Sätze + Verweis auf spec_schema.md), Root-Keys-Tabelle um `field:`
   (Field-only-Form) ergänzen.
3. **`AGENTS.md` (root, Englisch):** kurzer Absatz im YAML-/Spec-Laden-
   Abschnitt (analog dem FR-3-Textbaustein-Stil): field-only documents,
   `loadField*FromYaml` family, separate cache, application to
   `BinaryField` payloads, `header:` rejected.
4. **`include/iso8583/AGENTS.md` (Deutsch):** gleicher Hinweis, deutsche
   Fassung.
5. **`include/iso8583/ISOSpec.hh`:** Doku-Kommentare der neuen Entry-Points
   (wie §4). **Sphinx-/Doxygen-Beachtung:** neue Public-Declarations
   fließen in die CI-Doxygen-Pipeline (`sphinx -W`) — Kommentare müssen
   valide Doxygen-Syntax sein (im Gegensatz zu FR-3, das keine
   Public-Header veränderte).
6. `include/iso8583/ISOParser.hh`: Header-Doku um einen Satz ergänzen
   („Field-only-Parsers aus `loadField*FromYaml` lassen sich identisch
   an eine synthetisch leere `ISOMessage` anhängen"), optional: Verweis.

Commit: `[i](Info) Doku: Field-only-Specs (AGENTS.md ×2, spec_schema.md, yaml_format.md, ISOSpec.hh)`

### WP6 — Changelog (`changelog.md` + Spiegel `docs/changelog.md`, identisch)

Im `## Unreleased`-Abschnitt (0.6.0-Kandidat), nach dem
`remaining`-Breaking-Block, neuer Block:

```markdown
### [+](Added) Field-only-Specs: Einzel-Feld-Specs (z. B. DE55/ICC) auf BinaryField-Payloads anwenden (0.6.0-Kandidat)

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
```

Commit: `[~](Changed) Changelog: 0.6.0-Kandidat — Field-only-Specs (FE-1)`

### WP7 — Chatroom + Obsidian-Kontextnote (Haus-Stil, analog FR-3; kein Repo-Commit)

1. **Chatroom** `…/Obsidian/ai_connected/chatrooms/iso8583.md`
   (**append-only!**, Node-UTF-8-Append-Skript, Doppel-Append-Guard,
   Backticks escaped): neuen Block `## <ISO-8601> — iso8583-agent`:
   FE-1 umgesetzt (0.6.0-Kandidat) — field-only-Specs (z. B. DE55/ICC)
   laden und auf `BinaryField`-Payloads anwenden (`loadField*FromYaml`,
   `decodeField`, getrennter Cache); Consumer-Note: nach dem 0.6.0-Pin-
   Bump ist ICC-Payload-Standalone-Decoding ohne vollständige
   Nachrichtenspec möglich.
2. **Obsidian-Kontextnote** `ai_connected/iso8583-dev/FE-1 Field-only-Specs.md`
   (House-Style: Status-Header, Kontext-Note für Agenten/Sitzungen,
   Befund-Tabelle, WP/Commit-Tabelle, Verifikationstabelle,
   „Lokale Hilfsmittel", „Bewusst nicht erledigt") — via MCP
   `create_vault_file` (Falle: Safety-Classifier-Instabilität → nicht
   hämmern, Chat-Zusammenfassung als Fallback).

---

## 6. Akzeptanzkriterien

- [ ] AC1: `libiso8583_tests "[fe1]"` komplett grün in **beiden Presets**
      (`debug` + `debug-bertlv`), inkl. byte-identischer Roundtrips.
- [ ] AC2: `ctest --preset debug` + `ctest --preset debug-bertlv` +
      `"[e2e]"` grün, ohne Skip-Fallen (A4.33: Ausgabe gescannt).
- [ ] AC3: Fehler-Cases (Case G) fail-closed mit positioniertem
      `std::runtime_error` (auch `header:`-Ablehnung, Entscheidung c).
- [ ] AC4: Introspektion: genau ein `SpecFieldInfo` (Key `0`),
      `tlv_children` befüllt, `hasHeader() == false`, `name()`/`encoding`
      korrekt (Case A).
- [ ] AC5: Integration: Vollnachrichten-Decode → `BinaryField`-Extraktion
      → `decodeField` → identische Komponenten wie Voll-Decode;
      `parse(decodeField(...)) == payload` (Case E).
- [ ] AC6: Message-Spec-Ladepfad **verhaltenstreu** (bestehende
      Message-/Cache-Tests unverändert grün — Regression über die
      parametrisierte TOCTOU-Logik von WP3.3).
- [ ] AC7: Doku-Stellen aktualisiert (spec_schema.md, yaml_format.md,
      AGENTS.md ×2, ISOSpec.hh, ISOParser.hh-Verweis); Doxygen-Kommentare
      valider Doxygen-Syntax (sphinx -W).
- [ ] AC8: Changelog + Spiegel `docs/changelog.md` identisch, Eintrag im
      Unreleased/0.6.0.
- [ ] AC9: Additive only: `src/_parser.cc`, `src/_parser.hh`,
      `src/_preprocessor.cc` unverändert; kein ABI-Bruch.
- [ ] AC10: Chatroom-Block angehängt (append-only), Obsidian-Note
      gespeichert.

---

## 7. Risiken & Hinweise

| Risiko | Bewertung / Mitigation |
|--------|------------------------|
| **ABI:** additive only (neue Declarations im Public-Header; keine bestehenden Symbole geändert) | Kein Consumer-Rebuild nötig; Changelog-Note reicht. |
| Key-`0`-Semantik-Überschneidung (MTI vs. einzelnes Feld) | Bewusst dokumentiert: Field-only- und Message-Parsers nie auf derselben Nachricht mischen; separate Parser-Objekte → keine Laufzeit-Kollision. |
| Zwei Caches für denselben Pfad (Message- + Field-only-Spec) | Beide LRU-beschränkt (≤ 64); `invalidateCache(path)` und `invalidateFieldCache(path)` wirken auf unterschiedliche Caches → in Doku + Changelog explizit benannt. Alternative (Shape im Cache-Key) durch Entscheidung a verworfen. |
| `loadCachedBundle`-Parametrisierung (TOCTOU-Code geteilt, WP3.3) | Regression-Risiko auf dem Message-Pfad → unveränderte Message-Cache-Tests + neue Field-Cache-Tests, beide Presets (AC6). |
| `buildFieldParser`-Extraktion (WP2.1) | Behavior-invariantes Refactoring: Message-Pfad ruft dieselben Helfer in derselben Reihenfolge auf; Regression über die komplette bestehende Testbasis (Message-/TLV-/Nested-Tests) in beiden Presets. |
| sphinx -W CI | Neue Public-Declarations benötigen valide Doxygen-Kommentare (WP5) — im Gegensatz zu FR-3 (keine Public-Header) ist die Doxygen-Pipeline hier betroffen. |
| `header:`-Ablehnung | Bewusste Schärfe (Entscheidung c); dokumentierter Erweiterungspunkt (§8). |
| BERTLV-Tag-Typen | Kind-Keys via `TlvChildMap` (size_t-gekeyt in beiden Build-Modi); Tests verwenden int16-sichere Tags + BERTLV-guardete 2-Byte-Tags (Stil `test_tlv_parser.cc`). |
| Uncommittetes `pos::POSDataCode`-WIP auf `main` | FE-1 landet **darauf**: WIP erst finalisieren (Changelog-Spiegel synchronisieren, ctest beide Presets, Commit) → saubere, getrennte FE-1-Commits. |
| Changelog-Spiegel-Divergenz | `changelog.md` und `docs/changelog.md` im selben Commit pflegen (AGENTS.md §2/§14.2). |
| Safety-Classifier-Instabilität (Obsidian-Write) | WP7.2 nicht hämmern; Chat-Zusammenfassung als Fallback; Repo-Artefakte (Plan-Doku) sind classifier-unabhängig. |

---

## 8. Offene Punkte (nicht blockierend)

1. **Write-back in das Quell-`BinaryField`:** bewusst out of scope (kein
   neuer API-Bedarf; Roundtrip liefert `vector<uint8_t>` via `parse` —
   die Anwendung entscheidet, ob sie in das `BinaryField` zurückschreibt).
2. **Gemischte Dokumente** (Message + Field in einer Datei): verworfen
   (explizite Dokument-Form, `field:`/`fields:`-Ablehnung in beide
   Richtungen). Zukünftiger Erweiterungspunkt: eigenständige Sektion.
3. **`header:` für Field-only-Specs:** abgelehnt (Entscheidung c). Falls
   künftig ein isoliertes Feld hinter einem Netzwerk-Header stehen soll:
   bewusste, dokumentierte Lockerung — kein stiller Implikation.
4. **Mehrere Felder pro Field-only-Dokument:** nicht erforderlich
   (einzelner `field:`-Block); bei Bedarf später als `field:`-Sequenz
   erweiterbar — aus Scope.

---

## 9. Aufwand & Reihenfolge

| WP | Schätzung | Abhängigkeit |
|----|-----------|--------------|
| WP1 `validateFieldSpecYaml` | klein | — |
| WP2 `buildFieldBlockParser` (+ `buildFieldParser`-Extraktion) | mittel | WP1 |
| WP3 `loadFieldBundle` + getrennter Cache + Public-API | mittel | WP1–WP2 |
| WP4 `decodeField` + Tests | mittel | WP3 |
| WP5 Doku | mittel | WP4 (verifiziertes Verhalten dokumentieren) |
| WP6 Changelog | klein | WP5 |
| WP7 Chatroom + Obsidian | klein | WP6 (nach grünem Gesamtlauf) |

Voraussetzung (vor WP1): `pos::POSDataCode`-WIP finalisieren
(Changelog-Spiegel + ctest beide Presets + Commit).

Empfohlene Commits (Konvention §14.1), jeweils mit grünem Testlauf
(beide Presets):

1. `[+](Added) Spec-Lader: Field-only-Specs (loadField*FromYaml, geteilter Cache, FE-1)` (WP1–WP3)
2. `[+](Added) SpecDecoder::decodeField + Regressionstests Field-only-Specs (FE-1)` (WP4)
3. `[i](Info) Doku: Field-only-Specs (AGENTS.md ×2, spec_schema.md, yaml_format.md, ISOSpec.hh)` (WP5)
4. `[~](Changed) Changelog: 0.6.0-Kandidat — Field-only-Specs (FE-1)` (WP6)

WP7 (Chatroom/Obsidian) ist kein Repo-Commit. 0.6.0-Release bleibt ein
Maintainer-Schritt (AGENTS.md §14.2).