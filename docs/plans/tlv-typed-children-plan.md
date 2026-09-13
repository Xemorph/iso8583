# Implementation Plan — Typisierte TLV-Kinder + `bertlv`-Kurzform mit `children`

**Status:** In Umsetzung — vom Maintainer freigegeben (Freigabe = Arbeitsauftrag
„Proceed with the plan“, WP1–WP3 implementiert und lokal verifiziert: debug &
debug-bertlv Full-Suite grün).
Basis: Feature-Requests aus `ai_connected/iso8583/` (tng-wire-viewer, gepinnt auf
`453cf4c5` / v0.4.0):
- **FR-1** — Typisierte TLV-/BER-TLV-Kind-Dekodierung
- **FR-2** — Deklarierte Tags auch im `bertlv`-Scalar-Format

Ziel-Release: **0.5.0** (0.x → Breaking Changes erlaubt und dokumentiert).

---

## 0. Entscheidungen (vom Maintainer bestätigt)

| # | Entscheidung | Konsequenz |
|---|--------------|------------|
| D1 | **FR-1: Default ON, breaking** in 0.5.0 | Deklariertes `format`/`encoding` in TLV-`children` wird beim Decode wirksam, **ohne** neuen YAML-Schlüssel. Präzedenz: `strict: true` Default (0.3.0, Q2), Description-Propagation (0.2.0). `format: binary` (dominante EMV-Deklaration) bleibt No-op → echte Breakage-Oberfläche nur Specs, die `char`/`numeric` deklarieren **und** `BinaryField` lesen. |
| D2 | **FR-2: Option (a)** — `...bertlv` + `children:`-Map | Einheitliche YAML-Form „dynamisches BER-TLV mit bekannten Tags". `tlv:`-Block und `type: nested` bleiben bei `bertlv` verboten; `children` als **Sequence** bleibt verboten. |
| D3 | **`length` auf TLV-Kindern bleibt documentation-only** | Die TLV-Länge auf der Wire ist die einzige Wahrheit (EMV-Tags haben variable Längen, z. B. 5A = 5–16 Bytes). Nur `format` + `encoding` steuern die Typisierung. |
| D4 | **Strict-Propagation auf Kind-Codec** | `strict_` ist bereits in `ISOTLVParser` vorhanden (via `ISOBaseParser`, Propagation über `subParser()` in `_parser.hh:74`) — die Plan-Änderung besteht nur darin, `strict_` an den zwei neuen Codec-Call-Sites *zu verwenden*: strict → positioniertes `std::runtime_error`, non-strict → Legacy-Mapping (`.`/`?`). |
| D5 | **Validierungs-Whitelist für TLV-Kinder** | Erlaubt: `binary`, `char`, `numeric`, `nopad_char` (Encoding `ascii`/`ebcdic`/`bcd`). Verworfen (positioniert): L-präfixierte Formate (`llchar`, …), `bitmap`, `remaining`, `nop` — Widerspruch, weil die TLV-Länge das Length-Feld trägt. Gilt für **beide** TLV-Formen (`tlv:`-Block und `bertlv`). |
| D6 | **FR-2(a) impliziert die Introspektions-Lücke** | `SpecFieldInfo` exponiert heute **keine** `tlv_children` (auch nicht bei `tlv:`/`ber:true`-Feldern) — `makeSpecFieldInfo` iteriert nur den `f.children`-Vektor. FR-2s Akzeptanzkriterium („`ISOSpec::field(N)` soll die deklarierten Kinder zurückliefern") verlangt also ohnehin die API-Ergänzung (WP5). |

---

## 1. Ziellage (Verhaltens-Spezifikation)

### 1.1 FR-1 — Typisierung

| Deklaration im `children:`-Eintrag | Dekodiertes Komponententyp | Wire-Effekt |
|---|---|---|
| `format: char` / `numeric` / `nopad_char` (Encoding `ascii`, `ebcdic` oder `bcd`) | `OpaqueField` (String, codec-konvertiert) | Bytes werden über `codec::as<std::string, enc>` konvertiert (strict: unmappbar → Throw; non-strict: `.`/`?`) |
| `format: binary` (jedes Encoding) | `BinaryField` (Rohbytes) | **No-op** — exakt wie heute (Encoding wird ignoriert, wie bei allen `binary`-Feldern) |
| Tag **nicht** deklariert | `BinaryField` + generische `"SE<n>"`-Description | Unverändert (dynamische Tags bleiben) |

- `description` und `sensitive` (beide TLV-Formen): unverändert, bleiben wirksam
  (Propagation seit 0.2.0 bzw. 0.3.0) — wandern nur in die neue einheitliche
  Kind-Struktur (WP1).
- **Encode-Pfad** (`parser->parse()`): typisierte Kinder werden über
  `get<OpaqueField>` + `codec::to<enc>` zurück auf die Wire serialisiert.
  Typ-Fehlmatch (Komponententyp ≠ Spec-Art, z. B. `BinaryField`-Wert auf
  `char`-Kind) → `std::runtime_error` (Programmierfehler, strict-unabhängig).
  Fehlendes SE → Warnung + Skip (wie heute).
- Roundtrip-Invariante: Decode→Encode reproduziert die Wire-Bytes 1:1.

### 1.2 FR-2 — `bertlv` + `children`

```yaml
"055":
  format: lllbertlv
  length: 999
  description: "ICC Data"
  children:            # seit 0.5.0 erlaubt (Hex-Keys, ber-Notation)
    "9F26": { format: binary, description: "Application Cryptogram" }
    "9F10": { format: char, encoding: ascii, description: "Terminal Type" }
```

- Verboten bleiben: `tlv:`-Block, `type: nested`, `children` als Sequence
  (Fehlermeldungen entsprechend aktualisiert).
- Deklarierte Tags: getypt + Description; undeclared: `BinaryField` + `"SE<n>"`.
- Introspektion: `ISOSpec::field(55)->tlv_children` liefert die deklarierten
  Kinder (gleiche neue API auch für `tlv:`-Block-Felder, s. WP5).

---

## 2. Ist-Zustand (Reko auf `453cf4c5`)

| # | Befund | Ort |
|---|--------|-----|
| 1 | `store_se()` baut **immer** eine `BinaryField` (Kind-Typung existiert nicht) | `src/_tlv.cc:129–161` |
| 2 | Drei parallele Maps pro TLV-Parser (`DataEncodingMap`, `DescriptionMap`, `SensitiveMap`); `data_encoding_for()` ist toter Code (einzige Referenz: Test) | `src/_tlv.hh:79–166`, `src/_spec.cc:724–732` |
| 3 | `ISOTLVParser::parse()` liest Kinder nur als `get<BinaryField>` — ein als `OpaqueField` abgelegtes Kind würde beim Encode **stillschweigend skipped** (Datenverlust-Roundtrip-Bug, erst durch FR-1 sichtbar) | `src/_tlv.hh:441–467` |
| 4 | `validateSpecYaml()` wirft bei `...bertlv` + `children`/`tlv`/`type:nested` — aber `parseSpecField()` parse `children`-Maps **bereits korrekt** (Hex-Keys via `tlv->ber`, Child-Encoding aus `tlv:`) → FR-2(a) ist rein Validierung + Doku | `src/_spec.cc:292–302`, `:488–499` |
| 5 | `makeSpecFieldInfo()` iteriert nur `f.children` (Vektor) — `f.tlv_children` (Map) taucht in `SpecFieldInfo` **niemals** auf | `src/_spec.cc:780–796` |
| 6 | `strict_` wird bereits in den TLV-Subparser propagiert (`fp->subParser()->strict(v)`) — fehlt nur die Nutzung an den Codec-Call-Sites | `src/_parser.hh:70–75` |
| 7 | E2E-Tests deklarieren TLV-Kinder als `format: binary` (MC/Visa-DE48) bzw. ohne Kinder (BERTLV-DE55, bewusst Build-unabhängiger Tag `0x1F22`) → unter Typisierung unverändert grün | `tests/test_e2e_full_message.cc` |
| 8 | `data_encoding_for`/`DataEncodingMap` sind an `TNG_EXPORT`-Klassen hängen, aber privat (nur `src/` + Tests) → kann ohne öffentliche API-Änderung umgebaut/entfernt werden | `src/_tlv.hh:84–96` |

---

## 3. Arbeitspakete (umsetzungsreife Reihenfolge)

### WP1 — Kind-Struktur vereinheitlichen (`src/_tlv.hh`, `src/_tlv.cc`, `src/_spec.cc`)
1. Neues `tlv_detail::TlvChildInfo`:
   ```cpp
   struct TlvChildInfo {
       codec::Encoder enc;          // BINARY = Rohbytes (Keine Konversion)
       bool           text;         // true: OpaqueField via codec; false: BinaryField
       std::string    description;  // leer = nicht deklariert
       bool           sensitive{false};
   };
   using TlvChildMap = std::unordered_map<std::size_t, TlvChildInfo>;
   ```
   (Kein `length`-Mitglied — D3.)
2. `ISOTLVParser`-Konstruktor: drei Maps ersetzen durch `TlvChildMap`;
   `data_encoding_for()`/`DataEncodingMap` **entfernen** (toter Code,
   `TNG_EXPORT`-Klasse bleibt, aber privat). Helper `description_for_wire`/
   `sensitive_for` lesen aus `child_map_`; Long-lived-Storage-Pattern für
   Fallback-Descriptions (`generated_desc_cache_`) bleibt unverändert (§12.3).
3. `_spec.cc buildFieldParser` (TLV-Äste ~`:718–736`): `TlvChildMap` aus
   `f.tlv_children` bauen: `text = (format ∈ {IFA_CHAR, IFA_NUMERIC,
   IFA_NOPAD_CHAR})`, `enc =` aufgelöstes Kind-Encoding (`IF_BINARY`/`IFA_BINARY`
   → `Encoder::BINARY`, Encoding wird ignoriert — bestehende Semantik).
   `makeTlvParser`-Signaturen entsprechend anpassen (beide `ISOTLVParser<>`-
   `make_shared`-Aufrufe in `_spec.cc:635–677`).

### WP2 — Typisierte Dekodierung (`_tlv.cc: store_se`, `_tlv.hh: parse`)
1. `store_se(..., const TlvChildInfo* child /*nullptr = undeclared*/, bool strict)`:
   - `child == nullptr || !child->text` → `BinaryField` wie heute
     (Description/Sensitive aus `child` bzw. Fallback).
   - `child->text` → `codec::as<std::string, child->enc>(buf, data_off, se_len,
     strict)` → `OpaqueField` (Description/Sensitive/Wire-Offsets wie heute).
   - strict: unmappbare Bytes (E2A-Whitelist/BCD-Nibble) → positioniertes
     `std::runtime_error` (Codec-Exception, wie im Scalar-Pfad
     `_parser.hh:447–475`); non-strict: Legacy-Mapping.
2. `ISOTLVParser::parse()` übergibt `strict_` + Kind-Ptr an `store_se`.
   (Auch der TCC-Pfad bleibt unverändert — TCC ist orthogonal.)

### WP3 — Typisierte Kodierung + Roundtrip (`_tlv.hh: ISOTLVParser::parse`)
1. Kind-Schleife erweitern: `text` → `get<OpaqueField>(se)` + `codec::to<enc>
   (value, b_img, se_len, strict_)`; `binary` → `get<BinaryField>` wie heute.
2. Typ-Fehlmatch → `std::runtime_error` mit SE-Tag (D: bewusst
   strict-unabhängig, da Programmierfehler, kein Datenproblem).
3. SE-fehlend → Warnlog + Skip (bestehendes Verhalten, beide Arten).
4. Damit wird der in Befund 3 beschriebene stille Skip eliminiert.

### WP4 — Validierung (`_spec.cc: validateSpecYaml` + Child-Whitelist)
1. `...bertlv`: `children` **als Map** erlauben; Sequence, `tlv:`-Block und
   `type: nested` bleiben Fehler (Meldungen aktualisieren: „`children`
   (Hex-Map) ist seit 0.5.0 erlaubt …").
2. Neue Prüfung (beide TLV-Formen, D5): Kind-`format` außerhalb
   `{binary, char, numeric, nopad_char}` → positionierter
   `SpecValidationError` („TLV-Kind: Format 'llchar' unzulässig — die
   TLV-Länge liegt im Length-Feld; nutze 'char'"). Gilt auch für per
   `!use`/`!template`/`!merge` expandierte Kinder (Validierung läuft auf dem
   gepreprozessierten Baum → sieht die Endform).
3. Kind-`encoding` außerhalb `{ascii, ebcdic, bcd, binary}` → ebenfalls
   positionierter Fehler (BCD ist der nützliche Zusatz: BCD-Ziffern-SEs).

### WP5 — Introspektion (`include/iso8583/ISOSpec.hh`, `_spec.cc`)
1. `SpecFieldInfo` gewinnt ein Mitglied:
   ```cpp
   /// Deklarierte TLV-Kinder (Tags/SEs), gekeyt nach rohem Tag-Wert/SE-Nummer.
   /// Key ist bewusst `int` und NICHT TNG_KEY_TYPE: EMV-2-Byte-Tags wie 0x9F26
   /// passen ohne ISO8583_BERTLV nicht in int16_t.
   std::map<int, SpecFieldInfo> tlv_children;
   ```
   `SpecFieldInfo`-Mitglied des Kindes: `key` = Tag-Wert, `is_nested=false`,
   `format`/`sensitive`/`encoding` aus dem Kind-`SpecField` (leere Map, wenn
   keine Kinder deklariert).
2. `makeSpecFieldInfo()` (~`_spec.cc:780`): `f.tlv_children` zusätzlich in
   `info.tlv_children` abbilden (wirkt für `tlv:`-Block- und `bertlv`-Felder
   identisch — schließt Befund 5).
3. **ABI-Hinweis:** `SpecFieldInfo` ist `TNG_EXPORT` (Value-Return aus
   `ISOSpec::field()`); neues Mitglied = Layout-Wechsel → DLL-Consumer müssen
   neu gebaut werden (gleiche Änderungsklasse wie `strict`-Mitglied in 0.3.0;
   0.x erlaubt das).
4. Doxygen-Kommentar am neuen Mitglied (CI: `sphinx -W`).

### WP6 — Tests (nach WP2/WP3/WP4, parallel wo möglich)
**Migration (bestehende Tests):**
- `tests/test_tlv_parser.cc:202` (`data_encoding_for`-Test) → auf neue API
  umschreiben (oder mit entfernen).
- `tests/test_spec_loader.cc:671` (bertlv + `children`-**Sequence**) bleibt
  gültig (Sequence bleibt verboten); TLV-Children-Tests `:415–620` durchsehen:
  Assertions, die `get<BinaryField>` auf `format: char`-Kindern erwarten, auf
  `OpaqueField` migrieren (laut Reko: dort wird nur Description geprüft →
  voraussichtlich keine Änderung).
- E2E-Datei bleibt grün (Befund 7); **neues E2E-Szenario 4** in
  `tests/test_e2e_full_message.cc`: BMP 55 `lllbertlv` mit gemischten Kindern
  (`"5A"`: `char`/ascii deklariert, `"9F26"`: `binary`, ein undeclared Tag) —
  decode-Checks (OpaqueField-Wert, BinaryField-Rohdaten, `"SE<n>"`-Fallback)
  + Byte-für-Byte-Roundtrip. Tags build-unabhängig wählen (1-Byte-Tags wie
  `0x5A`, `0x95` oder `0x1F22`-Trick wie Szenario 1), damit der Test in
  int16- und int32-Builds läuft; reale 2-Byte-EMV-Tags zusätzlich nur im
  `debug-bertlv`-Preset (wie `test_tlv_parser.cc` bereits).

**Neu (FR-1):**
- `test_tlv_parser.cc`: ASCII/EBCDIC/BCD typisierte Kinder (z. B. EBCDIC-Kind
  → `OpaqueField` mit konvertiertem String), `binary`-No-Regression,
  undeclared → `BinaryField` + `SE<n>`, strict-Throw auf unmappbarem E2A-Byte
  vs. non-strict `.`, Encode-Roundtrip typisierter Kinder,
  Typ-Fehlmatch → Throw.
- `test_spec_loader.cc`: Whitelist-Fehlerfälle (L-präfix/`bitmap`/`remaining`/
  `nop`-Kind → positionierter Fehler), Bertlv + `children`-**Map** lädt und
  dekodiert (FR-2), falsches Kind-Encoding → Fehler.
- `test_spec_*.cc`/introspektion: `loadBothFromYaml` → `spec->field(55)->
  tlv_children` enthält deklarierte Kinder mit korrektem Format/Key (FR-2a).

**Abschlussverifikation:** lokale Full-Suite (`ctest`, inkl. `slow`) im
`debug`- **und** `debug-bertlv`-Preset, dann CI (GCC-13 + MSVC, ASan/TSan).

### WP7 — Doku
- `include/iso8583/AGENTS.md` (kanonische API-Referenz):
  - TLV-`children`-Abschnitt: Satz „**Aktuell wird nur `description`
    propagiert … jedes SE wird als rohe `BinaryField` dekodiert**" ersetzen
    durch Typisierungs-Verhalten (Typ-Tabellenzeilen) + Whitelist (D5).
  - `bertlv`-Absatz: „Kurzform … ohne Kinderliste" → „seit 0.5.0 mit
    optionalem `children`-Map (Hex-Keys); undeclared Tags werden dynamisch
    dekodiert" + Hinweis `tlv:`/`type:nested`/Sequence weiterhin verboten.
  - `SpecFieldInfo`-Beschreibung: `tlv_children`-Mitglied ergänzen.
  - YAML-Beispielblöcke (`"048"`, `"055"`, `"057"`) konsistent halten.
- `docs/internals/yaml_format.md`: TLV/BER-TLV-Tabelle (`...bertlv`-Zeile:
  „Kinderliste entfällt" → „optional, Hex-Map"), neuer Abschnitt
  „Typisierte TLV-Kinder" (Semantik, Whitelist, `length` documentation-only,
  Encoding-Auflösung), `encoding.md`: Anmerkung, dass Kind-Encoding die
  Codec-Konversion typisierter Kinder steuert.
- `docs/internals/encoding.md`: Auflösung „field-level > global > ''" gilt
  unverändert für TLV-Kinder (bereits so per `seEnc` in `parseSpecField`).

### WP8 — Changelog + Release 0.5.0
1. `changelog.md` **und** `docs/changelog.md` (byte-identisch!) — Unreleased:
   - `[!](BREAKING)` TLV-Kinder werden gemäß deklarierter `format`/`encoding`
     typisiert dekodiert (char/numeric → `OpaqueField`, binary → `BinaryField`
     No-op); Encode-Pfad entsprechend; Typ-Fehlmatch wirft.
   - `[+](Added)` `format: ...bertlv` mit optionalem `children:`-Map (Hex-Keys;
     `tlv:`/`type: nested`/Sequence weiterhin verboten).
   - `[+](Added)` `SpecFieldInfo::tlv_children` (Introspektion deklarierter
     TLV-Kinder; `int`-Keys, ABI-Änderung → DLL-Consumer neu bauen).
   - `[~](Updated)` TLV-Kind-Formate: Whitelist `binary|char|numeric|
     nopad_char` (L-präfix/`bitmap`/`remaining`/`nop` → positionierter
     Validierungsfehler); Kind-`length` bleibt documentation-only.
   - `[-](Removed)` intern: `ISOTLVParser::data_encoding_for`/`DataEncodingMap`
     (toter Code; private API).
2. Release-Schritte §14.2: vier Versionsstellen → Tag → GitHub Release →
   vcpkg-port-SHA512 befüllen (wie v0.4.0).

---

## 4. Akzeptanzkriterien (aus den FR-Notizen)

**FR-1:**
- [ ] A1: Kind mit `format: char`/`numeric` + `encoding` → korrekter String-Wert
      (auch EBCDIC/BCD).
- [ ] A2: Kind mit `format: binary` → `BinaryField` (regressionsfrei).
- [ ] A3: undeclared Tag → `BinaryField` + generische Description (dynamisch).
- [ ] A4: `description`/`sensitive` bleiben wirksam (keine Regression).
- [ ] A5: Roundtrip Decode→Encode byte-identisch (inkl. typisierter Kinder).
- [ ] A6: strict/non-strict-Verhalten am Kind-Codec (Throw vs. Legacy-Mapping).

**FR-2:**
- [ ] B1: `format: lllbertlv` + `children:`-Map lädt (validiert).
- [ ] B2: deklarierte Tags → Beschreibung (+ Typ gemäß A1) beim Decode.
- [ ] B3: undeclared Tags werden dynamisch dekodiert (`"SE<n>"`).
- [ ] B4: `ISOSpec::field(N)->tlv_children` exponiert die deklarierten Kinder.
- [ ] B5: `tlv:`-Block/`type: nested`/Sequence bei `bertlv` bleiben Fehler.

---

## 5. Risiken & Hinweise

| Risiko | Bewertung / Mitigation |
|--------|------------------------|
| **ABI:** `SpecFieldInfo`-Layout wächst (WP5) | 0.x-Policy: Breaking Changes erlaubt; DLL-Consumer müssen neu bauen (Changelog + Release-Notes). |
| **Key-Breite:** EMV-2-Byte-Tags (`9F26`, `9F27`, `9F34` …) brauchen `ISO8583_BERTLV`-Build (int32-Keys) — **FR-2 heb das nicht auf** | In Release-Notes + an FR-Autor (tng-wire-viewer): BERTLV-Build oder 1-Byte-Tags. `SpecFieldInfo::tlv_children` nutzt bewusst `int`-Keys, damit die **Introspektion** auch im int16-Build vollständige EMV-Tags anzeigen kann. |
| Bestehende Specs mit `char`/`numeric`-Kindern, die `BinaryField` lesen | Breaking Change (D1) — Changelog `[!]`; Migration = `get<BinaryField>` → `get<OpaqueField>` (oder `format: binary` deklarieren). |
| Degenerierte Kinder-Deklarationen (`llchar`/`bitmap` in TLV-Kindern) waren bisher stiller „documentation-only" | D5 macht sie zu positionierten Validierungsfehlern (Fail-closed-Hausstil, Q2-Präzedenz) — ebenfalls in Changelog. |
| Performance: pro-SE-Map-Lookup | Negligible (wenige Kinder, gleiche Kostenklasse wie heutige Description-Lookups). |
| E2E-Tests | E2E-Szenarien 2/3 deklarieren `binary`-Kinder → no-op; Szenario 4 neu (WP6). `test_e2e_full_message.cc` bleibt **letzter** Eintrag in `tests/CMakeLists.txt` (Regel §7/§15.11). |

---

## 6. Offene Punkte (nicht blockierend)

1. `format: binary` + `encoding: ascii/ebcdic` auf TLV-Kindern: Encoding bleibt
   ignoriert (Rohbytes) — konsistent mit allen anderen `binary`-Feldern; in
   Doku so hinweisen. (Alternative „binary + encoding = konvertierte Bytes"
   würde die No-op-Eigenschaft der dominanten EMV-Deklaration brechen → abgelehnt.)
2. Ob `!template`/`!merge` in TLV-Kind-Einträgen explizit weiter funktioniert:
   ja — Kinder gehen durch `parseSpecField` wie jede andere Felddeklaration;
   kein Extra-Work nötig (im Test-WP6 abdecken).

---

## 7. Aufwand & Reihenfolge

| WP | Schätzung | Abhängigkeit |
|----|-----------|--------------|
| WP1 Struktur | klein | — |
| WP2 Decode | mittel | WP1 |
| WP3 Encode/Roundtrip | mittel | WP1, WP2 |
| WP4 Validierung | klein | — (parallel zu WP2/WP3) |
| WP5 Introspektion | klein (API) | WP4 (bereinigte Semantik) |
| WP6 Tests | groß (Audit + neu) | WP2–WP5 |
| WP7 Doku | mittel | WP1–WP5 |
| WP8 Changelog/Release | klein | alles |

Empfohlene Commits (Konvention §14.1), jeweils mit grünem Testlauf:
1. `[~](Changed) TLV-Kinder: einheitliche Kind-Struktur (TlvChildMap) statt dreier Maps` (WP1)
2. `[+](Added) Typisierte TLV-/BERTLV-Kind-Dekodierung gemäß deklarierter format/encoding (strict-propagiert)` (WP2)
3. `[+](Added) TLV-Kinder-Encode: typisierte Kind-Felder wieder serialisieren (Roundtrip) + Typ-Mismatch Fail-closed` (WP3)
4. `[!](BREAKING) TLV-Kinder: Format-Whitelist (binary/char/numeric/nopad_char) + bertlv mit children-Map erlaubt` (WP4 + WP5-API)
5. `[+](Added) Introspektion: SpecFieldInfo::tlv_children (deklarierte TLV-Kinder)` (WP5)
6. `[~](Updated) Tests + Doku: typisierte TLV-Kinder, bertlv+children, Whitelist` (WP6 + WP7)
7. `[~](FIX) Changelog: 0.5.0 Einträge (beide Spiegel)` (WP8, vor Release)