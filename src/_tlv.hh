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
        TNG_EXPORT void log_error_se_overflow(std::size_t se_num, std::size_t se_len,
            std::size_t pos, std::size_t buf_sz);
        TNG_EXPORT void log_debug_tcc(const std::string& tcc);
        TNG_EXPORT void log_debug_se_read(std::size_t se_num, std::size_t se_len);
        TNG_EXPORT void log_debug_se_write(std::size_t se_num, std::size_t data_sz);

        TNG_EXPORT std::vector<TNG_KEY_TYPE> sorted_se_keys(const ISO_MAP& fields);

        TNG_EXPORT void store_se(
            const std::shared_ptr<ISOMessage>& msg,
            std::size_t  se_num,
            const std::vector<uint8_t>& buf,
            std::size_t  data_offset,
            std::size_t  data_len,
            std::size_t  wire_offset,
            std::size_t  wire_len);

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
        using DataEncodingMap = std::unordered_map<std::size_t, codec::Encoder>;

        explicit ISOTLVParser(DataEncodingMap data_enc_map = {})
            : ISOBaseParser("<tlv>", 0)
            , data_enc_map_(std::move(data_enc_map))
        {
        }

        bool emit_bitmap() const noexcept override { 
            return false; 
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

                tlv_detail::store_se(msg, se_num, b, pos, se_len,
                    base_offset + tag_start,
                    (pos - tag_start) + se_len);
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
                auto se = msg->get< ::TNG_NAMESPACE::BinaryField >(se_key);
                if (!se) continue;

                const auto& data = se->value();
                const auto  se_num = static_cast<std::size_t>(se_key);

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

        std::optional<codec::Encoder> data_encoding_for(std::size_t se_num) const {
            auto it = data_enc_map_.find(se_num);
            if (it != data_enc_map_.end()) return it->second;
            return std::nullopt;
        }

    private:
        DataEncodingMap data_enc_map_;
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
