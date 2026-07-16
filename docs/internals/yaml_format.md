# YAML spec format

## Minimal spec

```yaml
spec:     "My Spec"
encoding: ebcdic       # global: ascii | bcd | ebcdic | binary

fields:
  "000":               # MTI — always slot 000
    type: scalar
    format: numeric
    length: 4
  "001":               # Primary bitmap — always slot 001
    type: scalar
    format: bitmap
    length: 8
  "002":               # Primary Account Number
    type: scalar
    format: llchar
    length: 19
```

## Definitions and reuse

```yaml
definitions:
  pan_field:
    type: scalar
    format: llchar
    length: 19
    description: "Primary Account Number"

fields:
  "002": !use pan_field
```

## Multi-file specs

```yaml
# mastercard.yml
!include_files
- schemes/base.yml
- schemes/gmc.yml

spec: "Mastercard GMC"
encoding: ebcdic

fields:
  "002": !use pan_field   # defined in base.yml
```

## Directives

| Directive | Effect |
|---|---|
| `!include_files [a.yml, b.yml]` | Load external files; merge their `definitions` |
| `!use <name>` | Substitute a named definition |
| `!template P(F, N)` | Variable-length shorthand, e.g. `LL(CHAR, 19)` |
| `!merge [...]` | Merge maps; later entries overwrite earlier ones |
| `!include <name>` | Deprecated alias for `!use` |

## Format reference

| Format | Description |
|---|---|
| `numeric` | BCD or ASCII/EBCDIC digits |
| `char` | Character string |
| `binary` | Raw bytes (fixed length, encoding-neutral) |
| `bitmap` | Primary or secondary bitmap |
| `nop` | Skip / placeholder (no bytes consumed) |
| `llchar` | 2-digit length prefix + char data |
| `lllchar` | 3-digit length prefix + char data |
| `llbinary` | 2-digit length prefix + binary data |
| `lllbinary` | 3-digit length prefix + binary data |
| `remaining` | All remaining bytes of the parent buffer |

## Nested fields

```yaml
"061":
  type: nested
  format: binary
  length: 26
  description: "POS Data"
  children:
    - format: nop
      length: 0
    - format: numeric
      length: 1
      description: "POS Terminal Attendance"
    - format: remaining          # trailing optional field — no length prefix
      description: "POS Postal Code"
```

Access nested fields via dot-notation:

```cpp
msg->set("61.1", "0");   // sub-field 1 of DE61
```

## Template shorthand

```yaml
"002": !template LL(CHAR, 19)
# expands to: { type: scalar, format: LLCHAR, length: 19 }

"055":
  !merge
  - !template LLL(BINARY, 255)
  - description: "ICC / EMV Data"
```

Prefix letters: `L` (max 9), `LL` (max 99), `LLL` (max 999), `LLLL` (max 9999).
