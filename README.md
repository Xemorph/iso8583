# libiso8583

Eine C++17-Bibliothek zum **Parsen und Erzeugen von ISO-8583-Finanznachrichten**
— dem Protokoll, das Visa, Mastercard und die meisten Zahlungsnetzwerke nutzen.

Nachrichtenformate werden deklarativ über YAML-Spec-Dateien beschrieben, nicht
im Code fest verdrahtet:

```
YAML-Spec-Datei  ──►  SpecDecoder::loadFromYaml()      ──►  ISOParserPtrBase
                 │
                 └──►  SpecDecoder::loadBothFromYaml()  ──►  ISOParserPtrBase
                                                          └──► ISOSpec (Introspektion)

ISOParserPtrBase + Wire-Bytes  ──►  ISOMessage::unparse()    ──►  ISOMessage (dekodiert)
ISOMessage                     ──►  ISOParserPtrBase::parse() ──►  Wire-Bytes
```

## Lizenz

**Keine Open-Source-Lizenz.** Diese Software steht unter einer proprietären,
Source-Available-Lizenz (siehe [`LICENSE`](LICENSE) für den vollständigen,
verbindlichen Text). Kurzgefasst: Nutzung ist ohne gesonderte schriftliche
Zustimmung des Urhebers nur für private Zwecke, Ausbildung, Forschung oder
interne Evaluierung gestattet — **lies `LICENSE`, bevor du diese Bibliothek in
einem anderen Kontext einsetzt.**

## Voraussetzungen

- C++17-fähiger Compiler (GCC, Clang, MSVC)
- CMake ≥ 3.21
- [vcpkg](https://vcpkg.io) im Manifest-Modus (Abhängigkeiten werden über
  `vcpkg.json` automatisch aufgelöst)

Abhängigkeiten (alle über vcpkg): `nlohmann-json`, `fmt`, `ryml` (rapidyaml,
≥ 0.15.2), `robin-map`, optional `libiconv` (EBCDIC-Konvertierung unter
Nicht-Linux-Plattformen), `catch2` (nur für Tests).

## Bauen

```bash
cmake --preset debug          # siehe CMakePresets.json für alle Presets
cmake --build --preset debug
```

Wichtige CMake-Optionen (siehe `CMakeLists.txt`):

| Option | Standard | Bedeutung |
|---|---|---|
| `ISO8583_BUILD_SHARED` | `ON` | Shared Library (`.so`/`.dll`) statt statisch bauen |
| `ISO8583_ENABLE_ICONV` | `ON` | `libiconv` für EBCDIC-Konvertierung nutzen |
| `ISO8583_BUILD_TESTS`  | `OFF` | Unit-Tests bauen (benötigt Catch2) |
| `ISO8583_BERTLV`       | `OFF` | DE-Schlüsseltyp auf `int32_t` erweitern (volle BER-TLV/EMV-Tag-Unterstützung, z.B. 2-Byte-Tags wie `9F26`) |
| `ISO8583_INSTALL`      | `ON` | Install-Targets erzeugen |

## Einbindung in ein anderes CMake-Projekt

Nach der Installation (`ISO8583_INSTALL=ON` + `cmake --install`):

```cmake
find_package(iso8583 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE iso8583::iso8583)
```

Für den Fall, dass `libiso8583` selbst als vcpkg-Port konsumiert wird, siehe
[`vcpkg-port/`](vcpkg-port/) (`portfile.cmake` benötigt vor dem ersten Tag
noch die echte Repo-URL und den SHA512-Hash — im Portfile mit `# ← Bitte
anpassen` markiert).

## Kurzbeispiel

```cpp
#include <iso8583/ISOSpec.hh>
#include <iso8583/ISOMessage.hh>

using namespace tng;

auto [parser, spec] = spec::SpecDecoder::loadBothFromYaml("mastercard.yml");

// Wire-Bytes dekodieren
auto msg = std::make_shared<ISOMessage>();
msg->parser(parser);
msg->unparse(msg, raw_bytes);

// Introspektion
if (auto pan = spec->field(2))
    fmt::print("DE002: {} ({}LL-Prefix, max {} Zeichen)\n",
               pan->description, pan->format.prefix_digits, pan->format.max_length);
```

Vollständige API-Referenz, YAML-Spec-Format, Logging-Integration (inkl.
optionaler Quill-Bridge) und Konfigurationsdetails: siehe
[`include/iso8583/AGENTS.md`](include/iso8583/AGENTS.md).

Änderungshistorie: siehe [`CHANGELOG.md`](CHANGELOG.md).

## Tests

```bash
cmake --preset debug -DISO8583_BUILD_TESTS=ON
cmake --build --preset debug
ctest --preset debug
# oder direkt: ./build/debug/tests/libiso8583_tests
```
