# Encoding-System

## Auflösungsreihenfolge

```
Feld-Encoding  >  globales YAML-Encoding  >  "" (nur für encoding-neutrale Formate)
```

Jedes Feld-Ergebnis entsteht in `resolveEncoding()`:
encoding-neutrale Formate liefern immer das leere Encoding, alle
anderen Formate übernehmen das optionale Feld-`encoding` oder das
globale `encoding` der Spec.

## Encoding-neutrale Formate

Die folgenden Formate lesen/schreiben immer Rohtext, unabhängig von
jeder Encoding-Einstellung. Sie verwenden weder das globale Encoding
noch ein encoding-awarees Längenpräfix:

- `BINARY` (fixe Länge, kein Präfix)
- `BITMAP`
- `NOP` / `UNUSED`
- `REMAINING`

**Hinweis:** `LBINARY`, `LLBINARY`, `LLLBINARY`, `LLLLBINARY` sind
**nicht** encoding-neutral, da ihr Längenpräfix das Spec-Encoding
(EBCDIC/BCD/ASCII) verwendet.

## Feldweise Override

```yaml
spec:     "Mixed Spec"
encoding: ebcdic        # global

fields:
  "002":
    format: numeric
    encoding: bcd        # überschreibt global nur für dieses Feld
  "052":
    format: binary       # encoding-neutral — ignoriert global
```

## Encoding-Werte

| Wert | Längenpräfix | Daten |
|---|---|---|
| `ascii` | ASCII-Ziffern `'0'`–`'9'` | ASCII-Text |
| `bcd` | BCD-Nibbles | BCD-kodierte Ziffern |
| `ebcdic` | EBCDIC-Ziffern `0xF0`–`0xF9` | EBCDIC-Text |
| `binary` | Big-Endian-Bytes | Rohe Bytes |

## Kinder-Vererbung

Encoding-neutrale Felder geben das **globale** Encoding an ihre
Kinder weiter; encoding-bewusste Felder geben ihr eigenes
aufgelöstes Encoding weiter. Dadurch bleibt eine EBCDIC- oder BCD-
Spec konsistent, auch wenn zwischengeschaltete Container (z. B. ein
`binary`-DE) encoding-neutral sind.