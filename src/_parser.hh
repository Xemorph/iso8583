#pragma once

// [stdc++]
#include <deque>
#include <memory>
#include <sstream>
#include <utility>
// [tng]
#include <iso8583/detail/_components.hh>
#include <iso8583/_codec.hh>

#include "_padder.hh"

//using namespace std::string_literals; // available since C++14
using namespace nonstd::literals;


namespace TNG_NAMESPACE {

    // Useless 'struct' to define a unused data type
    struct UNUSED{};

    // Provides base functionality for the PSL
    class TNG_EXPORT ISOBaseParser 
        : public ISOParserPtrBase
    {
        using DEQUE = std::deque< std::shared_ptr< const ::TNG_NAMESPACE::ISOFieldParserPtrBase > >;
        using ITERATOR = DEQUE::iterator;
        using CONST_ITERATOR = DEQUE::const_iterator;
        // ISOFieldParserList
        DEQUE l_;
        // Description of this un/parser
        std::string d_;
        // For implementations where the tertiary bitmap is inside a Data Element (DE)
        TNG_KEY_TYPE bmp_3rd_ = INT16_MIN;
        // Header size if applicable;
        std::size_t hdr_sz_ = 0u;
    public:
        // Smart Pointer conceppt
        using ISOBaseParserSmartPtr = std::shared_ptr<ISOBaseParser>;

        // [Destructor]
        ~ISOBaseParser() = default;
        // [Constructor]
        // \param d (description) of this un/parser
        // \param hdrSz (header length), defaults to zero
        ISOBaseParser(nonstd::string_view d, std::size_t hdrSz = 0u)
            : d_(nonstd::to_string(d)), hdr_sz_(hdrSz)
        {}

        // Length of header if applicable
        std::size_t headerLength() const {
            return hdr_sz_;
        }
        // Set length of header if applicable
        void headerLength(std::size_t hdrSz) {
            hdr_sz_ = hdrSz;
        }

        // Checks if the bitmap has to be emitted
        // \return true if bitmap has to be emitted otherwise false
        bool emit_bitmap() const noexcept override {
            return (
                field_parser(1) ? 
                    field_parser(1)->type() == TNG_NAMESPACE::ISOFieldParserType::BITMAP :
                        false
            );
        }

        const std::shared_ptr< const ::TNG_NAMESPACE::ISOFieldParserPtrBase > field_parser(short key) const noexcept {
            try
            {
                return l_.at(key);
            }
            catch (const std::exception&)
            {
                return nullptr;
            }
        }

        nonstd::string_view field_description(short key) const {
            try
            {
                return l_.at(key)->description();
            }
            catch (const std::exception&)
            {
                return "<missing>"_sv;
            }
        }

        // Usually 2 for normal fields, 1 for bitmap-less or ANSI X9.2
        // \return key of first valid data element
        short first_field() const {
            if ((field_parser(0)->type() != TNG_NAMESPACE::ISOFieldParserType::NESTED) && l_.size() > 1)
                return field_parser(1)->type() == TNG_NAMESPACE::ISOFieldParserType::BITMAP ? 2 : 1;
            return 0;
        }

        // Returns the un/parser's description
        nonstd::string_view description() const { 
            return d_; 
        }

        // Returns the number of elements of the underlying container
        const std::size_t size(void) const {
            return l_.size();
        }

        // Parses the provided 'ISOComponent'
        // \param c (component) to parse
        std::vector<uint8_t> parse(ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) const override;

        // Unparses the provided byte-image
        // \param c (component) to store result into
        // \param b (byte-image) to unparse
        std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b) override;

        // Overload mit base_offset: wird vom nested-Pfad aufgerufen
        std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
                            const std::vector<uint8_t>& b,
                            std::size_t base_offset) override;

        // Adds a new 'ISOFieldParser' to the un/parser system
        // Under the hood we use a simple dynamic list, which means that the
        // passed 'ISOFieldParser' will be added at the end
        void add(ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr parser) {
            (void)l_.push_back(std::move(parser));
        }
    };

    template < typename T, Length l_, PrefixEncoder pe_, Encoder e_, Padder p_ >
    class TNG_EXPORT ISOFieldParser
        : public ISOFieldParserPtrBase
    {

        static_assert(l_ == Length::FIX ? (pe_ != PrefixEncoder::NONE ? false : true) : true, "Length::FIX must be used with PrefixEncoder::NONE");
        static_assert(l_ == Length::UNKNOWN ? (pe_ != PrefixEncoder::NONE ? false : true) : true, "Length::UNKNOWN must be used with PrefixEncoder::NONE");
        // Consumer
        static_assert(l_ == Length::CONSUME ? (pe_ != PrefixEncoder::NONE ? false : true) : true, "Length::CONSUMER must be used with PrefixEncoder::NONE");
        static_assert(l_ == Length::CONSUME ? (e_ != Encoder::BINARY ? false : true) : true, "Length::CONSUMER must be used with Encoder::BINARY");

        // Maximum allowed DE length
        std::size_t de_l_;
        // DE description
        std::string d_;

        // Data Element parser
        ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr n_ = nullptr;
        // Sub Element parser
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr c_ = nullptr;
        // Data container
        std::shared_ptr< ISOBinaryField > b_ = nullptr;
        
    public:
        // [Destrutcor]
        ~ISOFieldParser() = default;

        // [Constructor]
        ISOFieldParser()
            : de_l_(0), d_("<dummy>")
        {}

        // [Constructor]
        ISOFieldParser(std::size_t l, nonstd::string_view d)
            : de_l_(l), d_(nonstd::to_string(d))
        {}

        // [Constructor]
        template < typename U = T, typename std::enable_if_t<std::is_base_of_v<ISOBaseParser, U>, int> = 0 >
        ISOFieldParser(ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr nested,
            nonstd::string_view composite_description)
            : n_(std::move(nested)), c_(std::make_shared<U>(composite_description)) {
            b_ = std::make_shared<ISOBinaryField>(0);
        }

        template < typename U = T, typename std::enable_if_t<std::is_base_of_v<ISOBaseParser, U>, int> = 0 >
        void subParser(::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr c) {
            c_ = std::move(c);
        }

        /// Returns the inner ::TNG_NAMESPACE::ISOBaseParser when type() == NESTED
        /// Allows ::TNG_NAMESPACE::ISOMessage::set(dot-notation) to find sub element types
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
            subParser() const noexcept override {
            if constexpr (std::is_base_of_v<ISOBaseParser, T>)
                return c_;
            return nullptr;
        }

        // Returns the DE un/parser's description
        nonstd::string_view description() const override {
            if (n_)
                return n_->description();
            else
                return d_;
        }
        // Sets the DE un/parser's description
        // \param d (description) of DE
        void description(const nonstd::string_view& d) override {
            if (n_)
                n_->description(d);
            else
                d_ = nonstd::to_string(d);
        }

        const ::TNG_NAMESPACE::ISOFieldParserType type() const override {
            if constexpr (std::is_same_v< T, std::nullptr_t >)
                return ::TNG_NAMESPACE::ISOFieldParserType::UNUSED;
            else if constexpr (std::is_same_v< T, ::TNG_NAMESPACE::UNUSED >)
                return ::TNG_NAMESPACE::ISOFieldParserType::EXCEPTIONAL;
            else if constexpr (l_ == Length::UNKNOWN) // Special case, but required
                return ::TNG_NAMESPACE::ISOFieldParserType::REMAINING;
            else if constexpr (l_ == Length::CONSUME) // Special case, but required
                return ::TNG_NAMESPACE::ISOFieldParserType::REMAINING;
            else if constexpr (std::is_same_v< T, std::string >)
                return ::TNG_NAMESPACE::ISOFieldParserType::OPAQUE;
            else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
                return ::TNG_NAMESPACE::ISOFieldParserType::BINARY;
            else if constexpr (std::is_same_v< T, dynamic_bitset<> >)
                return ::TNG_NAMESPACE::ISOFieldParserType::BITMAP;
            else if constexpr (std::is_base_of_v<ISOBaseParser, T>)
                return ::TNG_NAMESPACE::ISOFieldParserType::NESTED;
            else
                static_assert(::TNG_NAMESPACE::dependent_false<T>::value, "undefined ISOFieldParserType");
        }

        virtual std::vector<uint8_t> parse(ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) const override {
            if constexpr (std::is_same_v< T, std::nullptr_t >) {
                return std::vector<uint8_t>{};
            }
            else if constexpr (std::is_base_of_v< ISOBaseParser, T >) {
                // NESTED: c_->parse() serialisiert die Kind-Felder in rohe Bytes.
                // n_->parse() verpackt diese Bytes dann mit dem korrekten Längen-
                // Prefix (identisch zu unparse(), nur in umgekehrter Richtung).
                // c_ = innerer ISOBaseParser (kennt die Kind-Feld-Struktur)
                // n_ = äußerer ISOBinaryFieldParser (trägt Prefix-Encoding + Länge)
                auto inner_bytes = c_->parse(c);

                // Temporäres ISOBinaryField für n_ erstellen und mit den inneren
                // Bytes befüllen – n_->parse() fügt den korrekten Prefix hinzu.
                auto wrapper = std::make_shared<ISOBinaryField>(c->key());
                wrapper->value(inner_bytes);
                return n_->parse(wrapper);
            }
            else if constexpr (std::is_same_v< T, ::TNG_NAMESPACE::UNUSED >) {
                throw std::runtime_error(
                    (std::ostringstream()
                        << "Parser should not pack 'DE"
                        << c->key()
                        << "'!"
                        ).str()
                );
            }
            else if constexpr (std::is_same_v< T, std::string >) {
                std::string data = nonstd::to_string((nonstd::string_view)std::dynamic_pointer_cast< ISOOpaqueField >(c)->value());
                if (data.size() > de_l_)
                    return std::vector<uint8_t>{};
                pad<p_>(data, de_l_); // Padding data
                std::vector<uint8_t> b_img(parsed_length<pe_, l_>() + required_sz_for_as<e_>(data.size()), 0);
                encode_length<pe_, l_>(data.size(), b_img); // Encode length if applicable
                to<e_>(data, b_img, parsed_length<pe_, l_>());
                return b_img;
            }
            else if constexpr (std::is_same_v< T, std::vector<uint8_t> >) {
                std::vector<uint8_t> data = std::dynamic_pointer_cast< ISOBinaryField >(c)->value();
                std::size_t pl = parsed_length<pe_, l_>();
                if (pl == 0 && data.size() != de_l_)
                    return std::vector<uint8_t>{};
                std::vector<uint8_t> b_img(pl + required_sz_for_as<e_>(data.size()), 0);
                encode_length<pe_, l_>(data.size(), b_img); // Encode length if applicable
                to<e_>(data, b_img, pl);
                return b_img;
            }
            else if constexpr (std::is_same_v< T, dynamic_bitset<> >) {
                dynamic_bitset<> b = std::dynamic_pointer_cast< ISOBitmap >(c)->value();
                std::size_t bytes = de_l_ >= 8 ? ((b.size() + 62) >> 6) << 3 : de_l_;
                std::size_t bits = bytes * 8;

                std::vector<uint8_t> d(bytes, 0x00);
                for (std::size_t i = 0u; i < bits; ++i)
                    if (b[i + 1])                     // +1 because we don't use bit 0 of dynamic_bitset
                        d[i >> 3] |= 0x80 >> i % 8;
                if (bits > 64)
                    d[0] |= 0x80;
                if (bits > 128)
                    d[8] |= 0x80;
                return d;
            }
            else
                static_assert(::TNG_NAMESPACE::dependent_false<T>::value, "don't know how to parse undefined type");
        }

        // Unparses the byte-image and stores the result into the user-defined ISOComponent
        // \param c (component), user-defined, to store result to
        // \param b (byte-image) to unparse from
        // \param o (offset) of byte-image to start from
        // \return Amount of bytes consumed from byte-image
        virtual std::size_t unparse(ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b, std::size_t o) const override {
            if constexpr (std::is_same_v< T, ::TNG_NAMESPACE::UNUSED >) {
                throw std::runtime_error(
                       (std::ostringstream()
                           << "Parser should not pack 'DE"
                           << c->key()
                           << "'!"
                           ).str()
                );
            }
            if constexpr (std::is_base_of_v<ISOBaseParser, T>) {
                // n_ liest den Längen-Prefix und extrahiert die Nutzdaten nach b_->value().
                // consumed_outer = Bytes die n_ im Original-Buffer b verbraucht hat
                // (Längen-Prefix + Nutzdaten).
                std::size_t consumed_outer = n_->unparse(b_, b, o);

                if (c->is_composite() && !b_->value().empty()) {
                    // BUG-FIX: parsed_length<pe_, l_>() ist für ISONestedFieldParser
                    // immer 0 (pe_=NONE, l_=FIX), weil pe_/l_ die Template-Parameter
                    // des äußeren ISOFieldParser<T,...> sind – nicht die des inneren n_.
                    //
                    // Der tatsächliche Prefix-Offset ergibt sich aus der Differenz:
                    //   consumed_outer = Prefix-Bytes + Nutzdaten-Bytes
                    //   b_->value().size() = Nutzdaten-Bytes
                    //   → actual_prefix = consumed_outer - b_->value().size()
                    //
                    // Beispiel BMP_003 (format: binary, length: 6, kein Prefix):
                    //   consumed_outer = 6, b_->value().size() = 6 → prefix = 0 ✓
                    //
                    // Beispiel DE063 (format: LLLBINARY, length: 50, 3 Bytes Prefix):
                    //   consumed_outer = 53, b_->value().size() = 50 → prefix = 3 ✓
                    const std::size_t actual_prefix = consumed_outer >= b_->value().size()
                        ? consumed_outer - b_->value().size()
                        : 0;
                    const std::size_t child_base_offset = o + actual_prefix;
                    c_->unparse(c, b_->value(), child_base_offset);
                }
                return consumed_outer;
            }
            if constexpr (!std::is_same_v< T, dynamic_bitset<> >) {
                if constexpr (std::is_same_v< T, std::nullptr_t >) {
                    return 0;
                }
                
                std::size_t l = 0u;
                if constexpr (l_ == Length::UNKNOWN || l_ == Length::CONSUME)
                    l /* remaining */ = b.size() - o;
                else
                    l = decode_length<pe_, l_>(b, o); // Something like this?

                // Checks for length are different between types
                if constexpr (l_ != Length::CONSUME)
                    if (l == 0 || l > de_l_)
                        l = de_l_;

                std::size_t ll = parsed_length<pe_, l_>();

                // Truncation:
                // -----------
                // Verfügbare Bytes im Buffer ab (o + Prefix)
                //  * l ist in logischen Einheiten (Ziffern für BCD / Bytes für BINARY/EBCDIC)
                //  * required_sz_for_as<e_>(l) gibt die tatsächlich benötigten Bytes an
                // => Vergleich muss in Bytes erfolgen!!!
                // 
                // Beispiel:
                // Bei Nichteinhaltung wird BCD sonst auf halbe Länge gekürzt
                //    (6 Bytes = 12 BCD-Ziffern, aber 6 < 12 würde fälschlicherweise truncaten).
                if (o + ll <= b.size()) { // 4+0 <= 7
                    const std::size_t available_bytes = b.size() - (o + ll);
                    const std::size_t needed_bytes = required_sz_for_as<e_>(l);
                    if (available_bytes < needed_bytes) {
                        // Wie viele vollständige logische Einheiten passen in available_bytes?
                        if constexpr (TNG_NAMESPACE::Encoder::BCD == e_)
                            l = available_bytes * 2;  // 1 Byte = 2 BCD-Ziffern
                        else
                            l = available_bytes;
                    }
                }
                else {
                    l = 0;
                }

                // Skip allocation of temporary function stack variable
                if constexpr (l_ != Length::CONSUME)
                    if constexpr (std::is_same_v< T, std::string >)
                        (void)std::dynamic_pointer_cast<ISOOpaqueField>(c)->value(as< T, e_ >(b, o + ll, l));
                    else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
                        (void)std::dynamic_pointer_cast<ISOBinaryField>(c)->value(as< T, e_ >(b, o + ll, l));

                const std::size_t total = ll + required_sz_for_as<e_>(l); // l= 3 ---> 3
                // wire_length: hier gesetzt, da erst jetzt die tatsächliche Byte-Länge bekannt ist.
                // wire_offset: wird vom aufrufenden ISOBaseParser mit dem absoluten Offset gesetzt,
                //              da ISOFieldParser nur den lokalen Offset o im aktuellen Sub-Buffer kennt.
                c->wire_length(total);
                return total;
            }
            else {
                // byte2bitset
                {
                    bool  b1 = (b[o] & 0x80) == 0x80;
                    bool b65 = (b.size() > o + 8) && ((b[o + 8] & 0x80) == 0x80);
                    std::size_t mbits = de_l_ << 3;
                    std::size_t len = (mbits > 128 && b1 && b65) ?   192 :
                                      (mbits > 64 && b1)         ?   128 :
                                      (mbits < 64)               ? mbits : 64;

                    dynamic_bitset<> bmp(len+1);
                    for (std::size_t i = 0; i < len; ++i)
                        if ((b[o + (i >> 3)] & 0x80 >> i % 8) > 0)
                            bmp.set(i+1);

                    len = bmp[1] ? 128 : 64;
                    if (de_l_ > 16 && bmp[1] && bmp[65])
                        len = 192;

                    (void)std::dynamic_pointer_cast< ISOBitmap >(c)->value(bmp);
                    return std::min(de_l_, len >> 3);
                }
            }
        }

        virtual ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr create_component(TNG_KEY_TYPE key) const override {
            if constexpr (std::is_same_v< T, std::nullptr_t >)
                return std::make_shared<ISOOpaqueField>(key);
            else if constexpr (std::is_same_v< T, ::TNG_NAMESPACE::UNUSED >) {
                throw std::runtime_error(
                    (std::ostringstream()
                        << "Parser should not create component for 'DE"
                        << key
                        << "'!"
                        ).str()
                );
            }
            else if constexpr (std::is_same_v< T, std::string >)
                return std::make_shared<ISOOpaqueField>(key);
            else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
                return std::make_shared<ISOBinaryField>(key);
            else if constexpr (std::is_same_v< T, int32_t >)
                return std::make_shared<ISOCodeField>(key);
            else if constexpr (std::is_same_v< T, dynamic_bitset<> >)
                return std::make_shared<ISOBitmap>(key);
            else if constexpr (std::is_base_of_v<ISOBaseParser, T>)
                return std::make_shared<ISOMessage>(key);
            else
                static_assert(::TNG_NAMESPACE::dependent_false<T>::value, "don't know how to create component");
        }
    };

    template < Length l, PrefixEncoder pe, Encoder e, Padder p = Padder::NONE >
    using ISOBinaryFieldParser = ISOFieldParser< std::vector<uint8_t>, l, pe, e, p >;
    template < Length l, PrefixEncoder pe, Encoder e, Padder p = Padder::NONE >
    using ISOOpaqueFieldParser = ISOFieldParser< std::string, l, pe, e, p >;
    template < Length l, PrefixEncoder pe, Encoder e, Padder p = Padder::NONE >
    using ISOCodeFieldParser = ISOFieldParser< int32_t, l, pe, e, p >;

    using ISOBitmapFieldParser = ISOFieldParser< dynamic_bitset<>, Length::FIX, PrefixEncoder::NONE, Encoder::BINARY, Padder::NONE >;
    template < typename ISOField >
    using ISONestedFieldParser = ISOFieldParser< ISOField, Length::FIX, PrefixEncoder::NONE, Encoder::BINARY, Padder::NONE >;

    // ── ISORemainderFieldParser ───────────────────────────────────────────────
    // Template-Alias für trailing variable-length Felder ohne eigenen Prefix.
    // Nutzt Length::UNKNOWN: der unparse()-Pfad berechnet die Länge zur Laufzeit
    // als (b.size() - o) anstatt einen Prefix zu dekodieren.
    //
    // Verwendung in YAML:   format: remaining
    // Verwendung in Code:   IF_REMAINING  (binary)
    //                       IFE_REMAINING (EBCDIC)
    //
    // Maximale Länge (de_l_) wird aus der Spec übernommen und als Obergrenze
    // angewendet – laut Mastercard-Spec ist BMP_061 Subfeld 15 max. 10 Bytes.
    template < typename T, Encoder e, Padder p = Padder::NONE >
    using ISORemainderFieldParser = ISOFieldParser< T, Length::UNKNOWN, PrefixEncoder::NONE, e, p>;
    using ISOConsumer = ISOFieldParser< std::vector<uint8_t>, Length::CONSUME, PrefixEncoder::NONE, Encoder::BINARY, Padder::NONE >;


}
