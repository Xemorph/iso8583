#pragma once

// [tng/config]
#include "../config.h"
// [stdc++]
#include <cassert>
#include <cinttypes>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
// [nlohmann/json]
#include <nlohmann/json.hpp>
// [tsl/robin_map]
#include <tsl/robin_map.h>

namespace TNG_NAMESPACE {
    namespace detail {
        /// @brief Internal hash-map alias.
        ///
        /// Isolates the rest of the library from the concrete hash-map
        /// implementation. Do **not** use `tsl::robin_map` directly.
        template <typename K, typename V>
        using flat_map = tsl::robin_map<K, V>;
    }
}
// [nonstd]
#include "extern/string_view.hpp"
// [tng]
#include "_interfaces.hh"

using json = nlohmann::json;
using namespace nonstd::literals;

namespace TNG_NAMESPACE {

    /// @brief Generic value-holding component that forms the leaf nodes of an ISOMessage.
    ///
    /// This template is not used directly.  Use the concrete typedefs instead:
    ///
    /// | Typedef             | T                         | Purpose                    |
    /// |---------------------|---------------------------|----------------------------|
    /// | `ISOOpaqueField`    | `std::string`             | Alphanumeric / EBCDIC / BCD text fields |
    /// | `ISOBinaryField`    | `std::vector<uint8_t>`    | Raw binary fields (e.g. PIN block, ICC data) |
    /// | `ISOBitmap`         | `dynamic_bitset<>`        | Primary / secondary bitmap  |
    /// | `ISOCodeField`      | `int32_t`                 | Integer code fields         |
    ///
    /// @tparam IntegerType  Must be an integer type no wider than `TNG_KEY_TYPE` (int16_t).
    /// @tparam T            Value type stored in this component.
    template < typename IntegerType, typename T >
    class TNG_EXPORT ISOComponent
        : public ISOComponentPtrBase
    {
        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be integral");
        static_assert(sizeof(IntegerType) <= sizeof(TNG_KEY_TYPE),
            "IntegerType must not be wider than TNG_KEY_TYPE (int16_t).");
    protected:
        IntegerType         k_;             ///< DE number (key).
        T                   d_;             ///< Stored value.
        nonstd::string_view desc_;          ///< Human-readable field name from the spec.
        nonstd::string_view expl_;          ///< Optional value explanation.
        std::size_t         wire_offset_ = 0; ///< Byte offset in the original wire buffer.
        std::size_t         wire_length_ = 0; ///< Bytes consumed (incl. length prefix).

    public:
        ~ISOComponent() = default;
        
        /// @brief Constructs a component with the given DE number and a default-initialised value.
        explicit ISOComponent(const IntegerType& key);

        /// @brief Constructs a component with a DE number and an initial value.
        ISOComponent(const IntegerType& key, const T& data);

        /// @brief Sets the DE number.
        void key(IntegerType k);

        /// @brief Returns the DE number as `TNG_KEY_TYPE` (int16_t).
        TNG_KEY_TYPE key() const override;

        /// @brief Sets the stored value by copy.
        void value(const T& d);

        /// @brief Returns a const reference to the stored value (zero-copy read).
        const T& value() const;

        /// @brief Returns a mutable reference to the stored value.
        T& value();

        /// @brief Returns a human-readable string of the value.
        ///
        /// - `ISOOpaqueField`  → the raw string
        /// - `ISOBinaryField`  → uppercase hex string (e.g. `"DEADBEEF"`)
        /// - `ISOBitmap`       → binary bit string
        std::string readable_value() const override;

        void description(const nonstd::string_view& desc) override;
        const nonstd::string_view description() const override;

        void explanation(const nonstd::string_view& expl) override;
        const nonstd::string_view explanation() const override;

        std::size_t wire_offset() const noexcept override { return wire_offset_; }
        void        wire_offset(std::size_t offset) noexcept override { wire_offset_ = offset; }
        std::size_t wire_length() const noexcept override { return wire_length_; }
        void        wire_length(std::size_t length) noexcept override { wire_length_ = length; }

        /// @brief Serialises this component to JSON.
        ///
        /// Always includes `"key"` and `"value"`.
        /// Includes `"wire_offset"` and `"wire_length"` when `wire_length_ > 0`.
        virtual json to_json() const override;

        /// @brief Returns `false` – leaf components are not composites.
        virtual bool is_composite() const override;

        virtual void print(bool nested) const override;
    };

    class TNG_EXPORT ISOTaggedField final
        : public ISOComponent<TNG_KEY_TYPE, std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase > >
    {
    private:
        std::string tag_;
        std::shared_ptr<::TNG_NAMESPACE::ISOComponentPtrBase> delegate_;
    public:
        // [Destructor]
        ~ISOTaggedField() = default;
        // [Constructor]
        ISOTaggedField(nonstd::string_view tag, std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase > delegate)
            : tag_{ tag }, ISOComponent(delegate->key())
        {
            delegate_ = std::move(delegate);
        }
    };
}

// ---------------------------------------------------------------------------
// Concrete field typedefs
// ---------------------------------------------------------------------------

/// @brief Container map type: `int16_t` → `shared_ptr<ISOComponentPtrBase>`.
typedef TNG_NAMESPACE::detail::flat_map<
    TNG_KEY_TYPE,
    std::shared_ptr<::TNG_NAMESPACE::ISOComponentPtrBase>
> ISO_MAP;

/// @brief Alphanumeric / text data element (EBCDIC, ASCII, BCD decoded to string).
typedef TNG_NAMESPACE::ISOComponent<TNG_KEY_TYPE, std::string> ISOOpaqueField;

/// @brief Raw binary data element (e.g. PIN block, ICC/EMV data, cryptograms).
typedef TNG_NAMESPACE::ISOComponent<TNG_KEY_TYPE, std::vector<uint8_t>> ISOBinaryField;

/// @brief Fast binary data element using `std::byte` storage.
typedef TNG_NAMESPACE::ISOComponent<TNG_KEY_TYPE, std::vector<std::byte>> ISOFastBinaryField;

/// @brief Bitmap data element (primary + optional secondary bitmap).
typedef TNG_NAMESPACE::ISOComponent<TNG_KEY_TYPE, dynamic_bitset<>> ISOBitmap;

/// @brief Integer code data element (e.g. response codes stored as int32_t).
typedef TNG_NAMESPACE::ISOComponent<TNG_KEY_TYPE, int32_t> ISOCodeField;

// Without this, GCC/Clang with -fvisibility=hidden emits local hidden-
// visibility copies in each TU, causing undefined-reference link errors
// when the tests try to resolve symbols from the shared library.
extern template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::string >;
extern template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::vector<uint8_t> >;
extern template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, dynamic_bitset<> >;

namespace TNG_NAMESPACE {

    /// @brief An ISO 8583 message – the central object of the library.
    ///
    /// `ISOMessage` is simultaneously a composite component and the top-level
    /// container for a parsed ISO 8583 network message.  It holds an ordered
    /// map of child components (DEs) and exposes thread-safe read/write access.
    ///
    /// @par Typical workflow
    ///
    /// **Receiving a message (unparse = wire bytes → fields):**
    /// @code
    ///   // 1. Load the spec once (expensive – cache the result)
    ///   auto parser = tng::spec::SpecDecoder::loadFromYaml("mastercard.yml");
    ///
    ///   // 2. Create a message and attach the parser
    ///   auto msg = std::make_shared<tng::ISOMessage>();
    ///   msg->parser(parser);
    ///
    ///   // 3. Decode the wire bytes
    ///   std::vector<uint8_t> raw = ...; // bytes from the network socket
    ///   msg->unparse(msg, raw);
    ///
    ///   // 4. Read fields
    ///   if (auto pan = msg->tryGet<ISOOpaqueField>(2))
    ///       std::cout << "PAN: " << (*pan)->value() << "\n";
    ///
    ///   auto amount = tng::ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 4, "000000000000");
    /// @endcode
    ///
    /// **Building a message (parse = fields → wire bytes):**
    /// @code
    ///   auto msg = std::make_shared<tng::ISOMessage>("0200");
    ///   msg->parser(parser);
    ///   msg->set(2, "4111111111111111");        // PAN
    ///   msg->set(3, "000000");                  // Processing code
    ///   msg->set(4, "000000010000");            // Amount
    ///   msg->set("48.72.1", "ABC");             // Nested: DE48 → SE72 → tag 1
    ///
    ///   std::vector<uint8_t> wireBytes = parser->parse(msg);
    /// @endcode
    ///
    /// @par Thread safety
    ///
    /// All `set`, `unset`, `reset` operations acquire an exclusive write lock.
    /// All `get`, `tryGet`, `has`, `size` operations acquire a shared read lock.
    /// Concurrent reads from multiple threads are safe.
    class TNG_EXPORT ISOMessage final
        : public ISOComponent<TNG_KEY_TYPE, ISO_MAP>
    {
    public:
        /// @brief Message direction used for logging and routing.
        enum class Direction {
            UNKNOWN = 0, ///< Not yet determined.
            INCOMING = 1, ///< Received from the network.
            OUTGOING = 2, ///< To be sent over the network.
        };

    private:
        ISO_MAP::key_type   hf_;        ///< Highest DE number currently set.
        bool                recalc_;    ///< Bitmap recalculation required flag.
        std::shared_mutex   d_lock_;    ///< Guards d_ (the field map).
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr p_ = nullptr;
        Direction           dir_ = Direction::UNKNOWN;
        ISOHeader::ISOHeaderSmartPtr hdr_;

    public:
        using ISOMessageSmartPtr = std::shared_ptr<ISOMessage>;
        using ISO_MAP_ITERATOR = ISO_MAP::iterator;
        using ISO_MAP_CONST_ITERATOR = ISO_MAP::const_iterator;

        // [Destructor]
        ~ISOMessage() = default;

        /// @brief Creates an empty root message (no MTI, no fields).
        ISOMessage();

        /// @brief Creates a nested / sub-message with the given DE key.
        ///
        /// Used internally when building nested structures via dot-notation set().
        /// @param key DE number of this sub-message in the parent ISOMessage.
        explicit ISOMessage(const ISO_MAP::key_type& key);

        /// @brief Creates a root message with the given Message Type Indicator.
        ///
        /// @param mti Four-digit MTI string, e.g. `"0200"` (financial request).
        explicit ISOMessage(nonstd::string_view mti);

        std::string readable_value() const override;
        bool is_composite() const override;
        virtual void print(bool nested) const override;
        virtual json to_json() const override {
            json j;
            if (k_ == -1)
                j["direction"] = (dir_ == Direction::INCOMING) ? "INCOMING" : "OUTGOING";
            else
                j["key"] = k_;

            // Wire-Position mitausgeben (hilfreich für Debugging und Protokoll-Analyse)
            if (wire_length_ > 0) {
                j["wire_offset"] = wire_offset_;
                j["wire_length"] = wire_length_;
            }

            // Konvertiere das ISO_MAP (Datenfelder) in JSON
            json fields_json = json::array();
            for (const auto& pair : d_) {
                fields_json.push_back(pair.second->to_json());
            }

            j["fields"] = fields_json;
            return j;
        }

        // ── Field map access ────────────────────────────────────────────────

        /// @brief Returns `true` if DE `key` is present in this message.
        /// @param key  DE number to check.
        bool has(const ISO_MAP::key_type& key);

        /// @brief Inserts or replaces a field component.
        ///
        /// Uses `insert_or_assign` so an existing entry with the same key is
        /// silently overwritten. Logs and returns `false` on `std::bad_alloc`
        /// or if `component` is `nullptr`.
        ///
        /// @param component Fully constructed field component.
        /// @return `true` on success.
        bool set(ISO_MAP::mapped_type component);

        /// @brief Sets a field by numeric key and string value.
        ///
        /// If no parser is attached the value is stored as an `ISOOpaqueField`.
        /// If a parser is attached the concrete field type is determined from
        /// the parser's @ref ISOFieldParserType:
        ///   - `OPAQUE` / `EXCEPTIONAL` / `REMAINING` / `UNUSED` → `ISOOpaqueField`
        ///   - `BINARY`   → `ISOBinaryField` (input `data` must be an uppercase hex string)
        ///   - `BITMAP`   → **error** (bitmap is computed automatically)
        ///   - `NESTED`   → **error** (use dot-notation overload instead)
        ///
        /// @param key  DE number.
        /// @param data String value.  For `BINARY` fields: uppercase hex string, e.g. `"DEADBEEF"`.
        /// @return `true` on success, `false` on type mismatch or bad hex input.
        bool set(const ISO_MAP::key_type& key, std::string data);

        /// @brief Sets a field by numeric key and string_view value.
        ///
        /// Convenience overload – constructs a `std::string` and delegates to
        /// @ref set(const ISO_MAP::key_type&, std::string).
        bool set(const ISO_MAP::key_type& key, std::string_view data);

        /// @brief Sets a field using dot-notation to address nested sub-fields.
        ///
        /// The dot-notation key is a `.`-separated list of DE numbers.
        /// Each segment addresses one nesting level.
        ///
        /// Examples:
        /// @code
        ///   msg.set("3.1", "00");      // DE3, sub-field 1
        ///   msg.set("48.72.1", "ABC"); // DE48 → SE72 → tag 1 (three levels)
        /// @endcode
        ///
        /// **Rules:**
        /// - If a parser is set, the addressed key must have `ISOFieldParserType::NESTED`;
        ///   otherwise the call fails and returns `false`.
        /// - Missing intermediate `ISOMessage` containers are created automatically.
        /// - The leaf value is created according to the same type-selection logic
        ///   as @ref set(const ISO_MAP::key_type&, std::string).
        ///
        /// @param dot_key  Dot-separated DE numbers, e.g. `"48.72.1"`.
        /// @param data     String value for the leaf field.
        /// @return `true` on success.
        bool set(const std::string& dot_key, std::string data);

        /// @brief Dot-notation overload accepting `std::string_view`.
        bool set(const std::string& dot_key, std::string_view data);

        /// @brief Removes DE `key` from this message.
        ///
        /// @param key DE number to remove.
        /// @return `true` if the key existed and was removed, `false` if not found.
        bool unset(const ISO_MAP::key_type& key);

        /// @brief Returns the field component for the given key, or `nullptr`.
        ///
        /// The optional template parameter `T` performs a `dynamic_pointer_cast`
        /// so the caller receives the concrete type directly.
        ///
        /// @code
        ///   auto pan  = msg->get<ISOOpaqueField>(2);   // returns shared_ptr<ISOOpaqueField>
        ///   auto data = msg->get<ISOBinaryField>(55);  // returns shared_ptr<ISOBinaryField>
        /// @endcode
        ///
        /// @tparam T   Concrete component type (default: `ISOComponentPtrBase`).
        /// @param  key DE number to look up.
        /// @return Typed `shared_ptr`, or `nullptr` if not found or wrong type.
        template< typename T = ::TNG_NAMESPACE::ISOComponentPtrBase >
        std::shared_ptr< T > get(const ISO_MAP::key_type& key) {
            // Guarded by std::mutex -> shared read
            std::shared_lock<std::shared_mutex> w_lock(d_lock_);
            // Try to find element
            ISO_MAP_ITERATOR itr = d_.find(key);
            if (itr == d_.end())
                return nullptr;
            return std::dynamic_pointer_cast<T>(itr->second);
        }

        /// @brief Returns the field component wrapped in `std::optional`.
        ///
        /// Returns `std::nullopt` when the key is absent **or** when the cast
        /// to `T` fails (wrong field type).  Prefer this over @ref get when the
        /// field is optional in the message flow.
        ///
        /// @code
        ///   if (auto track2 = msg->tryGet<ISOOpaqueField>(35))
        ///       process((*track2)->value());
        /// @endcode
        ///
        /// @tparam T   Concrete component type.
        /// @param  key DE number.
        /// @return `std::optional<shared_ptr<T>>`.
        template< typename T = ::TNG_NAMESPACE::ISOComponentPtrBase >
        std::optional< std::shared_ptr< T > > tryGet(const ISO_MAP::key_type& key) {
            std::shared_lock<std::shared_mutex> lock(d_lock_);
            auto itr = d_.find(key);
            if (itr == d_.end())
                return std::nullopt;

            auto casted = std::dynamic_pointer_cast<T>(itr->second);
            if (!casted)
                return std::nullopt;

            return casted;
        }

        /// @brief Returns a copy of the field's value, or `std::nullopt`.
        ///
        /// Convenience wrapper around @ref tryGet that directly returns the
        /// **value** rather than the component pointer.
        ///
        /// @code
        ///   auto stan = msg->tryGetValue<ISOOpaqueField>(11);
        ///   if (stan) std::cout << "STAN: " << *stan << "\n";
        /// @endcode
        ///
        /// @tparam T   Concrete component type.
        /// @param  key DE number.
        /// @return `std::optional<ValueType>`.  `ValueType` is deduced from `T::value()`.
        template<typename T = ::TNG_NAMESPACE::ISOComponentPtrBase >
        auto tryGetValue(const ISO_MAP::key_type& key)
            -> std::optional< std::decay_t< decltype(std::declval< T >().value()) > >
        {
            std::shared_lock<std::shared_mutex> lock(d_lock_);
            auto itr = d_.find(key);
            if (itr == d_.end())
                return std::nullopt;

            auto comp = std::dynamic_pointer_cast<T>(itr->second);
            if (!comp)
                return std::nullopt;

            return comp->value();
        }

        /// @brief Returns a `const` reference to the field's value via `std::reference_wrapper`.
        ///
        /// Use this overload when you need a **zero-copy** read of a large value
        /// (e.g. `std::vector<uint8_t>` for binary fields) and can guarantee that
        /// the message is not modified during use.
        ///
        /// @warning The returned reference is invalidated if the field is removed
        ///          or the message is destroyed.
        ///
        /// @tparam T   Concrete component type.
        /// @param  key DE number.
        /// @return `std::optional<std::reference_wrapper<const ValueType>>`.
        template<typename T = ::TNG_NAMESPACE::ISOComponentPtrBase >
        auto tryGetValueRef(const ISO_MAP::key_type& key)
            -> std::optional< std::reference_wrapper< const std::decay_t< decltype(std::declval<T>().value()) > > >
        {
            std::shared_lock<std::shared_mutex> lock(d_lock_);
            auto itr = d_.find(key);
            if (itr == d_.end())
                return std::nullopt;

            auto comp = std::dynamic_pointer_cast<T>(itr->second);
            if (!comp)
                return std::nullopt;

            return std::cref(comp->value());
        }

        /// @brief Removes all fields from this message.
        void reset(void);

        /// @brief Returns the number of fields currently stored.
        const std::size_t size() const noexcept;
    
        // ── Parser attachment ────────────────────────────────────────────────

        /// @brief Attaches a parser to this message.
        ///
        /// The parser is used by @ref unparse to decode wire bytes and by the
        /// type-aware @ref set overloads to select the correct field type.
        ///
        /// @param p Parser obtained from @ref tng::spec::SpecDecoder::loadFromYaml.
        void parser(const ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p);

        /// @brief Returns the currently attached parser, or `nullptr`.
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser() const;

        /// @brief Decodes a raw wire buffer into this message's field map.
        ///
        /// Requires a parser to be set via @ref parser(ISOParserPtrBaseSmartPtr).
        /// Reads MTI, bitmap and all active DEs into `c`.
        ///
        /// @param c   This message itself (`msg->unparse(msg, raw)`).
        /// @param b   Raw bytes from the network.
        /// @return    Number of bytes consumed, or `SIZE_MAX` if no parser is set.
        std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b);

        /// @brief Returns `true` if this is a nested sub-message (key != -1).
        bool isInner() const;

        // ── Header ───────────────────────────────────────────────────────────

        /// @brief Attaches a raw-bytes header (e.g. Visa BASE1).
        void header(const std::vector<uint8_t>& b);

        /// @brief Attaches a typed header object
        void header(::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr& hdr);

        /// @brief Returns the header as raw bytes.
        std::vector<uint8_t> header() const;

        /// @brief Returns the typed header object (may be `nullptr`).
        ::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr header();

        // ── Direction ────────────────────────────────────────────────────────

        /// @brief Sets the message direction (INCOMING / OUTGOING).
        void direction(Direction a);

        /// @brief Returns the message direction.
        Direction direction();

        // ── MTI classification ───────────────────────────────────────────────

        /// @brief Returns `true` if a Message Type Indicator (MTI) field is present.
        bool hasMTI();

        /// @brief Returns the four-digit MTI string (e.g. `"0200"`).
        /// @throws std::logic_error if no MTI field is set.
        nonstd::string_view mti();

        /// @brief Returns `true` if the 3rd MTI digit indicates a request (`0` or `2`).
        bool isRequest();

        /// @brief Returns `true` if the 3rd MTI digit indicates a response (`1` or `3`).
        bool isResponse();

        /// @brief Returns `true` if the 2nd MTI digit is `1` (Authorization).
        bool isAuthorization();

        /// @brief Returns `true` if the 2nd MTI digit is `2` (Financial).
        bool isFinancial();

        /// @brief Returns `true` if the 2nd MTI digit is `3` (File Action).
        bool isFileAction();

        /// @brief Returns `true` if the 2nd MTI digit is `4` (Reversal).
        bool isReversal();

        /// @brief Returns `true` if the 2nd MTI digit is `4` and 3rd is `2`/`3` (Chargeback).
        bool isChargeback();

        /// @brief Returns `true` if the 2nd MTI digit is `5` (Reconciliation).
        bool isReconciliation();

        /// @brief Returns `true` if the 2nd MTI digit is `6` (Administrative).
        bool isAdministrative();

        /// @brief Returns `true` if the 2nd MTI digit is `7` (Fee Collection).
        bool isFeeCollection();

        /// @brief Returns `true` if the 2nd MTI digit is `8` (Network Management).
        bool isNetworkManagement();

        /// @brief Returns `true` if the 4th MTI digit is `1` (Retransmission).
        bool isRetransmission();

    private:
        bool set_recursive(const TNG_KEY_TYPE* keys, std::size_t depth, std::string data);
    };

    // ── Header implementations ──────────────────────────────────────────────

    /// @brief Minimal passthrough header that stores raw bytes without interpretation.
    ///
    /// Suitable for proprietary headers where only the raw byte sequence matters.
    class TNG_EXPORT BaseHeader : public ::TNG_NAMESPACE::ISOHeader {
    protected:
        std::vector<uint8_t> header;
        bool asciiEncoding = false;

    public:
        BaseHeader() = default;

        /// @brief Constructs from raw bytes (calls unpack internally).
        explicit BaseHeader(const std::vector<uint8_t>& header) { unpack(header); }

        std::vector<uint8_t> pack() const override { return header; }
        std::size_t unpack(const std::vector<uint8_t>& b) override { header = b; return b.size(); }
        std::size_t size() const override { return header.size(); }
        void destination(const nonstd::string_view) override {}
        void source(const nonstd::string_view) override {}
        std::optional<nonstd::string_view> destination() const override { return std::nullopt; }
        std::optional<nonstd::string_view> source() const override { return std::nullopt; }
        void swapDirection() override {}
        std::unique_ptr<ISOHeader> clone() const override {
            auto h = std::make_unique<BaseHeader>();
            h->header = this->header;
            h->asciiEncoding = this->asciiEncoding;
            return h;
        }

        /// @brief Enables ASCII encoding mode for this header.
        void setAsciiEncoding(bool ascii) { asciiEncoding = ascii; }

        /// @brief Returns `true` if ASCII encoding is active.
        bool isAsciiEncoding() const { return asciiEncoding; }
    };

    /// @brief Visa / BASE1 network header (22 bytes, optional 4-byte reject extension).
    ///
    /// Layout (offsets from byte 0):
    /// | Byte | Field                         |
    /// |------|-------------------------------|
    /// |  0   | Header length                 |
    /// |  1   | Header format (8-bit flags)   |
    /// |  2   | Text format                   |
    /// | 3–4  | Total message length          |
    /// | 5–7  | Destination ID (6N BCD)       |
    /// | 8–10 | Source ID (6N BCD)            |
    /// | 11   | Round-trip control info       |
    /// | 12–13| BASE1 flags                   |
    /// | 14–16| Message status flags          |
    /// | 17   | Batch number                  |
    /// | 18–20| Reserved                      |
    /// | 21   | User info                     |
    ///
    /// Reject extension (present only in reject messages):
    /// | Byte | Field                         |
    /// |------|-------------------------------|
    /// | 22–23| Bitmap (2 bytes)              |
    /// | 24–25| Reject data group (2 bytes)   |
    class TNG_EXPORT BASE1Header final : public ::TNG_NAMESPACE::BaseHeader {
    public:
        static constexpr size_t LENGTH = 22; ///< Standard header size in bytes.

        BASE1Header();
        BASE1Header(nonstd::string_view _source, nonstd::string_view _destination);
        BASE1Header(nonstd::string_view _source, nonstd::string_view _destination, int _format);

        /// @brief Constructs from raw bytes (calls unpack internally).
        explicit BASE1Header(const std::vector<uint8_t>& header);

        /// @brief Returns the header length field value.
        int getHLen() const;

        /// @brief Sets the header format field (8-bit flags word).
        void setHFormat(int hformat);

        /// @brief Returns the text format byte.
        int format() const;

        /// @brief Sets the text format byte.
        void format(int format);

        void setRtCtl(int i);
        void setFlags(int i);
        void setStatus(int i);
        void setBatchNumber(int i);

        /// @brief Updates the total message length field (bytes 3–4).
        void setLen(std::size_t len);

        void destination(nonstd::string_view dest) override;
        void source(nonstd::string_view src) override;
        std::optional<nonstd::string_view> source() const override;
        std::optional<nonstd::string_view> destination() const override;

        /// @brief Swaps source and destination IDs (used when building responses).
        void swapDirection() override;

        /// @brief Returns `true` if this is a reject message (reject extension present).
        bool isRejected() const;

        /// @brief Returns the reject reason code string (from reject extension).
        std::string getRejectCode() const;

        friend std::ostream& operator<<(std::ostream& os, const BASE1Header& hdr);
    };

    /// @brief WLP Financial Operations (FO) header.
    ///
    /// Fixed-length header used in the WLP (Worldline Payment) gateway protocol.
    /// Total size: 93 bytes.
    class TNG_EXPORT WLP_FOHeader final : public ::TNG_NAMESPACE::BaseHeader {
    public:
        static constexpr size_t LENGTH = 4 + 10 + 10 + 4 + 26 + 1 + 20 + 16 + 2;

        WLP_FOHeader();
        WLP_FOHeader(nonstd::string_view record, nonstd::string_view mti,
            int version, nonstd::string_view uuid,
            nonstd::string_view reference, int payment);
        WLP_FOHeader(nonstd::string_view record, nonstd::string_view mti,
            int version, nonstd::string_view uuid,
            nonstd::string_view reference, nonstd::string_view payment);
        explicit WLP_FOHeader(const std::vector<uint8_t>& header);

        /// @brief Encodes the header fields into EBCDIC and returns the byte sequence.
        std::vector<uint8_t> pack() const override;

        /// @brief Decodes EBCDIC bytes into the header fields.
        std::size_t unpack(const std::vector<uint8_t>& b) override;

        void length(int len);
        int  length() const;
        void sysId(nonstd::string_view s);
        void record(nonstd::string_view s);
        void mti(nonstd::string_view s);
        void creationTs();
        void creationTs(nonstd::string_view s);
        void version(int v);
        int  version() const;
        void uuid(nonstd::string_view s);
        void reference(nonstd::string_view s);
        void payment(size_t payment);
        void payment(nonstd::string_view s);
        std::unique_ptr<ISOHeader> clone() const override;
    };

    // ── ISOUtils ────────────────────────────────────────────────────────────

    /// @brief Utility functions for working with `ISOMessage` objects.
    namespace ISOUtils {

        /// @brief Flattens all leaf fields of a message into a `string → string` map.
        ///
        /// Keys use dot-notation (`"2"`, `"48.72.1"`).  Values are
        /// `readable_value()` strings.  Composite fields (nested messages) are
        /// not included; only their leaf children appear.
        ///
        /// @param msg  The message to flatten.
        /// @return     Map from dot-notation DE key to string value.
        TNG_EXPORT std::map<std::string, std::string> flatten(const ISOMessage& msg);

        /// @brief Returns the field value or throws if the field is absent or wrong type.
        ///
        /// Use when the field **must** be present according to the spec.
        ///
        /// @code
        ///   std::string pan = tng::ISOUtils::getOrThrow<ISOOpaqueField>(msg, 2);
        /// @endcode
        ///
        /// @tparam T   Concrete field type.
        /// @param  msg Reference to the message.
        /// @param  key DE number.
        /// @return     The field's value (by copy).
        /// @throws std::out_of_range if the field is missing or has the wrong type.
        template <typename T>
        auto getOrThrow(ISOMessage& msg, TNG_KEY_TYPE key)
            -> decltype(std::declval<T>().value())
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt)
                throw std::out_of_range(
                    "DE" + std::to_string(key) + " missing or wrong type");
            return (*opt)->value();
        }

        /// @brief Returns the field value or a caller-supplied default.
        ///
        /// Use for optional fields where a sensible default exists.
        ///
        /// @code
        ///   auto currency = tng::ISOUtils::getOrDefault<ISOOpaqueField>(msg, 49, "978");
        /// @endcode
        ///
        /// @tparam T            Concrete field type.
        /// @param  msg          Reference to the message.
        /// @param  key          DE number.
        /// @param  defaultValue Value to return when the field is absent.
        /// @return              The field's value, or `defaultValue`.
        template <typename T, typename Default>
        auto getOrDefault(ISOMessage& msg, TNG_KEY_TYPE key, Default&& defaultValue)
            -> std::decay_t<decltype(std::declval<T>().value())>
        {
            using ValueType = std::decay_t<decltype(std::declval<T>().value())>;
            auto opt = msg.tryGet<T>(key);
            if (!opt) return ValueType(std::forward<Default>(defaultValue));
            return (*opt)->value();
        }


        /// @brief Calls `fn` with the field value if the field is present.
        ///
        /// Returns `true` when the field was found and `fn` was called.
        /// Avoids the nested `if (auto opt = ...) { ... }` pattern.
        ///
        /// @code
        ///   tng::ISOUtils::ifPresent<ISOOpaqueField>(msg, 11, [](const std::string& stan) {
        ///       log("STAN: {}", stan);
        ///   });
        /// @endcode
        ///
        /// @tparam T   Concrete field type.
        /// @tparam Fn  Callable with signature `void(const ValueType&)`.
        /// @param  msg Reference to the message.
        /// @param  key DE number.
        /// @param  fn  Callback invoked with the field value.
        /// @return     `true` if the field was found, `false` otherwise.
        template <typename T, typename Fn>
        bool ifPresent(ISOMessage& msg, TNG_KEY_TYPE key, Fn&& fn)
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt) return false;
            std::forward<Fn>(fn)((*opt)->value());
            return true;
        }

    } // namespace ISOUtils

}
