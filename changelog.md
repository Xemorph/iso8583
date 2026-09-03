# Changelog

## Unreleased

### Fixed

- **Windows-CI: Sandbox/Sidecar-Fehlpositiv "außerhalb der erlaubten Wurzel"**
  (12 Test-Failures in den Windows-Jobs): `isWithinRoot()` verglich den
  Kandidatenpfad (`fs::absolute`, nicht kanonisiert) rein lexikalisch mit der
  kanonisierten Sandbox-Wurzel. Windows-8.3-Kurznamen (GitHub-Windows-Runner
  setzen `TEMP=C:\Users\RUNNER~1\AppData\Local\Temp`; `fs::canonical` liefert
  `C:\Users\runneradmin\...`) und Symlinks/Junctions (macOS `/tmp` →
  `/private/tmp`) sind lexikalisch verschieden, aber physikalisch identisch —
  in-Wurzel-`!include_files` und `.smap`-Schreibzugriffe wurden fälschlich
  verworfen. Fix: nach negativem lexikalischem Vergleich Fallback auf
  `fs::weakly_canonical` des Kandidaten (Fail-closed bleibt erhalten: wirklich
  außerhalb liegende Pfade kanonisieren auf außerhalb liegende Formen).
- **Test-Härtung**: Die `TempDir`/`TempYaml`-Helfer benennen ihre
  Temp-Verzeichnisse/Dateien jetzt nach PID+Zähler statt Thread-ID-Hash. Unter
  `ctest -j` kollidierten mehrere Test-Prozesse prozessübergreifend über
  gleiche Haupt-Thread-IDs auf denselben Verzeichnisnamen (Rest-Dateien,
  `exists()`-Rennbedingungen zwischen parallelen Tests).
- **TOCTOU-Test (Windows-Flake)**: Der alte Diskriminator (exists()-Check im
  Fehlermoment) hatte selbst ein Mikrosekunden-TOCTOU-Fenster (Datei fehlt
  beim `open`-Fehler, ist beim `exists()`-Check schon wieder da) → seltene
  Fehl-Failures. Jetzt zählen nur IO-ebene-Ladefehler des Loaders
  (`Datei nicht lesbar`) als transient; echte Load-/Cache-Fehler (Sandbox,
  Parsing, Validierung) werden unverändert sofort weitergeworfen. Anhaltende
  IO-Fehler deckt die Race-Grenze ab.

## 0.3.0

> **Wichtig:** 0.3.0 ist ein Sicherheits-/Robustheits-Release für den
> produktiven Einsatz im Finanzumfeld (PCI). Mehrere Verhaltensänderungen sind
> bewusst **breaking** (0.x); Details unten. Die Public-API ist ansonsten
> stabil (nur additive Zugänge) — aber `ISOBaseParser` bekommt ein neues
> Mitglied (`strict_`), daher müssen **Shared-Library-Konsumenten neu
> kompiliert** werden (siehe ABI-Hinweis unten).

### Neu: Strict-Modus als Standard (fail-closed)

Die Bibliothek verhält sich jetzt im Default **strict**: statt abge-
schnittene Frames still zu clampen, überdimensionale Felder zu verwerfen
oder ungültige EBCDIC-Bytes auf `.` zu mappen, wirft der Dekodier-/Encoder-Pfad
einen **positionsgenauen** `std::runtime_error` mit dem `[ISO8583]`-Präfix
(Feld, Offset, Byte, Hexdump).

- Neues Spec-Rot-Attribut `strict: true|false` (Default **`true`**) und
  Laufzeit-`ISOBaseParser::strict(bool)` / `strict() const`.
- Betroffene Fälle: Pufferende-Trunkatur (Feld am Ende abgeschnitten),
  Serialisierung größer als Feld-Maximum (Frame würde fehlerhaft),
  Längenpräfix am Pufferende abgeschnitten, ungültiges EBCDIC-Byte
  (tabelle- und Orakelpfad), Bitmap-Byte am Pufferende, überlanger
  Wire-Header (WLP-FO 93 B / BASE1), TLV-`offset+N`-Prechecks.
- **Escape-Hatch für tolerante Integratoren:** `strict: false` in der Spec
  oder `parser.strict(false)` zur Laufzeit → altes (clamp/WARN/`.`-Sentinel)
  Verhalten, aber nie mehr *still* (jeder Fall loggt mindestens WARN/ERROR).

### ⚠️ Breaking: WLP-FO-Header wird jetzt vollständig (93 Byte) serialisiert

`WLP_FOHeader::pack()` erzeugte bisher nur 89 Byte und ließ das 4-Byte-ASCII-
Längenpräfix sowie Bytes 4..93 weg — ausgehende WLP-Frames waren dadurch
korrupt (Längenpräfix verloren, Rest verschoben). `pack()` liefert jetzt den
**vollen 93-Byte-Header**, und `parse()` prüft das **gepackte** Ergebnis
(früher: nur das gespeicherte Header-Objekt). Ein zu kurzer Wire-Header wirft
fail-closed statt zu OOB-Zugriff.

**Betrifft dich, falls** du WLP-FO-Nachrichten (Worldline) *erzeugst*: Die
ausgehenden Frame-Bytes ändern sich (Korrektur). Bei reinem Empfang
(`unparse`) ändert sich nichts.

### Neu: EBCDIC-Konvertierung tabellen-getrieben + ICU-78.3-Orakel-Pin

Die EBCDIC↔ASCII-Konvertierung ist jetzt **vollständig tabellen-getrieben**
(IBM-1047) und läuft **ohne** Laufzeit-Converter (kein libiconv, kein ICU zur
Laufzeit):

- `kEbcdicToAscii` / `kAsciiToEbcdic` / `kEbcdicValid` in
  `include/iso8583/_codec.hh` werden aus einem exakt gepinnten **ICU-78.3**-
  Orakel erzeugt (`tools/generate_ebcdic_tables/`, Build-/CI-only — ICU wird
  nie in die Laufzeit-Targets verlinkt).
- **Determinismus:** Die 256-Byte-Verdicts beider Richtungen sind gecheckt
  und über eine Cross-Toolchain-Diff in der CI abgeglichen (MSVC/clang/GCC
  liefern byte-identische Tabellen). Die historische libiconv-Debug-/
  Release-Divergenz ist damit eliminiert.
- **Strict-Whitelist** ist bewusst *stärker* als ICU: ICU 78.3 konvertiert
  alle 256 Bytes (C1-Steuerzeichen, Binär-Bytes), der Strict-Modus akzeptiert
  nur die 85 IBM-1047-Druck-/Ziffern-Bytes (E2A) bzw. die 84 mappablen
  ASCII-Zeichen (A2E). `tests/test_encoding_determinism.cc` pinnt beide
  Zählungen.
- `libiconv` / `ISO8583_ENABLE_ICONV` sind **deprigiert (Removal in 0.4)**
  und dienen nur noch als Übergangs-Fallback; der Standard-EBCDIC-Pfad nutzt
  sie nicht. Das Configure warnt bei `ISO8583_ENABLE_ICONV=ON`.

### Neu: Spec-Sandbox + Lade-Limits

Specs können Third-Party-/Remote-Herkunft haben; ein bösartiger oder
korrupter Spec darf den Host nicht kompromittieren. Der Lade-Pfad ist deshalb
**fail-closed** und beschränkt:

- **Include-Sandbox (Default an):** `!include_files`-Einträge, die außerhalb
der `roots` (leer → Verzeichnis der Top-Level-Spec) landen —
`../`-Traversals, absolute/UNC-Pfade, Symlink-Escapes — werden **abgelehnt**
(lexikalisch und, falls die Datei existiert, über den kanonisierten,
Symlink-auflösenden Pfad).
- **`SpecLoadOptions`** (neues public API, `ISOSpec.hh`) steuert:
  `sandbox`, `roots`, `allowSmapWrite` (Sidecar nur innerhalb `roots`),
  `maxSpecBytes` (32 MiB/Datei, beim Streamen erzwungen), `maxIncludeFiles`
  (1024), `maxSmapBytes` (16 MiB; übergroße Sidecars werden verworfen).
- **Leere `fields:`** → sauberer, positionsgenauer Fehler (statt der früheren
  `rbegin()==rend()`-UB); nicht-numerische DE-Keys und `std::stoi`-Entwürfe
  werden sauber validiert.
- Die alten `loadFromYaml(path, bool trackSourceMap)`-Overloads bleiben
  source-kompatibel (bauen interne Default-Optionen).

**Migration (Betrifft dich, falls):** Top-Level-Specs, die `!include_files`
außerhalb des eigenen Verzeichnisses nutzen, brauchen jetzt explizite `roots`
(`SpecLoadOptions::roots`) oder bewusst `sandbox=false`.

### Neu: `ISOMessage` ist thread-sicher teilbar

Eine `ISOMessage` kann jetzt **sicher von mehreren Threads gleichzeitig**
genutzt werden: alle öffentlichen Eintritte
(`set`/`unset`/`has`/`get`/`tryGet`/`tryGetValue`/`tryGetValueRef`/`reset`/
`keys`/`size`/`to_json`/`dump`/`parser`/`parse`/`unparse`/`header`/
`direction`/`hasMTI`/`mti`/`isRequest`/…) nehmen denselben **rekursiven
Message-Lock** genau einmal; interne Aufrufketten (z. B. `parse →
recalcBitmap → set`) laufen unter dem bereits gehaltenen Lock. Writer und
Reader schließen sich aus (ein Lock, kein paralleler-Reader-Modus).
`to_json`/`dump` sichern den Feldsatz unter dem Lock und formatieren außerhalb
(kurze Lock-Besitzzeiten).

- Parser-Objekte sind nach dem Laden **immutable** → sicher teilbar über
  Threads und Messages (paralleles `parse`/`unparse` auf *verschiedenen*
  Messages mit demselben Parser ist sicher).
- Logger-Globalen (`setLevel`/`setLogger`/`currentLogger`/`getLevel`) sind
  atomar.
- **Restrisiko (dokumentiert in `ISOMessage.hh`):** `mti()` liefert ein
  `string_view` *in* den mutablen Feld-Speicher — vor threadübergreifender
  Nutzung kopieren (`std::string m = msg->mti();`). `tryGetValueRef` ist eine
  zero-copy-Referenz mit derselben Einschränkung.

### Neu: PCI-Logging-Hygiene (`sensitive:`)

Neues Feld-Attribut `sensitive: true` (auch für TLV-`children`-Einträge und
`definitions:`): Der **Wert** wird in `dump()`/`operator<<` und in
Log-Ausgaben als `***` maskiert (die Beschreibung bleibt sichtbar). Auf
nested/TLV-BERTLV-Containern verbreitet es sich auf alle Children/Tags.
`value()`/`to_json()` bleiben bewusst **unmaskiert** (programmatische
Daten-API) — nie `to_json()` in einen Log-Sink für sensible Daten.
Produktiv-Loglevel: **WARN** oder niedriger.

### Spec-Cache-Härtung

- Cache-Eintrag trägt jetzt `{parser, spec, mtime, contentHash}`;
  **Publish-then-Verify**: Ein Parser wird nur unter exakt dem
  Dateisnapshot publiziert, aus dem er gebaut wurde → eine zur Laufzeit
  ausgetauschte Spec kann nie einen „gemischten" Parser liefern (TOCTOU am
  Publish-Punkt geschlossen).
- **LRU-Cap: 64 Einträge** (evictiert least-recent) — schließt das
  unbeschränkte Wachstum.
- `CacheValidation::TrustUntilInvalidated` bleibt, ist aber **als unsicher
  für rollende Spec-Änderungen** dokumentiert (Prozess bei Spec-Wechsel neu
  starten, `invalidateCache(path)` manuell aufrufen, oder `CheckEveryCall`
  verwenden).

### Memory-Sicherheit (Fail-closed statt Absturz)

- **A1:** Bitmap-Puffer-Prüfungen vor jedem Byte-Zugriff (positionsgenaues
  Throw statt OOB-Read); expliziter `bmp.size() > 65`-Guard.
- **A3:** Alle Header-Getter/Setter (WLP-FO, BASE1) rufen `require(n)` auf
  und werfen bei zu kurzem Header; Konstruktoren aus User-Vektoren validieren
  sofort (fail-closed, keine OOB-Write).
- **A5:** `ISOMessage::parser(p)` wirft bei falschem Parser-Typ.
- **A4 (Audit-Regel, Root-`AGENTS.md`):** `dynamic_bitset::operator[]`
  prüft nur per `assert()` (in Release aus) — **niemals** `bmp[n]` ohne
  vorherigen `bmp.size() > n`-Guard indexieren.
- **Iconv-Fallback (so lange gebaut):** E2BIG-Grow-Loop ist jetzt
  no-progress-erkennend und hard-capped (EBCDIC↔ASCII ist 1:1).
- **TLV-Härtung:** `read_num`/BCD-Policy mit expliziten `offset+N <=
  buf.size()`-Prechecks; `BerLength` lehnt `num_bytes > 8` ab (keine
  Shift-Overflow); `store_se` lehnt/warnt, wenn das BER-Tag nicht in
  `TNG_KEY_TYPE` passt (keine stille `static_cast`-Trunkierung → keine
  SE-Misrouting).

### Fuzzing + Sanitizer-CI

- **libFuzzer-Targets** (`tests/fuzz/`, nur mit `ISO8583_BUILD_FUZZERS=ON`):
  `fuzz_unparse` (F1), `fuzz_spec` (F2), `fuzz_serialize` (F3),
  `fuzz_header` (F4), `fuzz_tlv` (F5) + `fuzz_codec`.
- **CI-Matrix:** pro PR `debug` + `debug-asan` (MSVC & Linux/clang) + `tsan`
  (Linux/clang); **nightly** Fuzz-Soak (alle Targets) + Cross-Toolchain-
  Verdict-Diff (EBCDIC-Tabellen).
- **MSVC-ASan:** `parserTable()` ist jetzt ein bewusst ge-leaktes
  Prozess-Lebenszeit-Singleton — MSVC-ASan (Debug CRT) faultet sonst beim
  STL-`unordered_map`-`atexit`-Teardown; die Tabelle wird nie dealloziert
  (OS räumt beim Prozessende auf), die Logik bleibt unverändert.

### Sonstige Bugfixes

- **WLP-FO-Timestamp-Breite:** `getFormattedTimestamp()` erzeugt jetzt eine
  fixe 26-Zeichen-Timestamp (Subsekunden via `duration_cast<microseconds>` +
  `setw(6)`); rohe 100-ns-Clock-Ticks + `setw(4)` lieferten auf Windows
  24–29 Zeichen und einen ~10 % `Timestamp format error`-Flake.
- **`ISOMessage`-Sicherheitsnetz:** alle Standard-Ausnahmen ohne
  `[ISO8583]`-Präfix aus `parse`/`unparse` werden mit
  `[ISO8583] ISOMessage::parse|unparse: …` neu geworfen (keine rohen
  `std::system_error`/`std::stoi` entweichen).

### ABI-Hinweis

`ISOBaseParser` (public) bekommt das neue Mitglied `strict_` →
**Shared-Library-Konsumenten müssen neu kompiliert** werden. Es werden keine
bestehenden Symbole entfernt oder umbenannt; alle neuen Zugänge sind
additive.

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
