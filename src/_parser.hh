#pragma once

// [stdc++]
#include <deque>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
// [tng]
#include <iso8583/detail/_components.hh>
#include <iso8583/_codec.hh>

#include "_padder.hh"
#include "_logger.hh"   // TNG_LOG_* für die Fail-closed-Abfragen (B1/B2/B6)

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
        // Sentinel "nicht gesetzt": kleinstmöglicher Wert des jeweils aktiven
        // TNG_KEY_TYPE (int16_t standardmäßig, int32_t mit ISO8583_BERTLV).
        TNG_KEY_TYPE bmp_3rd_ = std::numeric_limits<TNG_KEY_TYPE>::min();
        // Header size if applicable;
        std::size_t hdr_sz_ = 0u;
        // [ISO8583] FR-3 (0.6.0): Container-Modus (Spec: type: nested,
        // text-basiertes Format, kein TLV): der Sub-Payload enthält
        // weder MTI noch Bitmap – Slot 0 ist das erste Kind-Feld.
        // Ohne dieses Flag würde die Slot-0/MTI-Semantik von
        // ISOBaseParser bei genau einem Kind-Feld das Kind im
        // Encode-Pfad doppelt serialisieren (MTI-Block UND Daten-Loop;
        // verkerrtes Längen-Prefix).
        mutable bool container_ = false;
        // [ISO8583] FR-12 (0.9.0): Bitmap-Container (nur mit container_) - der
        // Sub-Payload beginnt mit einer Bitmap von bitmap_container_ Byte;
        // Slot n = Bit n (Bit 1 ist ein NORMALES Kind, keine Sekundaer-
        // Bitmap), Slot 0 bleibt UNUSED. 0 = aus (Default, positionelle Kinder).
        mutable std::size_t bitmap_container_ = 0;
        // [ISO8583] FR-13 (0.9.0): Nibble-Packing (nur mit container_) - die
        // BCD-Kinder teilen sich ein Ziffern-Strom (1 Ziffer = 1 Nibble).
        // pack_digits_[slot] = Ziffernzahl des Kinds (0 = nop-Platzhalter);
        // pack_pad_/pack_pad_explicit_ = Padding bei ungerader Gesamtziffernzahl.
        mutable bool nibble_pack_ = false;
        mutable std::vector<std::size_t> pack_digits_;
        mutable ::TNG_NAMESPACE::codec::BcdPad pack_pad_ = ::TNG_NAMESPACE::codec::BcdPad::RIGHT_ZERO;
        mutable bool pack_pad_explicit_ = false;

        // Spezial-Pfade der Container-Modi (src/_parser.cc)
        std::vector<uint8_t> parseBitmapContainer(const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m) const;
        std::size_t unparseBitmapContainer(const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m,
            const std::vector<uint8_t>& b, std::size_t base_offset);
        std::vector<uint8_t> parseNibblePack(const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m) const;
        std::size_t unparseNibblePack(const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m,
            const std::vector<uint8_t>& b, std::size_t base_offset);
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

        // [ISO8583] FR-3 (0.6.0): Container-Modus setzen/abfragen
        // (s. container_). const-Setter wie strict(bool) – das Flag
        // betrifft ausschließlich dieses Parser-Objekt (Container-Sub-Parser).
        void container(bool v) const noexcept { container_ = v; }
        bool container() const noexcept { return container_; }

        // [ISO8583] FR-12 (0.9.0): Bitmap-Container-Modus (Bitmap-Groesse in Byte,
        // 0 = aus). Setzt zugleich den Container-Modus voraus (Loader).
        void bitmapContainer(std::size_t bytes) const noexcept { bitmap_container_ = bytes; }
        std::size_t bitmapContainer() const noexcept { return bitmap_container_; }

        // [ISO8583] FR-13 (0.9.0): Nibble-Packing-Modus. `digits[slot]` = Ziffern
        // je Kind-Slot (0 = nop-Platzhalter); `pad`/`padExplicit` = BCD-Padding.
        void nibblePack(std::vector<std::size_t> digits, ::TNG_NAMESPACE::codec::BcdPad pad,
            bool padExplicit) const {
            nibble_pack_ = true;
            pack_digits_ = std::move(digits);
            pack_pad_ = pad;
            pack_pad_explicit_ = padExplicit;
        }
        bool nibblePack() const noexcept { return nibble_pack_; }

        // [ISO8583] Strikter Modus (Default: true, s. ISOParserPtrBase::strict_).
        // Setzt den eigenen Modus UND propagiert ihn rekursiv auf alle
        // Feld-Parserv (inklusive der Inner-Parserv von NESTED-Feldern),
        // damit auch verschachtelte Sub-Nachrichten konsistent strikt/legacy
        // decodieren.
        using ISOParserPtrBase::strict; // Getter `bool strict() const` bleiben sichtbar
        void strict(bool v) const noexcept override {
            ISOParserPtrBase::strict(v);
            for (const auto& fp : l_) {
                fp->strict(v);
                if (auto sub = fp->subParser())
                    sub->strict(v);
            }
        }

        // Checks if the bitmap has to be emitted
        // \return true if bitmap has to be emitted otherwise false
        bool emit_bitmap() const noexcept override {
            // FR-3 (0.6.0): Container-Sub-Parser kennen keine Bitmap.
            return (
                !container_ && (
                field_parser(1) ? 
                    field_parser(1)->type() == TNG_NAMESPACE::ISOFieldParserType::BITMAP :
                        false
                )
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
            // FR-3 (0.6.0): Container-Modus – Slot 0 ist das erste
            // Kind-Feld (kein MTI/Bitmap-Vorfeld).
            if (container_)
                return 0;
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

    // [ISO8583] FR-3 (0.6.0): Fail-closed-Guard für den Container-Basis-
    // Parser (n_). Beide T=parser-Zweige von ISOFieldParser (parse: 
    // BinaryField-Wrapper, unparse: BinaryField-Scratch) setzen einen
    // binär-basierten Basis-Parser voraus. Ein nicht binär-basierter
    // Basis-Parser (z. B. string-basiertes lllchar) würde dort einen
    // Null-Pointer-Dereference auslösen (dynamic_pointer_cast<OpaqueField>
    // auf den BinaryField -> nullptr -> SIGSEGV).
    //
    // Die Spec-Ladung normalisiert Text-Container automatisch auf den
    // binären Zwilling (src/_spec.cc: containerBaseField) — dieser Guard
    // fängt daher nur manuell konstruierte ISONestedFieldParser-Instanzen
    // ab (Fail-closed statt SEGV).
    //
    // type() ist bei REMAINING nicht zwischen binär/string unterscheidbar
    // (l_ = UNKNOWN/CONSUME) -> zusätzlich create_component-Probe, aber
    // NUR bei type()==REMAINING (create_component eines UNUSED-Parsers
    // würde selbst werfen; OPAQUE/BINARY/... werden bereits über type()
    // entschieden).
    static void checkContainerBase(
        const ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr& base,
        TNG_KEY_TYPE de)
    {
        using PT = ::TNG_NAMESPACE::ISOFieldParserType;
        if (base == nullptr)
            throw std::runtime_error(
                "[ISO8583] Fail-closed (DE " + std::to_string(de) +
                "): Nested-Parser ohne Container-Basis-Parser "
                "(incomplett initialisiert).");
        if (base->type() != PT::BINARY && base->type() != PT::REMAINING)
            throw std::runtime_error(
                "[ISO8583] Fail-closed (DE " + std::to_string(de) +
                "): Container-Basis-Parser ist nicht binär-basiert. "
                "Text-basierte Containerformate (z. B. lllchar) werden beim "
                "Spec-Load automatisch auf den binären Zwilling normalisiert "
                "- dieser Fehler betrifft nur manuell konstruierte "
                "ISONestedFieldParser-Instanzen.");
        if (base->type() == PT::REMAINING &&
            std::dynamic_pointer_cast<::TNG_NAMESPACE::BinaryField>(
                base->create_component(0)) == nullptr)
            throw std::runtime_error(
                "[ISO8583] Fail-closed (DE " + std::to_string(de) +
                "): 'remaining'-Container-Basis ist text-basiert "
                "(binär-basierter Basis-Parser erforderlich; der Spec-Load "
                "normalisiert automatisch).");
    }

    template < typename T, codec::Length l_, codec::PrefixEncoder pe_, codec::Encoder e_, codec::Padder p_ >
    class TNG_EXPORT ISOFieldParser
        : public ISOFieldParserPtrBase
    {

        static_assert(l_ == codec::Length::FIX ? (pe_ != codec::PrefixEncoder::NONE ? false : true) : true, "Length::FIX must be used with PrefixEncoder::NONE");
        static_assert(l_ == codec::Length::UNKNOWN ? (pe_ != codec::PrefixEncoder::NONE ? false : true) : true, "Length::UNKNOWN must be used with PrefixEncoder::NONE");
        // Consumer
        static_assert(l_ == codec::Length::CONSUME ? (pe_ != codec::PrefixEncoder::NONE ? false : true) : true, "Length::CONSUMER must be used with PrefixEncoder::NONE");
        static_assert(l_ == codec::Length::CONSUME ? (e_ != codec::Encoder::BINARY ? false : true) : true, "Length::CONSUMER must be used with Encoder::BINARY");

        // Maximum allowed DE length
        std::size_t de_l_;
        // DE description
        std::string d_;

        // Data Element parser
        ::TNG_NAMESPACE::ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr n_ = nullptr;
        // Sub Element parser
        ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr c_ = nullptr;
        // Data container (NUR NOCH LEGACY - s. unparse(): Seit 3.3 wird
        // pro Aufruf ein lokaler Scratch-Buffer verwendet, damit ein über
        // mehrere Threads geteilter Parser keinen gemeinsamen, mutierten
        // Vector mehr besitzt (früher: b_ wurde bei JEDEM unparse() neu
        // befüllt -> Iterator-/Zeiger-Invalidation unter Concurrency).
        std::shared_ptr< ::TNG_NAMESPACE::BinaryField > b_ = nullptr;
        
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
            else if constexpr (l_ == codec::Length::UNKNOWN) // Special case, but required
                return ::TNG_NAMESPACE::ISOFieldParserType::REMAINING;
            else if constexpr (l_ == codec::Length::CONSUME) // Special case, but required
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
                // FR-3 (0.6.0): Fail-closed-Guard — ein nicht binär-
                // basierter Container-Basis-Parser crasht sonst unten
                // (BinaryField-Wrapper vs. string-basierter Basis-Parser).
                checkContainerBase(n_, c->key());
                // NESTED: c_->parse() serialisiert die Kind-Felder in rohe Bytes.
                // n_->parse() verpackt diese Bytes dann mit dem korrekten Längen-
                // Prefix (identisch zu unparse(), nur in umgekehrter Richtung).
                // c_ = innerer ISOBaseParser (kennt die Kind-Feld-Struktur)
                // n_ = äußerer ISOBinaryFieldParser (trägt Prefix-Encoding + Länge)
                auto inner_bytes = c_->parse(c);

                // Temporäres ISOBinaryField für n_ erstellen und mit den inneren
                // Bytes befüllen – n_->parse() fügt den korrekten Prefix hinzu.
                auto wrapper = std::make_shared< ::TNG_NAMESPACE::BinaryField >(c->key());
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
                // FR-10a (0.8.0): Typ-Guard statt ungeprüftem Cast-Dereferenz.
                auto opq = std::dynamic_pointer_cast< ISOOpaqueField >(c);
                if (!opq)
                    throw std::runtime_error(
                        "[ISO8583] DE" + std::to_string(c ? static_cast<int>(c->key()) : -1) +
                        " (Feld " + d_ + "): Komponente ist kein OpaqueField (Parser erwartet Textwert)");
                std::string data = nonstd::to_string((nonstd::string_view)opq->value());
                // [ISO8583] B2: Überdimensionierter Wert (Serialisierung zu groß).
                // strict: positioniert verwerfen; nicht-strikt: Legacy (loggen +
                // Feld auslassen) - ohne Abfrage würde der Wert still entsorgt,
                // ohne dass ein Fehler ersichtlich wäre.
                if (data.size() > de_l_) {
                    if (strict_)
                        throw std::runtime_error(
                            "Serialisierung zu groß: " + std::to_string(data.size()) +
                            " Einheiten > Maximum " + std::to_string(de_l_) +
                            " (Feld " + d_ + ")");
                    TNG_LOG_ERROR("[codec] Serialisierung zu groß (nicht-strikt): {} Einheiten > Maximum {} - Feld wird ausgelassen",
                        data.size(), de_l_);
                    return std::vector<uint8_t>{};
                }
                // FR-5 (Opt-in 'strict_length: true'): Unterlänge bei fester Länge.
                // strict: positioniert verwerfen; nicht-strikt: warnen + wie bisher
                // auffüllen. Nur Length::FIX (kein Präfix; 'remaining' = UNKNOWN ist
                // ein Maximum und nie betroffen).
                if (strict_length_ && l_ == codec::Length::FIX && data.size() < de_l_) {
                    if (strict_)
                        throw std::runtime_error(
                            "Serialisierung zu kurz: " + std::to_string(data.size()) +
                            " Einheiten < FIX-Länge " + std::to_string(de_l_) +
                            " (Feld " + d_ + ", strict_length)");
                    TNG_LOG_WARN("[codec] Serialisierung zu kurz (nicht-strikt, strict_length): {} Einheiten < FIX-Länge {} - Wert wird aufgefüllt (Feld {})",
                        data.size(), de_l_, d_);
                }
                codec::pad<p_>(data, de_l_); // Padding data
                std::vector<uint8_t> b_img(codec::parsed_length<pe_, l_>() + codec::required_sz_for_as<e_>(data.size()), 0);
                codec::encode_length<pe_, l_>(data.size(), b_img); // Encode length if applicable
                codec::to<e_>(data, b_img, codec::parsed_length<pe_, l_>(), strict_, bcd_pad_);
                return b_img;
            }
            else if constexpr (std::is_same_v< T, std::vector<uint8_t> >) {
                // FR-10a (0.8.0): Typ-Guard — ein ungeprüfter Cast-Dereferenz
                // endete bei falschem Komponententyp als Null-Zugriff.
                auto bin = std::dynamic_pointer_cast< ::TNG_NAMESPACE::BinaryField >(c);
                if (!bin)
                    throw std::runtime_error(
                        "[ISO8583] DE" + std::to_string(c ? static_cast<int>(c->key()) : -1) +
                        " (Feld " + d_ + "): Komponente ist kein BinaryField (Parser erwartet Binärwert)");
                std::vector<uint8_t> data = bin->value();
                std::size_t pl = codec::parsed_length<pe_, l_>();
                // [ISO8583] B2: FIX-Feld mit passender Länge / prefixed Feld
                // mit zu großem Wert. strict: verwerfen; nicht-strikt: Legacy
                // (loggen; bei prefixed Feldern kann das Präfix dadurch
                // unterdimensioniert bleiben).
                // FR-10a (0.8.0): 'remaining' (UNKNOWN/CONSUME) hat ebenfalls
                // pl == 0, 'length' ist dort aber nur ein Maximum - die
                // FIX-Prüfung gilt nur für Length::FIX (Muster FR-5).
                if (l_ == codec::Length::FIX && pl == 0 && data.size() != de_l_) {
                    if (strict_)
                        throw std::runtime_error(
                            "Serialisierung zu groß: " + std::to_string(data.size()) +
                            " Bytes != FIX-Länge " + std::to_string(de_l_) +
                            " (Feld " + d_ + ")");
                    TNG_LOG_ERROR("[codec] Serialisierung zu groß (nicht-strikt): {} Bytes != FIX-Länge {} - Feld wird ausgelassen",
                        data.size(), de_l_);
                    return std::vector<uint8_t>{};
                }
                if (pl > 0 && data.size() > de_l_) {
                    if (strict_)
                        throw std::runtime_error(
                            "Serialisierung zu groß: " + std::to_string(data.size()) +
                            " Bytes > Maximum " + std::to_string(de_l_) +
                            " (Feld " + d_ + ")");
                    TNG_LOG_ERROR("[codec] Serialisierung zu groß (nicht-strikt): {} Bytes > Maximum {} - Längenprfix könnte unterdimensioniert sein",
                        data.size(), de_l_);
                }
                // FR-10a: 'remaining' - Wert über dem deklarierten Maximum
                // (wie im String-Zweig: strict verwerfen, nicht-strikt
                // loggen und Feld auslassen).
                if (l_ == codec::Length::UNKNOWN && data.size() > de_l_) {
                    if (strict_)
                        throw std::runtime_error(
                            "Serialisierung zu groß: " + std::to_string(data.size()) +
                            " Bytes > Maximum " + std::to_string(de_l_) +
                            " (Feld " + d_ + ", remaining)");
                    TNG_LOG_ERROR("[codec] Serialisierung zu groß (nicht-strikt): {} Bytes > Maximum {} - Feld wird ausgelassen (remaining)",
                        data.size(), de_l_);
                    return std::vector<uint8_t>{};
                }
                std::vector<uint8_t> b_img(pl + codec::required_sz_for_as<e_>(data.size()), 0);
                codec::encode_length<pe_, l_>(data.size(), b_img); // Encode length if applicable
                codec::to<e_>(data, b_img, pl, strict_);
                return b_img;
            }
            else if constexpr (std::is_same_v< T, dynamic_bitset<> >) {
                // FR-10a (0.8.0): Typ-Guard statt ungeprüftem Cast-Dereferenz.
                auto bmpc = std::dynamic_pointer_cast< ::TNG_NAMESPACE::Bitmap >(c);
                if (!bmpc)
                    throw std::runtime_error(
                        "[ISO8583] DE" + std::to_string(c ? static_cast<int>(c->key()) : -1) +
                        " (Feld " + d_ + "): Komponente ist keine Bitmap");
                dynamic_bitset<> b = bmpc->value();
                std::size_t bytes = de_l_ >= 8 ? ((b.size() + 62) >> 6) << 3 : de_l_;
                // FR-9a (0.8.0): 'secondary: always' - Sekundär-Bitmap (16 Byte,
                // Bit 1) auch ohne Felder > 64. Der Loader erzwingt length >= 16;
                // ein programmatisch gesetztes Flag mit kleinerem length läuft
                // in den FR-9b-Guard unten.
                if (secondary_always_ && bytes < 16)
                    bytes = 16;
                // FR-9b (0.8.0): die Bitmap-Größe folgt den gesetzten Feldern,
                // nicht 'length' - Felder > 64 (bzw. > 128) mit 'length: 8'
                // (bzw. 16) ergäben eine Ausgabe, die der Decoder mit derselben
                // Spec nicht zurücklesen kann (er liest die Sekundär-Bitmap nur
                // ab length >= 16). strict: positionierter Fehler; nicht-strikt:
                // Warnung, Legacy-Ausgabe.
                if (de_l_ >= 8 && bytes > de_l_) {
                    const std::string msg =
                        "Bitmap: gesetzte Felder erfordern " +
                        std::to_string(bytes) + " Byte Bitmap, die Spec deklariert aber nur "
                        "'length: " + std::to_string(de_l_) + "' - die Ausgabe wäre mit dieser "
                        "Spec nicht rückdekodierbar ('length: " + std::to_string(bytes) +
                        "' setzen; FR-9)";
                    if (strict_)
                        throw std::runtime_error("[ISO8583] " + msg);
                    TNG_LOG_WARN("[codec] {} (nicht-strikt: Legacy-Ausgabe)", msg);
                }
                std::size_t bits = bytes * 8;

                std::vector<uint8_t> d(bytes, 0x00);
                // FR-9a: bei erzwungener Sekundär-Bitmap kann 'bits' die Bitset-
                // Größe übersteigen (Bit-Index i+1 muss < b.size() bleiben).
                const std::size_t setBits = std::min(bits, b.size() > 0 ? b.size() - 1 : std::size_t{ 0 });
                for (std::size_t i = 0u; i < setBits; ++i)
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
                // FR-3 (0.6.0): Fail-closed-Guard — ein nicht binär-
                // basierter Container-Basis-Parser crasht sonst unten
                // (BinaryField-Scratch vs. string-basierter Basis-Parser).
                checkContainerBase(n_, c->key());
                // [ISO8583] 3.3 (Thread-Sicherheit): PRO AUFRUF lokaler
                // Scratch-Buffer statt des Parser-Mitglieds b_. Der Parser
                // ist nach dem Load unveränderlich und wird über mehrere
                // Threads/Nachrichten geteilt (Doku ISOMessage.hh/_interfaces.hh)
                // - b_ würde bei jedem unparse() neu befüllt (Vector-
                // Reallokation), während ein zweiter Thread noch aus dem
                // selben Vector decodiert/iteriert -> "vector iterators in
                // range are from different containers" / "cannot seek
                // invalidated vector iterator" (MSVC-Debug-Asserts) bzw. AV.
                // parse() verwendet bereits das gleiche Muster (wrapper).
                auto scratch = std::make_shared< ::TNG_NAMESPACE::BinaryField >(0);
                // n_ liest den Längen-Prefix und extrahiert die Nutzdaten
                // nach scratch (lokal, keine geteilte Zustandsmutation).
                // consumed_outer = Bytes die n_ im Original-Buffer b verbraucht hat
                // (Längen-Prefix + Nutzdaten).
                std::size_t consumed_outer = n_->unparse(scratch, b, o);
                const std::vector<uint8_t>& payload = scratch->value();

                if (c->is_composite() && !payload.empty()) {
                    // BUG-FIX: parsed_length<pe_, l_>() ist für ISONestedFieldParser
                    // immer 0 (pe_=NONE, l_=FIX), weil pe_/l_ die Template-Parameter
                    // des äußeren ISOFieldParser<T,...> sind – nicht die des inneren n_.
                    //
                    // Der tatsächliche Prefix-Offset ergibt sich aus der Differenz:
                    //   consumed_outer = Prefix-Bytes + Nutzdaten-Bytes
                    //   payload.size() = Nutzdaten-Bytes
                    //   → actual_prefix = consumed_outer - payload.size()
                    //
                    // Beispiel BMP_003 (format: binary, length: 6, kein Prefix):
                    //   consumed_outer = 6, payload.size() = 6 → prefix = 0 ✓
                    //
                    // Beispiel DE063 (format: LLLBINARY, length: 50, 3 Bytes Prefix):
                    //   consumed_outer = 53, payload.size() = 50 → prefix = 3 ✓
                    const std::size_t actual_prefix = consumed_outer >= payload.size()
                        ? consumed_outer - payload.size()
                        : 0;
                    const std::size_t child_base_offset = o + actual_prefix;
                    c_->unparse(c, payload, child_base_offset);
                }
                // FE-1 (0.6.0): Container-Sub-Parser an die Kind-Nachricht anhängen,
                // damit ein aus der Voll-Nachricht dekodiertes DE sich selbst
                // re-serialisieren kann (de55->parse(de55) -> Kind-Frames ohne
                // äußeren Prefix = Field-only-Wire-Vertrag). Ohne den Parser hätte
                // die Kind-Nachricht p_ == nullptr und ISOMessage::parse() würde
                // leer zurückgeben - der Dot-Pfad (set_recursive_locked,
                // _components.cc) hängt denselben Sub-Parser bereits an.
                if (c->is_composite())
                    if (auto childMsg = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOMessage>(c))
                        childMsg->parser(c_);
                return consumed_outer;
            }
            if constexpr (!std::is_same_v< T, dynamic_bitset<> >) {
                if constexpr (std::is_same_v< T, std::nullptr_t >) {
                    return 0;
                }
                
                std::size_t l = 0u;
                const std::size_t ll = codec::parsed_length<pe_, l_>();

                // [ISO8583] B6: Längenprefix-Vorguard (beide Modi - Sicherheit):
                // Der Prefix selbst muss vollständig im Puffer liegen, sonst
                // wären die Decode-Lesungen (decode_length) unterhalb des
                // Pufferendes ein OOB-Read. Zwingend VOR decode_length():
                // die decode_length-Lesungen (b.at) werfen andernfalls eine
                // rohe std-Exception (out_of_range) statt dieses
                // positionierten Fehlers — die Guard würde sonst nie greifen.
                // (Für UNKNOWN/CONSUME ist ll == 0 → Guard ist ein No-Op.)
                if (ll > 0 && (o > b.size() || o + ll > b.size()))
                    throw std::runtime_error(
                        "Längenprefix am Pufferende abgeschnitten: benötigt " + std::to_string(ll) +
                        " Prefix-Bytes ab Offset " + std::to_string(o) + ", im Puffer vorhanden " +
                        std::to_string(o < b.size() ? b.size() - o : 0) + " Bytes");

                if constexpr (l_ == codec::Length::UNKNOWN || l_ == codec::Length::CONSUME) {
                    l /* remaining */ = b.size() - o;
                    // (0.6.0) remaining + BCD: b.size() - o ist die Byte-Zahl,
                    // BCD-Codecs zählen aber Ziffern (1 Byte = 2 Ziffern).
                    // Ohne Umrechnung würde as<string, BCD> nur die halbe
                    // Pufferlänge dekodieren.
                    if constexpr (codec::Encoder::BCD == e_)
                        l *= 2u;
                }
                else
                    l = codec::decode_length<pe_, l_>(b, o); // Something like this?

                // Checks for length are different between types
                if constexpr (l_ != codec::Length::CONSUME)
                    if (l == 0 || l > de_l_)
                        l = de_l_;

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
                //
                // [ISO8583] B1: Feldinhalt am Pufferende abgeschnitten.
                // strict: positioniertes std::runtime_error (Fail-closed, P2);
                // nicht-strikt: Legacy (loggen + auf verfügbare Einheiten kürzen).
                {
                    const std::size_t available_bytes = b.size() - (o + ll);
                    const std::size_t needed_bytes = codec::required_sz_for_as<e_>(l);
                    if (available_bytes < needed_bytes) {
                        if (strict_)
                            throw std::runtime_error(
                                "Feld am Pufferende abgeschnitten: erwartet " + std::to_string(l) +
                                " logische Einheiten (" + std::to_string(needed_bytes) +
                                " Bytes), verbleiben nur " + std::to_string(available_bytes) +
                                " Bytes ab Offset " + std::to_string(o + ll));
                        TNG_LOG_WARN("[codec] Feld am Pufferende abgeschnitten (nicht-strikt): erwartet {} Einheiten ({} Bytes), verbleiben {} Bytes - Wert wird gekürzt",
                            l, needed_bytes, available_bytes);
                        // Wie viele vollständige logische Einheiten passen in available_bytes?
                        if constexpr (TNG_NAMESPACE::codec::Encoder::BCD == e_)
                            l = available_bytes * 2;  // 1 Byte = 2 BCD-Ziffern
                        else
                            l = available_bytes;
                    }
                }

                // FR-7 (0.7.1): deklariertes 'bcd_pad:' → Padding-Nibble bei ungerader
                // Ziffernzahl validieren. strict: positionierter Fehler; nicht-strikt:
                // Warnung. Ohne Deklaration (Legacy) bleibt das Nibble ungeprüft.
                if constexpr (codec::Encoder::BCD == e_ && std::is_same_v< T, std::string >)
                    if (bcd_pad_explicit_ && !codec::detail::bcd_pad_nibble_ok(b, o + ll, l, bcd_pad_)) {
                        if (strict_)
                            throw std::runtime_error(
                                "BCD-Padding-Nibble weicht von 'bcd_pad' ab (Feld " + d_ +
                                ", Offset " + std::to_string(o + ll) + ", " + std::to_string(l) +
                                " Ziffern, erwartet " + (bcd_pad_ == codec::BcdPad::RIGHT_F ? "F hinten"
                                    : bcd_pad_ == codec::BcdPad::LEFT_ZERO ? "0 vorn" : "0 hinten") + ")");
                        TNG_LOG_WARN("[codec] BCD-Padding-Nibble weicht von 'bcd_pad' ab (nicht-strikt): Feld {}, Offset {}, {} Ziffern",
                            d_, o + ll, l);
                    }

                // Skip allocation of temporary function stack variable
                if constexpr (l_ != codec::Length::CONSUME)
                    if constexpr (std::is_same_v< T, std::string >)
                        // [ISO8583] Q4: strict-Modus gilt auch fuer die Codec-
                        // Dekodierung (EBCDIC-Whitelist-Pruefung); nicht-strikt
                        // bleibt die Legacy-Sentinel-Mapping (0x2E '.') erhalten.
                        (void)std::dynamic_pointer_cast< ::TNG_NAMESPACE::OpaqueField >(c)->value(::TNG_NAMESPACE::codec::as< T, e_ >(b, o + ll, l, strict_, bcd_pad_));
                    else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
                        (void)std::dynamic_pointer_cast< ::TNG_NAMESPACE::BinaryField >(c)->value(::TNG_NAMESPACE::codec::as< T, e_ >(b, o + ll, l, strict_));

                const std::size_t total = ll + codec::required_sz_for_as<e_>(l); // l= 3 ---> 3
                // wire_length: hier gesetzt, da erst jetzt die tatsächliche Byte-Länge bekannt ist.
                // wire_offset: wird vom aufrufenden ISOBaseParser mit dem absoluten Offset gesetzt,
                //              da ISOFieldParser nur den lokalen Offset o im aktuellen Sub-Buffer kennt.
                c->wire_length(total);
                return total;
            }
            else {
                // byte2bitset
                // [ISO8583] A1: OOB-Guards (beide Modi - Sicherheit): Bitmap-
                // Offset und -Bytes müssen vollständig im Puffer liegen.
                {
                    if (o >= b.size())
                        throw std::runtime_error(
                            "Bitmap-Offset " + std::to_string(o) +
                            " liegt außerhalb des Puffers (Größe " + std::to_string(b.size()) + ")");
                    bool  b1 = (b[o] & 0x80) == 0x80;
                    bool b65 = (b.size() > o + 8) && ((b[o + 8] & 0x80) == 0x80);
                    std::size_t mbits = de_l_ << 3;
                    std::size_t len = (mbits > 128 && b1 && b65) ?   192 :
                                      (mbits > 64 && b1)         ?   128 :
                                      (mbits < 64)               ? mbits : 64;

                    const std::size_t bmp_bytes = len >> 3;
                    if (o + bmp_bytes > b.size())
                        throw std::runtime_error(
                            "Bitmap am Pufferende abgeschnitten: benötigt " + std::to_string(bmp_bytes) +
                            " Bytes ab Offset " + std::to_string(o) + ", im Puffer vorhanden " +
                            std::to_string(b.size() - o) + " Bytes");

                    dynamic_bitset<> bmp(len+1);
                    for (std::size_t i = 0; i < len; ++i)
                        if ((b[o + (i >> 3)] & (0x80 >> (i % 8))) > 0)
                            bmp.set(i+1);

                    len = bmp[1] ? 128 : 64;
                    if (de_l_ > 16 && bmp.size() > 65 && bmp[1] && bmp[65])
                        len = 192;

                    (void)std::dynamic_pointer_cast< ::TNG_NAMESPACE::Bitmap >(c)->value(bmp);
                    return std::min(de_l_, len >> 3);
                }
            }
        }

        virtual ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr create_component(TNG_KEY_TYPE key) const override {
            if constexpr (std::is_same_v< T, std::nullptr_t >)
                return std::make_shared< ISOOpaqueField >(key);
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
                return std::make_shared< ::TNG_NAMESPACE::OpaqueField >(key);
            else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
                return std::make_shared< ::TNG_NAMESPACE::BinaryField >(key);
            else if constexpr (std::is_same_v< T, int32_t >)
                return std::make_shared< ::TNG_NAMESPACE::CodeField >(key);
            else if constexpr (std::is_same_v< T, dynamic_bitset<> >)
                return std::make_shared< ::TNG_NAMESPACE::Bitmap >(key);
            else if constexpr (std::is_base_of_v<ISOBaseParser, T>)
                return std::make_shared< ::TNG_NAMESPACE::ISOMessage >(key);
            else
                static_assert(::TNG_NAMESPACE::dependent_false<T>::value, "don't know how to create component");
        }
    };

    template < codec::Length l, codec::PrefixEncoder pe, codec::Encoder e, codec::Padder p = codec::Padder::NONE >
    using ISOBinaryFieldParser = ISOFieldParser< std::vector<uint8_t>, l, pe, e, p >;
    template < codec::Length l, codec::PrefixEncoder pe, codec::Encoder e, codec::Padder p = codec::Padder::NONE >
    using ISOOpaqueFieldParser = ISOFieldParser< std::string, l, pe, e, p >;
    template < codec::Length l, codec::PrefixEncoder pe, codec::Encoder e, codec::Padder p = codec::Padder::NONE >
    using ISOCodeFieldParser = ISOFieldParser< int32_t, l, pe, e, p >;

    using ISOBitmapFieldParser = ISOFieldParser< dynamic_bitset<>, codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BINARY, codec::Padder::NONE >;
    template < typename ISOField >
    using ISONestedFieldParser = ISOFieldParser< ISOField, codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BINARY, codec::Padder::NONE >;

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
    template < typename T, codec::Encoder e, codec::Padder p = codec::Padder::NONE >
    using ISORemainderFieldParser = ISOFieldParser< T, codec::Length::UNKNOWN, codec::PrefixEncoder::NONE, e, p>;
    using ISOConsumer = ISOFieldParser< std::vector<uint8_t>, codec::Length::CONSUME, codec::PrefixEncoder::NONE, codec::Encoder::BINARY, codec::Padder::NONE >;

    // ── AmountFieldParser ────────────────────────────────────────────────────
    // Dünner abgeleiteter Parser für den AMOUNT-Feldtyp (0.6.0):
    // vererbt parse/unparse unverändert von ISOFieldParser<std::string, …>
    // (AmountField leitet von OpaqueField ab — der String-Pfad greift),
    // override nur type() und create_component (→ AmountField).
    //
    // Verwendung in YAML:   format: amount|ascii | amount|bcd | amount|ebcdic
    // Verwendung in Code:   IFA_AMOUNT / IFB_AMOUNT / IFE_AMOUNT
    template < codec::Length l, codec::PrefixEncoder pe, codec::Encoder e, codec::Padder p >
    class TNG_EXPORT AmountFieldParser : public ISOFieldParser< std::string, l, pe, e, p >
    {
    public:
        // [Constructor] — delegiert an die Basis-Konstruktoren (Länge/Beschreibung,
        // Nested), damit z. B. die Spec-Parser-Tabelle (MAKE-Makro) den Parser
        // mit (len, desc) erzeugen kann. Ein using-declaration für die
        // Basis-Konstruktoren ist hier nicht möglich (MSVC C2873:
        // Using-Deklaration für Basiskonstruktoren eines Template-Spezialisierung
        // in einer Klassen-Vorlage).
        explicit AmountFieldParser()
            : ISOFieldParser< std::string, l, pe, e, p >() {}

        explicit AmountFieldParser(std::size_t len, nonstd::string_view desc)
            : ISOFieldParser< std::string, l, pe, e, p >(len, desc) {}

        template < typename U = std::string, typename std::enable_if_t<std::is_base_of_v<ISOBaseParser, U>, int> = 0 >
        explicit AmountFieldParser(ISOFieldParserPtrBase::ISOFieldParserPtrBaseSmartPtr nested,
            nonstd::string_view composite_description)
            : ISOFieldParser< std::string, l, pe, e, p >(std::move(nested), composite_description) {}

        const ISOFieldParserType type() const override { return ISOFieldParserType::AMOUNT; }

        // 0.6.0: Wire-Form (jPOS vs. Standard-ISO-8583) wird vom Loader über
        // 'scale:' gesetzt. nullopt → jPOS-Form (Default) bleibt.
        void setAmountScale(std::optional<int> sc) override
        {
            if (sc.has_value())
            {
                form_ = AmountForm::plain;
                declared_scale_ = *sc;
            }
        }

        // Nach 0.6.0: führendes Vorzeichenzeichen (nur mit Standardform).
        void setAmountSigned(bool sg) override { signed_ = sg; }

        ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr create_component(TNG_KEY_TYPE key) const override
        {
            if (form_ == AmountForm::plain)
                return std::make_shared<AmountField>(key, AmountForm::plain, declared_scale_, signed_);
            return std::make_shared<AmountField>(key);
        }

    private:
        // 0.6.0: Wire-Form (jPOS-ISOAmount 16-Z vs. Standard-ISO-8583 nackte Ziffern).
        AmountForm form_ { AmountForm::jpos };
        int declared_scale_ = 0;
        bool signed_ = false;
    };


}
