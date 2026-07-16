# Quick start

## Installation via vcpkg

```bash
# Add the overlay port
cmake -B build \
  -DVCPKG_OVERLAY_PORTS=path/to/libiso8583/vcpkg-port \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

cmake --build build
```

## CMake integration

```cmake
find_package(iso8583 CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE iso8583::iso8583)

# Windows DLL: enable dllimport
target_compile_definitions(my_target PRIVATE ISO8583_DLL)
```

## Decoding a message

```cpp
#include <iso8583/iso8583.h>

// 1. Load the spec once and cache it
auto parser = tng::spec::SpecDecoder::loadFromYaml("mastercard.yml");

// 2. Create a message, attach the parser
auto msg = std::make_shared<tng::ISOMessage>();
msg->parser(parser);

// 3. Decode raw bytes
std::vector<uint8_t> raw = receive_from_network();
msg->unparse(msg, raw);

// 4. Read fields
std::cout << "MTI: " << msg->mti() << "\n";

if (auto pan = msg->tryGet<ISOOpaqueField>(2))
    std::cout << "PAN: " << (*pan)->value() << "\n";

auto amount = tng::ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 4, "000000000000");
```

## Building a message

```cpp
auto msg = std::make_shared<tng::ISOMessage>("0200");
msg->parser(parser);

msg->set(2,  "4111111111111111");  // PAN
msg->set(3,  "000000");            // Processing code
msg->set(4,  "000000010000");      // Amount (cents)
msg->set(11, "000001");            // STAN

// Nested field via dot-notation (DE48 → SE72 → tag 1)
msg->set("48.72.1", "ABC");

// Encode to wire bytes
std::vector<uint8_t> wire = parser->parse(msg);
```

## Logging

```cpp
// Silence the library
tng::log::setLevel(tng::log::Level::OFF);

// Or use your own logger
class MyLogger : public tng::log::ISOLogger {
public:
    void log(tng::log::Level, std::string_view, int,
             std::string_view message) override {
        fmt::print("[iso8583] {}\n", message);
    }
};
static MyLogger g_logger;
tng::log::setLogger(&g_logger);
```
