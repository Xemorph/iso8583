# Encoding system

## Resolution order

```
Field encoding  >  Global YAML encoding  >  "" (encoding-neutral formats only)
```

## Encoding-neutral formats

The following formats always read/write raw bytes, regardless of any encoding
setting.  They do not use the global encoding and do not have an encoding-aware
length prefix:

- `BINARY` (fixed length, no prefix)
- `BITMAP`
- `NOP` / `UNUSED`
- `REMAINING`

**Note:** `LBINARY`, `LLBINARY`, `LLLBINARY`, `LLLLBINARY` are **not** encoding-neutral
because their length prefix uses the spec encoding (EBCDIC/BCD/ASCII).

## Per-field override

```yaml
spec:     "Mixed Spec"
encoding: ebcdic        # global

fields:
  "002":
    format: numeric
    encoding: bcd        # overrides global for this field only
  "052":
    format: binary       # encoding-neutral — ignores global
```

## Encoding values

| Value | Length prefix | Data |
|---|---|---|
| `ascii` | ASCII digits `'0'`–`'9'` | ASCII text |
| `bcd` | BCD nibbles | BCD-encoded digits |
| `ebcdic` | EBCDIC digits `0xF0`–`0xF9` | EBCDIC text |
| `binary` | Big-endian bytes | Raw bytes |
