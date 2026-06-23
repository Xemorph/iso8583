#pragma once

/**
 * @file iso8583.h
 * @brief Convenience-Master-Header – bindet die gesamte öffentliche API ein.
 *
 * Statt mehrere Header einzeln einzubinden, reicht:
 * @code
 *   #include <iso8583/iso8583.h>
 * @endcode
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  Öffentliche API-Ebenen                                         │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  Standardanwender                                               │
 * │    ISOMessage.hh  – Nachrichtenobjekt und alle Feldtypen        │
 * │    ISOSpec.hh     – YAML-Spezifikation laden (empfohlener Weg)  │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  Erfahrene Anwender (eigene Parser-Implementierung)             │
 * │    ISOParser.hh   – nur ISOParserPtrBase (abstrakt)             │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  Interne Implementierung (nicht Teil der öffentlichen API)      │
 * │    src/_parser.hh     – ISOBaseParser, ISOFieldParser<>         │
 * │    src/fmt_types.hh   – IF_BINARY, IFE_CHAR, … (Typ-Aliase)     │
 * │    src/_padder.hh     – Padding-Implementierung                 │
 * │    src/_prefixer.hh   – Prefix-Encoder-Implementierung          │
 * └─────────────────────────────────────────────────────────────────┘
 */

// ── Basis-Konfiguration ──────────────────────────────────────────────────────
#include "config.h"

// ── Hauptarbeitsobjekte (Standardanwender) ───────────────────────────────────
#include "ISOMessage.hh"    // ISOMessage, ISOOpaqueField, ISOBinaryField, …
#include "ISOSpec.hh"       // SpecDecoder::loadFromYaml()

// ── Erweiterter Zugang (Experten-API) ────────────────────────────────────────
#include "ISOParser.hh"     // ISOParserPtrBase (für eigene Parser-Implementierungen)
