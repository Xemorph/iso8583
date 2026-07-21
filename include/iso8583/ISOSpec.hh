#pragma once

/// @file ISOSpec.hh
/// @brief YAML spec loader – parser and introspectable spec object.
///
/// @par Two ways to load a spec
///
/// @par Option 1 – Parser only (no introspection needed):
///
///     auto parser = iso8583::spec::SpecDecoder::loadFromYaml("mastercard.yml");
///     msg->parser(parser);
///
/// @par Option 2 – Parser AND introspectable spec (preferred):
///
///     auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mastercard.yml");
///     msg->parser(parser);
///
///     // Query field structure at runtime
///     if (auto pan = spec->field(2))
///         fmt::print("DE002: {} ({}LL prefix, max {} chars)\n",
///                    pan->description,
///                    pan->format.prefix_digits,
///                    pan->format.max_length);
///
///     for (const auto& f : spec->fields())
///         fmt::print("DE{:03d}: {}\n", f.key, f.description);

#include "config.h"
#include "detail/_interfaces.hh"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TNG_NAMESPACE {
    namespace spec {

        // ── SpecFieldFormat ───────────────────────────────────────────────────

        /// @brief Wire format description for a single data element.
        ///
        /// Splits the combined YAML format string (e.g. `"LLCHAR"`) into
        /// its constituent parts so callers do not need to parse strings.
        ///
        ///     SpecFieldFormat fmt = spec->field(2)->format;
        ///     fmt.type          // "CHAR"
        ///     fmt.prefix_digits // 2   (LL prefix)
        ///     fmt.max_length    // 19
        struct TNG_EXPORT SpecFieldFormat {
            /// @brief Base format type without length prefix letters.
            ///
            /// Possible values: `"CHAR"`, `"NUMERIC"`, `"BINARY"`,
            /// `"BITMAP"`, `"NOP"`, `"REMAINING"`.
            std::string type;

            /// @brief Maximum payload length in logical units.
            ///
            ///   - `CHAR` / `NUMERIC`: number of characters or digits
            ///   - `BINARY`:           number of bytes
            ///   - `BITMAP` / `NOP` / `REMAINING`: 0 (no meaningful length)
            int         max_length = 0;  ///< kept as int for API compatibility; always >= 0

            /// @brief Number of length-prefix digits (ISO 8583 L/LL/LLL convention).
            ///
            /// | Value | Prefix | ISO 8583 max |
            /// |-------|--------|--------------|
            /// | 0     | none   | fixed length |
            /// | 1     | L      | 9            |
            /// | 2     | LL     | 99           |
            /// | 3     | LLL    | 999          |
            /// | 4     | LLLL   | 9999         |
            int prefix_digits = 0;
        };

        // ── SpecFieldInfo ─────────────────────────────────────────────────────

        /// @brief Introspectable description of one data element (DE) or sub-field.
        ///
        /// Returned by @ref ISOSpec::field and iterated via @ref ISOSpec::fields.
        struct TNG_EXPORT SpecFieldInfo {
            /// @brief DE number – same key used in @ref iso8583::ISOMessage.
            TNG_KEY_TYPE key = 0;

            /// @brief Human-readable field name from the YAML `description:` key.
            std::string description;

            /// @brief Wire format: base type, length-prefix digit count and max length.
            SpecFieldFormat format;

            /// @brief Encoding used for this field.
            ///
            /// `"EBCDIC"` | `"ASCII"` | `"BCD"` | `"BINARY"` | `""` (encoding-neutral)
            ///
            /// An empty string means the format is encoding-neutral (e.g. raw `BINARY`).
            std::string encoding;

            /// @brief `true` when this field is a nested composite (has children).
            ///
            /// Nested fields are populated via dot-notation:
            ///     msg->set("61.1", "0");
            bool is_nested = false;

            /// @brief `true` when this field is the bitmap DE.
            bool is_bitmap = false;

            /// @brief Sub-field descriptions for nested DEs.
            ///
            /// Empty for leaf fields.  Indexed from 0 (sub-field keys inside
            /// the nested parser start at 0).
            std::vector<SpecFieldInfo> children;
        };

        // ── ISOSpec ───────────────────────────────────────────────────────────

        /// @brief Introspectable representation of a loaded ISO 8583 spec.
        ///
        /// Allows querying the structure of the spec at runtime without
        /// re-reading the YAML file.  Obtain via
        /// @ref SpecDecoder::loadBothFromYaml.
        ///
        ///     auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mc.yml");
        ///
        ///     // Check existence
        ///     spec->has(2);       // true if DE002 is defined
        ///
        ///     // Query a field
        ///     if (auto f = spec->field(2)) {
        ///         f->description;           // "Primary Account Number"
        ///         f->format.type;           // "CHAR"
        ///         f->format.prefix_digits;  // 2
        ///         f->format.max_length;     // 19
        ///     }
        ///
        ///     // Iterate all DEs
        ///     for (const auto& f : spec->fields())
        ///         fmt::print("DE{:03d}: {}\n", f.key, f.description);
        class TNG_EXPORT ISOSpec {
        public:
            using SmartPtr = std::shared_ptr<ISOSpec>;

            /// @brief Human-readable spec name from the YAML `spec:` key.
            std::string_view name() const noexcept { return name_; }

            /// @brief Global encoding from the YAML `encoding:` key.
            ///
            /// One of `"EBCDIC"`, `"ASCII"`, `"BCD"`, `"BINARY"`, or `""`
            /// if no global encoding was specified.
            std::string_view encoding() const noexcept { return encoding_; }

            /// @brief Returns field info for a DE key, or `nullopt` if not defined.
            /// @param key DE number to look up.
            std::optional<SpecFieldInfo> field(TNG_KEY_TYPE key) const;

            /// @brief Returns `true` if DE `key` is defined in this spec.
            bool has(TNG_KEY_TYPE key) const noexcept;

            /// @brief All defined fields in ascending key order.
            ///
            /// Includes MTI (key 0) and bitmap (key 1).
            const std::vector<SpecFieldInfo>& fields() const noexcept { return fields_; }

            /// @brief Internal constructor – use @ref SpecDecoder::loadBothFromYaml.
            ISOSpec(std::string name, std::string encoding,
                std::vector<SpecFieldInfo> fields)
                : name_(std::move(name))
                , encoding_(std::move(encoding))
                , fields_(std::move(fields))
            {
            }

        private:
            std::string              name_;
            std::string              encoding_;
            std::vector<SpecFieldInfo> fields_;
        };

        // ── SpecDecoder ───────────────────────────────────────────────────────

        /// @brief Loads an ISO 8583 parser configuration from a YAML spec file.
        ///
        /// @par YAML spec format summary
        ///
        /// @par Global keys:
        ///
        ///     spec:     "My Spec Name"
        ///     encoding: ebcdic        # ascii | bcd | ebcdic | binary
        ///
        /// @par Field definition:
        ///
        ///     fields:
        ///       "000": { type: scalar, format: numeric, length: 4, description: "MTI" }
        ///       "001": { type: scalar, format: bitmap,  length: 8 }
        ///       "002": !use pan_field           # reference a definition
        ///       "055":
        ///         !merge
        ///         - !template LLL(BINARY, 255)
        ///         - description: "ICC Data"
        ///
        /// @par Supported directives:
        ///   - `!include_files [a.yml]` – load external definition files
        ///   - `!use <name>`            – substitute a named definition
        ///   - `!template P(F, N)`      – variable-length shorthand (e.g. `LL(CHAR, 19)`)
        ///   - `!merge [...]`           – merge maps, later entries overwrite
        class TNG_EXPORT SpecDecoder {
        public:
            /// @brief Loads a parser from a YAML spec file.
            ///
            /// Use this when you only need parsing/building and no introspection.
            ///
            ///     auto parser = iso8583::spec::SpecDecoder::loadFromYaml("mc.yml");
            ///     msg->parser(parser);
            ///
            /// @param path Path to the root YAML spec file.
            /// @return Opaque smart pointer to the configured parser.
            /// @throws std::runtime_error on invalid YAML or unknown format/encoding.
            static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
                loadFromYaml(const std::string& path);

            /// @brief Loads both a parser and an introspectable spec object.
            ///
            /// Reads and preprocesses the YAML file **once** and builds both
            /// objects from the same in-memory representation.  Always prefer
            /// this over calling `loadFromYaml` + a separate spec builder.
            ///
            ///     auto [parser, spec] = iso8583::spec::SpecDecoder::loadBothFromYaml("mc.yml");
            ///     msg->parser(parser);
            ///
            ///     // Introspect the loaded spec
            ///     for (const auto& f : spec->fields())
            ///         fmt::print("DE{:03d}: {}\n", f.key, f.description);
            ///
            /// @param path Path to the root YAML spec file.
            /// @return Pair of `{parser, spec}`.
            /// @throws std::runtime_error on invalid YAML or unknown format/encoding.
            static std::pair<
                ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr,
                ISOSpec::SmartPtr>
                loadBothFromYaml(const std::string& path);
        };

    } // namespace spec
} // namespace TNG_NAMESPACE
