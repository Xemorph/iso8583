# Schnellstart

## Installation über vcpkg

```bash
# Overlay-Port hinzufügen
cmake -B build \
  -DVCPKG_OVERLAY_PORTS=path/to/libiso8583/vcpkg-port \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

cmake --build build
```

## CMake-Integration

```cmake
find_package(iso8583 CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE iso8583::iso8583)

# Windows-DLL: dllimport aktivieren
target_compile_definitions(my_target PRIVATE ISO8583_DLL)
```

## Eine Nachricht dekodieren

```cpp
#include <iso8583/iso8583.h>

// 1. Spec einmal laden und zwischenspeichern
auto parser = iso8583::spec::SpecDecoder::loadFromYaml("mastercard.yml");

// 2. Nachricht anlegen und Parser zuordnen
auto msg = std::make_shared<iso8583::Message>();
msg->parser(parser);

// 3. Rohe Bytes dekodieren
std::vector<uint8_t> raw = receive_from_network();
msg->unparse(msg, raw);

// 4. Felder lesen
std::cout << "MTI: " << msg->mti() << "\n";

if (auto pan = msg->tryGet<iso8583::OpaqueField>(2))
    std::cout << "PAN: " << (*pan)->value() << "\n";

auto amount = iso8583::utils::getOrDefault<iso8583::OpaqueField>(*msg, 4, "000000000000");
```

## Eine Nachricht bauen

```cpp
auto msg = std::make_shared<iso8583::Message>("0200");
msg->parser(parser);

msg->set(2,  "4111111111111111");  // PAN
msg->set(3,  "000000");            // Processing code
msg->set(4,  "000000010000");      // Amount (Cents)
msg->set(11, "000001");            // STAN

// Verschachteltes Feld über Punkt-Notation (DE48 → SE72 → Tag 1)
msg->set("48.72.1", "ABC");

// In Wire-Bytes kodieren
std::vector<uint8_t> wire = parser->parse(msg);
```

## Logging

```cpp
// Die Bibliothek schweigen lassen
iso8583::log::setLevel(iso8583::log::Level::OFF);

// Oder einen eigenen Logger verwenden
class MyLogger : public iso8583::log::ISOLogger {
public:
    void log(iso8583::log::Level, std::string_view, int,
             std::string_view message) override {
        fmt::print("[iso8583] {}\n", message);
    }
};
static MyLogger g_logger;
iso8583::log::setLogger(&g_logger);
```