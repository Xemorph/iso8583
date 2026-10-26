# Plan: Bitmap-Container mit Bitmap aus einem frueheren Feld (`bitmap: { from }`)

Ziel: ein Bitmap-Container (`type: nested` + `bitmap:`) liest seine Bitmap nicht aus dem eigenen Anfang, sondern aus einem vorher dekodierten `format: binary`-Feld ausserhalb des Containers. Opt-in ueber `extensions: [bitmap_from]` am Spec-Wurzelschluessel, sonst Ladefehler. Gilt fuer `fields:` und `field:`.
Quelle: GitHub-Issue #1 (tng wire-Viewer, 2026-10-26, gegen 0.11.0). FR-Nummer vergibt der Maintainer; Version nach FR-17 (`fr17-hex-int-plan.md`), also 0.12.0 (additiv; FR-17 wird dann 0.13.0).

## Kernproblem

Ein Container sieht nur seine eigenen Bytes. `ISOFieldParser<NESTED>::unparse(c, b, o)` (`src/_parser.hh:563`) reicht dem Kind-Parser nur `payload`; `unparseBitmapContainer` (`src/_parser.cc:540`) liest die Bitmap aus `b[0..n)`. Ein Eltern-Zeiger an `ISOMessage` existiert nicht (Grep in `include/iso8583`: kein `parent`/`weak_ptr`). Die Bitmap-Quelle liegt in der Wurzel-Nachricht.

Entscheidung: **Decode-Kontext statt Signaturaenderung.** Die virtuelle `unparse(c, b, o)` steht in `detail/_interfaces.hh` (Vtable/ABI); eine Aenderung waere ein Bruch fuer alle Feld-Parser. Stattdessen setzt der Wurzel-`ISOBaseParser::unparse` einen `thread_local`-Zeiger auf die Wurzel-Nachricht (RAII, stellt beim Verlassen und bei Exceptions den alten Wert wieder her, reentrant). `unparseBitmapContainer` loest die Quelle per Dot-Pfad dort auf. Alle Parser bleiben unveraenderlich und teilbar (Hard Rule 10). Verworfen: Member an `ISOMessage` (Layout-/ABI-Aenderung), neuer Parameter (Vtable).

Bauen braucht den Vorlauf der Issue: Die Quelle (`4.1`) steht im Strom vor dem Container (`5`). `ISOBaseParser::parse` serialisiert in Strom-Reihenfolge, die Bitmap muss also vorher aus den gesetzten Kindern berechnet und per Dot-Pfad in die Nachricht geschrieben werden (`set_recursive_locked` loest beliebig tief auf).

## Meilensteine (je ein Commit)

- **M1 Tests (rot):** `tests/test_bitmap_from.cc` (in `tests/CMakeLists.txt` vor `test_e2e_full_message.cc` registrieren, ASCII-Namen). Fixture: Beispielnachricht `0100 04 1111 A000 0003414243 123456781234` mit `A000`, `4000`, `2000`, `0100`; dieselbe Spec als Field-only; Roundtrip bytegleich; Ladefehler ohne `extensions` (Meldung aus dem Issue); unbekannter Eintrag in `extensions`; gesetztes Quellfeld beim Bauen; gesetztes Bit ohne Kind (strict: positionierter Fehler, sonst Warnung); abgeschnittenes Kind; Restbytes im Container; alle Ladeverbote aus M2.
- **M2 Spec laden (`src/_spec.cc`):**
  - `extensions` als Wurzelschluessel in beiden Ladepfaden (`fields:` ab ~540, `field:` ab ~336); Liste von Strings, nur `bitmap_from` bekannt, alles andere `SpecValidationError` (fail-closed, positioniert). Pruefen, ob unbekannte Wurzelschluessel heute schon abgelehnt werden.
  - Im Zweig `isBitmapContainer` (~1064): `from` neben `length` erlauben (Fehlertext der Schluesselliste anpassen); ohne Schalter Fehler mit dem Wortlaut aus dem Issue. Der Schalter muss dafuer in `parseSpecField` ankommen (Parameter oder bestehender Kontext, beim Umsetzen lesen).
  - Nach dem Parsen der gesamten Spec Querpruefung (die Quelle steht ggf. in einem spaeter geparsten Feld): Pfad existiert; Quelle steht **strikt vor dem Container im Strom** (frueherer Top-Level-Schluessel, oder gleicher Elterncontainer mit kleinerem Index); `format: binary`, feste `length` == `bitmap.length`; kein Selbstbezug (deckt Zyklen ab, da nur rueckwaerts referenziert wird); hoechstens ein Container je Quelle; nicht mit `tlv`/`bertlv`/`pack` (bestehende Pruefung gilt schon).
  - Pro Wurzel-Parser Liste `(Container-Pfad, Quell-Pfad, Bytes)` ablegen, damit Decode und Bau nicht erneut suchen muessen.
- **M3 Decode (`src/_parser.cc`):** RAII-Kontext in `ISOBaseParser::unparse` (Wurzel, nicht Container-Rekursion); in `unparseBitmapContainer` bei `from`: Quelle ueber den Pfad holen (fehlt sie, ist die Spec/Reihenfolge verletzt: Fehler), Hex -> Bytes, Laenge gegen `bitmap_container_` pruefen, **keine** Bytes aus `b` verbrauchen, `Bitmap`-Komponente nicht noch einmal unter `BITMAP_KEY` ablegen (AK 4), restliche Schleife unveraendert (`consumed` startet bei 0). Field-only: Wurzel ist der `field:`-Container, Pfad relativ dazu (offene Frage 3).
- **M4 Bauen:** in `ISOBaseParser::parse` (Wurzel) vor Schritt 4: fuer jeden registrierten from-Container die gesetzten Kinder -> Bitmap-Bytes (gleiche Bit-Logik wie `parseBitmapContainer`), Fehler wenn die Quelle gesetzt ist, sonst Quelle per Dot-Pfad als Hex setzen. `parseBitmapContainer` gibt im from-Modus nur die Kinder aus (kein Bitmap-Praefix). Container-Laenge bleibt stabil, weil die Quelle feste Laenge hat. Bitmap-Kinder ohne Deklaration: wie heute strict-Fehler. Gemeinsamen Helfer fuer die Bitmap-Berechnung aus `parseBitmapContainer` herausziehen statt zu kopieren.
- **M5 Introspektion:** `SpecFieldInfo` (Public, `describe`) bekommt `bitmap_from` (leer = aus dem Strom); Feldanzahl-/Layout-Aenderung an einem exportierten Typ -> bewusst als additive Minor-Aenderung behandeln und dokumentieren (wie `tlv_is_ber`, FR-4). `describe`-Ausgabe zeigt `bitmap.from` und Laenge.
- **M6 Doku + Version:** `spec_schema.md` (neuer Abschnitt `extensions`, `bitmap.from` mit Ladeverboten, Format-Hinweis nicht ISO-8583), `yaml_format.md`, Changelog (nur Root), `.agents/yaml-spec.md` + `.agents/pitfalls.md` (thread_local-Kontext, Bauen-Vorlauf), `include/iso8583/AGENTS.md`; Version an 4 Stellen; Plan in `docs/index.rst`; Vault-Notiz; `sphinx -W`. Release bleibt maintainer-initiiert nach gruenem Docs-Run auf `main`.

## Verifikation

`cmake --preset debug && cmake --build --preset debug && ctest --preset debug` (Developer Prompt/vcvars), Test-Exe direkt (ctest-Umlaut-Anomalie), zusaetzlich Build mit `ISO8583_BERTLV`; `[spec]`/`[e2e]` gruen; alle Bestandsspecs unveraendert (ohne Schalter kein Verhaltensunterschied); `sphinx -W` nach Loeschen des alten `docs/_build`.

## Pushback / bewusst nicht gebaut

- Kein allgemeines Plugin-/Extension-System: `extensions` kennt genau einen Wert. Weitere Eintraege kommen erst mit einem Bedarf.
- Keine bedingten Felder (`present_if`), siehe Alternativen im Issue.
- `from` zeigt nur rueckwaerts; kein Vorwaerts-Lookup und keine Mehrfach-Quellen.
- Sub-Nachricht allein re-serialisieren (FE-1: `de55->parse(de55)`) kann die externe Quelle nicht berechnen: im from-Modus klarer Fehler statt falscher Bytes.

## Offene Fragen an den Maintainer

1. Schaltername: `extensions: [bitmap_from]` wie im Issue vorgeschlagen (Empfehlung: so lassen).
2. Skalar-`from` (z. B. `from: "63"`): die Regel "binary, feste Laenge" erlaubt es ohne Mehraufwand; Empfehlung: erlauben.
3. Field-only: Pfad relativ zum `field:`-Wurzel-Container. Das Issue verlangt Field-only-Support, nennt aber kein Beispiel. Soll das Quellfeld dort ein Geschwister im selben Container sein?

## Risiken

- Obiger Code ist teilweise nur gelesen, nicht gebaut: Ladeweg in `parseSpecField` (Schalter-Durchreichung), Wurzelschluessel-Pruefung und `set_recursive_locked` fuer Quellpfade in Containern sind ungeprueft (M1/M2 zeigen es).
- `thread_local` ist neuer globaler Zustand (`concepts-encoding.md` §13 mitpflegen); bei Reentrancy (Container in Container) muss der Kontext die aeusserste Wurzel behalten.
- Die Quelle wird per Dot-Pfad aus einem bereits mit `m->set(de)` abgelegten Feld gelesen; bei `sensitive: true` an der Quelle bleibt der Wert unmaskiert abrufbar (Hard Rule 9), nur `dump()` maskiert.

## Umsetzungsstand (umgesetzt, Version 0.12.0)

Alle Meilensteine M1-M6 auf `main`; Debug- und `debug-bertlv`-Build gruen (`[bitmap-from]` 10 Faelle, Gesamtsuite gruen), `sphinx -W` gruen. Entscheidungen der offenen Fragen: Schalter `extensions: [bitmap_from]`, Skalar-`from` erlaubt, Field-only-Pfad relativ zum `field:`-Container.

Abweichungen vom Entwurf:

- **Bauen:** ein bereits gesetztes Quellfeld mit **gleichem** Wert ist erlaubt (dekodierte Nachricht erneut serialisieren), ein abweichender Wert ist strict ein Fehler. Ein Container ohne Kinder schreibt Nullen in die Quelle.
- **Pufferende:** die Dekodierschleife bricht bei `consumed == b.size()` ab; from-Container werden dort trotzdem besucht und gelten als leer (Marker-Bit ohne Daten gueltig, Bit mit Daten Fehler). Ist DE5 laut Top-Level-Bitmap nicht vorhanden, die Quelle aber ungleich null, ist das ein Fehler.
- `remaining` mit null Restbytes bleibt unveraendert (strict Fehler "abgeschnitten"); der Marker-Fall laeuft ueber die Pufferende-Sonderbehandlung, nicht ueber `remaining`.
- Registry statt dynamischem Baum-Durchlauf: `bitmap_from_links_` am Wurzel-Parser, damit auch fehlende Container eine Quelle bekommen.
