# Plan: BERTLV — Constructed-Tag-Kinder (verschachtelte TLV-Werte) dekodieren & kodieren (0.6.4-Kandidat)

Status: **Entwurf** (Reko vom Maintainer-Chat, 2026-10-01) — freizugeben, bevor die Umsetzung startet
Reko-Basis: `main` = `f9e59c8` (nach v0.6.3)
Feature-ID: **zu vergeben** beim Aufnahme (nächstes freie FR-/FE-Nummer; Arbeitstitel „BERTLV constructed children“)
Anforderung: „BERTLV baut/kennt auch *constructed* Tags (BER-Constructed-Bit `0x20`). Gibt es einen Weg, diese zu unterstützen?“ — d. h.: ein TLV-Kind, dessen Wert selbst eine Folge von TLVs ist (z. B. EMV-Tag `69` *Transaction Status Information*, `82` *Card Verification Results*), soll nicht als rohes `BinaryField`-Blob geliefert, sondern **rekursiv in seine enthaltenen TLVs aufgeschlüsselt** werden — und im Encode-Pfad wieder zu einem constructed-Wert zurückkodiert.

---

## 0. Entscheidungen (Freigabe nötig vor Umsetzung)

| # | Entscheidung | Empfehlung |
|---|---|---|
| a | **YAML-Grammatik:** Ein TLV-Kind ist ein *constructed*-Container, wenn es einen **eigenen `tlv:`-Block** trägt (`tlv: { ber: true }` oder `tlv: { tag_bytes, len_bytes }`) — mit **oder** ohne eigene `children:`-Map. Ein solches Kind darf **kein** `format:`/`length:`/`encoding:` deklarieren (die äußere TLV-Rahmung trägt Tag+Length; diese Keys wären widersprüchlich) → sonst **fail-closed** beim Laden. Ein Kind **ohne** `tlv:` bleibt ein skalares Kind (Bestand unverändert). | **Ja** (explizit, deklarativ — passt zum Fail-closed-Charakter des Loaders) |
| b | **Auto-Erkennung über das Constructed-Bit?** Nein — nur explizit erklärte Container-Kinder. Ein *undeklariertes* constructed-Tag (ohne `tlv:`) bleibt, wie heute, ein dynamisches `BinaryField`-Blob (Rohbytes, `SE<n>`-/Tag-Fallback). Das rohe Tag-Bit wird bereits im Key mitgeführt (s. §1.2), aber keine Implizit-Logik darüber. | **Ja** (keine Implizit-Erkennung) |
| c | **Adressierung:** Die verschachtelten TLVs eines constructed-Kinds landen in einer **Sub-`Message`** und werden über die **bestehende Punkt-Notation** angesprochen, z. B. `57.69.63` (DE57 → Tag `69` → Tag `63`). Kein neuer Adressierungs-Mechanismus. | **Ja** |
| d | **Scope BER vs. fix-TLV:** Die Rekursion ist **Policy-unabhängig** (`TagPolicy`/`LenPolicy`) → für **beide** TLV-Formen implementieren (BER `ber: true` **und** fixer `tag_bytes`/`len_bytes`-TLV). Primär-Tests = EMV/BER; fix-TLV läuft über denselben Codepfad. | **Ja** |
| e | **Rekursionstiefe:** wie bei `!use`/nested-Kindern durch die bestehende **≤ 200**-Begrenzung gedeckt (fail-closed darüber). | **Ja** |
| f | **Key-Typ:** funktioniert in **beiden** Builds (`int16_t` Default, `int32_t` mit `ISO8583_BERTLV`). 2-Byte-Sub-Tags (≥ `0x8000`) brauchen weiterhin den BERTLV-Build — bestehende Regel, unverändert (s. `store_se`-Key-Width-Guard, `src/_tlv.cc:110`). | **Ja** |

> **Offene Frage an den Maintainer (ggf. vor Umsetzung klären):** Soll das Constructed-Bit (`0x20`) zusätzlich als **validierte Eigenschaft** geprüft werden (d. h.: Kind deklariert `tlv:` aber das Tag hat das Constructed-Bit *nicht* → Warnung)? Empfehlung: **nur** im Strict-Modus eine WARN loggen, nicht werfen — in der Praxis (EMV) ist `69`/`82`/`9Fxx`-Constructed standardisiert, aber die Spec ist die Quelle der Wahrheit. In §3 als Option notiert.

---

## 1. Kontext & Befunde (Stand `f9e59c8`)

### 1.1 Was „constructed“ heißt

In BER-TLV (ISO/IEC 8825‑1) trägt das erste Tag-Byte ein **Constructed-Bit** (Bit 6, `0x20`): `0x01` = primitive, `0x69`/`0x81` = constructed. Ein constructed-Wert ist **nicht** Rohdaten, sondern eine **Folge von TLVs** (z. B. EMV `69` = Liste von Status-Word-TLVs wie `63`).

### 1.2 Befund: das Tag ist *bereits* korrekt erfasst — nur der **Wert** fehlt

- `BerTag::read` (`src/_tlv_policy.hh:200-229`) behandelt das Tag als **Big-Endian-Konkatenation der rohen Tag-Bytes** (dekomponiert Klasse/Constructed/Tag-Nummer **nicht**). Das Constructed-Bit ist also **im Key enthalten**: `0x69` → Key `105`, `0x49` → Key `73`. Constructed- und primitive Tags sind daher bereits **unterschiedliche Keys**.
- Konsequenz: Ein constructed-*Top*-Tag lässt sich heute schon als Kind erklären und sein **gesamter** Wert als ein `BinaryField` auslesen. Das fehlt: die **Rekursion in den Wert** (Aufschlüsselung der inneren TLVs) und die **umgekehrte Kodierung**.

### 1.3 Befund: drei flache Annahmen blockieren die Rekursion

1. **`TlvChildInfo`** (`src/_tlv.hh:67-75`) kennt nur `enc/text/description/sensitive/amount/sign/scale` — keinen „dieses Kind ist selbst ein TLV-Container“-Fall.
2. **`ISOTLVParser::unparse`** (`src/_tlv.hh:176-248`) läuft eine flache Schleife `TagPolicy::read → LenPolicy::read → store_se` (Hook-Point: der `store_se`-Aufruf bei `:236`); **keine** Rekursion.
3. **Loader-Whitelist** (`src/_spec.cc:466-490`, `:751-794`): Kind-Formate sind auf `BINARY/CHAR/NUMERIC/NOPAD_CHAR/AMOUNT` beschränkt (`:485-487`); es gibt **keinen** „Container-Kind“-Fall. Ein Kind mit eigenem `tlv:`/`children:` wird daher nicht weiterverfolgt.

### 1.4 Gute Nachricht: viel der Infrastruktur existiert bereits

- **Loader-Rekursion vorhanden:** `parseSpecField` parst `children` (Map → `f.tlv_children`, Sequenz → `f.children`) und ruft **rekursiv** sich selbst für jedes Kind auf (`src/_spec.cc:751-794`). Ein Kind mit eigenem `tlv:`-Block + `children:`-Map wird bereits in ein Kind-`SpecField` mit eigenen `tlv`/`tlv_children` aufgelöst.
- **Introspektions-Rekursion vorhanden:** `makeSpecFieldInfo` läuft bereits in `tlv_children` rekursiv (`src/_spec.cc:1290-1291`) und setzt `tlv_is_ber` aus `f.tlv->ber` (`:1277`) — die `SpecFieldInfo`-Struktur (`include/iso8583/ISOSpec.hh:118-152`) hat `tlv_children` **und** `tlv_is_ber` bereits.
- **Sub-Message-Rekursions-Muster vorhanden:** Der NESTED-Pfad zeigt exakt, wie man ein Sub-Kind dekodiert und seinen Sub-Parser anhängt (`src/_parser.hh:490-502`: `c_->unparse(c, payload, child_base_offset)` + `childMsg->parser(c_)`). Das ist das Muster, das für constructed-Kinder zu spiegeln ist.
- **Enkodierungs-Mechanik vorhanden:** `ISOTLVParser::parse` (`src/_tlv.hh:250-327`) schreibt pro Key `TagPolicy::write + LenPolicy::write + data` — für ein constructed-Kind genügt es, `data` durch den re-serialisierten Sub-Payload zu ersetzen.

→ Der eigentliche Aufwand ist **kompakter** als ein Grundfeature: (1) Whitelist, (2) Laufzeit-Datenmodell + Rekursion in `unparse`/`parse`, (3) ein neuer Kind-„Typ“ (Container), (4) Tests + Doku.

### 1.5 Exakte Anknüpfungspunkte (file:line, Stand `f9e59c8`)

| Baustein | Ort |
|---|---|
| `BerTag::read` / `BerLength::read` (Constructed-Bit bleibt im rohen Tag-Wert) | `src/_tlv_policy.hh:200-229` / `:257-296` |
| `ISOTLVParser::unparse` (flache TLV-Schleife; Rekursions-Hook = `store_se`-Call) | `src/_tlv.hh:176-248` |
| `ISOTLVParser::parse` (Encode-Schleife; `msg->get<BinaryField>(se_key)`-Zweig) | `src/_tlv.hh:250-327` |
| `TlvChildInfo` (strukt. um `subParser` + `container` zu ergänzen) | `src/_tlv.hh:67-75` |
| `TlvChildMap` (= `unordered_map<std::size_t, TlvChildInfo>`) | `src/_tlv.hh:78` |
| `store_se` (neuer Container-Zweig) | `src/_tlv.cc:92-154` |
| `sorted_se_keys` (Filter `key >= 0`) | `src/_tlv.cc:82-90` |
| `buildTlvFieldParser` (Kind-`TlvChildMap` aufbauen; rekursiv Sub-Parser bauen) | `src/_spec.cc:1095-1140` |
| `makeTlvParser` (Dispatch fix/BER; `BERTLVParser`-Alias) | `src/_spec.cc:1138` (`makeTlvParser`) · Alias `src/_tlv.hh:421` |
| `parseSpecField` (Kind-`SpecField`-Rekursion; Container-Kind tolerieren) | `src/_spec.cc:751-794` |
| `validateSpecYaml` (Kind-Whitelist `:485-487` um Container-Kind erweitern) | `src/_spec.cc:335ff`, `:466-490` |
| NESTED-Komposition (`nested->subParser(buildTlvFieldParser(f))`) | `src/_spec.cc:1208-1234` |
| Sub-Message-Rekursions-Muster (unparse + `childMsg->parser`) | `src/_parser.hh:490-502` |
| `makeSpecFieldInfo` (tlv_children-Rekursion, `tlv_is_ber`) | `src/_spec.cc:1264-1294` |
| Public `SpecFieldInfo` (`tlv_children`, `tlv_is_ber`) + Doxygen | `include/iso8583/ISOSpec.hh:118-152` |

---

## 2. Design

**Zielmodell:** Ein TLV-Kind trägt entweder (a) einen skalaren Fall (`TlvChildInfo::text/enc/amount/…`, wie heute) **oder** (b) einen **Container-Fall**: `TlvChildInfo::container == true` + `TlvChildInfo::subParser` = der TLV-Parser des Kinds (rekursiv gebaut, `ISOParserPtrBase::SmartPtr`).

**Decode (`store_se`):** Bei `child->container` wird **keine** flache Komponente erzeugt. Stattdessen:
1. Sub-`Message` erzeugen (Key = Kind-Tag als `TNG_KEY_TYPE`),
2. deren Parser auf `child->subParser` setzen,
3. `child->subParser->unparse(subMsg, valueBytes, wire_offset)` aufrufen (Wert-Bytes = `data_offset..data_len` aus dem äußeren TLV-Frame — das Längenfeld war bereits von `LenPolicy` konsumiert, es gibt **kein** zusätzliches L-Präfix),
4. `msg->set(subMsg)`.

Damit sind die inneren TLVs als `57.69.63` … erreichbar (Punkt-Notation durch die Sub-`Message`), analog zum NESTED-Pfad.

**Encode (`ISOTLVParser::parse`):** Im Key-Loop, **vor** den Skalar-Zweigen:
- `if (child && child->container)`: `sub = msg->get<ISOMessage>(se_key)`; falls vorhanden & nicht leer → `data = sub->parser()->parse(sub)`; sonst `log_warn_se_missing` + skip (wie heute für fehlende Kinder). Danach `TagPolicy::write + LenPolicy::write + data` wie gehabt.
- `sorted_se_keys` (`src/_tlv.cc:82`) sieht das constructed-Kind bereits als einzelnen Root-Key (Tag ≥ 0) — die inneren Keys leben in der Sub-`Message` und stören den Root-Loop nicht.

**Threading:** Parser sind nach dem Laden immutable & teilbar; die neuen `subParser`-`shared_ptr` sind read-only nach `buildTlvFieldParser`. `store_se` reentert den *Kind*-Parser auf einer *eigenen* Sub-`Message` — kein gemeinsamer, zu schützender Zustand → **keine** neue Sperre nötig (das `fallback_description_cache_`-Lock in `_tlv.hh` bleibt unangetastet).

**PCI/`sensitive`:** Container-Kind erbt `sensitive` wie heute (`child.sensitive || f.sensitive`, `src/_spec.cc:1131`); das Flag verbreitet sich auf den Sub-Baum (bestehendes Verhalten für `sensitive: true` auf Containern).

---

## 3. Umsetzungsschritte (Meilensteine)

### M1 — Loader: Container-Kind erlauben & rekursiv aufbauen
1. `validateSpecYaml` (`src/_spec.cc:466-490`): Kind-Whitelist ergänzen — ein Kind-Knoten **mit** `tlv:`-Block = Container-Kind; für es den Skalar-Format-Check **überspringen** und stattdessen prüfen: (i) `tlv:` ist BER (`ber: true`) **oder** fix (`tag_bytes`/`len_bytes`); (ii) **keine** `format:`/`length:`/`encoding:` (widersprüchlich → positioniertes `SpecValidationError`). Container-Kind mit `children:` = deklariert; ohne `children:` = dynamisch (innere TLVs werden dynamisch dekodiert, wie ein undeclared-BERTLV-Container).
2. `parseSpecField` (`src/_spec.cc:751-794`): sicherstellen, dass ein Kind ohne `format:` (weil Container) nicht als Fehler gilt — kleine Guard-Verzweigung „Kind hat `tlv` → format-optional“. Die Rekursion in `parseSpecField` selbst ist bereits vorhanden.
3. Rekursionstiefen-Guard (≤ 200) auf dem Kind-Pfad, wie bei nested-Kindern.

### M2 — Laufzeit-Datenmodell
1. `TlvChildInfo` (`src/_tlv.hh:67-75`): `bool container = false;` + `std::shared_ptr<::TNG_NAMESPACE::ISOParserPtrBase> subParser;` ergänzen (Skalar-Felder für Nicht-Container unverändert). Kopierbar (shared_ptr), const-freundlich.

### M3 — Sub-Parser bauen
1. `buildTlvFieldParser` (`src/_spec.cc:1095-1140`): in der `f.tlv_children`-Schleife — falls `child.tlv` gesetzt (Container-Kind): `info.container = true; info.subParser = buildTlvFieldParser(child);` (rekursiv); `sensitive`/`encoding`-Ererbung wie heute. Sonst: Skalar-Pfad unverändert.

### M4 — Decode-Rekursion
1. `store_se` (`src/_tlv.cc:92-154`): neuen Container-Zweig **vor** dem Skalar-Bau: `if (child && child->container)` → Sub-`Message` erzeugen, `subMsg->parser(child->subParser)`, `child->subParser->unparse(subMsg, b, wire_offset)` (Wert-Bytes), `msg->set(subMsg)`, `return`. Der bestehende Key-Width-Guard (`:110`) bleibt für das Kind-Tag wirksam. (Muster: `src/_parser.hh:490-502`.)

### M5 — Encode-Rekursion
1. `ISOTLVParser::parse` (`src/_tlv.hh:250-327`): im Key-Loop Container-Zweig (s. §2 „Encode“). Fehlendes constructed-Kind → `log_warn_se_missing` + skip.

### M6 — Introspektion (fast automatisch)
1. `makeSpecFieldInfo` (`src/_spec.cc:1264-1294`) benötigt **keine** Strukturänderung: `tlv_children`-Rekursion (`:1290-1291`) + `tlv_is_ber` (`:1277`) reichen ein Container-Kind samt dessen eigener `tlv_children` automatisch durch. **Verifizieren:** Container-Kind meldet `is_nested = true` und trägt eigene, korrekt gefüllte `tlv_children`.
2. Doxygen `SpecFieldInfo::tlv_children` (`include/iso8583/ISOSpec.hh:120-139`): ergänzen, dass ein Kind selbst ein (BER/fixed) TLV-Container sein kann.
3. **ABI prüfen:** `TlvChildInfo` ist eine **private** Struktur in `src/_tlv.hh` (nicht public) → deren Erweiterung ändert **kein** Public-ABI. `SpecFieldInfo` ist unverändert (wir füllen nur existierende Mitglieder tiefer). Erwartet: **keine** Public-ABI-Änderung — im Commit-/Release-Text trotzdem explizit bestätigen.

### M7 — Tests (`tests/test_tlv_parser.cc` — bereits registriert)
- **Roundtrip BER:** BERTLV-Container (DE57-Style) mit constructed-Kind (z. B. Tag `69`), dessen Wert eine Folge primitiver TLVs (`63`, ggf. `9Fxx`) ist. Decode → `msg->get<...>` / Punkt-Notation `57.69.63` poppeln den erwarteten Wert; Re-Encode → **byte-identisch** zur Eingabe.
- **Roundtrip fix-TLV:** constructed-Kind in einem `tag_bytes/len_bytes`-Container → gleicher Codepfad, eigener Test.
- **Regression undeclared constructed:** constructed-Tag **ohne** `tlv:` → weiterhin `BinaryField`-Blob (Bestand bleibt).
- **Loader-Whitelist:** Spec mit Container-Kind lädt erfolgreich; Container-Kind mit illegalem `format:`/`length:` → positionierter `SpecValidationError` (fail-closed).
- **Introspektion:** `tlv_children` verschachtelt gefüllt, `tlv_is_ber` auf dem Kind korrekt.
- **Key-Typ:** 2-Byte-Sub-Tag (≥ `0x8000`) dekodiert im BERTLV-Build (int32); im Default-Build greift der Key-Width-Guard (Warnung + Skip) — je einen Fall.
- **Strict:** constructed-Kind in Strict-Modus (ungültige innere Bytes → positionierter Fehler) + non-strict.

### M8 — Doku
1. `docs/internals/spec_schema.md` §6 („Nested, TLV und BERTLV“): Unterabschnitt **Constructed-TLV-Kinder** + EMV-Beispiel (Tag `69`), Grammatik-Regel (Kind mit eigenem `tlv:` = Container; kein `format:`/`length:` auf Container-Kind), Whitelist-Hinweis.
2. `include/iso8583/AGENTS.md`: TLV-`children`-Abschnitt → Container-Kind-Regel + Beispiel; Whitelist-Punkt ergänzen; ggf. „Typische Fehler“-Zeile (kein `format:`/`length:` auf einem Container-Kind).
3. `.agents/yaml-spec.md` §5/§6: falls dort die Kind-Whitelist gespiegelt wird → anpassen.
4. `changelog.md` **und** `docs/changelog.md` (identisch halten) beim Release; Version `0.6.3 → 0.6.4` an **4** Stellen (`CMakeLists.txt`, `include/iso8583/config.h` `TNG_CORE_VERSION`, Root-`vcpkg.json`, `vcpkg-port/vcpkg.json`).
   > Hinweis: Die Versionsangabe in der Wurzel-`AGENTS.md` ist aktuell (Stand `f9e59c8`) auf `0.6.2` veraltet (git-log zeigt v0.6.3 released) — bei Gelegenheit mitkorrigieren.

### M9 — Verifikation & Rollout
```bash
# MSVC+Ninja: aus Developer Prompt / vcvars64.bat heraus (sonst LNK1104)
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
# einzugrenzen:
#   build/debug/bin/libiso8583_tests "[tlv]"      bzw.  "[spec]"
```
- Zusätzlich **BERTLV-Variante** bauen: `cmake -DISO8583_BERTLV=ON …` + `[tlv]`/`[spec]`-Suiten (2-Byte-Sub-Tags sind nur dort erreichbar).
- `sphinx -W`-Doku-Build muss grün bleiben (CI-erzwungen).
- **Release erst nach grünem Docs-Run auf `main`** (AGENTS.md §14.2); Releases sind maintainer-initiiert.

---

## 4. Definition of Done

- [ ] Roundtrip **byte-identisch** für BER- **und** fix-TLV constructed-Kinder (M7).
- [ ] Punkt-Notation-Ansprechbarkeit (`57.69.63` …) funktioniert (Decode + Encode).
- [ ] Fail-closed: Container-Kind mit `format:`/`length:` wird beim Laden mit positioniertem Fehler abgelehnt.
- [ ] Undeklarierte constructed-Tags bleiben `BinaryField` (kein Verhaltensbruch).
- [ ] Introspektion (`tlv_children` verschachtelt, `tlv_is_ber`) korrekt.
- [ ] **Beide** Key-Typ-Builds (Default int16, `ISO8583_BERTLV` int32) grün.
- [ ] Doku (`spec_schema.md` §6, `AGENTS.md`, ggf. `.agents/yaml-spec.md`) + `changelog.md`/`docs/changelog.md` aktualisiert; Versionsbump an 4 Stellen.
- [ ] Public-ABI-Impact bestätigt (erwartet: keiner — `TlvChildInfo` ist private).
- [ ] `[tlv]` + `[spec]` + `sphinx -W` grün; ctest-Skips im Output geprüft.

---

## 5. Risiken / offene Punkte

- **Nested-Sub-Message-Lebenszeit:** Sub-`Message` wird `msg->set(subMsg)` als Komponente gehalten — muss wie jede `ISOComponentPtrBase` überleben; `store_se` liefert sie als `shared_ptr` (kein dangelnder Zeiger). Analog NESTED-Pfad.
- **`ISOTaggedField`:** Der Typ „Tag + verwiesenes Feld“ (`include/iso8583/AGENTS.md`) ist hier **nicht** betroffen; constructed-Kinder nutzen die normale Sub-`Message`/`BinaryField`-/`OpaqueField`-Repräsentation. (Falls gewünscht: später optional ein `ISOTaggedField`-Wrapper — nicht Teil dieses Plans.)
- **Constructed-Bit-Validierung (Option, s. §0):** ggf. Strict-WARN, wenn `tlv:` deklariert aber Constructed-Bit fehlend. Entscheidung beim Freigabe-Review.
- **Field-only-Specs (FE-1):** Ein constructed-Kind innerhalb einer *field-only*-Spec (z. B. DE55) läuft über denselben `buildFieldBlockParser`-Pfad (`src/_spec.cc:1081ff`) — M3–M5 gelten dort identisch; einen field-only-Regressionstest ergänzen, wenn die Field-only-BERTLV-Kombination im Scope liegt.
