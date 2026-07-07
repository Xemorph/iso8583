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
// -- tsl::robin_map (via vcpkg: robin-map) -----------------------------------
// Ersetzt die archivierte martinus/robin-hood-hashing Bibliothek.
// Wird als vcpkg-Abhängigkeit eingebunden, nicht mehr als Single-Header.
// Anwender verwenden ausschließlich TNG_NAMESPACE::detail::flat_map –
// niemals tsl::robin_map direkt.
#include <tsl/robin_map.h>

namespace TNG_NAMESPACE {
    namespace detail {
        // Typalias – isoliert den Rest der Bibliothek von der konkreten
        // Hash-Map-Implementierung. Austausch erfordert nur eine Änderung hier.
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
    /*
    template < typename KeyType, typename DataType >
    class ISOComponentv2
    {
        static_assert(std::is_integral<KeyType>::value, "KeyType must be integral");
    protected:
        // key
        KeyType key_;
        // data
        DataType data_;
    public:
        // [Constructor]
        // \param key
        ISOComponentv2(const KeyType& key);
        // [Constructor]
        // \param key
        // \param data
        ISOComponentv2(const KeyType& key, const DataType& data);
        // Set key
        void key(const KeyType& key);
        // Get key
        const KeyType key() const;
        // Set data
        void data(const DataType& d);
        // Get data
        const DataType& data() const;
        template < typename = typename std::enable_if_t< std::is_same_v<DataType, std::string> > >
        nonstd::string_view value() const {
            return data_;
        }
        // Composites must override this function
        virtual bool is_composite() const;
    };
    */

    template < typename IntegerType, typename T >
    class TNG_EXPORT ISOComponent
        : public ISOComponentPtrBase
    {
        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be integral");
        static_assert(sizeof(IntegerType) <= sizeof(TNG_KEY_TYPE),
            "IntegerType darf nicht breiter als TNG_KEY_TYPE (int16_t) sein – "
            "sonst würden key()-Aufrufe über das Interface truncieren.");
    protected:
        // Key
        IntegerType k_;
        // Data
        T d_;
        // Description
        nonstd::string_view desc_;
        // Value explanation
        nonstd::string_view expl_;
        // Wire-Position: Byte-Offset im ursprünglichen Netzwerk-Buffer
        std::size_t wire_offset_ = 0;
        // Wire-Position: Anzahl Bytes die dieses Feld im Buffer belegt (inkl. Längen-Prefix)
        std::size_t wire_length_ = 0;
    public:
        // [Destructor]
        ~ISOComponent() = default;
        // [Constructor]
        ISOComponent(const IntegerType& key);
        // [Constructor]
        // \param key
        // \param data
        ISOComponent(const IntegerType& key, const T& data);

        void key(IntegerType k);

        // Gibt den Schlüssel als TNG_KEY_TYPE zurück (kein implizites int-Narrowing mehr).
        TNG_KEY_TYPE key() const override;

        void value(const T& d);
        const T& value() const;        // read-only view (keine Kopie)
        T& value();                    // mutable access
        std::string readable_value() const override;

        void description(const nonstd::string_view& desc) override;
        const nonstd::string_view description() const override;

        void explanation(const nonstd::string_view& expl) override;
        const nonstd::string_view explanation() const override;

        // Wire-Position (vom Parser nach dem Unparse gesetzt)
        std::size_t wire_offset() const noexcept override { return wire_offset_; }
        void        wire_offset(std::size_t offset) noexcept override { wire_offset_ = offset; }
        std::size_t wire_length() const noexcept override { return wire_length_; }
        void        wire_length(std::size_t length) noexcept override { wire_length_ = length; }

        // Konvertierung in JSON
        virtual json to_json() const override;

        // Composites must override this function
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

//typedef ::TNG_NAMESPACE::ISOComponentv2< TNG_KEY_TYPE, std::string > ISOOpaqueFieldv2;
//typedef ::TNG_NAMESPACE::ISOComponentv2< TNG_KEY_TYPE, std::vector<uint8_t> > ISOBinaryFieldv2;
//typedef ::TNG_NAMESPACE::ISOComponentv2< TNG_KEY_TYPE, std::byte > ISOFastBinaryFieldv2;
//typedef ::TNG_NAMESPACE::ISOComponentv2< TNG_KEY_TYPE, dynamic_bitset<> > ISOBitmapv2;
// Data Layout
// Forward declaration
//class ISOMessagev2;

//typedef std::unordered_map< TNG_KEY_TYPE, std::variant< ISOOpaqueFieldv2, ISOBinaryFieldv2, ISOFastBinaryFieldv2, ISOBitmapv2, ISOMessagev2 > > ISO_FAST_MAP;
//typedef ::TNG_NAMESPACE::type_list_maker< ISOOpaqueFieldv2, ISOBinaryFieldv2, ISOFastBinaryFieldv2, ISOBitmapv2 >::result ISO_MAP_TYPE;
//typedef ::TNG_NAMESPACE::multi_type_map< TNG_KEY_TYPE, ISO_MAP_TYPE > ISO_MAP_v2;

namespace TNG_NAMESPACE {
    /* ISOMessagev2
    class TNG_EXPORT ISOMessagev2 final
        : public ::TNG_NAMESPACE::ISOComponentv2< TNG_KEY_TYPE, ISO_FAST_MAP >
    {
    private:
        // Highest data element
        ISO_FAST_MAP::key_type hf_;
        // Bitmap needs be recalculated?
        bool recalc_;
        // Threading
        //    * Read is shared
        //    * Write is unique
        std::shared_mutex d_lock_;
    public:
        // [Destructor]
        ~ISOMessagev2() = default;
        // [Constructor]
        // Creates an empty 'ISOMessage'
        ISOMessagev2();
        // [Constructor]
        // Creates a nested 'ISOMessage'
        // \param key (in the outer 'ISOMessage') of the nested component
        ISOMessagev2(const ISO_FAST_MAP::key_type& key);
        // [Constructor]
        // Creates an 'ISOMessage' with given Message Type Indicator (MTI)
        // \param mti, Message Type Indicator (e.g. 0100 for Authorization)
        ISOMessagev2(nonstd::string_view mti);

        // Composites must override this function
        virtual bool is_composite() const override;
    };
    */
}

// Data Layout
typedef TNG_NAMESPACE::detail::flat_map< TNG_KEY_TYPE, std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase > > ISO_MAP;
// Components
typedef TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::string > ISOOpaqueField;
typedef TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::vector<uint8_t> > ISOBinaryField;
typedef TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::vector<std::byte> > ISOFastBinaryField;
typedef TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, dynamic_bitset<> > ISOBitmap;
typedef TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, int32_t > ISOCodeField;

namespace TNG_NAMESPACE {
    class TNG_EXPORT ISOMessage final
        : public ISOComponent<TNG_KEY_TYPE, ISO_MAP>
    {
    public:
        enum class Direction {
            UNKNOWN = 0,
            INCOMING = 1,
            OUTGOING = 2,
        };
    private:
        // Highest data element
        ISO_MAP::key_type hf_;
        // Bitmap needs be recalculated?
        bool recalc_;
        // Threading
        //    * Read is shared
        //    * Write is unique
        std::shared_mutex d_lock_;

        // PSL=Parser System Language
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr p_ = nullptr;
        // SNR=Send And Recieve
        Direction dir_ = Direction::UNKNOWN;
        // Header
        ISOHeader::ISOHeaderSmartPtr hdr_;
    public:
        // Smart Pointer concept
        using ISOMessageSmartPtr = std::shared_ptr<ISOMessage>;
        // ISO_MAP_ITERATOR
        using ISO_MAP_ITERATOR = ISO_MAP::iterator;
        using ISO_MAP_CONST_ITERATOR = ISO_MAP::const_iterator;

        // [Destructor]
        ~ISOMessage() = default;
        // [Constructor]
        // Creates an empty 'ISOMessage'
        ISOMessage();
        // [Constructor]
        // Creates a nested 'ISOMessage'
        // \param key (in the outer 'ISOMessage') of the nested component
        ISOMessage(const ISO_MAP::key_type& key);
        // [Constructor]
        // Creates an 'ISOMessage' with given Message Type Indicator (MTI)
        // \param mti, Message Type Indicator (e.g. 0100 for Authorization)
        ISOMessage(nonstd::string_view mti);

        // Composites must override this function
        std::string readable_value() const override;

        // Composites must override this function
        bool is_composite() const override;

        virtual void print(bool nested) const override;

        // Konvertierung in JSON
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

        // -------------------------
        // -          MAP          -
        // -------------------------

        /// <summary>
        /// Checks if a data element (DE) is set within this component ('ISOMessage')
        /// </summary>
        /// @param key number of data element
        /// @return true if found otherwise false
        bool has(const ISO_MAP::key_type& key);

        /// <summary>
        /// Set a new data element (DE) within this component ('ISOMessage').
        /// This is a write operation which is guarded by a mutex
        /// </summary>
        /// <param name="component">to set into this component (push_back)</param>
        /// <return>true if successfull otherwise false</return>
        bool set(ISO_MAP::mapped_type component);

        /// <summary>
        /// Unset a data element (DE) within in this component ('ISOMessage').
        /// This is a write operation which is guarded by mutex
        /// </summary>
        /// @param key of data element
        /// @return true if successfull otherwise false, it will also return false if the user-defined key has not been found
        bool unset(const ISO_MAP::key_type& key);


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


        /// <summary>
        /// Clears the whole underlying container
        /// </summary>
        void reset(void);

        /// @brief Returns the size of the underlying container
        /// @return number of elements inside the container
        const std::size_t size() const noexcept;
    
        // -------------------------
        // -          PSL          -
        // -------------------------

        // Set parser for this specific 'ISOMessage', the user-defined parser will be used
        // for un/parsing
        // \param p (parser) to set (e.g. std::shared_ptr<tng::MastercardParser>)
        void parser(const ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p);
        // Returns the user-defined parser instance if one was set else this function returns a nullptr
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr parser() const;

        // Unparses the user-defined byte image into the user-defined 'ISOMessage' with the configured parser
        // \param c (component) to store result into
        // \param b (byte image) of a message from network which needs to be unparsed
        // \return amount of consumed bytes from the byte image
        std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b);

        bool isInner() const;

        // -------------------------
        // -          PSL          -
        // -------------------------

        void header(const std::vector<uint8_t>& b);
        void header(::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr& hdr);

        std::vector<uint8_t> header() const;
        ::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr header();

        void direction(Direction a);

        Direction direction();

        bool hasMTI();

        nonstd::string_view mti();

        bool isRequest();

        bool isResponse();

        bool isAuthorization();

        bool isFinancial();

        bool isFileAction();

        bool isReversal();

        bool isChargeback();

        bool isReconciliation();

        bool isAdministrative();

        bool isFeeCollection();

        bool isNetworkManagement();

        bool isRetransmission();
    };

    class TNG_EXPORT BaseHeader : public ::TNG_NAMESPACE::ISOHeader {
    protected:
        std::vector<uint8_t> header;
        bool asciiEncoding = false;

    public:
        BaseHeader() = default;
        explicit BaseHeader(const std::vector<uint8_t>& header) { unpack(header); }

        std::vector<uint8_t> pack() const override {
            return header;
        }

        std::size_t unpack(const std::vector<uint8_t>& b) override {
            header = b;
            return b.size();
        }

        std::size_t size() const override {
            return header.size();
        }

        void destination(const nonstd::string_view /*dst*/) override {}
        void source(const nonstd::string_view /*src*/) override {}
        std::optional<nonstd::string_view> destination() const override { return std::nullopt; }
        std::optional<nonstd::string_view> source() const override { return std::nullopt; }
        void swapDirection() override {}

        std::unique_ptr<ISOHeader> clone() const override {
            auto h = std::make_unique<BaseHeader>();
            h->header = this->header;
            h->asciiEncoding = this->asciiEncoding;
            return h;
        }

        void setAsciiEncoding(bool ascii) { asciiEncoding = ascii; }
        bool isAsciiEncoding() const { return asciiEncoding; }
    };

    /*
     * BASE1 Header
     * <pre>
     *   0 hlen;         Fld  1: Header Length        1B      (Byte     0)
     *   1 hformat;      Fld  2: Header Format        8N,bit  (Byte     1)
     *   2 format;       Fld  3: Text Format          1B      (Byte     2)
     *   3 len[2];       Fld  4: Total Message Length 2B      (Byte  3- 4)
     *   5 dstId[3];     Fld  5: Destination Id       6N,BCD  (Byte  5- 7)
     *   8 srcId[3];     Fld  6: Source Id            6N,BCD  (Byte  8-10)
     *  11 rtCtl;        Fld  7: Round-Trip Ctrl Info 8N,bit  (Byte    11)
     *  12 flags[2];     Fld  8: BASE I Flags        16N,bit  (Byte 12-13)
     *  14 status[3];    Fld  9: Message Status Flags 24bits  (Byte 14-16)
     *  17 batchNbr;     Fld 10: Batch Number        1B       (Byte    17)
     *  18 reserved[3];  Fld 11: Reserved            3B       (Byte 18-20)
     *  21 userInfo;     Fld 12: User Info           1B       (Byte    21)
     *  The following fields are only presend in a reject message
     *  22 bitmap;       Fld 13: Bitmap              2B       (Byte 22-23)
     *  24 rejectdata;   Fld 14: Reject Data Group   2B       (Byte 24-25)
     * </pre>
     *
     */
    class TNG_EXPORT BASE1Header final : public ::TNG_NAMESPACE::BaseHeader {
    public:
        static constexpr size_t LENGTH = 22;

        BASE1Header();
        BASE1Header(nonstd::string_view _source, nonstd::string_view _destination);
        BASE1Header(nonstd::string_view _source, nonstd::string_view _destination, int _format);
        BASE1Header(const std::vector<uint8_t>& header);

        // Getter/Setter für die Header-Felder
        int getHLen() const;
        void setHFormat(int hformat);
        int format() const;

        void format(int format);
        void setRtCtl(int i);
        void setFlags(int i);
        void setStatus(int i);
        void setBatchNumber(int i);
        void setLen(std::size_t len);

        void destination(nonstd::string_view dest) override;
        void source(nonstd::string_view src) override;
        std::optional<nonstd::string_view> source() const override;
        std::optional<nonstd::string_view> destination() const override;
        void swapDirection() override;
        bool isRejected() const;
        std::string getRejectCode() const;

        friend std::ostream& operator<<(std::ostream& os, const BASE1Header& hdr);
    };

    class TNG_EXPORT WLP_FOHeader final : public ::TNG_NAMESPACE::BaseHeader {
    public:
        static constexpr size_t LENGTH = 4 + 10 + 10 + 4 + 26 + 1 + 20 + 16 + 2;

        WLP_FOHeader();
        WLP_FOHeader(
            nonstd::string_view _record,
            nonstd::string_view _mti,
            int _version,
            nonstd::string_view _uuid,
            nonstd::string_view _reference,
            int _payment
        );
        WLP_FOHeader(
            nonstd::string_view _record,
            nonstd::string_view _mti,
            int _version,
            nonstd::string_view _uuid,
            nonstd::string_view _reference,
            nonstd::string_view _payment
        );
        WLP_FOHeader(const std::vector<uint8_t>& header);

        // Packen: Header in EBCDIC konvertieren
        std::vector<uint8_t> pack() const override;

        // Unpacken: EBCDIC in ASCII konvertieren
        std::size_t unpack(const std::vector<uint8_t>& b) override;

        // Feld-Methoden

        void length(int len);
        int length() const;

        void sysId(nonstd::string_view s);

        void record(nonstd::string_view s);

        void mti(nonstd::string_view s);

        void creationTs();
        void creationTs(nonstd::string_view s);

        void version(int v);
        int version() const;

        void uuid(nonstd::string_view s);

        void reference(nonstd::string_view s);

        void payment(size_t payment);
        void payment(nonstd::string_view s);

        std::unique_ptr<ISOHeader> clone() const override;
    };

    namespace ISOUtils {

        // Flacht eine ISOMessage in eine std::map<string,string> ab.
        TNG_EXPORT std::map<std::string, std::string> flatten(const ISOMessage&);

        // Gibt den Feldwert zurück oder wirft std::out_of_range wenn das Feld
        // fehlt oder den falschen Typ hat.
        // Geeignet wenn das Feld laut Spec garantiert vorhanden sein muss.
        //
        // Beispiel:
        //   std::string pan = ISOUtils::getOrThrow<ISOOpaqueField>(msg, 2);
        template <typename T>
        auto getOrThrow(ISOMessage& msg, TNG_KEY_TYPE key)
            -> decltype(std::declval<T>().value())
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt)
                throw std::out_of_range(
                    "DE" + std::to_string(key) + " fehlt oder hat falschen Typ");
            return (*opt)->value();
        }

        // Gibt den Feldwert zurück oder einen Default-Wert wenn das Feld fehlt.
        // Geeignet für optionale Felder mit sinnvollem Fallback.
        //
        // Beispiel:
        //   std::string stan = ISOUtils::getOrDefault<ISOOpaqueField>(msg, 11, "000000");
        template <typename T>
        auto getOrDefault(ISOMessage& msg, TNG_KEY_TYPE key,
            decltype(std::declval<T>().value()) defaultValue)
            -> decltype(std::declval<T>().value())
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt) return defaultValue;
            return (*opt)->value();
        }

        // Ruft den Callback mit dem Feldwert auf wenn das Feld vorhanden ist.
        // Gibt true zurück wenn das Feld gefunden wurde, sonst false.
        // Geeignet um if (auto x = msg.tryGet<...>(...)) { ... } zu vermeiden.
        //
        // Beispiel:
        //   ISOUtils::ifPresent<ISOOpaqueField>(msg, 2, [](const std::string& pan) {
        //       std::cout << "PAN: " << pan << "\n";
        //   });
        template <typename T, typename Fn>
        bool ifPresent(ISOMessage& msg, TNG_KEY_TYPE key, Fn&& fn)
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt) return false;
            std::forward<Fn>(fn)((*opt)->value());
            return true;
        }

    }

}
