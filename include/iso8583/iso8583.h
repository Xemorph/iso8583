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
#include "ISOMessage.hh"    // ISOMessage, ISOOpaqueField, ISOBinaryField, …
#include "ISOSpec.hh"       // SpecDecoder::loadFromYaml()
#include "ISOLog.hh"        // setLevel(), setLogger(), ISOLogger
#include "ISOParser.hh"     // ISOParserPtrBase (für eigene Parser-Implementierungen)
