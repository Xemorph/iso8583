#pragma once

// =============================================================================
// ISOTLVParser<TagPolicy, LenPolicy, HAS_TCC, TCC_ENC>
// =============================================================================
//
// Generalisierte Fassung: Tag- und Length-Kodierung sind als Policy-Typen
// parametrisiert (siehe _tlv_policy.hh), statt fest auf eine Byte-Anzahl
// und einen Encoder verdrahtet zu sein. Das erlaubt sowohl die bisherigen
// festen ISO-8583-TLV-Varianten (Mastercard/Visa) als auch BER-TLV
// (ISO/IEC 8825-1, EMV Book 3 Annex B), bei dem Tag- und Length-Felder
// variabel lang sind.
//
// TCC (Tag/Coding-Control-Byte, wie z.B. bei Mastercard DE55 vorangestellt)
// ist kein Teil der eigentlichen Tag/Length-Struktur und daher über HAS_TCC
// + einen eigenen Encoder (TCC_ENC) parametrisiert, statt über TagPolicy
// mitzulaufen - eine BER-TLV-Struktur hat kein TCC-Feld in diesem Sinn.
//
// Rückwärtskompatibilität: ISOTLVParser_MC und ISOTLVParser_VI verhalten
// sich exakt wie zuvor (siehe Aliase am Ende dieser Datei) und werden nach
// wie vor vollständig zur Compile-Zeit instanziiert/inlinebar - TagPolicy/
// LenPolicy sind reine Typen ohne Laufzeit-Overhead. Nur BerTag/BerLength
// bringen echte Laufzeit-Verzweigung mit, weil die Byte-Anzahl von den
// Daten abhängt - das ist inhärent zu BER-TLV, kein Overhead durch das
// Pattern selbst.
// =============================================================================

// [stdc++]
#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>
// [tng]
#include <iso8583/iso8583.h>
#include "_parser.hh"
#include "_tlv_policy.hh"

namespace TNG_NAMESPACE {

    namespace tlv_detail {

        // ── Laufzeit-Helfer (implementiert in _tlv.cc) ────────────────────────
        // Kapseln alle TNG_LOG_* Aufrufe damit diese nicht in jeder
        // Template-Instanziierung inline kompiliert werden und keine
        // "unresolved symbol"-Fehler in Test-TUs erzeugen.

        TNG_EXPORT void log_error_not_composite();
        TNG_EXPORT void log_warn_tcc_missing();
        TNG_EXPORT void log_warn_tcc_not_set();
        TNG_EXPORT void log_warn_se_missing(std::size_t se_num);
        TNG_EXPORT void log_error_se_overflow(std::size_t se_num, std::size_t se_len,
            std::size_t pos, std::size_t buf_sz);
        TNG_EXPORT void log_debug_tcc(const std::string& tcc);
        TNG_EXPORT void log_debug_se_read(std::size_t se_num, std::size_t se_len);
        TNG_EXPORT void log_debug_se_write(std::size_t se_num, std::size_t data_sz);

        TNG_EXPORT std::vector<TNG_KEY_TYPE> sorted_se_keys(const ISO_MAP& fields);

        // ------------------------------------------------------------------------
        // FR-1 (0.5.0): Einheitliche Kind-Struktur für TLV-Kind-Deklarationen
        // ------------------------------------------------------------------------

        /// @brief Einheitliche Kind-Struktur für TLV-Kind-Deklarationen
        ///        (YAML-'children'-Einträge, s. docs/internals/yaml_format.md).
        ///        Seit 0.5.0 ersetzt sie die drei parallelen Maps
        ///        (DataEncodingMap/DescriptionMap/SensitiveMap).
        struct TlvChildInfo {
            codec::Encoder enc = codec::Encoder::BINARY; ///< Daten-Encoding (Text-Kind: ascii/ebcdic/bcd, sonst BINARY = rohe Bytes)
            bool           text = false;                 ///< true = char/numeric/nopad_char → OpaqueField
            std::string    description;                  ///< explizite Beschreibung (leer = "SE<n>"-Fallback)
            bool           sensitive = false;            ///< PCI-Masking (Kind-ebene oder vom Elternfeld geerbt)
            bool           amount = false;               ///< 0.6.0: format amount → AmountField statt OpaqueField
            bool           sign = false;                 ///< nach 0.6.0: 'sign: true' (führendes C/D/+/-, nur amount+scale)
            std::optional<int> scale;                    ///< 0.6.0: deklarierte 'scale:' (nullopt = jPOS-Form; nur amount)
            // 0.6.4: constructed-Container-Kind (TLV-Kind mit eigenem 'tlv:'-
            // Block): der Kind-Wert ist selbst eine Folge von TLVs und wird
            // nicht als Skalar (Skalar-Felder oben) sondern über subParser
            // rekursiv in eine Sub-Message dekodiert / zurückkodiert.
            bool                             container = false; ///< true = Container-Kind (constructed)
            std::shared_ptr<ISOParserPtrBase> subParser;         ///< Sub-Parser des Container-Kinds (read-only nach Build; container ⇒ gesetzt)
            // FR-7 (0.8.0): 'bcd_pad:' für BCD-Text-Kinder (Feld-Key oder Root-Default).
            // bcd_pad_explicit = deklariert → Decode validiert das Padding-Nibble und
            // nutzt die deklarierte 'length' (Ziffern, ungerade) als Ziffernzahl.
            codec::BcdPad  bcd_pad = codec::BcdPad::RIGHT_ZERO; ///< Padding-Variante bei ungerader Ziffernzahl
            bool           bcd_pad_explicit = false;            ///< true = 'bcd_pad' deklariert (validieren)
            std::size_t    digits = 0;                          ///< deklarierte 'length' (Ziffern; 0 = unbekannt); nur mit bcd_pad_explicit
        };

        /// @brief Tag (bzw. SE-Nummer) → TlvChildInfo.
        using TlvChildMap = std::unordered_map<std::size_t, TlvChildInfo>;

        /// Runtime-Dispatch über das Kind-Encoding: `codec::as<>`/`codec::to<>`
        /// sind Templates über das COMPILE-ZEIT-Encoding, das Kind-Encoding
        /// ist erst zur Laufzeit bekannt.
        /// @note BINARY wird manuell behandelt (rohe Byte-Kopie), da
        ///       `codec::to<BINARY, std::string>` keine gültige Instanzierung ist.
        /// FR-7: Ziffernzahl eines BCD-Text-Kinds aus der TLV-Byte-Länge. Ohne
        /// 'bcd_pad' (Legacy) = 2 Ziffern/Byte. Mit 'bcd_pad' und einer deklarierten
        /// **ungeraden** 'length' (Ziffern), die zur Byte-Länge passt
        /// (`(length + 1) / 2 == bytes`), wird die deklarierte Ziffernzahl benutzt —
        /// nur so lässt sich das Padding-Nibble von einer Ziffer unterscheiden.
        inline std::size_t child_bcd_digits(const TlvChildInfo& child, std::size_t bytes) noexcept {
            if (child.bcd_pad_explicit && (child.digits & 1u) != 0u &&
                (child.digits + 1) / 2 == bytes)
                return child.digits;
            return bytes * 2;
        }

        inline std::string child_as_string(const TlvChildInfo& child, const std::vector<uint8_t>& buf,
            std::size_t offset, std::size_t length, bool strict) {
            const codec::Encoder enc = child.enc;
            switch (enc) {
                case codec::Encoder::ASCII:  return codec::as< std::string, codec::Encoder::ASCII >(buf, offset, length, strict);
                case codec::Encoder::EBCDIC: return codec::as< std::string, codec::Encoder::EBCDIC >(buf, offset, length, strict);
                case codec::Encoder::BCD:
                    // BCD: TLV-Länge ist in BYTES, codec::as<...,BCD> zählt
                    // ZIFFERN (2 pro Byte) -> Factor 2 (gerade Byte-Zahl,
                    // da BCD immer 2 Ziffern pro Byte packt).
                    return codec::as< std::string, codec::Encoder::BCD >(buf, offset,
                        child_bcd_digits(child, length), strict, child.bcd_pad);
                case codec::Encoder::BINARY: // Fall-through – rohe Bytes
                default:
                    return std::string(buf.begin() + static_cast<std::ptrdiff_t>(offset),
                                       buf.begin() + static_cast<std::ptrdiff_t>(offset + length));
            }
        }

        inline void child_to_string(const TlvChildInfo& child, const std::string& value,
            std::vector<uint8_t>& out, std::size_t offset, bool strict) {
            const codec::Encoder enc = child.enc;
            switch (enc) {
                case codec::Encoder::ASCII:  codec::to< codec::Encoder::ASCII >(value, out, offset, strict); break;
                case codec::Encoder::EBCDIC: codec::to< codec::Encoder::EBCDIC >(value, out, offset, strict); break;
                case codec::Encoder::BCD:    codec::to< codec::Encoder::BCD >(value, out, offset, strict, child.bcd_pad); break;
                case codec::Encoder::BINARY: // Fall-through – rohe Byte-Kopie
                default:
                    for (std::size_t i = 0; i < value.size(); ++i)
                        out[offset + i] = static_cast<uint8_t>(value[i]);
                    break;
            }
        }

        /// Benötigte Byte-Anzahl der kodierten Darstellung eines String-Werts
        /// (zur Laufzeit, vgl. `codec::required_sz_for_as<e>`).
        inline std::size_t child_required_sz(codec::Encoder enc, std::size_t n) {
            if (enc == codec::Encoder::BCD)
                return (n + 1) / 2;
            return n; // ASCII / EBCDIC / BINARY: 1:1
        }

        /// Speichert ein dekodiertes SE in die Ziel-Message.
        ///
        /// FR-1 (0.5.0): Deklarierte Text-Kinder (char/numeric/nopad_char)
        /// werden per Codec in eine OpaqueField gespeichert (strict: nicht-mappbare
        /// Bytes werfen ein std::runtime_error; nicht-strikt: Legacy-Sentinel-Mapping);
        /// binäre Kinder und undeklarierte Tags bleiben BinaryField (rohe Bytes).
        /// 0.6.4: Container-Kinder (constructed, `child->container` + `child->subParser`)
        /// erzeugen KEIN Skalar-Feld: der Kind-Wert (Folge von TLVs) wird über den
        /// Sub-Parser in eine Sub-Message dekodiert und per `msg->set()` angehängt
        /// (innere TLVs sind dann per Punkt-Notation erreichbar, z.B. "57.69.63").
        TNG_EXPORT void store_se(
            const std::shared_ptr<ISOMessage>& msg,
            std::size_t  se_num,
            const std::vector<uint8_t>& buf,
            std::size_t  data_offset,
            std::size_t  data_len,
            std::size_t  wire_offset,
            std::size_t  wire_len,
            const nonstd::string_view& description,
            bool         sensitive,
            const TlvChildInfo* child, ///< nullptr = undeklariertes Tag
            bool         strict);      ///< propagierter Strict-Modus (Codec-Whitelist)

    } // namespace tlv_detail

    // ── ISOTLVParser ──────────────────────────────────────────────────────────

    template <
        typename TagPolicy,
        typename LenPolicy,
        bool HAS_TCC = false,
        codec::Encoder TCC_ENC = codec::Encoder::EBCDIC
    >
    class TNG_EXPORT ISOTLVParser : public ISOBaseParser {
        ISOTLV_STATIC_ASSERT_TAG_POLICY(TagPolicy);
        ISOTLV_STATIC_ASSERT_LEN_POLICY(LenPolicy);

    public:
        static constexpr TNG_KEY_TYPE TCC_KEY = -2;

        /// @brief Konstruiert den TLV-Parser.
        /// @param child_map  Deklarierte Kind-Elemente (Tag/SE → TlvChildInfo;
        ///        FR-1/FR-2, 0.5.0). Leere Map = alles undeklariert → BinaryField.
        /// @param sensitive_all  PCI: gesamtes Feld sensitiv (erbt alle Kinder).
        explicit ISOTLVParser(tlv_detail::TlvChildMap child_map = {},
            bool sensitive_all = false)
            : ISOBaseParser("<tlv>", 0)
            , child_map_(std::move(child_map))
            , sensitive_all_(sensitive_all)
        {
        }

        bool emit_bitmap() const noexcept override {
            return false;
        }

        // [ISO8583] Strikter Modus: 0.6.4-Container-Kinder (constructed)
        // halten ihre Sub-Parserv in child_map_ — diese sind NICHT eigene
        // Feld-Parserv dieses Parsers (l_ ist bei einem reinen TLV-Parser
        // leer), daher erreicht sie ISOBaseParser::strict() nicht. Hier
        // explizit weiterreichen (rekursiv: die Sub-Parserv sind selbst
        // ISOTLVParser/ISOBaseParser und propagieren auf ihre eigenen Kinder).
        void strict(bool v) const noexcept override {
            ISOBaseParser::strict(v);
            for (const auto& [_, info] : child_map_)
                if (info.container && info.subParser)
                    info.subParser->strict(v);
        }

        std::size_t unparse(
            ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
            const std::vector<uint8_t>& b) override
        {
            return unparse(c, b, 0);
        }

        std::size_t unparse(
            ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
            const std::vector<uint8_t>& b,
            std::size_t base_offset) override
        {
            auto msg = std::dynamic_pointer_cast< ::TNG_NAMESPACE::ISOMessage >(c);
            if (!msg) { tlv_detail::log_error_not_composite(); return 0; }

            std::size_t pos = 0;

            if constexpr (HAS_TCC) {
                if (b.empty()) { tlv_detail::log_warn_tcc_missing(); return 0; }
                const auto tcc_str = ::TNG_NAMESPACE::codec::as<std::string, TCC_ENC>(b, pos, 1);
                auto tcc = std::make_shared< ::TNG_NAMESPACE::OpaqueField> (TCC_KEY);
                tcc->value(tcc_str);
                tcc->description("TCC");
                tcc->wire_offset(base_offset + pos);
                tcc->wire_length(1);
                msg->set(tcc);
                pos += 1;
                tlv_detail::log_debug_tcc(tcc_str);
            }

            // Anders als in der Fixed-Byte-Variante lässt sich die
            // Mindest-Restlänge (Tag + Length) nicht mehr vorab konstant
            // bestimmen, da beide Felder variabel lang sein können (BER).
            // Jede Policy meldet Lesefehler daher selbst über consumed==0
            // (und loggt sie), was die Schleife sauber abbricht.
            while (pos < b.size()) {
                const auto [se_num, tag_consumed] = TagPolicy::read(b, pos);
                if (tag_consumed == 0)
                    break; // Fehler bereits von TagPolicy geloggt
                const std::size_t tag_start = pos;
                pos += tag_consumed;

                if (pos >= b.size()) {
                    tlv_detail::log_error_se_length_missing(se_num, pos, b.size());
                    break;
                }

                const auto [se_len, len_consumed] = LenPolicy::read(b, pos);
                if (len_consumed == 0)
                    break; // Fehler bereits von LenPolicy geloggt
                pos += len_consumed;

                if (pos + se_len > b.size()) {
                    tlv_detail::log_error_se_overflow(se_num, se_len, pos, b.size());
                    break;
                }

                // FR-1 (0.5.0): deklarierter Text-Kind → OpaqueField (Codec),
                // sonst/undeklariert → BinaryField; strict wird propagiert.
                const tlv_detail::TlvChildInfo* child = child_info(se_num);
                tlv_detail::store_se(msg, se_num, b, pos, se_len,
                    base_offset + tag_start,
                    (pos - tag_start) + se_len,
                    description_for_wire(se_num),
                    sensitive_for_wire(se_num),
                    child,
                    strict_);
                tlv_detail::log_debug_se_read(se_num, se_len);
                pos += se_len;
            }

            return pos;
        }

        std::vector<uint8_t> parse(ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) const override {
            auto msg = std::dynamic_pointer_cast< ::TNG_NAMESPACE::ISOMessage >(c);
            if (!msg) { 
                tlv_detail::log_error_not_composite(); return {}; 
            }

            std::vector<uint8_t> out;

            if constexpr (HAS_TCC) {
                auto tcc_comp = msg->get< ::TNG_NAMESPACE::OpaqueField >(TCC_KEY);
                if (!tcc_comp) {
                    tlv_detail::log_warn_tcc_not_set();
                    if constexpr (TCC_ENC == codec::Encoder::ASCII)      out.push_back(0x20u);
                    else if constexpr (TCC_ENC == codec::Encoder::BCD)   out.push_back(0x00u);
                    else                                           out.push_back(0x40u);
                }
                else {
                    std::vector<uint8_t> tcc_buf(1, 0x00);
                    ::TNG_NAMESPACE::codec::to<TCC_ENC>(tcc_comp->value(), tcc_buf, 0);
                    out.insert(out.end(), tcc_buf.begin(), tcc_buf.end());
                }
            }

            const auto se_keys = tlv_detail::sorted_se_keys(msg->value());

            for (const TNG_KEY_TYPE se_key : se_keys) {
                const std::size_t se_num = static_cast<std::size_t>(se_key);
                // FR-1 (0.5.0): deklarierter Text-Kind (char/numeric/nopad_char)
                // erwartet eine OpaqueField (Codec-Rückkonvertierung, strict wird
                // propagiert); binäre/undeklarierte Kinder erwarten BinaryField.
                const tlv_detail::TlvChildInfo* child = child_info(se_key);
                const bool want_text = (child && child->text);

                std::vector<uint8_t> data;
                if (child && child->container && child->subParser) {
                    // 0.6.4: Container-Kind (constructed): der Frame-Wert ist
                    // die (re-)serialisierte Sub-Message; ihr Sub-Parser wurde
                    // beim Decode angehängt (store_se) bzw. ist über
                    // msg->set(parser,...) gesetzt. Fehlt das Kind (oder ist
                    // es ohne Parser), gilt die übliche SE-fehlt-Semantik.
                    const auto sub = msg->get< ::TNG_NAMESPACE::ISOMessage >(se_key);
                    if (!sub) {
                        if (msg->has(se_key))
                            // Fail-closed: anderes Komponenten-Typ vorhanden
                            // (Programmierfehler, nicht Datenkorruption).
                            throw std::runtime_error(
                                "[ISO8583] TLV-Kind SE" + std::to_string(se_num) +
                                ": Spec erwartet ein Container-Kind (constructed), es ist aber "
                                "ein anderes Komponenten-Typ gesetzt");
                        tlv_detail::log_warn_se_missing(se_num);
                        continue;
                    }
                    if (!sub->parser()) {
                        tlv_detail::log_warn_se_missing(se_num);
                        continue;
                    }
                    data = sub->parser()->parse(sub);
                }
                else if (want_text) {
                    const auto of = msg->get< ::TNG_NAMESPACE::OpaqueField >(se_key);
                    if (!of) {
                        if (msg->has(se_key))
                            // Fail-closed: anderes Komponenten-Typ vorhanden
                            // (Programmierfehler, nicht Datenkorruption).
                            throw std::runtime_error(
                                "[ISO8583] TLV-Kind SE" + std::to_string(se_num) +
                                ": Spec erwartet ein Textfeld (char/numeric), es ist aber "
                                "ein anderes Komponenten-Typ gesetzt");
                        tlv_detail::log_warn_se_missing(se_num);
                        continue;
                    }
                    data.resize(tlv_detail::child_required_sz(child->enc, of->value().size()));
                    tlv_detail::child_to_string(*child, of->value(), data, 0, strict_);
                }
                else {
                    const auto se = msg->get< ::TNG_NAMESPACE::BinaryField >(se_key);
                    if (!se) {
                        if (msg->has(se_key))
                            throw std::runtime_error(
                                "[ISO8583] TLV-Kind SE" + std::to_string(se_num) +
                                ": Spec erwartet ein Binärfeld, es ist aber "
                                "ein anderes Komponenten-Typ gesetzt");
                        tlv_detail::log_warn_se_missing(se_num);
                        continue;
                    }
                    data = se->value();
                }

                const std::size_t tag_off = out.size();
                out.resize(out.size() + TagPolicy::required_size(se_num), 0x00);
                TagPolicy::write(out, tag_off, se_num);

                const std::size_t len_off = out.size();
                out.resize(out.size() + LenPolicy::required_size(data.size()), 0x00);
                LenPolicy::write(out, len_off, data.size());

                out.insert(out.end(), data.begin(), data.end());
                tlv_detail::log_debug_se_write(se_num, data.size());
            }

            return out;
        }

        /// @brief Deklariertes Kind für `se_num` (Tag/SE-Nummer) oder nullptr
        ///        (undeklariert → BinaryField + "SE<n>"-Fallback).
        [[nodiscard]] const tlv_detail::TlvChildInfo* child_info(std::size_t se_num) const noexcept {
            const auto it = child_map_.find(se_num);
            return (it == child_map_.end()) ? nullptr : &it->second;
        }

        // Liefert eine LANGLEBIGE (an dieses Parser-Objekt gebundene) Sicht auf
        // die Beschreibung für `se_num` - NIEMALS eine Kopie/Temporäre.
        //
        // KRITISCH: ISOComponent::description(const nonstd::string_view&)
        // speichert NUR eine Sicht, keine eigene Kopie (siehe _components.cc).
        // Ein Aufruf wie `se->description(irgendein_temporäres_std::string)`
        // erzeugt deshalb eine dangelnde Sicht, sobald die Temporäre am Ende
        // des Ausdrucks zerstört wird - betraf früher auch schon den
        // generischen "SE26"-Fallback, nur unauffällig (kurze Strings landen
        // typischerweise in der Small-String-Optimization und werden nicht
        // sofort überschrieben; empirisch mit einer längeren, aus 'children'
        // deklarierten Beschreibung wie "Application Cryptogram" aufgedeckt).
        // Deshalb: explizite Beschreibungen leben in child_map_ (schon
        // bei Konstruktion befüllt, Parser-Lebensdauer), generierte "SE<n>"-
        // Fallbacks werden HIER EINMALIG erzeugt und in einem eigenen,
        // ebenfalls Parser-langlebigen Cache abgelegt.
        //
        // [ISO8583] 3.3 (Thread-Sicherheit): Der Fallback-Cache ist MUTABLE
        // Parser-Zustand und wird bei jedem Decode eines NICHT deklarierten
        // SE-Tags gefüllt. Da ein Parser nach dem Load über mehrere Threads
        // geteilt werden kann (Doku: "Parsers are immutable after load"),
        // wäre ein gleichzeitiges try_emplace auf dem unordered_map ein
        // Daten-Race mit Heap-Korruption (empirisch aufgedeckt: 4 Threads,
        // undeclarierter SE72 -> AV). Der Sperrbereich deckt NUR den
        // Fallback-Pfad; die Hot-Pfad-Suche in child_map_ bleibt
        // lock-frei (read-only nach dem Konstruktor).
        nonstd::string_view description_for_wire(std::size_t se_num) const {
            if (const tlv_detail::TlvChildInfo* child = child_info(se_num);
                child && !child->description.empty())
                return nonstd::string_view(child->description);

            const std::lock_guard lock(fallback_cache_mutex_);
            auto [cacheIt, inserted] = fallback_description_cache_.try_emplace(
                se_num, "SE" + std::to_string(se_num));
            return nonstd::string_view(cacheIt->second);
        }

        // [ISO8583] 3.4 (PCI): Sensitivität eines SE-Tags — entweder global
        // (sensitive_all_, z. B. BERTLV-Container mit 'sensitive: true') oder
        // pro Tag aus child_map_ (YAML-Kind-Deklaration, bereits inkl.
        // Eltern-Erbung: kind.sensitive || feld.sensitive). Lock-frei:
        // child_map_ ist read-only nach dem Konstruktor.
        bool sensitive_for_wire(std::size_t se_num) const {
            if (sensitive_all_)
                return true;
            if (const tlv_detail::TlvChildInfo* child = child_info(se_num))
                return child->sensitive;
            return false;
        }

    private:
        // FR-1/FR-2 (0.5.0): deklarierte Kind-Elemente (read-only nach dem
        // Konstruktor) — ersetzt die früheren drei parallelen Maps.
        tlv_detail::TlvChildMap child_map_;
        // [ISO8583] 3.4 (PCI): Flag für Container-Ebene (BERTLV 'sensitive: true').
        bool            sensitive_all_ = false;
        // mutable: unparse() ist zwar selbst nicht const, description_for_wire()
        // wird aber bewusst als const-Methode angeboten (liest nur, "erzeugt"
        // höchstens einen Cache-Eintrag - kein Teil des eigentlichen
        // Tag/Length/Value-Zustands).
        mutable std::unordered_map<std::size_t, std::string> fallback_description_cache_;
        // [ISO8583] 3.3: schützt fallback_description_cache_ gegen
        // gleichzeitiges Füllen aus mehreren Threads (geteilter Parser).
        mutable std::mutex fallback_cache_mutex_;
    };

    // ── Vordefinierte Aliase ─────────────────────────────────────────────────

    /// Mastercard-Style: 2 Byte EBCDIC-Tag, 2 Byte EBCDIC-Länge, TCC-Byte
    /// vorangestellt (identisches Verhalten zur bisherigen
    /// ISOTLVParser<2, 2, true, Encoder::EBCDIC, Encoder::EBCDIC>).
    using ISOTLVParser_MC = ISOTLVParser<
        FixedNumericTag<2, codec::Encoder::EBCDIC>,
        FixedNumericLength<2, codec::Encoder::EBCDIC>,
        true, codec::Encoder::EBCDIC>;

    /// Visa-Style: 2 Byte BCD-Tag, 1 Byte BCD-Länge, kein TCC (identisches
    /// Verhalten zur bisherigen ISOTLVParser<2, 1, false, Encoder::BCD, Encoder::BCD>).
    using ISOTLVParser_VI = ISOTLVParser<
        FixedNumericTag<2, codec::Encoder::BCD>,
        FixedNumericLength<1, codec::Encoder::BCD>,
        false>;

    /// BER-TLV (ISO/IEC 8825-1, EMV Book 3 Annex B): variable Tag- und
    /// Length-Länge, kein TCC-Feld.
    using BERTLVParser = ISOTLVParser<BerTag, BerLength, false>;

} // namespace TNG_NAMESPACE
