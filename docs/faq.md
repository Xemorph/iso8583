# FAQ / Häufige Fehlerszenarien

Kurzreferenz für Fehlermeldungen, die in Integrations- und Support-Kontexten
auftauchen. Jeder Eintrag enthält Mechanismus, Evidenz und Triageschritte.

## "Resource deadlock would occur" (`std::system_error`) nach `unparse()`

**Symptom:** Ein Host-Anwendungsprogramm wirft nach dem Dekodieren einer
(korrekten) ISO-8583-Nachricht `std::system_error: Resource deadlock would
occur` — typischerweise erst bei der **zweiten** Nachricht desselben
Threads/der selben Verbindung, wenn die erste Nachricht bereits fehlerhaft
war (z. B. strict-Modus-Abbruch wegen ungültiger EBCDIC-Bytes).

**Mechanismus (MSVC/Windows):** Die Fehlermeldung ist
`std::system_error{std::errc::resource_deadlock_would_occur}`. Die MSVC-STL
erzeugt diesen exakten `what()`-Text nur an drei Stellen:

| Stelle | Auslöser |
|---|---|
| `_Mutex_base::lock()` (`<mutex>`) | **erneutes Sperren einer nicht-rekursiven Sperre** (`std::mutex`, exklusives `std::shared_mutex::lock()`, ebenso `std::scoped_lock`/`std::unique_lock`) **durch den Thread, der sie bereits hält** |
| `unique_lock::_Validate()` | `lock()` auf einem `unique_lock`, der die Sperre bereits hält |
| `std::thread::join()` | Join des aufrufenden Threads selbst |

Verifiziert mit einem Mini-Repro auf VS2022/MSVC 14.44 (Windows):

```text
std::mutex self re-lock           -> THROW: resource deadlock would occur
std::recursive_mutex self re-lock -> no throw
std::shared_mutex self re-lock    -> stillsteht (lock_shared ist noexcept)
```

**Plattformasymmetrie (erkenntliches Merkmal):** Das *selbe* Muster
(erneutes Sperren einer nicht-rekursiven Sperre) stillsteht unter Linux/GCC ewig —
nur MSVC wandelt es in eine Ausnahme um. Deshalb erscheint der Fehler nur in
Windows/MSVC-Deployments. `std::call_once` ist auf MSVC InitOnce-basiert und
wirft bei Re-Entry **nicht** (der Callback wird einfach erneut ausgeführt) —
die Meldung kommt also nie aus `call_once`/`once_flag`.

**Warum es nicht aus libiso8583 kommt** (Audit aller Sperrprimitiven auf dem
`unparse()`-Pfad, Stand 0.3.0+):

- Alle Message-Sperren (`ISOMessage`) sind `std::recursive_mutex` —
  Re-Entry wirft nie (siehe Repro oben).
- Die **einzige** nicht-rekursive `std::mutex` im Decode-Pfad ist die
  TLV-Fallback-Beschreibungscache-Sperre (`src/_tlv.hh`) — ein RAII
  `lock_guard`, der exakt eine `unordered_map::try_emplace` abdeckt; ein
  Thread kann sie nicht erneut betreten.
- Die Spec-Cache-Sperre (`std::shared_mutex` in `src/_spec.cc`) wird streng
  sequenziell (shared → unique, nie verschachtelt) verwendet und läuft nur
  beim Spec-**Load**, nicht in `unparse()`. Selbst ein Re-Entry dort würde
  unter MSVC stillstehen, nicht werfen.
- Kein `call_once`/`once_flag`, keine Magic-Static und kein `thread_local`
  auf dem `unparse()`-Pfad: Seit 0.3.0 ist die EBCDIC-Konvertierung reine
  Tabellen-Lookups (`e2a_n` aus `kEbcdicToAscii`/`kAsciiToEbcdic`,
  ICU-78.3-gepinnt, s. [`internals/encoding.md`](internals/encoding.md)) —
  kein iconv, kein ICU zur Laufzeit. Seit 0.4.0 (Entfernung des iconv-
  Fallbacks) enthält der Baum gar keine `thread_local`-Deskriptoren mehr.
- Auch bei 0.2.x (iconv-basierter Codec) würde ein fehlgeschlagene
  `iconv_open`/`EILSEQ` mit einem völlig anderen Fehlertext auftauchen,
  nicht mit dieser.

**Folgerung:** Die Meldung stammt aus dem **Host-Prozess**, nicht aus der
Bibliothek. Das Zwei-Nachrichten-Muster ist die klassische Signatur einer
**ausgelösten oder verschachtelten nicht-rekursiven Sperre** in Host-Code:

- **Nachricht 1** (z. B. mit rohen Steuerbytes wie `0x00`/`0x02` hinter dem
  MTI) wird früh abgelehnt (strict-Whitelist) — der Code läuft nie bis zur
  zweiten Sperre.
- **Nachricht 2** läuft weiter, und derselbe Thread sperrt erneut eine
  nicht-rekursive Sperre, die er bereits hält → MSVC wirft genau die
  berichtete Meldung.

**Typische Verdächtige im Host-Code:**

1. Eigene Handler-/Worker-Sperren: Methode A hält eine `std::mutex` (m) und
   ruft Methode B auf, die dieselbe Sperre erneut sperrt. Oder manuelles
   `m.lock()`/`m.unlock()`, bei dem der Exception-Pfad von Nachricht 1 das
   `unlock()` übersprungen hat — eine Win32-Sperre bleibt dem Thread
   „gehört“, und das nächste `lock()` wirft.
2. Logger-Callback (`log::setLogger`): Der Callback sperrt eine
   nicht-rekursive Sperre und wird auf demselben Thread re-entered (die
   Bibliothek kann den Logger während des Halts der rekursiven
   Message-Sperre aufrufen; Callback → `dump()`/Formatierung → zweiter
   Log-Aufruf → zweite Sperre derselben eigenen Sperre).
3. `std::thread::join()` auf dem aufrufenden Thread im Thread-Pool-/
   Gateway-Code.

**Triage (Host-Seite):**

1. **Kompletten Callstack** zur ersten Ausnahme holen: Der VS-Debugger
   bricht bei `std::system_error`; der Frame direkt oberhalb von
   `std::mutex::lock()` in `MSVCP140.dll` benennt das Host-Modul.
2. Differenzialtest: Die verdächtige `std::mutex` durch
   `std::recursive_mutex` ersetzen — verschwindet der Fehler, ist das die
   Sperre.
3. Bei Nutzern ≤ 0.3.x: **Upgrade auf ≥ 0.4.0** — der iconv-Fallback
   (`ISO8583_ENABLE_ICONV`, `thread_local`-Deskriptoren) ist vollständig
   entfernt, die EBCDIC-Konvertierung läuft rein tabellenbasiert.

**Prävention im eigenen Host-Code:** Logger-Callbacks und alle
Handler-Pfade, die unter Bibliotheks-Aufrufen laufen, dürfen nur
**reentrant-fähige** Sperren (`std::recursive_mutex`, `std::shared_mutex`
mit konsistentem Shared/Unique-Zyklus) oder lock-freie Mechanismen
verwenden; manuelles `lock()`/`unlock()` um Code, der werfen kann, ist
tabu (RAII-Lockguards verwenden).