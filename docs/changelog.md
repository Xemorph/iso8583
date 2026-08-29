# Changelog

## 0.2.1

### Bugfix: `!merge`-Tag bei Sequenz-Definitionen ging beim Vorverarbeiten verloren

Felder, die über `!use` auf eine `!merge`-Definition referenzieren, deren Wert
eine **Sequenz** ist (z. B. `bmp_35: !merge` → Liste aus `!template`-/
`scalar`-Einträgen), wurden nicht mehr expandiert. Die Definitions-Extraktion
verwendet eine gleichbaum-interne `merge_with()` des rapidyaml-Parsers
(v0.15.2), die anschließend das `!merge`-Val-Tag des Zielknotens leerstellt —
obwohl `has_val_tag()` weiter `true` meldet. Map-basierte Definitionen waren
nicht betroffen, deshalb fiel der Fehler nur bei sequenziellen `!merge`-
Definitionen auf. Die ungeexpandierte Sequenz erreichte den SpecDecoder,
deren erstes Element ohne `format` (leer) blieb: fünf `<dummy>`-WARNs gefolgt
von `ERR Unbekannte Format/Encoding-Kombination`.

Behoben, indem das Val-Tag nach der Extraktion aus dem (weiterhin lesbaren)
Quellknoten wiederhergestellt wird, bevor der Self-Merge es leeren kann.
Ergänzend ein Regressionstest, der das reale Muster (`!merge`-Sequenz-
Definition in einer `!include_files`-Datei, per `!use` referenziert)
abdeckt. Keine Änderung an bestehenden Specs nötig.

## 0.2.0

### Neu: hexadezimale Tag-Notation für BER-TLV `children`

Bei `tlv: {ber: true}` werden `children`-Schlüssel jetzt standardmäßig als
**hexadezimal** interpretiert (`"9F26"`, `"5A"`, `"1A"`), passend zur in
EMV Book 3 / ISO 7816 üblichen Schreibweise. Fix-Format-TLV (Mastercard/
Visa, `tag_bytes`/`len_bytes` gegeben) bleibt unverändert **dezimal**
(SE-Nummern, z.B. `"26"`) — keine Breaking Change für bestehende Specs.
Ein explizites `"0x"`-Präfix (`"0x1A"`) erzwingt hexadezimal unabhängig vom
TLV-Modus. Ungültige Schlüssel werfen jetzt eine klare, positionsgenaue
Fehlermeldung statt einer rohen `std::stoi`-Exception.

### Neu: `children`-Beschreibungen tatsächlich wirksam

Bisher wurden `format`/`description`/`encoding`-Angaben in einem TLV-
`children`-Block vollständig ignoriert — jedes SE/Tag wurde beim Dekodieren
immer als `BinaryField` mit generischer Beschreibung `"SE<n>"` gespeichert,
unabhängig davon, was in der Spec deklariert war. Ab jetzt wird zumindest
`description` korrekt übernommen. `format`/Typisierung pro Tag bleiben
bewusst zurückgestellt (größerer Eingriff, separate Entscheidung) — jedes
SE/Tag wird weiterhin als `BinaryField` dekodiert.

### Bugfix: dangelnde `string_view` bei TLV-Beschreibungen

Beim Umsetzen der description-Propagierung wurde ein **vorbestehender**
Speicherfehler gefunden und behoben: `ISOComponent::description()` speichert
nur eine `nonstd::string_view` (keine eigene Kopie) — der bisherige
generische `"SE" + se_num`-Fallback übergab dafür ein temporäres
`std::string`, das sofort nach dem Aufruf zerstört wurde. In der Praxis
meist unauffällig (kurze Strings landen typischerweise in der Small-String-
Optimization und werden nicht sofort überschrieben), aber echtes
Undefined Behavior — mit AddressSanitizer verifiziert behoben. `ISOTLVParser`
hält jetzt selbst langlebigen Speicher für sowohl deklarierte als auch
generierte Fallback-Beschreibungen vor.

### Unter 0.2.0: yaml-cpp → rapidyaml-Migration

Der komplette YAML-Spec-Ladepfad (`SpecDecoder::loadFromYaml` und verwandte
Funktionen) wurde von [yaml-cpp](https://github.com/jbeder/yaml-cpp) auf
[rapidyaml](https://github.com/biojppm/rapidyaml) (`ryml`) umgestellt.
Gründe: 100%ige Abdeckung des offiziellen YAML-Testsuites, keine bekannten
Schwachstellen, spürbar bessere Ladeperformance (siehe unten).

### ⚠️ Breaking Change: `!include_files` benötigt jetzt einen `---`-Trenner

yaml-cpp tolerierte stillschweigend mehrere YAML-Dokumente in einer Datei
**ohne** `---`-Trenner dazwischen (z.B. eine `!include_files`-Sequenz direkt
gefolgt vom eigentlichen Spec-Inhalt). Das ist nach YAML 1.2 spezifikations-
widrig, und rapidyaml (spezifikationskonform) akzeptiert es zu Recht nicht
mehr.

**Betrifft dich, falls** eine deiner Spec-Dateien `!include_files` am
Dateianfang nutzt. Migration: `---` zwischen die `!include_files`-Sequenz und
den restlichen Inhalt einfügen.

Vorher (funktionierte nur mit yaml-cpp):
```yaml
!include_files
- common_definitions.yml
spec: "My Spec"
encoding: ebcdic
fields:
  "000": !use mti_field
```

Nachher (spezifikationskonform, funktioniert mit beiden Bibliotheken):
```yaml
!include_files
- common_definitions.yml
---
spec: "My Spec"
encoding: ebcdic
fields:
  "000": !use mti_field
```

Ein einmaliger `grep -rl '^!include_files$' *.yml` über den eigenen
Spec-Bestand findet alle betroffenen Dateien.

### Neu: globale ryml-Fehlerbehandlung

ryml ruft bei Parse-/Validierungsfehlern standardmäßig `std::abort()` auf,
nicht etwa eine C++-Exception. Damit `SpecDecoder::loadFromYaml()` weiterhin
zuverlässig einen fangbaren `std::runtime_error` wirft (wie dokumentiert und
vom Rest der Bibliothek erwartet), installiert `libiso8583` beim ersten
Preprocessing-Aufruf **einmalig, für den gesamten Prozess** eigene
ryml-Fehler-Callbacks (`ryml::set_callbacks`).

**Betrifft dich, falls** deine Anwendung *zusätzlich, unabhängig von
libiso8583* direkt mit rapidyaml arbeitet und dabei eigene
Fehler-Callbacks setzt: `libiso8583` überschreibt diese beim ersten eigenen
Ladevorgang. Für die meisten Fälle unkritisch (Exceptions statt `abort()`
sind für eingebettete Bibliotheken i.d.R. ohnehin die gewünschte Wahl), aber
gut zu wissen, falls es zu unerwartetem Verhalten in eurer eigenen
ryml-Nutzung kommt.

### Neu: Rekursionstiefenschutz + Erkennung zirkulärer `!use`-Referenzen

Vorher konnte eine extrem tief verschachtelte oder versehentlich zirkuläre
(`!use`-Kette, die auf sich selbst zurückverweist) Spec-Datei zu einem
Stack Overflow (hartem Absturz) statt einer sauberen Fehlermeldung führen.
Beides wirft jetzt einen `std::runtime_error` mit klarer Beschreibung.

### Performance

Gemessen an einer ~65-Felder-Spec (Release-Build, gleiche Methodik vorher/nachher):

| Modus | vorher (yaml-cpp) | nachher (ryml) |
|---|---|---|
| `loadFromYaml` (mit Positions-Tracking) | ~1,28 ms | ~750–800 µs (**~1,6–1,7×**) |
| `loadFromYaml` (ohne Positions-Tracking) | ~1,34 ms | ~580–680 µs (**~2×**) |
| `loadFromYamlCached`-Treffer (Standard) | ~1,45 µs | ~1,0–1,2 µs (**~1,3×**) |
| `loadFromYamlCached`-Treffer (`TrustUntilInvalidated`) | ~0,35 µs | ~0,25–0,27 µs (**~1,3×**) |

### SourceMap: Zeilennummern → Knoten-Identität

Fehlerpositionen (z.B. "welche Datei/Zeile enthält das ungültige `length:
-5`?") werden jetzt über eine tree-interne Knoten-Identität statt über
Zeilennummern im prozessierten Dokument nachverfolgt. Das ist präziser als
vorher — insbesondere bei Fehlern innerhalb einer über `!use` aus einer
*anderen* Datei referenzierten Definition, wo die alte, zeilennummer-basierte
Zuordnung ungenau werden konnte.

**Betrifft dich, falls** du `.smap`-Sidecar-Dateien einer *älteren*
`libiso8583`-Version im Dateisystem liegen hast: Diese werden automatisch als
veraltet erkannt (über ein `format_version`-Feld) und beim nächsten Laden
transparent neu erzeugt — keine manuelle Aktion nötig, aber der erste
Ladevorgang nach dem Upgrade ist für betroffene Dateien einmalig etwas
langsamer (baut die Sidecar neu auf).

### Sonstiges

- `yaml-cpp` ist keine Abhängigkeit mehr (weder Build noch vcpkg-Manifest).
- Neue Abhängigkeit: `ryml` (rapidyaml), Version ≥ 0.15.2.
- `SpecPreProcessor::preprocessFile()` (unbenutzte, nicht-öffentliche
  Altlast) wurde entfernt.
