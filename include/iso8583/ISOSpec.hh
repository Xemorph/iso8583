#pragma once

/// @file ISOSpec.hh
/// @brief YAML-based specification loader for ISO 8583 parsers.
///
/// This is the **recommended entry point** for standard users.  It hides the
/// internal parser class hierarchy entirely and returns an opaque smart pointer
/// that can be passed directly to @ref tng::ISOMessage::parser.
///
/// @par Quick start
/// @code
///   #include <iso8583/ISOSpec.hh>
///   #include <iso8583/ISOMessage.hh>
///
///   // Load the spec once and cache the result (parsing YAML is expensive)
///   auto parser = tng::spec::SpecDecoder::loadFromYaml("mastercard.yml");
///
///   // Attach to a message and decode raw bytes
///   auto msg = std::make_shared<tng::ISOMessage>();
///   msg->parser(parser);
///
///   std::vector<uint8_t> raw = receive_from_network();
///   msg->unparse(msg, raw);
///
///   // Access fields
///   if (auto pan = msg->tryGet<ISOOpaqueField>(2))
///       use_pan((*pan)->value());
/// @endcode
///
/// @par YAML spec format
///
/// The YAML file describes a single ISO 8583 message specification.
/// See `examples/encoding_example.yml` for a complete annotated example.
///
/// **Top-level keys:**
/// ```yaml
/// spec:     "My Spec Name"        # human-readable label
/// encoding: ebcdic                # global encoding: ascii | bcd | ebcdic | binary
///
/// definitions:                    # reusable field snippets
///   pan_field:
///     type: scalar
///     format: llchar
///     length: 19
///
/// fields:
///   "000":                        # MTI – always key 000
///     type: scalar
///     format: numeric
///     length: 4
///   "001":                        # Primary bitmap – always key 001
///     type: scalar
///     format: bitmap
///     length: 8
///   "002": !use pan_field         # reuse definition
///   "003":
///     type: nested
///     format: binary
///     length: 6
///     children:
///       - format: numeric
///         length: 2
///         description: "Transaction Type"
/// ```
///
/// **Supported directives:**
///
/// | Directive              | Description                                                  |
/// |------------------------|--------------------------------------------------------------|
/// | `!include_files [...]` | Load external YAML files and merge their `definitions`.      |
/// | `!use <name>`          | Substitute a named definition from `definitions`.            |
/// | `!template P(F, N)`    | Shorthand for variable-length fields, e.g. `LL(CHAR, 19)`.  |
/// | `!merge [...]`         | Merge multiple maps into one (later entries overwrite).      |
///
/// **Supported formats:** `numeric`, `char`, `binary`, `bitmap`, `nop`,
/// `llchar`, `lllchar`, `llbinary`, `lllbinary`, `remaining`, …
///
/// **Supported encodings:** `ascii`, `bcd`, `ebcdic`, `binary`
///
/// @note `!include` is a deprecated alias for `!use` (emits a warning).

// [tng]
#include "config.h"
#include "ISOParser.hh"
// [stdc++]
#include <memory>
#include <string>

namespace TNG_NAMESPACE {
    namespace spec {

        /// @brief Loads an ISO 8583 parser configuration from a YAML file.
        ///
        /// The returned parser can be shared across multiple `ISOMessage`
        /// instances.  Building the parser from YAML involves file I/O, YAML
        /// preprocessing and spec validation – cache the result.
        class TNG_EXPORT SpecDecoder {
        public:
            /// @brief Parses a YAML spec file and returns a configured parser.
            ///
            /// Supported YAML features:
            /// 
            ///   - `!include_files` – import external definition files
            /// 
            ///   - `!use`           – reference a named definition
            /// 
            ///   - `!template`      – variable-length field shorthand
            /// 
            ///   - `!merge`         – combine multiple field maps
            ///
            /// @param path  Path to the root YAML spec file.
            /// @return      Opaque smart pointer to the configured parser.
            ///              Pass directly to @ref ISOMessage::parser.
            /// @throws std::runtime_error  If the file is not found, cannot be
            ///                             read, contains invalid YAML syntax,
            ///                             or references an unknown
            ///                             format/encoding combination.
            static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
                loadFromYaml(const std::string& /*path*/ );
        };

    }
}
