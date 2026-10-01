# YAML spec format & private implementation map

> Verbatim section(s) §5, §6 of the former monolithic AGENTS.md (section numbers are stable; other docs cite them). Entry point: `../AGENTS.md`.

## 5. YAML spec format (summary — full reference: `docs/internals/yaml_format.md` and `include/iso8583/AGENTS.md`)

```yaml
!include_files            # optional, must be first document in the file
- common_definitions.yml
---                       # required document separator (YAML 1.2, strict since 0.2.0)
spec:     "My Spec"
encoding: ebcdic          # global: ascii | bcd | ebcdic | binary
strict:    true           # optional: strict decoding (default true); false = legacy '.'/'?' mapping
header:   93              # optional: N-byte network header in front of the message body (absent = none)
definitions:              # reusable named building blocks
  pan_field: { type: scalar, format: llchar, length: 19 }
fields:
  "000": { type: scalar, format: numeric, length: 4 }   # MTI — always slot 000
  "001": { type: scalar, format: bitmap,  length: 8 }   # Bitmap — always slot 001
  "002": !use pan_field
  "003": { type: scalar, format: numeric, length: 6, encoding: bcd }  # per-field override
  "052": { type: scalar, format: binary, length: 8, sensitive: true }  # PCI: value → "***" in dumps/logs
  "055": { !merge [ !template LLL(BINARY, 255), description: "ICC Data" ] }
  "056": { format: lllbertlv, length: 999 }             # BER-TLV container, scalar only (ISO/IEC 8825-1, EMV Book 3 Annex B); since 0.5.0 an optional children: map (hex tags) may declare typed children
  "057":                                                  # TLV with declared tags
    type: nested
    format: lllbinary
    length: 999
    tlv: { ber: true }
    children:                    # Map = TLV mode
      "9F26": { format: binary, length: 8, description: "Application Cryptogram" }
      "5A":   { format: binary, length: 10, description: "Application PAN" }
  "048":                                                  # fixed-format TLV (MC/Visa style)
    type: nested
    format: lllchar
    length: 999
    tlv: { tag_bytes: 2, len_bytes: 2 }
    children:
      "26": { format: char, length: 10 }   # decimal SE numbers (no ber: true)
  "061":
    type: nested
    format: binary
    length: 26
    children:                          # list = plain nested sub-fields
      - { format: numeric, length: 1 }
      - { format: remaining, length: 10 }  # trailing bytes (max 10; 'length' required since 0.6.0)
```

Directives: `!include_files [a.yml, b.yml]` (root level, **must be followed by `---`**), `!use <name>`, `!template P(F, N)` (e.g. `LL(CHAR, 19)`), `!merge [...]`, `!include` (deprecated alias of `!use`, emits a warning).

Formats: `numeric`, `char`, `binary`, `bitmap`, `nop`, `remaining`, plus L-prefix variants (`llchar`, `lllchar`, `llllchar` (ascii only), `llbinary`, `lllbinary`, `llllbinary`, …) and `bertlv` (optionally `l/ll/lll/llllbertlv`). `remaining` (0.6.0): `length` is **required** (acts as a maximum/clamp) and the format follows the resolved encoding — `""`/`binary` → raw `BinaryField`, `ascii`/`ebcdic`/`bcd` → `OpaqueField`. `bertlv` is **scalar-only** — since 0.5.0 (FR-2) it may carry an optional `children:` **map** (hex tag keys) declaring known/expected tags for typed decoding; undeclared tags stay dynamic. `type: nested`, a separate `tlv:` block, and `children` as a sequence remain forbidden (fail-closed at load). At runtime it produces a nested `Message` whose child keys are the raw BER tag values (`BERTLVParser` in `src/_tlv.hh`).

TLV `children` key notation:
- `tlv: {ber: true}` → keys are **hex** (`"9F26"`, `"5A"`), per EMV Book 3 / ISO 7816 convention.
- fixed-format TLV (`tag_bytes`/`len_bytes`) → keys are **decimal** SE numbers (`"26"`); supported range `tag_bytes` 1–2, `len_bytes` 1–3 (0.6.3, previously max. 2) for `ascii`/`ebcdic`/`bcd` with/without `tcc` (out-of-range values warn at runtime and fall back to the Mastercard 2/2 EBCDIC default).
- Explicit `"0x1A"` prefix forces hex regardless of mode.
- Since 0.5.0 declared TLV children are **typed** (both the `tlv:` block and the `...bertlv` shorthand, D5 whitelist, fail-closed at load): `char`/`numeric`/`nopad_char` decode to `OpaqueField` via the codec (encoding `ascii`/`ebcdic`/`bcd`, declared or inherited — for `...bertlv` children the encoding must be declared explicitly, nothing is inherited), `binary` and undeclared tags stay raw `BinaryField`. Allowed child formats: `binary`, `char`, `numeric`, `nopad_char` (L-prefixed formats, `bitmap`, `remaining`, `nop` are rejected — the TLV length lives in the frame's length field). `length` remains documentation-only. Declared children are introspectable via `SpecFieldInfo::tlv_children` (`std::map<int, SpecFieldInfo>`; the `int` key carries the full tag value so 2-byte EMV tags like `0x9F26` also fit `int16_t` builds — note the layout change for shared-library consumers); the container's TLV mode is introspectable via `SpecFieldInfo::tlv_is_ber` (0.6.0, FR-4) — `true` in BER-TLV mode (`tlv: {ber: true}` or the `...bertlv` shorthand), `false` in fixed SE mode and for non-TLV fields (another `SpecFieldInfo` layout change for shared-library consumers). Undeclared tags fall back to a generic `"SE<n>"` description.

**Text-based nested containers:** `type: nested` works with any container format, incl. text-based ones (`lllchar`, `llchar`, `llllchar` (ascii only)). Since 0.6.0 the loader normalizes the container **base parser** to its binary twin — wire-neutral: same L-counter + prefix encoding, container data reaches the children as raw bytes (each child resolves its own encoding). Before 0.6.0 such containers crashed (`SIGSEGV`) during `unparse()`/`parse()`. Introspection (`ISOSpec::field`) reports the declared format.

Loader behaviors worth knowing:
- **`remaining` semantics (0.6.0):** `format: remaining` without `length` is rejected with a positioned `SpecValidationError` at load time (fail-closed; without a maximum 0 bytes would be decoded). With a text encoding the decoded component is `OpaqueField`, with no encoding/`binary` it is `BinaryField` (raw bytes, unchanged). Introspection: `SpecFieldFormat::max_length` reports the declared `length` for `REMAINING` (previously forced to 0).
- **`amount` field type (0.6.0):** `format: amount` (encodings `ascii`/`ebcdic`/`bcd`) decodes into `iso8583::AmountField` (jPOS `ISOAmount` convention: `zeropad3(currencyNumericCode)` + 1-digit scale + `zeropad12(amountInteger)`, 16 chars — e.g. EUR 19.99 → `"978200000001999"`). The component inherits the string component's parse/unparse paths (thin `AmountFieldParser`); typed accessors (`currency()`, `scale()`, `minorUnits()`, `amount()`, `readable_value()` = `"978/19.99"`) parse the wire value on demand and throw `std::invalid_argument` on length < 12 or jPOS "rounding problem"; `to_json()` adds `currency`/`amount`/`minor_units`. `length: 16` is customary. Also allowed as typed TLV child format (decoded as `AmountField`). **Optional `scale: N` key** (only valid on `format: amount`, integer ≥ 0, fail-closed positioned `SpecValidationError` otherwise): absent = jPOS form as above; present = standard ISO-8583 form (bare `length` digits, declared scale `N`, no currency on the wire → `currency()==nullptr`, `to_json()` without `currency` but with `scale`). Introspection: `SpecFieldInfo::amount_scale` (`std::optional<int>`, ABI layout change). **Optional `sign: true` key** (after 0.6.0; only with `scale:`, not with `bcd`, fail-closed otherwise): wire starts with a sign character `C`/`+` (positive) or `D`/`-` (negative) followed by bare digits (`length` includes the sign char, e.g. DE 28–31 `length: 9`); `minorUnits()`/`amount()` become signed, `readable_value()` gets a leading `-`, plus `hasSign()`/`isNegative()`, `to_json()` adds `negative`; introspection `SpecFieldInfo::amount_signed` (ABI).
- **`strict_length` (0.6.2, FR-5):** opt-in check for undersized values on fixed-length fields (no L-prefix; `remaining` and L-prefixed fields never affected). Root key = default for all fields, per-field key overrides it. Default `false` = legacy silent padding (`numeric`/`amount` left `0`, `char` right space). With `true` and `strict` (default), `parse()` throws `Serialisierung zu kurz …`; non-strict: warning + padding. Runtime: `ISOFieldParserPtrBase::strictLength(bool)`.
- **Root keys:** besides `spec:`/`encoding:`, the loader reads `strict:` (bool, default `true`), `strict_length:` (bool, default `false`) and `header:` (int; N-byte network header in front of the message body, absent/0 = no header). Introspection: `header` via `ISOSpec::hasHeader()`/`ISOSpec::headerSize()`; `strict` via the parser (`ISOParserPtrBase::strict()`, not on `ISOSpec`).
- **Field-only documents (0.6.0):** exactly one top-level `field:` block instead of `fields:` (single field declaration, parsed at the synthetic key `0`, no header/MTI). `fields:` and `header:` in a field-only document, and `field:` in a message document, are rejected fail-closed with positioned errors. Public entry points: `SpecDecoder::loadField*FromYaml(...)` / `SpecDecoder::decodeField(...)`, with their own cache (`invalidateFieldCache` / `clearFieldCache`). Full reference: `docs/internals/spec_schema.md` §11 and `docs/internals/yaml_format.md`.
- **PCI masking (0.3.0):** `sensitive: true` on a field (or on a TLV `children` entry; also valid in `definitions:`) marks the field sensitive — its value is rendered as `***` in `dump()`/`operator<<` (description stays visible). On nested/TLV/BERTLV containers it propagates to all children/tags. `value()`/`to_json()` are deliberately **unmasked** (programmatic data API). Full reference: `docs/internals/yaml_format.md`.
- **Include sandbox (0.3.0):** with the default `SpecLoadOptions`, every `!include_files` entry is resolved and **rejected** (`[ISO8583] Sandbox: …`) if it lands outside `roots` (empty = parent dir of the top-level spec) — lexically (`../`, absolute, UNC) and, when the file exists, on its fully canonicalized (symlink-resolving) path. Source files are streamed with a per-file size cap (`maxSpecBytes`), the total number of distinct files is capped (`maxIncludeFiles`), and `fields:` must be a **non-empty map** — empty maps, sequences, and digit-overflow DE keys produce positioned `SpecValidationError`s, never raw `std::stoi` exceptions. Sidecar **writes** are gated on `allowSmapWrite` + sandbox roots; sidecar **reads** are size-capped (`maxSmapBytes`).
- Errors from rapidyaml are converted to catchable, **positioned** `std::runtime_error` via **process-wide, once-installed** `ryml::set_callbacks` (ryml's default is `std::abort()`). This overrides any host app's own ryml callbacks on first load. Full taxonomy + rules for new code: §13.
- Recursion-depth protection + circular `!use` detection → clean `std::runtime_error` instead of stack overflow.
- Error positions are tracked via tree-internal node identity through a `.smap` sidecar (SourceMap, `src/_sourcemap.cc`). **Sidecar contract:** the sidecar (`<spec>.smap`, next to the spec file) carries a **SHA-256 hash over all source files**; on load it is accepted only if that hash matches, else it is **discarded and regenerated** (same for corruption or missing file). Sidecars from older library versions are detected via a `format_version` field and regenerated once. Operational consequences (read-only dirs, cleanup): §12.1.
- ⚠️ 0.2.0 breaking change: `!include_files` requires the `---` separator (yaml-cpp tolerated its absence).
- 0.2.1 fixed `!merge` definitions whose value is a **sequence** (referenced via `!use`) being lost during preprocessing (rapidyaml same-tree `merge_with` empties the source node's val-tag).

---

## 6. Private implementation map (`src/`)

| File | Role |
|---|---|
| `_components.cc` (1001 ln) + `include/.../detail/_components.hh` | `ISOComponent<key,value>` template, `ISOTaggedField`, `ISOMessage`, headers (`BaseHeader`/`BASE1Header`/`WLP_FOHeader`). Contains **explicit template instantiations** — needs `-fvisibility=default` under GCC/Clang hidden-visibility. New value types must be instantiated here (see §11.2). |
| `_spec.cc` (971 ln) | `SpecDecoder` + field-map → parser-tree construction; installs the global ryml error callbacks; recursion/circular-`!use` guards; positioned-error formatting (SourceMap node identity, `lookup_nearest` fallback for generated nodes). |
| `_preprocessor.cc` (708 ln) | YAML preprocessing: `definitions` extraction, `!use`/`!merge`/`!include_files` expansion (rapidyaml); builds and persists the `.smap` sidecar. |
| `_sourcemap.cc/.hh` | Node-identity-based error positions, `.smap` sidecar cache (format documented in the header). |
| `_parser.cc/.hh` (private) | `ISOBaseParser`, `ISOFieldParser<>` — the concrete parser machinery; deliberately **not** part of the public API (only the abstract `ISOParserPtrBase`/`ISOFieldParserPtrBase`/`ISOFieldParserType`/`ISOHeader` are, via `ISOParser.hh`). |
| `_tlv.cc/.hh`, `_tlv_policy.hh` | TLV parsers: fixed-format TLV, `BERTLVParser` (ISO/IEC 8825-1, `BerTag`), tag/length policies. |
| `_codec.cc` + `include/.../_codec.hh` + `detail/_codec_impl.hh` | Prefixer/encoder tables (`PrefixEncoder`, `Length`, `Encoder` enums; EBCDIC digit tables, ASCII↔EBCDIC conversion tables `kEbcdicToAscii`/`kAsciiToEbcdic`/`kEbcdicValid` = IBM-1047, **ICU-78.3-oracle-pinned**, §4) and the constexpr codecs. **This is the reason C++20 is mandatory.** |
| `_padder.cc`, `_date.hh`, `_utils.cc`, `_logger.cc/.hh`, `config.cc` | Padding, date helpers (vendored Hinnant `date.h`), misc utils, default logger backend, config plumbing. |
| `fmt_types.hh` | Private `IFE_*`/`IFA_*` field-type aliases used by `SpecDecoder`. |

`include/iso8583/detail/extern/` bundles `dynamic_bitset.hpp` (+ `libpopcnt.hpp`) and `nonstd::string_view` — no external fetch needed; `config.h` includes the bitset with `DYNAMIC_BITSET_*` guards. Vendored third-party files and their licenses: §14.3.

---
