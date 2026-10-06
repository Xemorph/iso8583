# FR-11 / FR-12 / FR-13 Implementierungsplan: Unterfelder in VISA-Containern

> **Status:** in Umsetzung (Plan erstellt 2026-10-06). Maintainer-Entscheidung 2026-10-06: Empfehlungen E1–E4 übernommen,
> **E5 = ein gebündeltes Release 0.9.0 mit FR-12 + FR-13** (FR-11 durch FR-12 abgedeckt); Commit pro Meilenstein.
> **Ursprung:** drei Feature-Requests von `tng-wire-viewer` (VISA DE60/DE62/DE63/DE126) —
> Vault `ai_connected`: `iso8583/FR-11 …`, `FR-12 …`, `FR-13 …`.
> **Repo-Stand bei Erstellung:** `main` (0.8.0, Commit `bf21dc6`).
> **Ziel-Release:** 0.9.0 (neue Mitglieder in `SpecFieldInfo` = ABI-Änderung, wie bei 0.8.0).

---

## 1. Überblick und Empfehlung

| FR | Inhalt | Art | Prio (Konsument) | Aufwand |
|---|---|---|---|---|
| **FR-12** | `nested`-Container mit Bitmap-Kopf; Kinder per Bit-Nummer (Map statt Liste) | Feature, additiv | mittel | **mittel–groß** (neuer Container-Modus in Loader + Parser) |
| **FR-11** | Bit 1 als normales Feld in Unter-Bitmaps (`secondary: never`) | Feature | mittel (62.1 häufig gesetzt) | **entfällt, wenn FR-12 umgesetzt wird** (E1) |
| **FR-13** | Nibble-Unterfelder (mehrere BCD-Ziffern-Kinder teilen sich ein Byte) | Feature, additiv | niedrig (Workaround sauber) | mittel (neuer Pack-Modus, Codec nur wiederverwendet) |

**Empfohlene Reihenfolge:** FR-12 (löst FR-11 mit) → FR-13 → Doku/Release gebündelt.
Begründung: FR-11 als Top-Level-Variante (`secondary: never` + eigener Schlüssel) trifft die
Slot-Struktur von `ISOBaseParser` (Slot 1 = Bitmap, Schlüssel `001` belegt) und wäre ein
invasiver Umbau für einen Workaround-Pfad (Mini-Nachricht), den FR-12 ohnehin ablöst. Der
Konsument schreibt in FR-12 selbst: FR-11 sei nur Zwischenschritt. FR-13 ist unabhängig und
kann bei Bedarf verworfen werden (Notiz erlaubt das ausdrücklich).

---

## 2. FR-12 — Bitmap-gesteuerter `nested`-Container

### 2.1 Verifizierte Ausgangslage (Code gelesen)

- `buildNestedSubParser()` (`src/_spec.cc` ~Z. 1543) baut einen `ISOBaseParser` mit
  `container(true)`; Kinder = positionelle Slots ab 0. `emit_bitmap()` ist im Container-Modus
  immer `false` (`src/_parser.hh` ~Z. 95–104), `first_field()` = 0.
- Der Nested-Wert ist ein `ISOMessage`-Kind (`childMsg->parser(c_)`, `src/_parser.hh` ~Z. 561);
  Kinder liegen als Komponenten unter ihrer **Schlüsselnummer** → Punkt-Notation `"62.7"` und
  `recalcBitmap_locked()` (`src/_components.cc` ~Z. 890) existieren bereits für Kind-Nachrichten.
- **Wichtig für Bit 1:** Eine Nachricht legt IHRE Bitmap unter `Message::BITMAP_KEY = -1` ab
  (`include/iso8583/detail/_components.hh` ~Z. 377), nicht unter Schlüssel 1. Im Container ist
  Schlüssel `1` daher **frei** → Bit 1 kann dort ein normales Kind sein. (Der Sekundär-Bitmap-Sonderfall
  steckt nur in `ISOBaseParser::unparse` bei `emit_bitmap()` und im Bitmap-Feldparser, nicht im Komponentenmodell.)
- Top-Level-Decode (`src/_parser.cc` ~Z. 283–416) ist die Vorlage: Bitmap lesen → `bmp[i]` pro Slot
  → Kind dekodieren; Encode (~Z. 121–187) schreibt Bitmap, dann gesetzte Felder in Slot-Reihenfolge.
- Loader: `children` als **Map** ist heute für TLV reserviert (`validateTlvChildMap`,
  `_spec.cc` ~Z. 683–689); eine Map bei nicht-TLV-nested ist bisher kein definierter Fall.

### 2.2 Design

- **D12.1 — Syntax:** `bitmap: { length: N }` am `nested`-Feld (Nicht-TLV) + `children:` als **Map**
  `"<Bit-Nr>": <Feld-Definition>`. Genau die Form aus dem FR-Vorschlag. `bitmap:` ohne Map-`children`
  und Map-`children` ohne `bitmap:` (bei Nicht-TLV) → Ladefehler (fail-closed, positioniert).
- **D12.2 — Parser:** `ISOBaseParser` bekommt einen zweiten Container-Modus
  `bitmap_container_bytes_` (0 = aus, Default; mutable wie `container_`). Im Modus:
  - Decode: `N` Byte Bitmap lesen (Bit n = Bytes `(n-1)/8`, MSB zuerst — gleiche Konvention wie
    `ISOBitmapFieldParser`), Bitmap-Komponente unter `BITMAP_KEY` setzen, danach nur Slots mit
    gesetztem Bit in Bit-Reihenfolge dekodieren. **Bit 1 ist normales Kind** (kein `peek[1]`-Guard).
  - Gesetztes Bit ohne Kind-Definition: strict → positionierter Fehler; nicht-strikt → Warnung,
    Rest wird nicht dekodiert (Folgebytes unlesbar) → `consumed != b.size()`-Pfad greift.
  - Encode: Bitmap aus den gesetzten Kind-Schlüsseln (über `recalcBitmap_locked`, Größe auf `N`
    festgelegt, nicht auf Vielfaches von 64 aufrunden), dann Kinder in Bit-Reihenfolge.
  - Kind-Schlüssel > `8*N` → Ladefehler; beim Setzen/Encode eines Kindes ohne Slot: positionierter Fehler.
- **D12.3 — Slot-Layout:** Kind-Parser stehen in `l_` an Index = Bit-Nummer; Slot 0 = `UNUSED`-Platzhalter,
  Lücken ebenfalls `UNUSED` (wie Top-Level). `first_field()` im neuen Modus = 1.
- **D12.4 — Keine mehrstufigen Bitmaps** (Sekundär-Bitmap im Container): bewusst nicht Teil von FR-12
  (Konsument braucht es nicht, nicht untersucht). Kind-Bit 1 ist immer ein normales Feld; das wird in
  `spec_schema.md` als Abgrenzung dokumentiert.
- **D12.5 — Introspektion (ABI):** `SpecFieldInfo` erhält `bitmap_container_length` (Bytes, 0 = kein
  Bitmap-Container) und pro Kind die Bit-Nummer (Kind-`key` ist bereits die Bit-Nummer; zu prüfen in
  WP1, ob ein neues Feld nötig ist oder `key` genügt). `SpecFieldInfo` am Ende erweitern (ABI-Regel wie
  `secondary_bitmap`), explizite Instanziierung/`extern template` prüfen (Hard rule 12).
- **D12.6 — Wechselwirkungen (WP1 prüfen, Tests):** `sensitive`-Vererbung (wie `buildNestedSubParser`),
  `strict_`-Propagation (`strict(bool)` iteriert `l_` → greift automatisch), Threading (Parser bleibt
  unveränderlich, Modus-Flags nur im Loader gesetzt), Bitmap-Komponente erscheint in `dump()`/`to_json()`
  unter `BITMAP_KEY` — Verhalten wie bei Top-Level-Nachrichten, nur dokumentieren.

### 2.3 Arbeitspakete

| WP | Inhalt | Dateien |
|---|---|---|
| **WP1** | Messpunkte: heutiges Verhalten (positionell + `bitmap`-Kind), Kind-Key-Typ (`int16_t`/`int32_t`-Build), ob `ISOMessage::recalcBitmap_locked()` für Container-Größe `N` parametrierbar ist, `dump()`/`to_json()` mit `BITMAP_KEY` im Kind | Tests (Charakterisierung), keine Produktänderung |
| **WP2** | Loader: `bitmap:`-Block parsen (`length`, Whitelist `secondary` nicht erlaubt), Map-`children` für Nicht-TLV, alle Fail-closed-Prüfungen (§2.4) mit `SpecValidationError`/SourceMap | `src/_spec.cc` (+ `docs/internals/spec_schema.md` §6) |
| **WP3** | `ISOBaseParser`: Modus-Flag + Decode/Encode-Pfad, `first_field()`/`emit_bitmap()`-Anpassung, Fehlertexte positioniert | `include/iso8583/detail/_interfaces.hh`/`src/_parser.hh`, `src/_parser.cc` |
| **WP4** | `buildNestedSubParser` verdrahten, `SpecFieldInfo`-Felder, Doku-Kommentare `///` | `src/_spec.cc`, `include/iso8583/ISOSpec.hh` |
| **WP5** | Tests `tests/test_bitmap_container.cc` (neu, in `tests/CMakeLists.txt` registrieren; vor `test_e2e_full_message.cc`) — Akzeptanzkriterien (§2.5), Roundtrip, strict/non-strict, beide Key-Typ-Builds (`ISO8583_BERTLV` an/aus) | `tests/` |
| **WP6** | Doku: `spec_schema.md`, `yaml_format.md`, `include/iso8583/AGENTS.md` (DE), `.agents/yaml-spec.md` + `.agents/pitfalls.md`, `changelog.md` = `docs/changelog.md`, Beispiel-Spec VISA DE62; `sphinx -W` | `docs/`, `.agents/` |

### 2.4 Fail-closed beim Laden

`bitmap:` ohne Map-`children` · Map-`children` ohne `bitmap:` (Nicht-TLV) · `bitmap` zusammen mit `tlv:`/`bertlv`
· Bit-Nummer nicht numerisch, `< 1` oder `> 8*length` · doppelte Bit-Nummer · `length` < 1 · Kind-Format `bitmap`/`nop` im
Container · Container ohne binäres Frame-Format (bestehende `checkContainerBase`-Regel gilt weiter).

### 2.5 Akzeptanzkriterien (aus dem Vault, testbar)

1. Echte DE62-Nutzdaten `42 00 00 00 00 00 00 00 | 62.2 (8 B BCD) | 62.7 (26 B EBCDIC)` → `"62.2"`, `"62.7"`
   erreichbar; Build ist byte-identisch.
2. Bitmap `C0 00 …` + 62.1 `E8` + 62.2 → `62.1 = "Y"`, `62.2`; Roundtrip byte-genau (**deckt FR-11-AK 3**).
3. Gesetztes Bit ohne Definition scheitert strict positioniert.
4. Bestandsspecs mit Listen-`children` byte-identisch (Regressionslauf aller bestehenden Suites).

---

## 3. FR-11 — Bit 1 als normales Feld

**Empfehlung E1: durch FR-12 abgedeckt, kein eigener Code.** Der Konsument formuliert FR-12 als „würde
FR-11 miterledigen"; die Akzeptanzkriterien 1–3 von FR-11 werden durch §2.5 Nr. 2 erfüllt, Kriterium 2
(Bestand byte-identisch) durch Nr. 4.

Falls der Maintainer FR-11 dennoch als eigenständige Top-Level-Option will (E1 = „doch umsetzen"):

- `secondary: never` als dritter Wert neben `auto`/`always` (Loader `src/_spec.cc` ~Z. 968; Parser-Flag
  analog `secondary_always_`); Guards in `ISOBaseParser::unparse` (FR-9b-Guard ~Z. 311) und im
  Bitmap-Encode (~Z. 453–475) überspringen.
- **Offenes Designproblem:** Schlüssel für Bit 1. Slot 1 ist die Bitmap (`l_.at(1)`), `001` also belegt;
  Varianten `bitmap_key`/Bitmap bei `000` verlangen eine Umstellung der Slot-Konvention (`first_field()`,
  `emit_bitmap()`, `buildFallbackBitmap`). Hoher Aufwand/Regressionsrisiko für einen Pfad, der nur den
  Mini-Nachricht-Workaround verlängert → nicht empfohlen.
- Konsumentenhinweis im Vault: Mini-Nachricht-Workaround kann 62.1 weiterhin nicht; der Weg ist FR-12.

---

## 4. FR-13 — Nibble-Unterfelder

### 4.1 Verifizierte Ausgangslage

- Jedes Kind eines `nested`-Containers belegt ganze Bytes (Messung im Vault: `numeric|bcd, length: 1` → 1 Byte
  + Padding-Nibble; `bcd_pad` FR-7 steuert nur das Padding *innerhalb* eines Felds).
- Codec ist wiederverwendbar: `codec::as<string, BCD>` / `codec::to<BCD>` mit `bcd_pad`
  (`include/iso8583/_codec.hh`, `detail/_codec_impl.hh`); `bcd_pad_nibble_ok` (`_codec.hh` ~Z. 277).

### 4.2 Design

- **D13.1 — Syntax:** Container-Schalter `pack: nibble` an `nested` (Nicht-TLV, Nicht-Bitmap-Container). Name
  `pack` statt `packed: true`, damit künftige Einheiten (`pack: byte` = Default) möglich bleiben. Default
  unverändert.
- **D13.2 — Semantik:** Alle Kinder müssen `numeric` mit `encoding: bcd` und fester `length` (Ziffern) sein.
  Container-Nutzdaten = Ziffernstrom der Kinder in Reihenfolge, gepackt auf `ceil(Σ Ziffern / 2)` Byte.
  - **Decode:** Nutzdaten (nach Container-Längenpräfix) → Ziffernstrom (`as<string,BCD>` über *alle* Bytes) →
    pro Kind `length` Ziffern abschneiden; **Kinder, deren Ziffern nicht mehr im Strom liegen, bleiben ungesetzt**
    (kürzerer Container ohne Byte 7 bleibt erlaubt, AK 1). Schneidet die Nutzdatenmenge ein Kind **mittendrin**
    ab → strict: positionierter Fehler; nicht-strikt: Warnung + gekürzt/ausgelassen. Padding-Nibble nach `bcd_pad`
    validieren, sobald `bcd_pad` explizit deklariert ist (Muster FR-7, strict = Fehler).
  - **Encode:** gesetzte Kinder konkatenieren; **Lücken** (ungesetztes Kind vor einem gesetzten) → strict:
    positionierter Fehler (Wire-Layout wäre uneindeutig) — Alternative `default:`-Ziffern ist Zusatz (E3). Ungerade
    Gesamtziffernzahl → Padding nach `bcd_pad` (AK 3).
- **D13.3 — Umsetzung:** neuer Container-Parser-Modus im `ISOBaseParser` (`nibble_pack_` neben `container_`),
  Decode/Encode als eigene Funktion in `src/_parser.cc`, die den vorhandenen BCD-Codec aufruft. Kein neuer Codec,
  keine Änderung am Skalar-Pfad (`ISOFieldParser<std::string,…>` bleibt unberührt → Bestand byte-identisch).
- **D13.4 — Fail-closed beim Laden:** `pack` nur mit lauter BCD-numeric-Kindern fester Länge; Σ Ziffern ≤ `2 * length`
  (Container-`length` ist Byte-Maximum bei `lbinary`) — **nicht** „==", da kürzere Container erlaubt sind;
  `pack` mit `tlv:`/`bitmap:`/Kind-`remaining`/`bcd_pad` auf Kind-Ebene → Ladefehler (`bcd_pad` nur am Container oder Root-Default).
  Unbekannter `pack`-Wert → positionierter Fehler.
- **D13.5 — Introspektion (ABI):** `SpecFieldInfo::pack` (`""`/`"nibble"`), Kind-Ziffernzahl steht bereits in `length`.
  Wird zusammen mit FR-12-Feldern in einem ABI-Schritt ergänzt.
- **D13.6 — Wire-Offsets:** `wire_offset()`/`wire_length()` der Kinder haben bei Nibble-Packing keine Byte-Grenzen;
  Entscheidung E4 (Empfehlung: Offset = Byte, in dem das Kind beginnt; Länge = Anzahl berührter Bytes; dokumentieren).

### 4.3 Arbeitspakete

| WP | Inhalt |
|---|---|
| **WP7** | Charakterisierung (`bcd_pad`-Messungen aus dem Vault als Tests festschreiben, damit Default stabil bleibt) |
| **WP8** | Loader: `pack`-Schlüssel + Prüfungen §D13.4 |
| **WP9** | Parser-Modus Decode/Encode (§D13.2) inkl. strict/non-strict, Padding-Validierung |
| **WP10** | `SpecFieldInfo::pack` + Doku-Kommentare |
| **WP11** | Tests `tests/test_nibble_pack.cc` (AK 1–3, DE60 mit 10 Kindern, 5-Byte-Container ohne Byte 7, ungerade Ziffernzahl je `bcd_pad`-Variante, Lücken, Mittendrin-Abschnitt, beide Key-Typ-Builds) |
| **WP12** | Doku (`spec_schema.md` §6 + §3, AGENTS (DE) + `.agents/*`, Changelog identisch, Anleitung „keine Nibble-Unterfelder" streichen) |

---

## 5. Gemeinsame Querschnittspunkte

- **Reihenfolge der Commits:** je WP-Gruppe eigene Serie, Präfix-Konvention aus AGENTS.md (`[+](Added)` …);
  Doku-Commits getrennt; `.agents/*`/`include/iso8583/AGENTS.md` im selben Commit wie die Verhaltensänderung.
- **Hard rules:** C++20; Loader wirft *positionierte* `SpecValidationError`; `strict_` muss jeden neuen Codec-/Parser-Pfad
  erreichen (`ISOBaseParser::strict(bool)` propagiert über `l_` — neue Pfade ohne eigene Parser-Instanz prüfen);
  Memory-Safety: jeder `dynamic_bitset`-Zugriff mit `bmp.size() > n`-Guard, `npos` nie in Key-Typ wandeln (Muster P4,
  `_parser.cc` ~Z. 332–349); Threading unverändert (Flags nur im Loader).
- **Test-Matrix:** Debug-Preset + `ISO8583_BERTLV=ON`-Build (Key-Typ `int32_t`), `[spec]`-Suite, danach volle `ctest`;
  Skips im Ctest-Output prüfen (Skips zählen als Passed); `TEST_CASE`-Namen ASCII.
- **Regression:** vorhandene Spec-Beispiele (`specs/`, `examples/`) und alle bisherigen nested-/TLV-Tests unverändert grün
  (Default-Verhalten = Bytegleichheit).

## 6. Version, Release, Doku

- Versionssprung **0.9.0** (additive Features + ABI-Erweiterung `SpecFieldInfo`); 4 Versionsstellen
  (`CMakeLists.txt`, `include/iso8583/config.h` `TNG_CORE_VERSION`, root `vcpkg.json`, `vcpkg-port/vcpkg.json`);
  `changelog.md` und `docs/changelog.md` identisch.
- Release erst nach grünem Docs-Run auf `main` (`.agents/process-release.md` §14.2), maintainer-initiiert;
  vcpkg-Port-SHA512 danach.
- Konsument (`tng-wire-viewer`) braucht nach Release: Pin-Update; Vault-Notizen FR-11/12/13 schließen (Status,
  Commit-Hashes) + Chatroom-Post an `tng-wire-viewer-agent`.

## 7. Offene Entscheidungen (Maintainer)

| # | Frage | Empfehlung |
|---|---|---|
| **E1** | FR-11 als eigenständige Top-Level-Option oder durch FR-12 abgedeckt schließen? | durch FR-12 abdecken (§3) |
| **E2** | FR-13 überhaupt umsetzen (Notiz erlaubt „Workaround genügt")? | ja, aber nach FR-12 und mit kleinem Scope (nur `pack: nibble`) |
| **E3** | Lücken beim Packen: strict-Fehler oder `default:`-Ziffern pro Kind? | strict-Fehler (kein neues YAML-Key) |
| **E4** | `wire_offset`/`wire_length` bei Nibble-Kindern | Offset = Startbyte, Länge = berührte Bytes, dokumentieren |
| **E5** | Ein Release 0.9.0 für beide oder FR-12 vorab (Konsument-Nutzen höher)? | FR-12 zuerst fertigstellen, FR-13 nur nachziehen, wenn es in dasselbe Fenster passt |

## 8. Risiken

- **Mittel:** FR-12 berührt `ISOBaseParser::unparse/parse` (zentraler Pfad). Mitigation: neuer Modus ist durch Flag
  isoliert, Default-Pfad unverändert; Charakterisierungstests in WP1 vor dem Eingriff.
- **Mittel:** `recalcBitmap_locked()` rundet auf Vielfache von 64 Bit auf; Container-Bitmap hat feste Byte-Länge `N` —
  Bitmap-Größe im Container-Encode explizit steuern (WP1 klärt, ob dort Parametrierung oder lokale Berechnung nötig ist).
- **Niedrig:** ABI-Erweiterung `SpecFieldInfo` — am Ende anhängen, gemeinsam mit FR-13 in einem Schritt.
- **Unsicherheit der Datenbasis:** je **eine** echte Nachricht (DE62 Bits 2/7, DE60 5 Byte); Unterfeld-Layouts 62.3–62.26 und
  Bedeutung der DE60-Werte nur aus der Visa-Tabelle. Tests bilden das ab; keine Semantik (z. B. Wertebereiche) in der Library.
