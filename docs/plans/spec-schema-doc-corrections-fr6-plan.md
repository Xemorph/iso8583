# Plan: Doku-Korrektur — Format×Encoding-Matrix & BCD-Breitenregeln (nach FR-6 / v0.7.0)

> **Für den Implementierer (anderer Agent):** Dies ist ein **reiner Doku-Job**.
> Es wird **kein Quellcode** geändert (weder `parserTable()` noch
> `_codec_impl.hh` noch `fmt_types.hh`). Die Bibliothek ist korrekt; nur drei
> Doku-Stellen weichen von Quellcode/Dispatch-Tabelle ab. Alle Befunde unten
> wurden bereits gegen den Quellcode verifiziert (iso8583-agent, 2026-10-05,
> `main`) — du kannst sie als gegeben nehmen, der Verifizierungs-Schritt in
> §6 schützt trotzdem.

---

## 0. TL;DR

Drei Doku-Abweichungen in `docs/internals/spec_schema.md` §3 (Format×Encoding-
Matrix + Breiten-/Zählregeln) und ihren **Spiegeln**
(`docs/internals/yaml_format.md`, `include/iso8583/AGENTS.md`) nachziehen,
damit die Doku das seit v0.6.0/v0.7.0 **released** Verhalten exakt abbildet.
Zusätzlich: ein Satz BCD-Padding-Semantik (Antwort auf die Rückfrage aus dem
Chatroom).

- **Kein** Verhaltens-, API- oder ABI-Change.
- **Kein** Version-Bump.
- Changelog-Eintrag **optional** (siehe §7).

---

## 1. Herkunft

Chatroom `chatrooms/iso8583.md` (Vault `ai_connected`), Eintrag
**2026-10-05T16:54** von `tng-wire-viewer-agent` (Pin auf `v0.7.0` /
`b494d848`): drei Doku-Stellen, an denen `spec_schema.md` §3 von
Dispatch-Tabelle bzw. Quelltext abweicht + eine Rückfrage zur BCD-
Präfix-Padding-Nibble. Der Konsument folgt jeweils dem Quelltext und hat die
Abweichungen bei sich per Override abgefangen — Handlungsbedarf ist **nur die
Doku selbst** in diesem Repo.

**Quellen (Source of Truth, schon verifiziert):**

| Fakt | Ort |
|---|---|
| BCD-Präfixbreite `parsed_length<BCD> = (digits + 1) >> 1` → **L/LL = 1 Byte, LLL = 2, LLLL = 2** | `include/iso8583/detail/_codec_impl.hh:19` |
| BCD-Nutzdaten-Padding: Buffer wird pre-zeroed, ungenutztes Low-Nibble bleibt `0`; Decode mappt `0` → `'0'` | `_codec_impl.hh:210-216` (Encode) / `:152-161` (Decode) |
| Dispatch-Tabelle (alle gültigen `Format\|Encoding[\|Prefix]`-Combos) | `src/_spec.cc:1011-1135` (`parserTable()`) |
| Typ-Aliase `IFB_LNUM`/`IFB_LLNUM`/`IFE_LLNUM`/`IFA_LLLLBINARY`/… | `src/fmt_types.hh` |
| **Bereits korrekter** Referenztext (BCD: L/LL = 1 Byte, LLL/LLLL = 2 Byte) | `docs/plans/fr6-prefix-encoding-plan.md:113-114` |

---

## 2. Verifizierte Befunde

### Befund 1 — BCD-Präfixbreite (tng-Punkt 1)
Quellcode: `_codec_impl.hh:19` rechnet `(digits + 1) >> 1`, d. h.
**L = 1, LL = 1, LLL = 2, LLLL = 2 Byte**. Die Doku sagt „immer **1 Byte**
(zwei BCD-Ziffern, unabhängig von L/LL/…)" — **falsch** für LLL/LLLL.
Nachgemessen durch tng (eigene Spec, `visa_base1`): DE48 `lllchar|bcd`
→ Präfix `0010` (**2 Byte**); DE35 `llchar|bcd` → `21` (1 Byte).
→ **Doku korrigieren** (nicht Code). Die FR-6-Plan-Notiz
(`fr6-prefix-encoding-plan.md:113`) hat die Breite schon richtig beschrieben —
die §3-Tabelle wurde nie nachgezogen.

### Befund 2 — `lnum`/`llnum`-Matrixzeile (tng-Punkt 2)
`parserTable()` führt seit 0.7.0: `LNUM|BCD` (`_spec.cc:1096`),
`LLNUM|BCD` (`:1097`), `LNUM|EBCDIC` (`:1086`), `LLNUM|EBCDIC` (`:1098`).
Die Doku-§3-Matrix (Zeile `lnum`/`llnum`) zeigt `bcd ✘` und
`ebcdic = „lnum ✔ (kein llnum)"` — **unvollständig**: `lnum|bcd`,
`llnum|bcd` **und** `lnum`/`llnum|ebcdic` sind alle verfügbar.
`yaml_format.md` trägt `lnum`/`llnum` bei **BCD** gar nicht (BCD-Tabelle)
und bei **EBCDIC** nur `lnum`, nicht `llnum` (EBCDIC-Tabelle).

### Befund 3 — `llllbinary`-Matrixzeile (tng-Punkt 3)
`parserTable()` führt `LLLLBINARY|ASCII` (`_spec.cc:1068`, 0.6.0/FR-3 —
Zwilling für die `llllchar|ascii`-Container-Normalisierung) **und**
`LLLLBINARY|EBCDIC` (`:1083`). Die Doku-§3-Matrix zeigt für `llllbinary`
nur `ebcdic ✔`, `ascii ✘` — die `ascii`-Spalte fehlt.

### Rückfrage — BCD-Padding-Nibble (tng, keine Dringlichkeit)
Bei **ungerader** Ziffernzahl füllt der BCD-String-Pfad
(`IFB_LCHAR`/`LLCHAR`/`LLLCHAR`, `IFB_LNUM`/`LLNUM` — alle
`ISOOpaqueFieldParser<…, Encoder::BCD>`) das letzte, ungenutzte **Low-Nibble
mit `0`** (Buffer wird vor dem Füllen auf `0x00` gesetzt, `_codec_impl.hh:210-
211`), **nicht** mit `F`. Bei der Dekodierung wird dieses Nibble zurück nach
`'0'` gemappt (`:152-161`). Beispiel: der Wert `"123"` (3 Ziffern) wird zu
BCD `12 30`. → In §3 (BCD-Semantik) als ein Satz dokumentieren.

---

## 3. Exakte Edits (Vorher → Nachher)

> Alle drei Dateien sind **Spiegel** — nach den Edits müssen sie denselben
> Fakt tragen. `include/iso8583/AGENTS.md` wird über `docs/AGENTS.md`
> (`{include}`) automatisch in die Sphinx-Doku gezogen.

### A. `docs/internals/spec_schema.md`

**A1 — Matrixzeile `lnum`/`llnum` (Zeile 113):**
```diff
-| `lnum`/`llnum` | ✔ | ✘ | `lnum` ✔ (kein `llnum`) | ✘ |
+| `lnum` / `llnum` | ✔ | ✔ (0.7.0: `IFB_LNUM`/`IFB_LLNUM`) | ✔ (`IFE_LNUM`/`IFE_LLNUM`) | ✘ |
```

**A2 — Matrixzeile `llllbinary` (Zeile 116):**
```diff
-| `llllbinary` | ✘ | ✘ | ✔ | ✘ |
+| `llllbinary` | ✔ (0.6.0, FR-3: `IFA_LLLLBINARY`) | ✘ | ✔ (0.6.0: `IFE_LLLLBINARY`) | ✘ |
```
(Hinweis: die Daten bleiben roh/`binary`-kodiert; die `ascii`-Spalte ist der
`llllchar|ascii`-Container-Normalisierungs-Zwilling — siehe „Wichtige
Nuancen" direkt unter der Tabelle.)

**A3 — Breiten-/Zählregeln, Zeile `bcd` (Zeile 183):**
```diff
-  | `bcd` | immer **1 Byte** (zwei BCD-Ziffern, unabhängig von L/LL/…) | *Ziffern* (1 Byte = 2 Ziffern); BCD-Zeichen sind Dezimalziffern: `0x16` = sechzehn, `0x0A` = zehn |
+  | `bcd` | **1 Byte für L/LL, 2 Byte für LLL/LLLL** (je 2 BCD-Ziffern/Byte, aufgerundet: `parsed_length = (L + 1) >> 1`) | *Ziffern* (1 Byte = 2 Ziffern); BCD-Zeichen sind Dezimalziffern: `0x16` = sechzehn, `0x0A` = zehn |
```
(▶ **Wichtig:** vor dem Edit die *tatsächliche* Vorher-Zeile 1:1 abgleichen —
die hex-Beispiele (`0x16`/`0x10` oder `0x0A`) können leicht abweichen; nur die
**Breiten-Spalte** (2. Spalte) ändern, die Zählung-Spalte (3.) unverändert
lassen.)

**A4 — BCD-Padding-Semantik (Liste „Wichtige Nuancen", Eintrag „BCD-Semantik",
ca. Zeile 156-160):** einen Satz ergänzen, z. B. an das Ende des bestehenden
`- **BCD-Semantik:**`-Absatzes:
```
  Bei **ungerader** Ziffernzahl wird das letzte, ungenutzte Low-Nibble des
  abschließenden Bytes mit **`0`** gefüllt (nicht `F`): der Wert `123` wird
  zu BCD `12 30`; beim Decode mappt dieses Nibble wieder auf `0`.
```

### B. `docs/internals/yaml_format.md`

**B1 — BCD-Tabelle (nach Zeile 186, d. h. nach der `lchar`-Zeile):** neue
Zeile einfügen:
```
| `lnum` / `llnum` (0.7.0) | `IFB_LNUM` / `IFB_LLNUM` | BCD-Längenpräfix + gepackte BCD-Ziffern (VISA-BASE-I-DE2-artig; Identitätslücke `lnum|bcd`/`llnum|bcd`) |
```

**B2 — EBCDIC-Tabelle, Zeile 194:**
```diff
-| `numeric` / `lnum` | `IFE_NUMERIC` / `IFE_LNUM` | EBCDIC-Ziffern |
+| `numeric` / `lnum` / `llnum` (0.7.0) | `IFE_NUMERIC` / `IFE_LNUM` / `IFE_LLNUM` | EBCDIC-Ziffern |
```

### C. `include/iso8583/AGENTS.md` (öffentliche API, Spiegel)

**C1 — `prefix_encoding`-Bullet, Breitenregeln (Zeilen 579-581):**
```diff
-  LLL=3, LLLL=4, Big-Endian); BCD-Präfix = 1 Byte = zwei Dezimalziffern
-  (`0x16` = sechzehn).
+  LLL=3, LLLL=4, Big-Endian); BCD-Präfix = 1 Byte für L/LL, 2 Byte für
+  LLL/LLLL (je 2 BCD-Ziffern/Byte, aufgerundet; `0x16` = sechzehn).
```

> **Vorher-Strings:** Alle Diff-Zeilen „Vorher" wurden am Stand `main`
> (2026-10-05) entnommen. Falls sich eine Zeile seitdem minimal verschoben
> hat, per `grep` nach dem eindeutigen Kernbruchstück orientieren (
> „immer **1 Byte**", „kein `llnum`", „BCD-Präfix = 1 Byte",
> „`numeric` / `lnum`" + `IFE_LNUM`), nicht blind nach Zeilennummer.

---

## 4. Spiegel-Konsistenz (Pflicht)

Nach den Edits müssen **drei** Dateien denselben Fakt tragen:
1. `docs/internals/spec_schema.md` §3 (normativ)
2. `docs/internals/yaml_format.md` (BCD/EBCDIC-Tabellen)
3. `include/iso8583/AGENTS.md` (Breitenregel im `prefix_encoding`-Bullet)

`(docs/AGENTS.md` inkludiert 3. und braucht daher keinen eigenen Edit.)

---

## 5. Was ausdrücklich NICHT geändert wird

- **Kein Quellcode**: `src/_spec.cc` (`parserTable()`),
  `include/iso8583/detail/_codec_impl.hh`, `src/fmt_types.hh`.
- **Kein** Format-/Encoding-/API-/ABI-Change, **kein** Version-Bump.
- **Kein** neuer obligatorischer Unit-Test (reine Doku-Korrektur).

---

## 6. Verifikation (Akzeptanz-Gate)

1. **Grep-Gate** — keine der Abweichungen darf in den drei Doku-Dateien mehr
   vorkommen:
   ```bash
   # muss leer bleiben:
   grep -rn "immer \*\*1 Byte\*\*\|kein \`llnum\`\|BCD-Präfix = 1 Byte" \
        docs/internals/spec_schema.md docs/internals/yaml_format.md include/iso8583/AGENTS.md
   # llllbinary ascii-Zelle muss jetzt ✔ zeigen:
   grep -n "llllbinary" docs/internals/spec_schema.md
   # lnum/llnum BCD/EBCDIC-Abdeckung in yaml_format.md:
   grep -n "IFB_LNUM\|IFE_LLNUM" docs/internals/yaml_format.md
   ```
2. **Sphinx-Doku-Build mit `-W`** (CI erzwingt `-W`; ein sauberer Build ohne
   Warnung/Fehler ist das eigentliche Gate). Aufbau wie in
   `.agents/build-test-ci.md` §9 (Doku-Target). Erwartet: „build succeeded"
   bzw. kein `sphinx -W`-Abbruch. (Die Edit-Auswirkungen sind reine
   Markdown-/RST-Tabelle — kein Code, daher reicht der Doku-Build; ein C++-
   Rebuild ist nicht nötig.)
3. **Optional (nur wenn die BCD-Padding-Aussage testfest gemacht werden
   soll):** ein kleiner Roundtrip-Assert für `lllchar|bcd` (2-Byte-Präfix)
   und einen ungeraden BCD-Wert (`"123"` → `12 30`) in die bestehende
   `[spec]`-/`[e2e]`-Suite. **Nicht vorgeschrieben** für die Doku-Korrektur.
4. **Optional (Rückmeldung an den Konsumenten):** Eintrag im Chatroom
   `chatrooms/iso8583.md` (Vault `ai_connected`, als `##`-Sektion,
   `iso8583-agent`), der die drei Punkte bestätigt + die Padding-Antwort
   (`0`) mitgibt. (Nur, wenn der Konsument darauf wartet — sonst weglassen.)

---

## 7. Changelog & Version

- **Default: kein Changelog-Eintrag** — rein redaktionelle Nachjustierung,
  die bereits releasedes v0.6.0/v0.7.0-Verhalten korrekt abbildet (keine
  Verhaltensänderung gegenüber einem Consumer).
- Falls der Maintainer es trotzdem möchte: **kurzer** `[~](Fixed)`-Eintrag
  („Doku: spec_schema.md §3 / yaml_format.md / AGENTS.md auf FR-6-Verhalten
  nachgezogen (BCD-Präfixbreite, `lnum`/`llnum`, `llllbinary|ascii`,
  BCD-Padding)"), der in **beiden** Changelogs **identisch** steht
  (`changelog.md` und `docs/changelog.md` — AGENTS.md: beide müssen decken-
  gleich sein). Unter „Unreleased"/einem Nachtrags-Block, **nicht** unter der
  bereits getaggten 0.7.0-Sektion. **Kein** Version-Bump.

---

## 8. Commit

Ein logischer, doku-only-Commit, der **alle drei Spiegel-Dateien zusammen**
fasst (damit die Spiegel in genau einer Änderung konsistent bleiben):

```
[~](Updated) Doku: §3-Matrix/Breitenregeln + Spiegel (BCD-Präfixbreite, lnum/llnum, llllbinary|ascii, BCD-Padding) nach FR-6

Co-Authored-By: Claude Code <noreply@anthropic.com>
```

- Commit-Nachricht: ASCII-Präfix-Schema des Repos (s. AGENTS.md „Process").
- **Nicht** committen: `docs/plans/`-Dateien außer dieser, wenn nicht
  gewollt; definitiv die untracked Scratch-Dateien
  (`.zvec-grep/`, `claude.txt`, `*.stackdump`, `handoff-*.md`,
  `init_locale.bat`) **nicht** einbeziehen.
- Vor dem Commit CRLF prüfen (Repo ist CRLF; `file`/`git diff --stat` —
  s. Memory „iso8583 CRLF pitfall": Edit-Tool/Python nutzen, kein
  `sed -i` in Git-Bash).
