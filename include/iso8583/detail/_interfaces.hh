#pragma once

// [tng]
#include "../config.h"
// [stdc++]
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
// [nonstd]
#include "extern/string_view.hpp"
// [nlohmann/json]
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace TNG_NAMESPACE {

    /// @brief Abstract base class for all ISO 8583 field components.
    ///
    /// Every data element (DE) held inside an @ref ISOMessage derives from this
    /// class. Concrete types are:
    ///   - `ISOOpaqueField`   – string-valued fields (alphanumeric)
    ///   - `ISOBinaryField`   – binary fields (`std::vector<uint8_t>`)
    ///   - `ISOBitmap`        – bitmap field (`dynamic_bitset<>`)
    ///   - `ISOCodeField`     – integer code fields (`int32_t`)
    ///   - `ISOMessage`       – composite / nested message (is_composite() == true)
    class TNG_EXPORT ISOComponentPtrBase {
    public:
        /// @brief Shared-pointer alias used throughout the library.
        using ISOComponentPtrBaseSmartPtr = std::shared_ptr<ISOComponentPtrBase>;

        // [Destructor]
        virtual ~ISOComponentPtrBase() = default;

        /// @brief Returns the numeric key (DE number) of this component.
        ///
        /// The key is an `int16_t` (`TNG_KEY_TYPE`).  Special value `-1` is
        /// used for the root `ISOMessage` that is not itself a sub-field.
        virtual TNG_KEY_TYPE key() const = 0;

        /// @brief Byte offset of this field inside the original wire buffer.
        ///
        /// Set by the parser after a successful @ref ISOMessage::unparse call.
        /// A value of `0` means "not set" (the MTI field also starts at 0).
        /// Use `std::numeric_limits<std::size_t>::max()` to signal "unknown".
        virtual std::size_t wire_offset() const noexcept = 0;

        /// @brief Sets the byte offset inside the wire buffer.
        /// @param offset Absolute byte position in the original network buffer.
        virtual void wire_offset(std::size_t offset) noexcept = 0;

        /// @brief Number of bytes this field occupies in the wire buffer,
        ///        **including** any length-prefix bytes.
        ///
        /// Together with @ref wire_offset, the raw bytes of any field can be
        /// located precisely inside the original buffer.
        virtual std::size_t wire_length() const noexcept = 0;

        /// @brief Sets the wire length (including prefix).
        /// @param length Number of bytes consumed in the network buffer.
        virtual void wire_length(std::size_t length) noexcept = 0;

        /// @brief Returns a human-readable string representation of the value.
        ///
        /// For `ISOOpaqueField` this is the raw string. For `ISOBinaryField`
        /// it is an uppercase hex string. For `ISOMessage` (composite) it
        /// returns an empty string – iterate the child fields instead.
        virtual std::string readable_value() const = 0;

        /// @brief Sets the human-readable description (field name from the spec).
        /// @param desc E.g. `"Primary Account Number"`.
        virtual void description(const nonstd::string_view& desc) = 0;

        /// @brief Returns the field description set by the parser or the user.
        virtual const nonstd::string_view description() const = 0;

        /// @brief Sets an optional explanation string (e.g. decoded meaning of a code).
        virtual void explanation(const nonstd::string_view& expl) = 0;

        /// @brief Returns the optional explanation string.
        virtual const nonstd::string_view explanation() const = 0;

        /// @brief Returns `true` if this component is a composite (ISOMessage).
        ///
        /// Composite components hold child components and have no direct value.
        /// Leaf components (ISOOpaqueField, ISOBinaryField, …) return `false`.
        virtual bool is_composite() const = 0;

        /// @brief Prints a debug representation to stdout.
        /// @param nested `true` when called recursively for a sub-field.
        virtual void print(bool nested) const = 0;

        /// @brief Serialises the component to a JSON object.
        ///
        /// The returned JSON always contains `"key"` and `"value"`.
        /// When wire position tracking is active it also contains
        /// `"wire_offset"` and `"wire_length"`.
        virtual json to_json() const = 0;
    };

    /// @brief Classifies the data type handled by an @ref ISOFieldParserPtrBase.
    ///
    /// Used by @ref ISOMessage::set(key, data) to decide which concrete
    /// `ISOComponent` subtype to create, and to reject invalid operations
    /// (e.g. manually setting a BITMAP or NESTED field).
    enum class ISOFieldParserType {
        UNUSED,      ///< Placeholder / skip field (no data consumed).
        EXCEPTIONAL, ///< Reserved / error sentinel.
        OPAQUE,      ///< String field → `ISOOpaqueField`.
        BINARY,      ///< Raw binary field → `ISOBinaryField` (hex string input).
        BITMAP,      ///< Bitmap – computed automatically, cannot be set manually.
        NESTED,      ///< Composite sub-message – use dot-notation set() to populate.
        REMAINING,   ///< Consumes all remaining bytes of the parent buffer (no prefix).
    };

    /// @brief Abstract base class for message-level parsers.
    ///
    /// A parser knows the full layout of a message type (MTI, bitmap, all DEs).
    /// Obtain a concrete instance via @ref tng::spec::SpecDecoder::loadFromYaml.
    ///
    /// @see ISOMessage::parser(ISOParserPtrBaseSmartPtr)
    /// @see ISOMessage::unparse
    class TNG_EXPORT ISOParserPtrBase
    {
    public:
        /// @brief Shared-pointer alias.
        using ISOParserPtrBaseSmartPtr = std::shared_ptr<ISOParserPtrBase>;

        // [Destructor]
        virtual ~ISOParserPtrBase() = default;


        // -------------------------
        // -          PSL          -
        // -------------------------


        /// @brief Returns `true` if this parser generates a bitmap.
        ///
        /// Most ISO 8583 parsers emit a bitmap.  Specialised sub-parsers for
        /// nested fields (e.g. DE048 sub-elements) typically do not.
        virtual bool emit_bitmap() const noexcept = 0;

        /// @brief Deserialises a full message from a wire buffer (top-level).
        ///
        /// Reads MTI, bitmap and all active DEs from `b` and populates `c`.
        /// @param c  Root `ISOMessage` that receives all decoded fields.
        /// @param b  Raw bytes received from the network.
        /// @return   Total number of bytes consumed.
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b) = 0;

        /// @brief Deserialises with an explicit base offset for wire-position tracking.
        ///
        /// Called recursively by the nested-field path so that child fields
        /// report their `wire_offset` relative to the **original** message
        /// buffer rather than the extracted sub-buffer.
        ///
        /// @param c           Target message / composite component.
        /// @param b           Sub-buffer containing only this field's payload.
        /// @param base_offset Absolute byte position of the parent field in the
        ///                    original network buffer.
        /// @return Number of bytes consumed from `b`.
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
                                    const std::vector<uint8_t>& b,
                                    std::size_t base_offset) {
            return unparse(c, b);  // Default: Offset wird ignoriert
        }

        /// @brief Serialises all fields of the component into a wire byte sequence.
        /// @param c Message whose fields should be encoded.
        /// @return  Encoded bytes ready for transmission.
        virtual std::vector<uint8_t> parse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr) const = 0;
    };

    // Forward decleration
    class TNG_EXPORT ISOFieldParserPtrBase
    {
    public:
        /// @brief Shared-pointer alias.
        using ISOFieldParserPtrBaseSmartPtr = std::shared_ptr<ISOFieldParserPtrBase>;

        // [Destructor]
        virtual ~ISOFieldParserPtrBase() = default;

        /// @brief Returns the human-readable name of the data element (DE).
        virtual nonstd::string_view description() const = 0;

        /// @brief Sets the human-readable name of the DE.
        virtual void description(const nonstd::string_view&) = 0;


        // -------------------------
        // -          PSL          -
        // -------------------------


        /// @brief Returns the parser type, determining which ISOComponent is created.
        virtual const TNG_NAMESPACE::ISOFieldParserType type() const = 0;

        /// @brief Returns the inner parser for NESTED fields, `nullptr` otherwise.
        ///
        /// Used by @ref ISOMessage::set(dot_key, data) to validate sub-field
        /// types without requiring a downcast to the concrete parser class.
        virtual ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
            subParser() const noexcept { return nullptr; }

        /// @brief Serialises the given component to a wire byte sequence.
        /// @param c Component whose value should be encoded.
        /// @return Encoded bytes ready for transmission.
        virtual std::vector<uint8_t> parse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr) const = 0;

        /// @brief Deserialises bytes from the wire buffer into the component.
        /// @param c   Target component that receives the decoded value.
        /// @param b   The full wire buffer.
        /// @param o   Byte offset inside `b` where this field starts.
        /// @return Number of bytes consumed from `b`.
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b, std::size_t o) const = 0;

        /// @brief Creates a fresh, empty component for this DE.
        /// @param key DE number to assign to the new component.
        /// @return Heap-allocated component of the appropriate subtype.
        virtual ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr create_component(TNG_KEY_TYPE) const = 0;
    };

    /// @brief Abstract base class for message headers prepended to the ISO 8583 payload.
    ///
    /// Concrete implementations: @ref tng::BaseHeader, @ref tng::BASE1Header.
    class TNG_EXPORT ISOHeader {
    public:
        /// @brief Shared-pointer alias.
        using ISOHeaderSmartPtr = std::shared_ptr<ISOHeader>;

        virtual ~ISOHeader() = default;

        /// @brief Encodes the header into a byte sequence for transmission.
        virtual std::vector<uint8_t> pack() const = 0;

        /// @brief Decodes the header from a raw byte buffer.
        /// @param b   Buffer that starts with the header bytes.
        /// @return    Number of bytes consumed.
        virtual std::size_t unpack(const std::vector<uint8_t>& b) = 0;

        /// @brief Sets the destination routing address.
        virtual void destination(const nonstd::string_view dst) = 0;

        /// @brief Returns the destination routing address, if present.
        virtual std::optional<nonstd::string_view> destination() const = 0;

        /// @brief Sets the source routing address.
        virtual void source(const nonstd::string_view src) = 0;

        /// @brief Returns the source routing address, if present.
        virtual std::optional<nonstd::string_view> source() const = 0;

        /// @brief Returns the size of the header in bytes.
        virtual std::size_t size() const = 0;

        /// @brief Swaps source and destination addresses (used when building responses).
        virtual void swapDirection() = 0;

        /// @brief Creates a deep copy of this header.
        virtual std::unique_ptr<ISOHeader> clone() const = 0;
    };
}
