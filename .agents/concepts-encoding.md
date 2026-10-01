# Core concepts, encoding, error/global state

> Verbatim section(s) §4, §13 of the former monolithic AGENTS.md (section numbers are stable; other docs cite them). Entry point: `../AGENTS.md`.

## 4. Core concepts

### Namespace & key type
- Everything public lives in `iso8583` (`TNG_NAMESPACE`, macro overridable by consumers).
- DE keys: `TNG_KEY_TYPE` / `iso8583::key_type` — **`int16_t` by default**, `int32_t` when `ISO8583_BERTLV` is defined, freely overridable via `#define ISO8583_KEY_TYPE <type>` (highest priority).
  - Needed for EMV: real 2-byte BER-TLV tags with first byte ≥ 0x80 (`9F26`, `5F24`, …) exceed `int16_t`'s max (32767) as big-endian values.
  - **ABI-critical**: flows into the virtual signature of `ISOComponentPtrBase::key()`. Every consumer of a shared library must be compiled with the same key type. Via CMake this is automatic (PUBLIC definition); with manual includes the macro must be set identically in library *and* consumers.
- Special key values: `-1` = root `Message` (not a sub-field), `0` = MTI slot, `1` = primary bitmap slot, `-2` = internally reserved (TLV TCC fields). DEs 2–64 primary bitmap, 65–128 secondary, 129–192 tertiary.

### Object model
All field components derive from `ISOComponentPtrBase` (abstract) /
`ISOComponent<key,value>` (concrete template) and are always held in
`std::shared_ptr`. Public leaf/composite types:

| Type | Value type | Typical use |
|---|---|---|
| `iso8583::OpaqueField` | `std::string` | text, PAN, amounts, EBCDIC/BCD as char string |
| `iso8583::BinaryField` | `std::vector<uint8_t>` | PIN-block, ICC/EMV data, cryptograms (set via **uppercase hex string**) |
| `iso8583::FastBinaryField` | `std::vector<std::byte>` | binary with `std::byte` storage |
| `iso8583::Bitmap` | `dynamic_bitset<>` (bundled in `detail/extern/`) | primary/secondary bitmap — **never set manually, auto-computed** |
| `iso8583::CodeField` | `int32_t` | numeric answer codes |
| `iso8583::Message` | `ISO_MAP` (see below) | composite/nested sub-message |
| `iso8583::ISOTaggedField` | — | decoded TLV SE (tag + referenced field) |

> Old names `ISOOpaqueField`, `ISOBinaryField`, `ISOBitmap`, `ISOCodeField`,
> `ISOFastBinaryField`, `ISOMessage` are **deprecated aliases** — write new
> code with the new names (`OpaqueField`, `Message`, …).

**`ISO_MAP` is fixed, not user-selectable.** It is a typedef
(`include/iso8583/detail/_components.hh`):
`detail::flat_map<TNG_KEY_TYPE, std::shared_ptr<ISOComponentPtrBase>>` with
`detail::flat_map = tsl::robin_map`. There is no `TNG_MAP_TYPE`-style hook —
the container is part of the public API and the library ABI, so do not try
to swap in e.g. `std::map`.

### Message API in one paragraph
```cpp
auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mastercard.yml");

auto msg = std::make_shared<iso8583::Message>("0200");  // MTI in ctor (optional)
msg->parser(parser);                                     // REQUIRED before unparse/parse
msg->unparse(msg, rawWireBytes);                         // decode
// or build:
msg->set(2, "4111111111111111");   // plain DE → OpaqueField
msg->set(52, "0102030405060708");  // BinaryField: uppercase hex string
msg->set("48.72.1", "ABC");        // dot-notation nesting (DE48 → SE72 → tag 1)
std::vector<uint8_t> wire = parser->parse(msg);          // encode
```

Field accessors (choose the right one):
- `msg->get<T>(key)` → `shared_ptr<T>` or `nullptr`
- `msg->tryGet<T>(key)` → `std::optional<shared_ptr<T>>`
- `msg->tryGetValue<T>(key)` → `std::optional<ValueType>` (copy)
- `msg->tryGetValueRef<T>(key)` → `std::optional<reference_wrapper>` (zero-copy)
- `iso8583::utils::getOrThrow<T>(msg, key)` / `getOrDefault<T>(msg, key, fallback)` / `ifPresent<T>(msg, key, fn)` / `flatten(msg)` (flat map `"48.72.1" → value`) / `utils::makeBitmap(des...)`.

Other `Message` features: `mti()` (throws `std::logic_error` if no MTI — check `hasMTI()` first), `to_json()`, `dump(os)` / `operator<<`, wire-position tracking (`wire_offset()`/`wire_length()` per field, set by `unparse()` — beware the `0`-ambiguity, §12.2), and MTI classification helpers: `isAuthorization()`, `isRequest()`, `isResponse()`, `isFinancial()`, `isFileAction()`, `isReversal()`, `isChargeback()`, `isReconciliation()`, `isAdministrative()`, `isFeeCollection()`, `isNetworkManagement()`, `isRetransmission()`.

Headers (network framing attached to a message): `BaseHeader`,
`BASE1Header` (Visa; `isRejected()`, `getRejectCode()`), `WLP_FOHeader`
(Worldline). `msg->header(hdr)` before encode/decode.

### Spec loading & introspection
- `spec::SpecDecoder::loadFromYaml(path)` → `ISOParserPtrBase` (cheap).
- `spec::SpecDecoder::loadBothFromYaml(path)` → `tuple<parser, shared_ptr<ISOSpec>>` (extra pass over the field map; use when you need runtime field metadata).
- `...Cached` variants: process-wide cache (max 64 entries total, LRU eviction). `CacheValidation::CheckEveryCall` (default; mtime pre-filter ~1 µs, SHA-256 re-hash over all source files when mtime changed, publish-then-verify — a parser is published only under the exact file snapshot it was built from, so a hot-swapped spec can never yield a "mixed" parser) vs `CacheValidation::TrustUntilInvalidated` (~25 ns hit, no change detection — **unsafe for specs that may change at runtime**: restart the process on spec change, call `SpecDecoder::invalidateCache(path)` manually, or use `CheckEveryCall`). `SpecDecoder::clearCache()` empties everything.
- **`SpecLoadOptions`** (0.3.0, full reference in `docs/internals/yaml_format.md`): the load overloads `load{Both}FromYaml{Cached}(path, const SpecLoadOptions&, …)` take a trust model. Defaults: `sandbox=true` (`!include_files` entries resolving outside `roots` — `../`-traversals, absolute/UNC paths, symlink escapes — are **rejected fail-closed**; empty `roots` = parent dir of the top-level spec, which is user-supplied and not sandboxed), `allowSmapWrite=true` (sidecar is only written if inside the sandbox roots), `maxSpecBytes=32 MiB` (per source file, enforced while streaming), `maxIncludeFiles=1024` (distinct files per load), `maxSmapBytes=16 MiB` (oversize sidecars are discarded and regenerated). The legacy `bool trackSourceMap` overloads remain source-compatible and build defaults internally.
- `ISOSpec` introspection: `spec->name()`, `spec->encoding()`, `spec->hasHeader()`, `spec->headerSize()` (root YAML `header:` key — `hasHeader()` reports key presence, `headerSize()` returns its value, 0 if absent; the parser treats 0 like "no header"), `spec->has(de)`, `spec->field(de)` → `SpecFieldInfo{key, description, format{type, prefix_digits, max_length}, encoding, is_nested, is_bitmap, children, tlv_children, tlv_is_ber}`, `spec->fields()` (key-ordered range).
- **Field-only documents (0.6.0):** a second document form describes a *single* field instead of a full message — a top-level `field:` block (non-empty map, same grammar as `fields:` entries) instead of `fields:`; `fields:` and `header:` inside a field-only document are rejected fail-closed (an isolated field has no MTI, bitmap, or header). Load via the `loadField*FromYaml` family (`loadFieldFromYaml`, `loadFieldFromYamlCached`, `loadFieldBothFromYaml{,Cached}`); the one field is parsed at the **synthetic key `0`** (the MTI key — never attach a field-only parser and a message parser to the *same* `Message`, and never interpret one file in both forms). The resulting parsers run on exactly the bytes a `BinaryField` holds after a full-message decode: `SpecDecoder::decodeField(parser, *de55)` (convenience; equivalent to an empty `Message` + `parser(...)` + `unparse`) decodes that payload, and encoding the result round-trips **byte-identically** without touching the source field. Typical use: DE55 ICC data (Mastercard SE / EMV BER-TLV). Field-only specs use a **separate loader cache** (same LRU ≤ 64 / publish-then-verify mechanics): `SpecDecoder::invalidateFieldCache(path)` / `clearFieldCache()` manage only that cache — if you load one file in both forms, invalidate **both** caches on change (or use the default `CheckEveryCall`). Full form, examples, and the wire contract: `docs/internals/spec_schema.md` §11.

### Logging (`iso8583::log`)
- `log::setLevel(log::Level)` — default is **WARN**; levels up to `OFF`.
- `log::setLogger(&myLogger)` with a class deriving `log::ISOLogger` (pure virtual `log(Level, file, line, message)`).
- **Quill integration with a DLL build: use `log::QuillBridge` (include `<quill/LogMacros.h>` BEFORE `<iso8583/ISOLog.hh>`), never `setQuillLogger()`** — Quill's process-singleton is broken across the DLL boundary.
- `fmt` is a PRIVATE dependency: the library does not leak fmt into public headers.
- **PCI/production: keep the level at WARN or lower.** `INFO`/`DEBUG` add per-field encode/decode detail (sizes, offsets, descriptions — never raw values of `sensitive` fields). The only surface where raw field values reach log output is `dump()` (e.g. when the application logs a dump) — mark card data with `sensitive: true` (§5) so dumps show `***`.

### Thread safety
- **One `ISOMessage` from N threads: supported** (model since 0.3.0): every public entry point (`set`/`unset`/`has`/`get`/`tryGet`/`tryGetValue`/`tryGetValueRef`/`reset`/`keys`/`size`/`to_json`/`dump`/`parser`/`parse`/`unparse`/`header`/`direction`/`hasMTI`/`mti`/`isRequest`/… ) acquires the same **recursive message lock** exactly once; internal call chains (e.g. `parse → recalcBitmap → set`, parser callbacks into `set()`) run under the already-held lock. Writers and readers are mutually exclusive (single lock, no parallel-reader mode). `to_json`/`dump` snapshot the field set under the lock and format outside it.
- **Parsers are immutable after load** → shareable across threads and across messages (concurrent `parse`/`unparse` on *different* messages using the same parser is safe).
- Logger globals (`setLevel`/`setLogger`/`currentLogger`/`getLevel`) are atomic (F3).
- Residual hazards (documented in `ISOMessage.hh`): `mti()` returns a `string_view` **into the mutable field storage** — copy it before cross-thread use (`std::string m = msg->mti();`); `tryGetValueRef` is a zero-copy reference with the same caveat.

### Encoding system (see `docs/internals/encoding.md`)
Resolution order per field: **field-level `encoding` > global spec `encoding` > `""`** (only allowed for encoding-neutral formats).

Encoding-neutral (raw bytes, ignore all encoding settings): `BINARY` (fixed), `BITMAP`, `NOP`/`UNUSED`. Note: `LBINARY`/`LLBINARY`/`LLLBINARY`/`LLLLBINARY` are **not** neutral — their length prefixes use the spec encoding. **`REMAINING` is no longer neutral (0.6.0)**: it follows the resolved field/global encoding — no encoding/`binary` → raw `BinaryField` (as before), `ascii`/`ebcdic`/`bcd` → `OpaqueField` text/digits — and **requires `length`** (maximum; fail-closed `SpecValidationError` without it). See §5 and `docs/internals/spec_schema.md` §4.

| Encoding | Length prefixes use | Data |
|---|---|---|
| `ascii` | ASCII digits | ASCII text |
| `bcd` | BCD nibbles | BCD digits |
| `ebcdic` | EBCDIC digits `0xF0`–`0xF9` | EBCDIC text |
| `binary` | big-endian bytes | raw bytes |

Child inheritance: encoding-neutral fields pass the **global** encoding to children; encoding-aware fields pass their own resolved encoding — keeps mixed specs (e.g. EBCDIC container with a `binary` DE inside) consistent.

**Determinism & oracle pin (since 0.3.0).** EBCDIC conversion is **fully table-driven**: `kEbcdicToAscii` / `kAsciiToEbcdic` / `kEbcdicValid` in `include/iso8583/_codec.hh` — no runtime converter (no libiconv, no ICU) in the build (the transitional libiconv fallback was removed in 0.4.0). The tables are proven against a pinned **ICU 78.3** oracle: `tools/generate_ebcdic_tables/` regenerates/verifies the checked-in verdict JSONs (`pinned/icu_verdicts_{e2a,a2e}.json`, all 256 bytes per direction via `ucnv_convertEx`), and `tests/test_encoding_determinism.cc` sweeps all 256 bytes of both codecs against those verdicts plus the strict-mode throw rules. Consequences worth knowing:

- The library's EBCDIC **whitelist is intentionally stricter than ICU**: ICU 78.3 converts all 256 EBCDIC bytes (C1 controls, binary bytes included), while strict mode accepts only the 85-byte IBM-1047 printable/digit whitelist (E2A) and — for A2E — the 84 mappable ASCII characters. `tests/` pins both counts; a table change that moves them fails determinism tests.
- Non-strict (legacy) behavior is unchanged: unmappable E2A bytes map to the `.` sentinel (`0x2E`), unmappable A2E characters to `0x6F` (`?`). Documented A2E exception: `'?'` (`0x3F`) has no table mapping (falls back to `0x6F`) yet is **never rejected**, even in strict mode (`c != '?'` clause in `to<>`) — it always serializes as `0x6F`.
- The parser's `strict()` flag is propagated to **all four** codec conversion call sites in `src/_parser.hh` (encode/decode × string/binary) — a new codec call site that forgets `strict_` silently downgrades strict specs to legacy behavior.
- Residual limitation: EBCDIC **length prefixes** are decoded as raw low nibbles (`b[i] & 0x0F`, `decode_length`) without whitelist validation — `constexpr` cannot throw; a corrupted prefix is caught fail-closed by the downstream strict data-byte guard (and by the B1 length checks).
- Header unpacking (`WLP_FOHeader`/`BASE1Header`) stays **non-strict** by design: header classes carry no strict state and keep the legacy `rejectInvalid=false` conversion.

---

## 13. Error-handling conventions & global state

### Exception taxonomy
| Source | Exception | Notes |
|---|---|---|
| YAML spec loading/validation (any loader entry point) | catchable `std::runtime_error` **with `file:line:col` position** | ryml's default `std::abort()` is converted via a **process-wide, once-installed** `ryml::set_callbacks` (installed on first load); positions come from the SourceMap (node identity, `lookup_nearest` fallback for generated nodes — `src/_spec.cc`). |
| `Message::mti()` without MTI set | `std::logic_error` | Guard with `hasMTI()`. |
| `utils::getOrThrow<T>()` on missing/wrong-type field | throws | For optional reads use `tryGet*` / `getOrDefault` instead. |

**Rule for new loader/validation code:** always produce positioned errors through the SourceMap mechanism; **never let raw standard-library exceptions escape** (precedent: 0.2.0 replaced raw `std::stoi` escapes with clear, positioned error messages).

### Process-global state the library installs
| State | Installed when | Consequence for host apps |
|---|---|---|
| ryml error callbacks (`ryml::set_callbacks`) | first spec load | **Overwrites the host app's own ryml callbacks** if it uses rapidyaml independently — usually harmless (exceptions instead of `abort()` are the right choice), but known to cause surprises in exotic setups. |
| Spec cache (per absolute path) | `...Cached` loader variants | In-memory, lives for the process; `invalidateCache(path)` / `clearCache()`. |
| Logger + log level (`iso8583::log`) | always (default level WARN) | Global; replace via `setLogger()`. |
| DE key type (`TNG_KEY_TYPE` / `ISO8583_BERTLV`) | compile time (ABI) | Library and all consumers must match — see §4. |

---
