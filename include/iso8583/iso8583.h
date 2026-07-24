#pragma once
/// @file iso8583.h
/// @brief Convenience master header — includes the entire public API.
///
/// Instead of including individual headers, a single include suffices:
/// @code
///   #include <iso8583/iso8583.h>
/// @endcode
///
/// ┌-----------------------------------------------------------------┐
/// │  API Layers                                                     │
/// ├-----------------------------------------------------------------┤
/// │  Standard users                                                 │
/// │    ISOMessage.hh  – Message object and field types              │
/// │    ISOSpec.hh     – Loading YAML-Specification                  │
/// |    ISOLog.hh      -                                             |
/// │    ISOUtils.hh    – makeBitmap() and other small helpers        │
/// │    POSDataCode.hh – pos::POSDataCode (POS capability decoding)  │
/// │    Currency.hh    – currency::Currency (ISO 4217 lookup table)  │
/// ├-----------------------------------------------------------------┤
/// │  Advanced users (custom parser)                                 │
/// │    ISOParser.hh   – only ISOParserPtrBase (astract)             │
/// |    _codec.hh      - (codec enums)                               |
/// ├-----------------------------------------------------------------┤
/// │  Internal (not public)                                          │
/// │    src/_parser.hh     – ISOBaseParser, ISOFieldParser<>         │
/// │    src/fmt_types.hh   – IF_BINARY, IFE_CHAR, … (Type-Alias)     │
/// └-----------------------------------------------------------------┘
#include "config.h"
#include "ISOMessage.hh"    // Message, OpaqueField, BinaryField, ...
#include "ISOSpec.hh"       // SpecDecoder::loadFromYaml()
#include "ISOLog.hh"        // setLevel(), setLogger(), ISOLogger
#include "ISOParser.hh"     // ISOParserPtrBase (für eigene Parser-Implementierungen)
#include "ISOUtils.hh"      // utils::makeBitmap()
#include "POSDataCode.hh"   // pos::POSDataCode
#include "Currency.hh"      // currency::Currency, findByAlpha()/findByNumeric()
