#pragma once

/// @file ISOParser.hh
/// @brief Advanced parser access for experienced users.
///
/// @warning **Most users do not need this header.**
///          @ref ISOSpec.hh is sufficient for loading a spec and decoding messages.
///          Include this header only if you need to implement a custom parser.
///
/// Including this header gives access to:
///   - @ref tng::ISOParserPtrBase      – abstract base class for message parsers
///   - @ref tng::ISOFieldParserPtrBase – abstract base class for field parsers
///   - @ref tng::ISOFieldParserType    – enum of field types
///   - @ref tng::ISOHeader             – abstract header base class
///
/// The concrete implementations (`ISOBaseParser`, `ISOFieldParser<>`,
/// the `IFE_*` / `IFA_*` field-type aliases) live in `src/` and are
/// deliberately excluded from the public API.
///
/// @par Implementing a custom parser
///
/// Derive from @ref tng::ISOParserPtrBase and implement the three pure virtual
/// methods.  The parser can then be attached to an `ISOMessage` just like one
/// returned by @ref tng::spec::SpecDecoder::loadFromYaml.
///
/// @code
///   #include <iso8583/ISOParser.hh>
///   #include <iso8583/ISOMessage.hh>
///
///   class FixedLayoutParser : public tng::ISOParserPtrBase {
///   public:
///       bool emit_bitmap() noexcept override { return true; }
///
///       std::size_t unparse(
///           tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
///           const std::vector<uint8_t>& b) override
///       {
///           // Decode MTI, bitmap and all DEs from b into c.
///           // Use ISOMessage::set() to populate fields.
///           return b.size();
///       }
///
///       std::vector<uint8_t> parse(
///           tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) override
///       {
///           // Encode all fields in c into a wire byte sequence.
///           return {};
///       }
///   };
///
///   // Usage:
///   auto parser = std::make_shared<FixedLayoutParser>();
///   auto msg    = std::make_shared<tng::ISOMessage>();
///   msg->parser(parser);
///   msg->unparse(msg, rawBytes);
/// @endcode

#include "config.h"
#include "detail/_interfaces.hh"
