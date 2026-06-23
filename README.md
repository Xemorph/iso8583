# libiso8583

C++17-Bibliothek zum Parsen und Erzeugen von ISO 8583-Finanznachrichten.

## Features

- Vollständiger ISO 8583 Parser/Encoder
- EBCDIC ↔ ASCII Konvertierung (optional via libiconv)
- BCD, Binary, ASCII Encoding
- Bitmap-Verarbeitung (Primary / Secondary / Tertiary)
- YAML-basierte Spezifikations-Dateien
- Logging via [Quill](https://github.com/odygrd/quill) (konfigurierbar / injizierbar)
- Shared Library (DLL/SO) und Static Library

---

## Abhängigkeiten

| Bibliothek    | Zweck                              | vcpkg-Name      |
|---------------|------------------------------------|-----------------|
| nlohmann-json | JSON-Serialisierung                | `nlohmann-json` |
| fmtlib        | String-Formatting                  | `fmt`           |
| yaml-cpp      | YAML-Spezifikations-Parser         | `yaml-cpp`      |
| Quill         | Logging                            | `quill`         |
| libiconv      | EBCDIC-Kodierung (optional)        | `libiconv`      |

---

## Bauen mit CMake + vcpkg

### Voraussetzungen

- CMake ≥ 3.21
- C++17-fähiger Compiler (MSVC 2019+, GCC 10+, Clang 13+)
- vcpkg

### Schritt 1 – Abhängigkeiten installieren

```sh
vcpkg install nlohmann-json fmt yaml-cpp quill libiconv robin-map catch2
```

Der `builtin-baseline` in `vcpkg-configuration.json` pinnt alle Pakete auf einen bestimmten vcpkg-Commit. Um auf den neuesten Stand zu aktualisieren:

```sh
# Baseline auf aktuellen vcpkg master setzen (einmalig oder bei Bedarf)
vcpkg x-update-baseline
```

### Schritt 2 – Konfigurieren und Bauen

```sh
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DISO8583_BUILD_SHARED=ON \
      -DISO8583_ENABLE_ICONV=ON \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
cmake --install build --prefix /usr/local
```

---

## API-Übersicht

Die Bibliothek ist in drei klar getrennte Ebenen aufgeteilt:

```
┌─────────────────────────────────────────────────────────────┐
│  Standardanwender                                           │
│    ISOMessage.hh  – Nachrichtenobjekt und alle Feldtypen    │
│    ISOSpec.hh     – YAML-Spezifikation laden                │
├─────────────────────────────────────────────────────────────┤
│  Erfahrene Anwender (eigene Parser-Implementierung)         │
│    ISOParser.hh   – nur ISOParserPtrBase (abstrakt)         │
├─────────────────────────────────────────────────────────────┤
│  Intern – NICHT Teil der öffentlichen API                   │
│    src/_parser.hh     – ISOBaseParser, ISOFieldParser<>     │
│    src/fmt_types.hh   – IF_BINARY, IFE_CHAR, … (Aliase)    │
└─────────────────────────────────────────────────────────────┘
```

### Standardanwender: YAML-Spezifikation laden und Nachricht entpacken

```cpp
#include <iso8583/iso8583.h>   // oder einzeln:
// #include <iso8583/ISOMessage.hh>
// #include <iso8583/ISOSpec.hh>

// 1. Parser aus YAML-Spezifikation erzeugen
auto parser = tng::spec::SpecDecoder::loadFromYaml("visa_spec.yaml");

// 2. Leere Nachricht erstellen und Parser zuweisen
auto msg = std::make_shared<tng::ISOMessage>();
msg->parser(parser);

// 3. Rohe Bytes entpacken (Netzwerk-Frame → Felder)
std::vector<uint8_t> rawBytes = /* ... aus dem Netzwerk ... */;
msg->unparse(msg, rawBytes);

// 4. Felder lesen
if (auto pan = msg->tryGet<ISOOpaqueField>(2))
    std::cout << "PAN:  " << (*pan)->readable_value() << "\n";

if (auto amount = msg->tryGet<ISOOpaqueField>(4))
    std::cout << "Betrag: " << (*amount)->readable_value() << "\n";

// 5. Als JSON ausgeben (z. B. für Logging/Debugging)
std::cout << msg->to_json().dump(2) << "\n";

// 6. MTI-Helfer
if (msg->isAuthorization() && msg->isRequest())
    std::cout << "Das ist eine Autorisierungsanfrage\n";
```

### Nachricht packen (Felder → Bytes)

```cpp
auto msg = std::make_shared<tng::ISOMessage>("0100");  // MTI direkt im Konstruktor
msg->parser(parser);

// Feld setzen
auto pan = std::make_shared<ISOOpaqueField>(2);
pan->value("4111111111111111");
msg->set(pan);

auto amount = std::make_shared<ISOOpaqueField>(4);
amount->value("000000010000");
msg->set(amount);

// Bytes erzeugen
// std::vector<uint8_t> bytes = msg->parse(msg);  // sobald implementiert
```

### Erfahrene Anwender: Eigenen Parser implementieren

```cpp
#include <iso8583/ISOParser.hh>
#include <iso8583/ISOMessage.hh>

class MyCustomParser : public tng::ISOParserPtrBase {
public:
    bool emit_bitmap() noexcept override { return true; }

    std::size_t unparse(
        tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
        const std::vector<uint8_t>& b) override
    {
        // ... eigene Byte-zu-Feld-Logik ...
        return b.size();
    }

    std::vector<uint8_t> parse(
        tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) override
    {
        // ... eigene Feld-zu-Byte-Logik ...
        return {};
    }
};

// Nutzung wie ein YAML-Parser
auto parser = std::make_shared<MyCustomParser>();
auto msg = std::make_shared<tng::ISOMessage>();
msg->parser(parser);
```

### Logging konfigurieren

```cpp
#include <iso8583/iso8583.h>

tng::log::setLevel(tng::log::Level::DEBUG);

// Oder eigenen Quill-Logger injizieren
quill::Logger* myLogger = quill::get_logger("myapp");
tng::log::setExternalLogger(myLogger);
```

---

## Integration in eigene Projekte

### Nach `cmake --install`

```cmake
find_package(iso8583 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iso8583::iso8583)

# Windows DLL: dllimport aktivieren
target_compile_definitions(my_app PRIVATE ISO8583_DLL)
```

---

## Projektstruktur

```
libiso8583/
├── CMakeLists.txt
├── vcpkg.json
├── cmake/
│   └── iso8583Config.cmake.in
├── include/iso8583/              ← Öffentliche API
│   ├── iso8583.h                 ← Convenience-Master-Header
│   ├── ISOMessage.hh             ← ISOMessage, Feldtypen, Header-Klassen  ← STANDARDANWENDER
│   ├── ISOSpec.hh                ← SpecDecoder::loadFromYaml()             ← STANDARDANWENDER
│   ├── ISOParser.hh              ← ISOParserPtrBase                        ← EXPERTEN
│   ├── config.h                  ← TNG_EXPORT, TNG_NAMESPACE, TNG_KEY_TYPE
│   ├── _interfaces.hh            ← Abstrakte Basisklassen
│   ├── _components.hh            ← Vollständige ISOMessage-Klassendefin.
│   ├── _robin_hood.hh            ← Hash-Map (transitiv von _components.hh)
│   ├── _encoder.hh               ← Encoder-Enum und Template-Funktionen
│   ├── _prefixer.hh              ← PrefixEncoder, Length Enums
│   ├── _currency.hh / _date.hh   ← Domänentypen
│   ├── _framework.hh / _memory.hh / _meta.hh
│   ├── dynamic_bitset.hpp / libpopcnt.hpp / string_view.hpp
│   └── _interfaces.hh
└── src/                          ← Private Implementierung
    ├── config.cc
    ├── _components.cc/.hh
    ├── _parser.cc/.hh            ← ISOBaseParser, ISOFieldParser<> (INTERN)
    ├── fmt_types.hh              ← IF_BINARY, IFE_CHAR, … (INTERN)
    ├── _spec.cc/.hh
    ├── _preprocessor.cc/.hh
    ├── _padder.cc/.hh
    ├── _prefixer.hh (intern)
    ├── _pos.cc/.hh
    ├── _logger.cc/.hh
    ├── _iconv_wrapper.cc/.hh
    └── _utils.cc/.hh
```

---

## Bugfixes gegenüber Original

| Datei | Problem | Fix |
|---|---|---|
| `config.cc` | `#ifdef _WIN32 \|\| _WIN64` → ungültige Präprozessorsyntax | Ersetzt durch `#if defined(_WIN32) \|\| defined(_WIN64)` |
| `_logger.hh` | Doppeltes `#pragma once` | Entfernt |
| `config.h` | `TNG_EXPORT` war immer `dllexport` | Import/Export-Guard ergänzt |
| `_spec.hh/.cc` | `loadFromYaml()` gab `shared_ptr<ISOBaseParser>` zurück → exponiert interne Klasse | Rückgabetyp auf `ISOParserPtrBaseSmartPtr` geändert |
| Alle Dateien | UTF-8 BOM + CRLF | Normalisiert auf LF, kein BOM |
