# Plan: FR-3-Abschluss + SIGSEGV-Fix in text-basierten Nested-Containern (0.6.0-Kandidat)

Status: **Freigegeben** (2026-09-14, Entscheidungen D1–D5 bestätigt) — Ausführung läuft
Reko-Basis: `9e72d23` (Post-v0.5.0), Handoff `handoff-iso8583-fr3-20260914.md`
Vorherige FR-1/FR-2: `docs/plans/tlv-typed-children-plan.md` (v0.5.0)

---

## 0. Entscheidungen (vom Maintainer bestätigt, 2026-09-14)

| # | Entscheidung |
|---|-------------|
| D1 | **Normalisierung im Loader** (`buildFieldParser`, NESTED-Zweig), nicht in `parseSpecField` — auf einer lokalen `SpecField`-Kopie, damit die Introspektion (`ISOSpec::field`) das deklarierte Format meldet. Präzedenz: BERTLV-Shorthand-Rewrite (`_spec.cc`, `isBerTlvShorthand` → `f.type = NESTED`). |
| D2 | **Runtime-Guard Fail-closed** in `ISOFieldParser<T=parser>` (beide Zweige `parse`/`unparse`) als Defense-in-Depth gegen manuell konstruierte `ISONestedFieldParser`-Instanzen — positioniertes `std::runtime_error` statt SEGV. |
| D3 | **Tabelle ergänzen** um den einen fehlenden Zwilling `LLLLBINARY\|ASCII` (`IFA_LLLLBINARY`) — kein Codec-Änderungsbedarf (Data-Encoder bleibt `BINARY` = Roh-Bytes). |
| D4 | **Doku behält `lllchar`** in den Beispielen (AGENTS.md ×2, spec_schema.md) + normativer Normalisierungs-Hinweis. `lllchar` ist nach dem Fix korrekt und verhaltensreichlicher: es vererbt an Kinder das eigene Container-Encoding, `lllbinary` (neutral) vererbt das globale. |
| D5 | **Chatroom-Antwort an tng-wire-viewer** inkl. Rückfrage nach deren exaktem DE-Spec-Snippet (Container-Format + `tlv:`-Block + Kind-Deklaration + Pin), um das v0.5.0-Symptom (BinaryField statt OpaqueField) einzuordnen. |

---

## 1. Kontext & Befunde

### 1.1 CRASH (höchste Priorität — betrifft JEDEN text-basierten Nested-Container)

**Symptom:** `type: nested` + text-basiertes Containerformat (`char`/`numeric`/
`nopad_char`, mit L-Prefix `llchar`/`lllchar`/`llllchar`(ascii), sowie
`remaining` + Text-Encoding) → **SIGSEGV** in `unparse()`/`parse()`.
Alle Doku-Beispiele (AGENTS.md: DE48 `lllchar`+`tlv`, DE61 nested) sind betroffen.

**Probe-Matrix** (`tests/test_tlv_fixed_typed_probe.cc`, untracked,
registriert in `tests/CMakeLists.txt`):

| Case | Spec | Ergebnis |
|---|---|---|
| A | `lllbinary` + tlv + typisiertes EBCDIC-Kind `"71"` | Grün (OpaqueField „33V ", Roundtrip ok) |
| B | `lllbinary` + tlv + binary-Kind | Grün (BinaryField `F3F3E540`) |
| C | `lllchar` + tlv + Kind | **SIGSEGV** |
| D | `lllchar` + children-Sequenz (ohne tlv) | **SIGSEGV** (nicht TLV-spezifisch) |

**Ursache** (`src/_parser.hh`, `ISOFieldParser<T = ISOBaseParser>`):
Der Nested-Zweig arbeitet immer mit einem **`BinaryField`**:

- `unparse()` (~Zeile 365–382): lokaler Scratch-Buffer
  `auto scratch = std::make_shared<BinaryField>(0); n_->unparse(scratch, b, o);`
- `parse()` (~Zeile 250–267):
  `auto wrapper = std::make_shared<BinaryField>(c->key()); wrapper->value(inner_bytes); n_->parse(wrapper);`

`n_` ist der **Container-Basis-Parser** (`ISOFieldParserPtrBaseSmartPtr`,
Member ~Zeile 155). Bei text-basierten Formaten erzeugt
`createScalarParser` einen **string-basierten** Parser
(z. B. `IFE_LLLCHAR = ISOOpaqueFieldParser<LLL, EBCDIC, EBCDIC>`,
`IFB_LLLCHAR = ISOOpaqueFieldParser<LLL, BCD, BCD>`). dessen
`unparse`/`parse`-Zweig macht
`std::dynamic_pointer_cast<OpaqueField>(scratch)->value(...)` →
**nullptr-Dereferenz → SIGSEGV** (Casts bei ~Zeile 301/482 bzw. 279/484).

Latent seit 0.3.0 (Scratch-Pattern für Thread-Sicherheit); v0.5.0
byte-identisch betroffen. Der Loader (`buildFieldParser`, NESTED-Zweig,
`_spec.cc:812`: `auto base = createScalarParser(f);`) prüft die
Basis-Binärnatur nicht — Doku-Beispiel (`lllchar`) ist die Falle.

### 1.2 FR-3-Status (typisierte Kinder im fixen `tag_bytes`/`len_bytes`-Modus)

- **Laufzeit funktioniert bereits** (seither 0.5.0) auf **binären**
  Containern: Case A grün — `lllbinary` + `tcc: true` + Kind
  `"71": {format: char, encoding: ebcdic}` → `OpaqueField` „33V ",
  Roundtrip byte-identisch. Die TLV-Mechanik (`TlvChildMap` →
  `makeTlvParser` → `store_se`) ist modus-unabhängig (fix und BER
  teilen sich den Pfad) und zwischen v0.5.0 und HEAD byte-identisch.
- **Consumer-Symptom** (tng-wire-viewer, gepinnt auf `9517d36`/v0.5.0):
  deklariertes `char`+`ebcdic`-Kind kam als **BinaryField** mit Roh-HEX
  (`F3F3E540`) zurück. Auf `lllbinary`-Containern ist nach Case A
  ausgeschlossen → wahrscheinlichste Erklärung: deren Container-Format
  ist text-basiert (`lllchar`, laut Doku-Beispiel) oder abweichende
  `tlv:`-Encoding-Konfiguration. **Nicht ohne deren Spec-Snippet
  erklärbar → Rückfrage im Chatroom (D5).**
- **Library-Lieferumfang FR-3:** Crash-Fix (dieser Plan) + Doku +
  Bestätigung gegenüber dem Consumer (Typisierung funktioniert;
  `lllchar`-Container funktionieren ab 0.6.0 ebenfalls).

### 1.3 Reko-Ergebnisse (exakte Insertionspunkte, Stand `9e72d23`)

| Baustein | Ort |
|---|---|
| `parserTable()` (Leaky-Singleton, Key = `"Format\|Encoding"`) | `src/_spec.cc:648` |
| `createScalarParser(f)` (2-Stufen-Lookup `format\|encoding` → Fallback `format\|` → klares `runtime_error`) | `src/_spec.cc:726` |
| `buildFieldParser`, NESTED-Zweig, `auto base = createScalarParser(f);` | `src/_spec.cc:812` |
| Kinder-Encoding-Erbschaft: `childEnc = isEncodingNeutral(f.format) ? defaultEncoding : f.encoding` | `src/_spec.cc:564` |
| TLV-Kind-Parsing (`asHex = f.tlv && f.tlv->ber` → fix = dezimale Keys) | `src/_spec.cc:597–626` |
| `makeTlvParser` (fix: Tag/Length-Encoder aus `tlv.encoding`, Default EBCDIC) | `src/_spec.cc:749` |
| Introspektion `makeSpecFieldInfo` (nutzt `f.format`, d. h. deklariert) | `src/_spec.cc:920ff` |
| Crash-Stellen + T=parser-Zweige (`parse` ~250, `unparse` ~365) | `src/_parser.hh` |
| `create_component()` (string→`OpaqueField`, `vector<uint8_t>`→`BinaryField`, `T=parser`→`ISOMessage`, `dynamic_bitset`→`Bitmap`, `int32_t`→`CodeField`, `nullptr_t`→`OpaqueField`, `UNUSED`→**throw**) | `src/_parser.hh:534` |
| `type()` (`REMAINING` = `l_ ∈ {UNKNOWN, CONSUME}` — **vor** der String/Binary-Unterscheidung!) | `src/_parser.hh:230` |
| Aliase (privat) | `src/fmt_types.hh` |

**Tabelle-Lücken** (binäre Zwillinge für die Normalisierung):

| Text-Basis (Container) | Encoding | Zwilling-Key | Status |
|---|---|---|---|
| `CHAR`/`NUMERIC`/`NOPAD_CHAR` (FIX) | ascii/bcd/ebcdic | → `BINARY\|` (`IF_BINARY`, roh) | ✔ vorhanden |
| `L*CHAR`/`L*NUM` (L/LL/LLL) | ascii | `L*BINARY\|ASCII` (`IFA_L*BINARY`) | ✔ vorhanden |
| `L*CHAR` (L/LL/LLL) | bcd | `L*BINARY\|BCD` (`IFB_L*BINARY`) | ✔ vorhanden |
| `L*CHAR`/`L*NUM` (L/LL/LLL) | ebcdic | `L*BINARY\|EBCDIC` (`IFE_L*BINARY`, Data `BINARY` = roh) | ✔ vorhanden |
| `LLLLCHAR` | ascii | `LLLLBINARY\|ASCII` (`IFA_LLLLBINARY`) | **FEHLT → WP1** |
| `REMAINING` | text-Enc | → `REMAINING\|` (`IF_REMAINING`, roh) | ✔ vorhanden |

Wichtig: `BINARY\|EBCDIC` = `IFE_BINARY` mit **`HEX_EBCDIC`-Data-Encoder**
(skalare EBCDIC-`binary`-Sondersemantik) — die FIX-Text-Container müssen
deshalb explizit nach `BINARY\|` (`IF_BINARY`, Roh-Bytes) normalisieren,
nicht nach `BINARY\|EBCDIC`.

Codec-Voraussetzung erfüllt: `codec::as<vector<uint8_t>, e>` unterstützt
genau `e ∈ {BINARY, HEX_EBCDIC}` (Roh-Kopie bzw. HEX-Decode),
`codec::to<e, vector<uint8_t>>` analog (`_codec_impl.hh:117/221`) —
alle benötigten Zwillings-Aliase verwenden `Encoder::BINARY` (roh) als
Data-Encoder → **kein Codec-Änderungsbedarf**.

---

## 2. Ziellage (Verhaltens-Spezifikation)

1. **Alle** `type: nested`-Containerformate laden und decodieren ohne
   Crash; Wire-Format bleibt unverändert:
   - Container-Rahmen (Längen-Präfix + Daten): gleicher L-Zähler +
     gleicher Prefix-Encoder wie vorher; Container-Daten = Roh-Bytes,
     die an die Kinder weitergereicht werden.
   - Kinder decodieren/kodieren mit ihrem eigenen Encoding
     (Erbschaftsregel **unverändert**: Text-Container → eigenes Encoding,
     neutrale Container → globales Encoding).
2. **Loader-Normalisierung** (nur Parser-Konstruktion, intern):
   - `L* + CHAR/NUMERIC/NOPAD_CHAR` → `L*BINARY` (L-Zähler + Prefix-Encoding
     erhalten; `LLLLCHAR|ASCII` → `LLLLBINARY|ASCII`, neu in WP1).
   - `FIX + CHAR/NUMERIC/NOPAD_CHAR` → `BINARY|` (`IF_BINARY`, roh;
     Encoding explizit geleert — `BINARY|EBCDIC`-HEX_EBCDIC-Falle).
   - `REMAINING + Text-Encoding` → `REMAINING|` (`IF_REMAINING`, roh).
   - Skalare Textfelder (nicht nested) bleiben string-basiert
     (Verhalten unverändert).
3. **Introspektion** meldet das **deklarierte** Format
   (`spec->field(48)->format.type == "char"`, `prefix_digits == 3`).
4. **Runtime-Guard (Fail-closed):** nicht-binärer Container-Basis-Parser
   (manuell konstruiert) → positioniertes `std::runtime_error`
   (`[ISO8583] … Fail-closed …`) statt SEGV, in beiden Zweigen.
5. **FR-3:** typisierte TLV-Kinder im fixen Modus funktionieren in
   **text-basierten** Containern exakt wie in binären (Case C grün:
   `OpaqueField` + byte-identischer Roundtrip).
6. **Doku:** `lllchar`-Beispiele bleiben (D4) + Normalisierungs-Hinweis in
   allen vier Referenzstellen; `llllchar` (ascii-only) wird als
   Container-Format vollständig dokumentiert.
7. **Changelog:** Eintrag im Unreleased/0.6.0-Abschnitt (Fix + FR-3),
   `changelog.md` und Spiegel `docs/changelog.md` identisch.
8. **Kein Public-API/ABI-Änderung** (nur private Header
   `src/fmt_types.hh`, `src/_parser.hh`, `src/_spec.cc`).

---

## 3. Arbeitspakete (umsetzungsreife Reihenfolge, TDD)

> Build/Verifikation (jedes WP, vgl. AGENTS.md §7):
> `cmake --preset debug` (VCPKG_ROOT gesetzt) →
> `cmake --build --preset debug` → gezielt:
> `build/debug/tests/libiso8583_tests "<tags>"` → am Ende
> `ctest --preset debug` + `libiso8583_tests "[e2e]"`;
> ctest-Ausgabe auf Skip-Warnungen sichten (A4.33).

### WP1 — Parser-Tabelle ergänzen (`src/fmt_types.hh`, `src/_spec.cc`)

1. **`src/fmt_types.hh`** — ASCII-Sektion, nach `IFA_LLLBINARY`:

   ```cpp
   // Binary variable Länge (ASCII Length-Prefix)
   using IFA_LLLLBINARY = ISOBinaryFieldParser< codec::Length::LLLL, codec::PrefixEncoder::ASCII, codec::Encoder::BINARY >;
   ```

2. **`src/_spec.cc`, `parserTable()`** — ASCII-Block, nach
   `{ "LLLBINARY|ASCII", ... }`:

   ```cpp
   { "LLLLBINARY|ASCII",  MAKE(IFA_LLLLBINARY)  },
   ```

   (Keine neuen `BINARY|ASCII`/`BINARY|BCD`-Einträge nötig: FIX-Text-
   Container normalisieren nach `BINARY|` — s. Ziellage 2.)

3. **Verifikation:** Build grün. (Der echte Proof kommt in WP4, Case E —
   vor WP2 würde `llllchar|ascii`-Container ohnehin erst in WP2+WP1
   zusammen funktionieren.)

Commit: `[+](Added) Parser-Tabelle: LLLLBINARY|ASCII-Zwilling (IFA_LLLLBINARY)`

---

### WP2 — Loader-Normalisierung (`src/_spec.cc`)

1. **Neuer Static-Helfer** direkt vor `buildFieldParser` (nach
   `createScalarParser`), mit deutschem Kommentarblock:

   ```cpp
   // (0.6.0, FR-3) Container-Basis-Parser normalisieren.
   //
   // Text-basierte Containerformate (char/numeric/nopad_char mit oder ohne
   // L-Prefix, remaining + Text-Encoding) erzeugen via createScalarParser
   // einen string-basierten Basis-Parser. In den T=parser-Zweigen von
   // ISOFieldParser (BinaryField-Scratch bei unparse / BinaryField-Wrapper
   // bei parse, s. src/_parser.hh) crasht das mit einem
   // Null-Pointer-Dereference (dynamic_pointer_cast<OpaqueField> auf den
   // BinaryField -> nullptr) — SIGSEGV.
   //
   // Die Wire-Präfix-Semantik des binären Zwillings ist identisch
   // (gleicher L-Zähler + Prefix-Encoder), und Container-Nutzdaten sind
   // immer rohe Bytes, die an die Kinder weitergereicht werden (jedes Kind
   // löst sein eigenes Encoding auf) -> die Normalisierung ist wire-neutral:
   //   L* + Text              -> L*BINARY   (Prefix-Encoding bleibt)
   //   FIX + Text             -> "BINARY|"  (IF_BINARY, Rohbytes; NICHT
   //   "BINARY|EBCDIC" — das wäre IFE_BINARY mit HEX_EBCDIC-Data-Encoder)
   //   REMAINING + Text-Enc   -> "REMAINING|" (IF_REMAINING, Rohbytes)
   //
   // Wichtig: nur der lokale Parser-Bau wird normalisiert; das SpecField
   // selbst bleibt unverändert, damit die Introspektion (ISOSpec::field)
   // das deklarierte Format meldet. Skalare (nicht nested) Textfelder
   // bleiben string-basiert (Verhalten unverändert).
   static SpecField containerBaseField(const SpecField& f) {
       SpecField cf = f;
       std::size_t ls = 0;
       while (ls < cf.format.size() && cf.format[ls] == 'L') ++ls;
       const std::string rest = cf.format.substr(ls);
       if (rest == "CHAR" || rest == "NUMERIC" || rest == "NOPAD_CHAR") {
           if (ls == 0) {
               cf.format = "BINARY";
               cf.encoding = "";
           }
           else
               cf.format = cf.format.substr(0, ls) + "BINARY";
       }
       else if (cf.format == "REMAINING" &&
                (cf.encoding == "ASCII" || cf.encoding == "EBCDIC" || cf.encoding == "BCD"))
           cf.encoding = "";
       return cf;
   }
   ```

2. **`buildFieldParser`, NESTED-Zweig** (Zeile ~812), eine Zeile ändern:

   ```cpp
   // FR-3 (0.6.0): Container-Basis-Parser auf den binären Zwilling
   // normalisieren (wire-neutral, s. containerBaseField) — sonst
   // SIGSEGV bei text-basierten Formaten (BinaryField-Scratch/-Wrapper
   // vs. string-basierter Basis-Parser).
   auto base = createScalarParser(containerBaseField(f));
   ```

3. **Verifikation (RED→GREEN via Probe):**
   - `build/debug/tests/libiso8583_tests "[fr3probe][crash][fr3tlv]"`
     → Case C decodiert jetzt ohne SEGV.
   - `... "[fr3probe][crash][fr3nested]"` → Case D grün.
   - (Die Probe-Tests haben nach `unparse` nur `CAPTURE` — sie laufen
     durch; die echten Assertions kommen in WP4.)

Commit (mit WP3 zusammen): `[#](Fixed) SIGSEGV in text-basierten Nested-Containern:
Loader-Normalisierung auf den binären Zwilling + Fail-closed-Guard`

---

### WP3 — Runtime-Guard Fail-closed (`src/_parser.hh`)

1. **TDD-RED zuerst:** neuer Test (Teil von WP4-Datei, s. u., Case I):
   manuell konstruierte `ISONestedFieldParser<ISOBaseParser>` mit
   string-basierter Basis (`IFE_LLLCHAR`) → `REQUIRE_THROWS_AS(...
   std::runtime_error)` in `unparse`. Vor der Guard-Implementierung
   **crasht** der Test (SEGV statt Exception) = RED-Nachweis.

2. **Static-Helfer** in `src/_parser.hh` (Dateibereich, nach den
   includes / vor `class ISOFieldParser`; Datei-`static` = interne
   Linkage pro TU, passt zum Header-Stil):

   ```cpp
   // [ISO8583] FR-3 (0.6.0): Fail-closed-Guard für den Container-Basis-
   // Parser (n_). Beide T=parser-Zweige arbeiten mit einem BinaryField
   // (unparse: lokaler Scratch-Buffer; parse: Wrapper). Ein nicht
   // binär-basierter Basis-Parser (z. B. string-basiertes lllchar) würde
   // dort einen Null-Pointer-Dereference auslösen (SEGV). Die Spec-Ladung
   // normalisiert Text-Container automatisch auf den binären Zwilling
   // (src/_spec.cc: containerBaseField); dieser Guard fängt manuell
   // konstruierte ISONestedFieldParser-Instanzen ab (Fail-closed).
   // REMAINING ist in type() nicht zwischen binär/string unterscheidbar
   // (l_ UNKNOWN/CONSUME) -> zusätzlich create_component-Probe, aber NUR
   // bei type()==REMAINING (create_component von UNUSED wirft sonst).
   static void checkContainerBase(
       const ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr& base,
       TNG_KEY_TYPE de)
   {
       using PT = ::TNG_NAMESPACE::ISOFieldParserType;
       if (base == nullptr)
           throw std::runtime_error(
               "[ISO8583] Fail-closed (DE " + std::to_string(de) +
               "): Nested-Parser ohne Container-Basis-Parser "
               "(incomplett initialisiert).");
       if (base->type() != PT::BINARY && base->type() != PT::REMAINING)
           throw std::runtime_error(
               "[ISO8583] Fail-closed (DE " + std::to_string(de) +
               "): Container-Basis-Parser ist nicht binär-basiert. "
               "Text-basierte Containerformate (z. B. lllchar) werden beim "
               "Spec-Load automatisch auf den binären Zwilling normalisiert "
               "- dieser Fehler betrifft nur manuell konstruierte "
               "ISONestedFieldParser-Instanzen.");
       if (base->type() == PT::REMAINING &&
           std::dynamic_pointer_cast<::TNG_NAMESPACE::BinaryField>(
               base->create_component(0)) == nullptr)
           throw std::runtime_error(
               "[ISO8583] Fail-closed (DE " + std::to_string(de) +
               "): 'remaining'-Container-Basis ist text-basiert "
               "(binär-basierter Basis-Parser erforderlich; der Spec-Load "
               "normalisiert automatisch).");
   }
   ```

3. **Aufrufstellen** (je eine Zeile):
   - `parse()`: **Start** des Zweigs
     `else if constexpr (std::is_base_of_v< ISOBaseParser, T >)`
     (~Zeile 250), vor `c_->parse(c)`:
     `checkContainerBase(n_, c->key());`
   - `unparse()`: **Start** des Zweigs
     `if constexpr (std::is_base_of_v<ISOBaseParser, T>)`
     (~Zeile 365), vor der Scratch-Anlage:
     `checkContainerBase(n_, c->key());`

4. **Verifikation:** Case I (unparse-Zweig) + Case I' (parse-Zweig:
   `parser->parse(msg)` wirft ebenfalls) grün; Cases A–H grün.

Commit: wie WP2 (ein Commit, logisch: „Crash-Fix").

---

### WP4 — Probe → dauerhafter Regressionstest (`tests/`)

1. **Umbenennen** `tests/test_tlv_fixed_typed_probe.cc` →
   `tests/test_nested_text_container.cc`; Header-Kommentar ersetzen
   (Probe-Hinweis → dauerhafter Regressionstest für: text-basierte
   Nested-Container + FR-3 typisierte Kinder im fixen TLV-Modus +
   Guard). **`tests/CMakeLists.txt`**: Eintrag entsprechend umbenennen
   (Position vor `test_e2e_full_message.cc` bleibt — e2e zuletzt).

2. **Cases A–D** aus der Probe übernehmen und aufwerten:
   - A/B: `REQUIRE_NOTHROW(unparse)` + Assertions + Roundtrip
     `CHECK(msg->parse(msg) == raw)` (bestehen).
   - **C** (`lllchar` + tlv + `char`/`ebcdic`-Kind):
     `REQUIRE_NOTHROW(msg->unparse(msg, raw));`
     `de48->get<OpaqueField>(71)` != nullptr, `value() == "33V "`,
     `get<BinaryField>(71) == nullptr`; Roundtrip byte-identisch.
     Zusätzlich (einmal, hier): **Introspektion** via
     `loadBothFromYaml`: `spec->field(48)->format.type == "char"`,
     `prefix_digits == 3` (deklariert, nicht normalisiert);
     `dump()` enthält „33V " (nicht-sensitive Kind sichtbar).
   - **D** (`lllchar` + children-Sequenz): `REQUIRE_NOTHROW(unparse)`;
     Kind 0 = `OpaqueField` „123", Kind 1 = `OpaqueField` „ABC";
     Roundtrip byte-identisch.

3. **Neue Cases** (gleiche Datei, eigene `TempYaml`-Helper wie in der
   Probe; Tags `[fr3nested]` + Domain-Tag):
   - **E** `llllchar` + globale `encoding: ascii` + children-Sequenz
     (ascii-Textkinder): LLLL-Zwillingspfad (braucht WP1) →
     `REQUIRE_NOTHROW` + Roundtrip.
   - **F** `remaining` + Text-Encoding (globale `encoding: ebcdic` oder
     Feld-Override `ascii`) + `length` (0.6.0-Pflicht) + children-
     Sequenz: Container-Basis wird zu `IF_REMAINING` normalisiert →
     `REQUIRE_NOTHROW` + Roundtrip.
   - **G** `lllchar` mit **Feld-Override** `encoding: ascii` bei
     globaler `ebcdic` + ascii-Textkinder: ASCII-Prefix-Pfad
     (`IFA_LLLBINARY`-Basis); Roundtrip.
   - **H** `lllchar` + `encoding: bcd` + `numeric`/`bcd`-Kind:
     BCD-Prefix-Pfad (`IFB_LLLBINARY`-Basis); Roundtrip.
   - **I** **Guard (manuell konstruiert)** — Datei braucht
     `#include "_parser.hh"` (wie `test_field_parser.cc` etc.):
     ```cpp
     auto base = std::make_shared<IFE_LLLCHAR>(999, "Base");
     auto sub  = std::make_shared<ISOBaseParser>("Sub");
     sub->add(std::make_shared<IFE_CHAR>(3, "C0"));
     auto nested = std::make_shared<ISONestedFieldParser<ISOBaseParser>>(base, sub);
     auto msg = std::make_shared<Message>();
     // ... DE2/DE11/DE48 wie Cases A-D befüllen (lllchar-Konvention) ...
     REQUIRE_THROWS_AS(nested->unparse(component, raw, off), std::runtime_error);
     ```
     (Konkretisierung der Komponente/Offsets beim Umsetzen an die
     Signatur `unparse(component, buffer, offset)` anlehnen; Message-
     Ebene: Guard testet den Parser direkt — `nested->unparse(...)` auf
     einer `ISOMessage`-Komponente.)
   - **I'** Guard parse-Zweig: gleiches Setup, `REQUIRE_THROWS_AS
     (nested->parse(component), std::runtime_error)`.
   - **J** Guard-Sanity: manuelle Instanz mit **binärer** Basis
     (`IFE_LLLBINARY`/`IF_BINARY`) + Sub → `REQUIRE_NOTHROW`
     (unparse + parse) — Guard ist nicht über-eager.

4. **Volle Verifikation:** Build + `libiso8583_tests "[fr3nested]"`
   grün + `ctest --preset debug` + `libiso8583_tests "[e2e]"`;
   Skip-Warnungen sichten.

Commit: `[+](Added) Regressionstests: text-basierte Nested-Container +
FR-3 (dauerhafter Ersatz für die Probe-Matrix)`

---

### WP5 — Doku (D4: `lllchar` behalten + Normalisierungs-Hinweis)

1. **`AGENTS.md` (root, Englisch):**
   - §5 YAML-Beispiel: DE48-Block (`format: lllchar`) **beibehalten**.
   - Formats-Absatz (~Zeile 314): `llllchar` (ascii only) ergänzen
     (steht nur `llllbinary`).
   - Neuer kurzer Absatz nach dem TLV-children-Notation-Block:
     > **Text-based nested containers:** `type: nested` works with any
     > container format, incl. text-based ones (`lllchar`, `llchar`,
     > `llllchar` (ascii only)). Since 0.6.0 the loader normalizes the
     > container **base parser** to its binary twin — wire-neutral: same
     > L-counter + prefix encoding, container data reaches the children
     > as raw bytes (each child resolves its own encoding). Before 0.6.0
     > such containers crashed (`SIGSEGV`) during `unparse()`/`parse()`.
     > Introspection (`ISOSpec::field`) reports the declared format.
2. **`include/iso8583/AGENTS.md` (Deutsch):**
   - Beispiel DE48 (~Zeile 427–435): `lllchar` beibehalten + kurzer
     Hinweis-Kommentar (Normalisierung, s. o.).
   - Format-Liste (~Zeile 490): `llllchar` ergänzen.
   - Gleicher Normalisierungs-Absatz wie root (deutsche Fassung).
3. **`docs/internals/spec_schema.md`:**
   - Beispiele (~Zeile 208, ~Zeile 346): `lllchar` beibehalten.
   - Format-Matrix (~Zeile 103): Zeile `l*`-Varianten bleibt (llllchar
     ascii-only ist bereits notiert) — dort + im nested/TLV-Abschnitt
     neuen Unterabschnitt **„Container-Basis-Parser-Normalisierung
     (0.6.0)"** (normativ): die drei Normalisierungs-Regeln aus
     Ziellage 2 + HEX_EBCDIC-Hinweis (`BINARY|EBCDIC`-Falle) +
     Introspektion meldet Deklariertes + Vor-0.6.0 = SIGSEGV.
4. **`docs/internals/yaml_format.md`:** am `type: nested`/TLV-Dokuort
   denselben Hinweis (2–4 Sätze, auf spec_schema.md verweisen).
5. **Kein Sphinx-/Doxygen-Risiko:** keine neuen Public-Header, keine
   `///`-Änderungen → `sphinx -W`-CI unbeeinflusst.

Commit: `[i](Info) Doku: Container-Basis-Normalisierung (AGENTS.md ×2,
spec_schema.md, yaml_format.md)`

---

### WP6 — Changelog (`changelog.md` + Spiegel `docs/changelog.md`, identisch)

Im `## Unreleased`-Abschnitt (0.6.0-Kandidat), nach dem
`remaining`-Breaking-Block, neuen Block (Heading-Stil wie die
bestehenden Einträge des Abschnitts):

```markdown
### [#](Fixed) SIGSEGV in text-basierten Nested-Containern (0.6.0-Kandidat)

- `type: nested` + text-basiertes Containerformat (`char`/`numeric`/
  `nopad_char`, L-Prefix `llchar`/`lllchar`/`llllchar` (ascii) sowie
  `remaining` + Text-Encoding) crashte in `unparse()`/`parse()` mit
  SIGSEGV: der Container-Basis-Parser (string-basiert) empfing den
  `BinaryField`-Scratch/-Wrapper des Nested-Zweigs (seit 0.3.0
  Thread-Sicherheits-Pattern) → Null-Pointer-Dereferenz. Die
  AGENTS.md-Beispiele (DE48 `lllchar`+`tlv`) waren betroffen; v0.5.0
  ebenso.
- Fix: Der Loader normalisiert den Container-Basis-Parser auf den
  binären Zwilling (wire-neutral: gleicher L-Zähler + Prefix-Encoding,
  Container-Daten bleiben Roh-Bytes für die Kinder; Introspektion
  meldet weiterhin das deklarierte Format). Neue Tabelle-Lücke
  geschlossen: `llllbinary|ascii` (`IFA_LLLLBINARY`).
- Fail-closed-Guard: manuell konstruierte `ISONestedFieldParser`-
  Instanzen mit nicht-binärer Container-Basis werfen jetzt ein
  positioniertes `std::runtime_error` statt SEGV.
- FR-3: Typisierte TLV-Kinder im fixen `tag_bytes`/`len_bytes`-Modus
  funktionieren damit auch in text-basierten Containern (in binären,
  z. B. `lllbinary`, bereits seit 0.5.0).
```

Commit: `[~](Changed) Changelog: 0.6.0-Kandidat —
Nested-Container-Fix + FR-3-Bestätigung`

---

### WP7 — Chatroom + Obsidian-Kontextnote

1. **Chatroom** `…/Obsidian/ai_connected/chatrooms/iso8583.md`
   (**append-only!**): neuen Block `## <ISO-8601> — iso8583-agent`
   anhängen (D5):
   - **FR-3-Bestätigung:** Typisierte Kinder im fixen TLV-Modus
     funktionieren in 0.5.0 auf binären Containern
     (`lllbinary`/`binary`) — Beleg: Probe-Case A
     (`lllbinary` + `tcc: true` + Kind `"71"` `char`/`ebcdic` →
     `OpaqueField` „33V ", Roundtrip byte-identisch); Mechanik ist
     modus-unabhängig (fix/BER teilen sich `store_se`/`TlvChildMap`).
   - **Ihr Symptom** (BinaryField/Roh-HEX statt OpaqueField bei
     v0.5.0/Pin `9517d36`): auf `lllbinary`-Containern reproduzierbar
     **nicht** → Bitte um exaktes Spec-Snippet des betroffenen DEs
     (Container-`format`, `tlv:`-Block `tag_bytes`/`len_bytes`/`tcc`/
     `encoding`, Kind-Deklaration) + welche Kind-Keys auf dem Wire
     standen (HEX-Dump des DE-Payloads).
   - **Crash-Hinweis:** `lllchar`-Container (das AGENTS.md-Beispiel!)
     crashten bis 0.6.0 mit SIGSEGV — falls deren Spec `lllchar`
     nutzte: ab 0.6.0 gefixt (Normalisierung auf binären Zwilling),
     typisierte Kinder funktionieren dort ebenfalls.
   - Ausblick: 0.6.0 mit Fix + Doku; Release-Zeitpunkt offen.
2. **Obsidian-Kontextnote**
   `ai_connected/iso8583-dev/FR-3 Abschluss + SIGSEGV-Fix textbasierte Container.md`
   (House-Style: Status-Header, Kontext-Note für Agenten/Sitzungen,
   Befund-Tabelle, WP/Commit-Tabelle, Verifikationstabelle,
   „Lokale Hilfsmittel", „Bewusst nicht erledigt") — via MCP
   `create_vault_file`.

---

## 4. Akzeptanzkriterien

- [ ] AC1: `libiso8583_tests "[fr3nested]"` komplett grün (A–J, inkl.
      Roundtrips byte-identisch).
- [ ] AC2: `ctest --preset debug` + `"[e2e]"` grün, ohne Skip-Fallen
      (A4.33: Ausgabe gescannt).
- [ ] AC3: `llllchar|ascii`-Container lädt (WP1-Nachweis in Case E).
- [ ] AC4: Introspektion meldet deklariertes Format
      (`char`/prefix 3 bei `lllchar`, Case C).
- [ ] AC5: Manuell konstruierter nicht-binärer Container-Basis wirft
      Fail-closed-`runtime_error` (Cases I/I'), binäre Basis bleibt
      durchlässig (Case J).
- [ ] AC6: Doku-Stellen (AGENTS.md ×2, spec_schema.md, yaml_format.md)
      zeigen `lllchar`-Beispiele + Normalisierungs-Hinweis; `llllchar`
      (ascii-only) gelistet.
- [ ] AC7: Changelog + Spiegel identisch, Eintrag im Unreleased/0.6.0.
- [ ] AC8: Chatroom-Block angehängt (append-only), Obsidian-Note
      gespeichert.
- [ ] AC9: Kein Public-API/ABI-Bruch (nur private Header berührt).

---

## 5. Risiken & Hinweise

| Risiko | Bewertung / Mitigation |
|--------|------------------------|
| **ABI:** keine (nur private Header `src/*.hh`; `IFA_LLLLBINARY` privat in `fmt/fmt_types.hh`) | Kein Consumer-Rebuild nötig; Changelog-Note reicht. |
| `BINARY\|EBCDIC`-HEX_EBCDIC-Falle | FIX-Text-Container normalisieren explizit nach `BINARY\|` (Encoding geleert) — in WP2-Helfer + Doku dokumentiert. |
| `LLLLBINARY\|BCD` fehlt weiterhin | `llllchar`+`bcd`-Container → klares Load-Error („Unbekannte Format/Encoding-Kombination") — akzeptabel: skalares `llllchar` existiert nur für ascii (konsistent). |
| Lücken-Semantik: nested `char\|bcd` (skalar ungültig) wird als Container geladen (→ `BINARY\|`) | Bewusst: Container = Roh-Bytes an Kinder, keine Wire-Ambiguität (FIX, kein Prefix); in Doku-Note als Toleranz erwähnen. |
| Guard triggert fälschlich | Guard prüft `type()` ∈ {BINARY, REMAINING}; REMAINING nur per `create_component`-Probe disambiguiert (nicht bei UNUSED/OPAQUE/… → kein `create_component`-Throw). Case J als Sanity-Check. |
| `ISOMessage`-Thread-Safety (0.3.0-Modell) | Guard ist read-only (keine Shared-State-Mutation); kein Locking-Verstoß. |
| Test-Datei-Umbenennung + CMake-Liste | `test_e2e_full_message.cc` bleibt **letzter** Eintrag (AGENTS.md §7/§15.11) — nur die Probe-Zeile umbenennen. |
| Changelog-Spiegel-Divergenz | `changelog.md` und `docs/changelog.md` im selben Commit pflegen (AGENTS.md §2/§14.2). |
| Consumer-Pin `9517d36` (v0.5.0) | Fix kommt erst mit 0.6.0 — Chatroom-Antwort (WP7) muss das explizit machen (kein „schon gefixt" gegenüber dem gepinnten Stand). |

---

## 6. Offene Punkte (nicht blockierend)

1. **Consumer-Spec** (D5): exaktes DE-Snippet + Payload-HEX von
   tng-wire-viewer — bestimmt, ob ihr v0.5.0-Symptom (BinaryField
   trotz `char`-Deklaration) auf `lllchar`-Container (Crash-Klasse)
   oder auf eine `tlv:`-Encoding-Diskrepanz zurückgeht.
2. `LLLBINARY\|BCD`- bzw. `LLLLBINARY\|BCD`-Zwillinge: bewusst nicht
   ergänzt (kein skalares Gegenstück, kein Bedarf).
3. Loader-Warnung bei text-basierten Containern: bewusst **keine**
   (Normalisierung ist deterministisch und wire-neutral; Doku reicht) —
   falls der Maintainer doch möchte: `TNG_LOG_INFO` in `containerBaseField`
   wäre ein Follow-up ohne Planänderung.

---

## 7. Aufwand & Reihenfolge

| WP | Schätzung | Abhängigkeit |
|----|-----------|--------------|
| WP1 Tabelle (`IFA_LLLLBINARY`) | klein | — |
| WP2 Loader-Normalisierung | klein–mittel | WP1 (nur für LLLL-Fall) |
| WP3 Runtime-Guard | klein | — (parallel zu WP2) |
| WP4 Tests (Probe → dauerhaft) | mittel | WP1–WP3 |
| WP5 Doku | mittel | WP4 (verifiziertes Verhalten dokumentieren) |
| WP6 Changelog | klein | WP5 |
| WP7 Chatroom + Obsidian | klein | WP6 (nach grünem Gesamtlauf) |

Empfohlene Commits (Konvention §14.1), jeweils mit grünem Testlauf:
1. `[+](Added) Parser-Tabelle: LLLLBINARY|ASCII-Zwilling (IFA_LLLLBINARY)` (WP1)
2. `[#](Fixed) SIGSEGV in text-basierten Nested-Containern: Loader-Normalisierung + Fail-closed-Guard` (WP2+WP3)
3. `[+](Added) Regressionstests: text-basierte Nested-Container + FR-3` (WP4)
4. `[i](Info) Doku: Container-Basis-Normalisierung` (WP5)
5. `[~](Changed) Changelog: 0.6.0-Kandidat — Nested-Container-Fix + FR-3` (WP6)

WP7 (Chatroom/Obsidian) ist kein Repo-Commit.