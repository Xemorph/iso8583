# AGENTS.md — libiso8583 public API

This file is the primary reference for AI agents and code-generation tools
working with the `include/iso8583/` directory.  Read it before generating or
modifying any code that uses libiso8583.

---

## What this library does

libiso8583 is a C++17 library for **parsing and building ISO 8583 financial
messages** — the protocol used by Visa, Mastercard, and most payment networks.

Core workflow:

```
YAML spec file  ──► SpecDecoder::loadFromYaml()      ──► ISOParserPtrBase
                │
                └──► SpecDecoder::loadBothFromYaml()  ──► ISOParserPtrBase
                                                        └──► ISOSpec (introspection)

ISOParserPtrBase + raw wire bytes  ──► ISOMessage::unparse()  ──► ISOMessage (decoded)
ISOMessage                         ──► ISOParserPtrBase::parse() ──► wire bytes
```

---

## Namespace and key type

```cpp
// Everything lives in namespace tng::
using namespace tng;   // optional convenience

// DE numbers (data element keys) are int16_t by default
TNG_KEY_TYPE  key = 2;   // expands to tng::key_type = int16_t
// Special value -1 = root ISOMessage (not itself a sub-field)
// Special value -2 = reserved internally for TLV TCC fields
```

### Konfigurierbarer Schlüsseltyp (BER-TLV / EMV)

`TNG_KEY_TYPE` ist standardmäßig `int16_t` (-32768…32767). Reale 2-Byte-EMV-Tags
mit erstem Byte `>= 0x80` (z.B. `9F26`, `5F24` - praktisch alle `9Fxx`/`5Fxx`-Tags)
ergeben als Big-Endian-Wert `> 32767` und passen dort nicht hinein.

Für volle BER-TLV/EMV-Tag-Unterstützung `ISO8583_BERTLV` definieren (schaltet
automatisch auf `int32_t` um):

```cmake
# CMake
target_compile_definitions(your_target PUBLIC ISO8583_BERTLV)
# oder beim Konfigurieren:
cmake -DISO8583_BERTLV=ON ...
```

```cpp
// Manuell (ohne CMake-Option), VOR dem ersten iso8583-Include:
#define ISO8583_BERTLV
#include <iso8583/iso8583.h>
```

Wer einen noch größeren (oder anderen) Schlüsseltyp braucht, kann ihn frei
festlegen - das überstimmt auch `ISO8583_BERTLV`:

```cpp
#define ISO8583_KEY_TYPE int64_t
```

**Wichtig (ABI):** Der Schlüsseltyp fließt in die virtuelle Signatur von
`ISOComponentPtrBase::key()` ein. Wird libiso8583 als Shared Library gebaut,
muss JEDER Konsument mit demselben Wert übersetzt werden. Über CMake genügt
`target_link_libraries(... iso8583::iso8583)`, da `ISO8583_BERTLV` als
`PUBLIC`-Compile-Definition automatisch weitergereicht wird. Bei manueller
Einbindung ohne CMake-Target muss das Makro von Hand in Bibliothek UND allen
Konsumenten identisch gesetzt werden.

---

## Public headers — what to include

| Header | Use when |
|---|---|
| `<iso8583/iso8583.h>` | Always: pulls in everything below |
| `<iso8583/ISOMessage.hh>` | Working with messages and field types |
| `<iso8583/ISOSpec.hh>` | Loading a YAML spec file; introspecting the loaded spec (`ISOSpec`, `SpecFieldInfo`, `SpecFieldFormat`) |
| `<iso8583/ISOLog.hh>` | Configuring library logging |
| `<iso8583/ISOParser.hh>` | Implementing a custom parser (advanced) |
| `<iso8583/_codec.hh>` | Using codec enums/functions directly (advanced) |

Headers inside `detail/` are implementation details — do **not** include them
directly.

---

## Field types

| C++ type | Value type | Typical use |
|---|---|---|
| `ISOOpaqueField` | `std::string` | Text, numeric, EBCDIC/BCD decoded to string |
| `ISOBinaryField` | `std::vector<uint8_t>` | PIN block, ICC/EMV data, cryptograms |
| `ISOBitmap` | `dynamic_bitset<>` | Primary / secondary bitmap |
| `ISOCodeField` | `int32_t` | Integer response codes |
| `tng::ISOMessage` | `ISO_MAP` (field map) | Composite / nested sub-message |

All types inherit from `ISOComponentPtrBase` and are always held in
`std::shared_ptr`.

---

## Decoding a message (unparse = wire bytes → fields)

```cpp
#include <iso8583/iso8583.h>

// 1. Load spec once — cache this, it is expensive
auto parser = tng::spec::SpecDecoder::loadFromYaml("mastercard.yml");

// 2. Create message and attach parser
auto msg = std::make_shared<tng::ISOMessage>();
msg->parser(parser);

// 3. Decode
std::vector<uint8_t> raw = get_from_network();
msg->unparse(msg, raw);

// 4. Read fields
if (auto pan = msg->tryGet<ISOOpaqueField>(2))          // optional – field may be absent
    std::cout << (*pan)->value() << "\n";

auto amount  = tng::ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 4, "000000000000");
auto mti_str = msg->mti();  // e.g. "0200"

// 5. MTI classification
if (msg->isAuthorization() && msg->isRequest())  { /* 01xx */ }
if (msg->isFinancial()     && msg->isResponse()) { /* 021x */ }
```

---

## Building a message (parse = fields → wire bytes)

```cpp
auto msg = std::make_shared<tng::ISOMessage>("0200");  // MTI in constructor
msg->parser(parser);

// Simple fields
msg->set(2,  "4111111111111111");   // PAN          – ISOOpaqueField
msg->set(3,  "000000");             // Proc. code
msg->set(4,  "000000010000");       // Amount (cents)
msg->set(11, "000001");             // STAN

// Binary field — value must be an uppercase hex string
msg->set(52, "0102030405060708");   // PIN block     – ISOBinaryField

// Nested field via dot-notation
msg->set("48.72.1", "ABC");         // DE48 → SE72 → tag 1
msg->set("3.1",     "00");          // DE3 sub-field 1

// Encode to wire bytes
std::vector<uint8_t> wire = parser->parse(msg);
```

---

## Reading fields — choose the right accessor

```cpp
// get<T>()  — returns nullptr if missing or wrong type
auto f = msg->get<ISOOpaqueField>(2);
if (f) use(f->value());

// tryGet<T>() — returns std::optional<shared_ptr<T>>
if (auto opt = msg->tryGet<ISOOpaqueField>(35))
    use((*opt)->value());

// tryGetValue<T>() — returns std::optional<ValueType> (copy)
if (auto val = msg->tryGetValue<ISOOpaqueField>(11))
    use(*val);  // val is std::optional<std::string>

// tryGetValueRef<T>() — returns optional reference_wrapper (zero-copy)
if (auto ref = msg->tryGetValueRef<ISOBinaryField>(55))
    use(ref->get());  // zero-copy access to std::vector<uint8_t>

// ISOUtils helpers
auto pan  = tng::ISOUtils::getOrThrow<ISOOpaqueField>(*msg, 2);   // throws if absent
auto curr = tng::ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 49, "978");
tng::ISOUtils::ifPresent<ISOOpaqueField>(*msg, 11, [](const std::string& stan) {
    log("STAN: {}", stan);
});
```

---

## Spec introspection (ISOSpec)

`loadBothFromYaml` returns both a parser and an `ISOSpec` object that lets you
query the structure of the loaded spec at runtime.

```cpp
#include <iso8583/iso8583.h>

auto [parser, spec] = tng::spec::SpecDecoder::loadBothFromYaml("mastercard.yml");

// Attach parser to messages as usual
msg->parser(parser);

// --- Introspection ---

// Name and global encoding from the YAML "spec:" / "encoding:" keys
spec->name();      // e.g. "Mastercard GMC"
spec->encoding();  // e.g. "EBCDIC"

// Check whether a DE is defined
spec->has(2);      // true if DE002 exists

// Query a single field
if (auto f = spec->field(2)) {
    f->description;            // "Primary Account Number"
    f->format.type;            // "CHAR"
    f->format.prefix_digits;   // 2  (= LL prefix)
    f->format.max_length;      // 19
    f->encoding;               // "EBCDIC"
    f->is_nested;              // false
    f->is_bitmap;              // false
}

// Query a nested field and its children
if (auto pos = spec->field(61)) {
    pos->is_nested;            // true
    pos->children.size();      // number of sub-fields
    pos->children[0].description;           // "POS Terminal Attendance"
    pos->children[0].format.prefix_digits;  // 0  (fixed)
}

// Iterate all defined DEs in key order
for (const auto& f : spec->fields())
    fmt::print("DE{:03d}  {:<30}  {}{}  max={}
",
        f.key, f.description,
        f.format.prefix_digits > 0
            ? std::string(f.format.prefix_digits, 'L') : "FIX",
        f.format.type,
        f.format.max_length);
```

### SpecFieldFormat fields

| Member | Type | Meaning |
|---|---|---|
| `type` | `std::string` | Base format: `"CHAR"`, `"NUMERIC"`, `"BINARY"`, `"BITMAP"`, `"NOP"`, `"REMAINING"` |
| `prefix_digits` | `int` | `0`=fixed, `1`=L, `2`=LL, `3`=LLL, `4`=LLLL |
| `max_length` | `int` | Maximum payload length in logical units (chars, digits, or bytes) |

### SpecFieldInfo fields

| Member | Type | Meaning |
|---|---|---|
| `key` | `TNG_KEY_TYPE` | DE number |
| `description` | `std::string` | Human-readable name from spec YAML |
| `format` | `SpecFieldFormat` | Wire format (see above) |
| `encoding` | `std::string` | `"EBCDIC"`, `"ASCII"`, `"BCD"`, `"BINARY"`, `""` (neutral) |
| `is_nested` | `bool` | `true` for composite sub-message DEs |
| `is_bitmap` | `bool` | `true` for the bitmap DE |
| `children` | `vector<SpecFieldInfo>` | Sub-fields of nested DEs (empty for leaves) |

### When to use loadFromYaml vs loadBothFromYaml

| | `loadFromYaml` | `loadBothFromYaml` |
|---|---|---|
| Parsing / building messages | ✓ | ✓ |
| Querying field names / formats at runtime | ✗ | ✓ |
| UI field lists, validators, documentation | ✗ | ✓ |
| Overhead | minimal | one extra pass over the field map |

---

## YAML spec format

```yaml
spec:     "My Spec"
encoding: ebcdic          # global: ascii | bcd | ebcdic | binary

definitions:              # reusable snippets
  pan_field:
    type: scalar
    format: llchar
    length: 19

fields:
  "000":                  # MTI — always key 000
    type: scalar
    format: numeric
    length: 4
  "001":                  # Bitmap — always key 001
    type: scalar
    format: bitmap
    length: 8
  "002": !use pan_field   # reference a definition
  "003":                  # explicit field
    type: scalar
    format: numeric
    length: 6
    encoding: bcd         # override global encoding for this field
  "055":                  # variable-length binary
    !merge
    - !template LLL(BINARY, 255)
    - description: "ICC Data"
  "056":                  # BER-TLV container (EMV ICC data) — scalar-only
    format: lllbertlv     # LLL-prefix + BER-TLV payload (ISO/IEC 8825-1)
    length: 999
    description: "ICC Data (BER-TLV)"
    # No 'type: nested', 'children', or 'tlv:' block — BER-TLV tags are
    # dynamic (not a fixed, pre-declared SE list), so none is needed.
  "061":                  # nested field with children
    type: nested
    format: binary
    length: 26
    children:
      - format: numeric
        length: 1
        description: "POS Terminal Attendance"
      - format: remaining   # consumes all leftover bytes — no length prefix
        description: "POS Postal Code"
```

**Directives:**
- `!include_files [a.yml, b.yml]` — load external definition files (root level)
- `!use <name>` — substitute a named definition
- `!template P(F, N)` — variable-length shorthand, e.g. `LL(CHAR, 19)` → `{ type: scalar, format: LLCHAR, length: 19 }`
- `!merge [...]` — merge maps, later entries overwrite earlier ones
- `!include <name>` — **deprecated** alias for `!use` (emits a warning)

**Format/encoding combinations:**
- `numeric`, `char`, `binary`, `bitmap`, `nop`
- `llchar`, `lllchar`, `llbinary`, `lllbinary`, `llllbinary`
- `remaining` — reads all bytes remaining in the parent buffer
- `bertlv` (optionally `l`/`ll`/`lll`/`llllbertlv`) — BER-TLV container
  (ISO/IEC 8825-1, EMV Book 3 Annex B); **scalar-only**, must NOT be combined
  with `type: nested`, `children`, or a `tlv:` block. Produces a nested
  `ISOMessage` at runtime whose child keys are the raw BER tag values
  (see `BERTLVParser` in `src/_tlv.hh`). Requires `ISO8583_BERTLV` (see
  above) if any tag exceeds the `int16_t` range, e.g. real 2-byte EMV tags
  like `9F26`.

**Encodings:** `ascii`, `bcd`, `ebcdic`, `binary`

---

## Logging

```cpp
#include <iso8583/ISOLog.hh>

// Option A — level only (default is WARN)
tng::log::setLevel(tng::log::Level::DEBUG);

// Option B — custom logger
class MyLogger : public tng::log::ISOLogger {
public:
    void log(tng::log::Level level, std::string_view file,
             int line, std::string_view message) override {
        fmt::print("[iso8583] {}\n", message);
    }
};
static MyLogger g_logger;
tng::log::setLogger(&g_logger);

// Option C — Quill bridge (when libiso8583 is a DLL)
// Include quill BEFORE ISOLog.hh to activate QuillBridge:
#include <quill/LogMacros.h>
#include <iso8583/ISOLog.hh>
static tng::log::QuillBridge bridge(myQuillLogger);
tng::log::setLogger(&bridge);
tng::log::setLevel(tng::log::Level::DEBUG);
```

**Important (DLL):** Do not use `setQuillLogger()` when libiso8583 is a DLL.
Quill's per-process singleton is split at the DLL boundary — use `QuillBridge`
instead so that log macros expand in the host EXE's Quill instance.

---

## Wire position tracking

After `unparse()`, every field carries its position in the original buffer:

```cpp
auto de2 = msg->get<ISOOpaqueField>(2);
de2->wire_offset();  // byte offset in the original raw buffer
de2->wire_length();  // bytes consumed (including length-prefix)
```

Use `ISOUtils::flatten()` to get a flat map of all leaf fields:

```cpp
auto flat = tng::ISOUtils::flatten(*msg);
// flat["2"]      = "4111111111111111"
// flat["48.72.1"] = "ABC"
```

---

## Headers

```cpp
// Attach before encoding/decoding
auto hdr = std::make_shared<tng::BASE1Header>("000001", "000002");
msg->header(hdr);

// Read after decoding
auto h = std::dynamic_pointer_cast<tng::BASE1Header>(msg->header());
if (h && h->isRejected())
    handle_reject(h->getRejectCode());
```

Available header types: `tng::BaseHeader`, `tng::BASE1Header` (Visa),
`tng::WLP_FOHeader` (Worldline).

---

## Common mistakes to avoid

| Mistake | Correct approach |
|---|---|
| Including `detail/` headers directly | Use `<iso8583/iso8583.h>` |
| Calling `msg->unparse()` without a parser | Call `msg->parser(parser)` first |
| Using `int` or `int32_t` as DE keys | Use `TNG_KEY_TYPE` (`int16_t`) |
| `setQuillLogger()` with a DLL build | Use `QuillBridge` + `setLogger()` |
| Setting a `BITMAP` field manually | Bitmaps are computed automatically |
| Setting a `NESTED` field with a plain string | Use dot-notation: `msg->set("3.1", "00")` |
| Passing a hex string to a non-BINARY field | Only `ISOBinaryField` accepts hex input |
| Passing raw bytes to `ISOBinaryField` | Pass an uppercase hex string, e.g. `"DEADBEEF"` |
| `msg->mti()` before checking `hasMTI()` | Throws `std::logic_error` if MTI is absent |

---

## Thread safety

| Operation | Lock type |
|---|---|
| `set`, `unset`, `reset`, `unparse` | Exclusive write lock |
| `get`, `tryGet`, `has`, `size` | Shared read lock |

Concurrent reads from multiple threads are safe.
Never modify a message while another thread is reading it without external synchronisation.

---

## Key numeric DE ranges

| Range | Meaning |
|---|---|
| -1 | Root `ISOMessage` (not a sub-field) |
| 0 | MTI slot |
| 1 | Primary bitmap slot |
| 2–64 | Primary bitmap DEs |
| 65–128 | Secondary bitmap DEs |
| 129–192 | Tertiary bitmap DEs |
| Sub-field keys start at 0 | Indexed within their parent nested message |
