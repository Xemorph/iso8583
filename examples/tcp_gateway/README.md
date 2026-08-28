# Beispiel: Minimal-ISO-8583-TCP-Gateway

Ein kleines, vollständiges Beispielprojekt: Ein TCP-Server (`gateway.cc`), der auf
einem Port lauscht, **pro Verbindung genau eine kodierte ISO-8583-Nachricht**
empfangen, sie mit `libiso8583` (`ISOMessage::unparse()`) dekodiert und per
Standardausgabe meldet, ob die Dekodierung **SUCCESS** oder **FAIL** war —
inklusive MTI, Feldanzahl und dem dekodierten JSON-Dump aller Felder.

Zweck: zeigen, wie man `libiso8583` in ein C++-Programm einbindet und rohe
Wire-Bytes (z. B. von einem Mainframe-Host) in strukturierte Nachrichten
verwandelt — inklusive ASCII- **und** EBCDIC-(IBM-1047)-Pfad.

## Dateien

| Datei | Zweck |
|---|---|
| `gateway.cc` | Der TCP-Gateway (C++20, Windows + POSIX). Pro Verbindung: Frame lesen → `unparse()` → Ergebnis ausgeben. |
| `send_test.py` | Python-Testclient (nur Standardbibliothek). Baut eine Authorization-Request (MTI `0100`, DE 2, 3, 4, 11, 41, 49) und schickt sie per TCP an den Gateway. |
| `ascii_spec.yml` | Feldspezifikation, Wire-Codierung **ASCII** (passt zu `send_test.py ... ascii`). |
| `spec_ebcdic.yml` | Gleiche Felder, Wire-Codierung **EBCDIC / IBM-1047** (passt zu `send_test.py ... ebcdic`). |
| `CMakeLists.txt` | CMake-Target `iso8583_tcp_gateway` (wird nur eingebunden, wenn `ISO8583_BUILD_EXAMPLES=ON` gesetzt ist). |

## Voraussetzungen

- CMake ≥ 3.21
- [vcpkg](https://vcpkg.io) mit gesetzter Umgebungsvariable `VCPKG_ROOT`
  (die Abhängigkeiten — `nlohmann-json`, `fmt`, `ryml`, `robin-map`, `libiconv` —
  werden automatisch im Manifest-Modus über `vcpkg.json` aufgelöst)
- C++20-Compiler:
  - **Windows:** Visual Studio 2022 oder neuer (MSVC). Am einfachsten von einer
    *Developer Command Prompt* aus bauen (oder zuerst
    `vcvars64.bat` ausführen) — sonst fehlt dem Ninja-Generator die SDK-Umgebung
    (Fehlerbild: `LNK1104: kernel32.lib`).
  - **Linux/macOS:** g++ ≥ 9 oder clang ≥ 10 mit `libtool`-Headers (`sys/socket.h` u. a.).
- Python 3 (nur für den Testclient, nicht für den Gateway selbst)

## Compilieren

Das Beispiel ist **opt-in** — der Default-Build der Bibliothek wird nicht
berührt. Man schaltet es mit `-DISO8583_BUILD_EXAMPLES=ON` ein.

### Variante A: Preset des Projekts (Windows, Ninja)

```bat
:: aus der Developer Command Prompt (oder nach vcvars64.bat):
cmake --preset debug -DISO8583_BUILD_EXAMPLES=ON
cmake --build  --preset debug --target iso8583_tcp_gateway
```

> Hinweis: `--target iso8583_tcp_gateway` baut **nur** den Gateway statt des
> kompletten Projekts inkl. Tests. Ohne `--target` baut das Preset `debug`
> zusätzlich die Unit-Tests (dafür wird Catch2 über vcpkg geholt).
>
> Auf Maschinen **nur** mit Visual Studio 2026 (VS 18) funktionieren die
> `msvc-*`-Presets nicht, da sie `Visual Studio 17 2022` als Generator
> hard-codieren — dort die Ninja-Presets (`debug`/`release`) verwenden,
> wie oben.

### Variante B: eigenständiges Build-Verzeichnis (Linux/macOS oder Windows)

```sh
cmake -B build/tcp-gateway \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DISO8583_BUILD_EXAMPLES=ON \
  -DISO8583_BUILD_TESTS=OFF
cmake --build build/tcp-gateway --target iso8583_tcp_gateway
```

### Ergebnis nach dem Build (im Binärverzeichnis, z. B. `build/debug/bin/`)

- `iso8583_tcp_gateway(.exe)` — der Gateway
- `iso8583.dll` bzw. `libiso8583.so` — wird bei Shared-Builds **automatisch
  daneben kopiert**, die Demo startet ohne extra `PATH`/`LD_LIBRARY_PATH`-Setup
- `ascii_spec.yml`, `spec_ebcdic.yml` — werden ebenfalls daneben kopiert
  (der Standard-Spec-Pfad des Gateways ist `./ascii_spec.yml` **relativ zum
  Arbeitsverzeichnis**)

## Starten & Testen

Zwei Terminals:

**Terminal 1 — Gateway starten** (ASCII-Modus, Defaults: Port 9000,
`./ascii_spec.yml`):

```sh
cd build/debug/bin            # damit ./ascii_spec.yml gefunden wird
./iso8583_tcp_gateway
# oder mit expliziten Parametern:
#   ./iso8583_tcp_gateway [Port] [Spec-Datei.yml]
```

Erwartet:

```
SPEC  : ascii_spec.yml (loaded)
LISTEN: 0.0.0.0:9000 (Ctrl+C to stop)
```

**Terminal 2 — Frame senden** (aus `examples/tcp_gateway/`):

```sh
python send_test.py                    # -> 127.0.0.1:9000, ASCII-Frame
python send_test.py 127.0.0.1 9000 ascii
```

Der Gateway zeigt pro Verbindung u. a.:

```
CONN  : 127.0.0.1:50915
RECV: 68 bytes
MTI   : 0100  (non-financial, request / authorization)
FIELDS: 8 (incl. MTI + bitmap)
JSON  :
{ ... "DE002"=4111111111111111, "DE003"=000000, "DE004"=000000012345,
         "DE011"=000042, "DE041"=TERMINAL1, "DE049"=978 ... }
RESULT: SUCCESS - message 0100 decoded, 8 field(s)
```

### EBCDIC-Modus (IBM-1047)

Beim Gateway die EBCDIC-Spec laden, dann mit dem Sender den EBCDIC-Parameter
setzen — **beides muss zusammenpassen** (die Spec-Datei bestimmt, in welcher
Codierung der Gateway die Byte-Daten der Felder interpretiert):

```sh
./iso8583_tcp_gateway 9000 spec_ebcdic.yml   # Terminal 1
python send_test.py 127.0.0.1 9000 ebcdic    # Terminal 2
```

Beide Pfade (ASCII und EBCDIC) liefern mit dem selben 68-Byte-Frame identisch
dekodierte Feldwerte.

## Frame-Format (Wire-Protokoll des Beispiels)

Der Gateway erwartet einen **rohen Frame ohne eigene Längen-/Endemarke**
(„so wie er kommt", wie von vielen Host-Protokollen). Das Frame der Demo:

| Teil | Bytes | Kodierung |
|---|---|---|
| MTI `0100` | 4 | wie Spec (`ascii`/`ebcdic`) |
| Primary Bitmap | 8 | **raw** (encodingsunabhängig) |
| DE002 (LL `16` + 16 Ziffern) | 18 | wie Spec |
| DE003 (fix 6) | 6 | wie Spec |
| DE004 (fix 12) | 12 | wie Spec |
| DE011 (fix 6) | 6 | wie Spec |
| DE041 (LL `09` + `TERMINAL1`) | 11 | wie Spec |
| DE049 (fix 3) | 3 | wie Spec |

Ende des Frames: Der Sender schickt nach `sendall` ein half-close
(`shutdown(SHUT_WR)`). Der Gateway liest mit `recv()` weiter, bis der Peer
schließt (EOF) oder **5 s Stille** vergehen (`RX_TIMEOUT_MS` in `gateway.cc`)
— beides wird als „Frame fertig" gewertet.

Zwei Konventionen, die zur Bibliothek passen müssen:

- **Bitmap-Setzung:** MSB-first pro Byte, exakt wie `iso8583::utils::makeBitmap()`:
  `p = de - 1; byte = p / 8; bit = 7 - p % 8`.
- **EBCDIC-Tabelle:** IBM-1047 (CCID), bytegenau die `kAsciiToEbcdic`-Tabelle
  aus `include/iso8583/_codec.hh` (Ziffern `0xF0–0xF9`, `A–Z` `0xC1–0xE9`,
  `a–z` `0x81–0xA9`). `send_test.py` trägt dieselbe Tabelle nach. Wer in
  `send_test.py` neue Zeichen testet, muss die Tabelle ergänzen und darf nur
  ASCII-Zeichen verwenden, die in der IBM-1047-Tabelle existieren.

## Wichtige CMake-Optionen (aus dem Root-`CMakeLists.txt`)

| Option | Default | Wirkung für dieses Beispiel |
|---|---|---|
| `ISO8583_BUILD_EXAMPLES` | `OFF` | `ON` → `examples/tcp_gateway` wird eingebunden |
| `ISO8583_BUILD_SHARED` | `ON` | Shared-Build: das Beispiel definiert `ISO8583_DLL` (MSVC: dllimport) selbst, und die `iso8583`-Binärdatei wird neben den Gateway kopiert |
| `ISO8583_ENABLE_ICONV` | `ON` | EBCDIC-Konvertierung der Bibliothek über `libiconv` |

## Grenzen des Beispiels (bewusst)

- **Demo, kein Production-Server:** seriell pro Verbindung (eine nach der
  anderen), kein Keep-Alive-Protokoll, kein Framing mit Längenzähler, keine
  Authentifizierung, kein TLS.
- Ein Frame pro Verbindung; für echte Dauer-Verbindungen mit vielen
  aufeinanderfolgenden Frames muss der Frame-Grenzwert (z. B. über eine
  Längen- oder Endemarke) ergänzt werden.
- Nur IPv4 (`AF_INET`), Single-Thread-Loop mit `select`-Timeout.