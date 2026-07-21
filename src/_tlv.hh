#pragma once

// =============================================================================
// ISOTLVParser<TAG_BYTES, LEN_BYTES, HAS_TCC, TAG_ENC, LEN_ENC>
// =============================================================================

// [stdc++]
#include <algorithm>
#include <optional>
#include <unordered_map>
// [tng]
#include <iso8583/iso8583.h>
#include "_parser.hh"

namespace TNG_NAMESPACE {

    namespace tlv_detail {

        // ── Compile-Zeit: read/write numerischer Felder ───────────────────────

        template <codec::Encoder e_, std::size_t N>
        static constexpr std::size_t read_num(
            const std::vector<uint8_t>& buf, std::size_t offset)
        {
            static_assert(N >= 1 && N <= 4, "N must be 1..4");
            if constexpr (e_ == codec::Encoder::BCD) {
                std::size_t val = 0;
                for (std::size_t i = 0; i < N; ++i) {
                    const uint8_t b = buf[offset + i];
                    val = val * 100
                        + static_cast<std::size_t>((b >> 4) & 0x0F) * 10
                        + static_cast<std::size_t>(b & 0x0F);
                }
                return val;
            }
            else {
                const auto s = ::TNG_NAMESPACE::codec::as<std::string, e_>(buf, offset, N);
                std::size_t val = 0;
                for (char c : s) {
                    if (c < '0' || c > '9') return 0;
                    val = val * 10 + static_cast<std::size_t>(c - '0');
                }
                return val;
            }
        }

        template <codec::Encoder e_, std::size_t N>
        static constexpr void write_num(
            std::vector<uint8_t>& buf, std::size_t offset, std::size_t value)
        {
            static_assert(N >= 1 && N <= 4, "N must be 1..4");
            if constexpr (e_ == codec::Encoder::BCD) {
                std::size_t v = value;
                for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(N) - 1;
                    i >= 0; --i)
                {
                    const uint8_t lo = static_cast<uint8_t>(v % 10); v /= 10;
                    const uint8_t hi = static_cast<uint8_t>(v % 10); v /= 10;
                    buf[offset + static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>((hi << 4) | lo);
                }
            }
            else {
                std::string s(N, '0');
                std::size_t v = value;
                for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(N) - 1;
                    i >= 0; --i)
                {
                    s[static_cast<std::size_t>(i)] =
                        static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                ::TNG_NAMESPACE::codec::to<e_>(s, buf, offset);
            }
        }

        // ── Runtime-Helpers (implementiert in _tlv_parser.cc) ─────────────────
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
        std::size_t TAG_BYTES,
        std::size_t LEN_BYTES,
        bool HAS_TCC,
        codec::Encoder TAG_ENC,
        codec::Encoder LEN_ENC
    >
    class TNG_EXPORT ISOTLVParser : public ISOBaseParser {
        static_assert(TAG_BYTES >= 1 && TAG_BYTES <= 4, "TAG_BYTES must be 1..4");
        static_assert(LEN_BYTES >= 1 && LEN_BYTES <= 4, "LEN_BYTES must be 1..4");

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
                const auto tcc_str = ::TNG_NAMESPACE::codec::as<std::string, TAG_ENC>(b, pos, 1);
                auto tcc = std::make_shared< ::TNG_NAMESPACE::OpaqueField> (TCC_KEY);
                tcc->value(tcc_str);
                tcc->description("TCC");
                tcc->wire_offset(base_offset + pos);
                tcc->wire_length(1);
                msg->set(tcc);
                pos += 1;
                tlv_detail::log_debug_tcc(tcc_str);
            }

            while (pos + TAG_BYTES + LEN_BYTES <= b.size()) {
                const std::size_t se_num =
                    tlv_detail::read_num<TAG_ENC, TAG_BYTES>(b, pos);
                pos += TAG_BYTES;

                const std::size_t se_len =
                    tlv_detail::read_num<LEN_ENC, LEN_BYTES>(b, pos);
                pos += LEN_BYTES;

                if (pos + se_len > b.size()) {
                    tlv_detail::log_error_se_overflow(se_num, se_len, pos, b.size());
                    break;
                }

                tlv_detail::store_se(msg, se_num, b, pos, se_len,
                    base_offset + pos - TAG_BYTES - LEN_BYTES,
                    TAG_BYTES + LEN_BYTES + se_len);
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
                    if constexpr (TAG_ENC == codec::Encoder::ASCII)      out.push_back(0x20u);
                    else if constexpr (TAG_ENC == codec::Encoder::BCD)   out.push_back(0x00u);
                    else                                           out.push_back(0x40u);
                }
                else {
                    std::vector<uint8_t> tcc_buf(1, 0x00);
                    ::TNG_NAMESPACE::codec::to<TAG_ENC>(tcc_comp->value(), tcc_buf, 0);
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
                out.resize(out.size() + TAG_BYTES, 0x00);
                tlv_detail::write_num<TAG_ENC, TAG_BYTES>(out, tag_off, se_num);

                const std::size_t len_off = out.size();
                out.resize(out.size() + LEN_BYTES, 0x00);
                tlv_detail::write_num<LEN_ENC, LEN_BYTES>(out, len_off, data.size());

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

    // ── Vordefinierte Aliases ─────────────────────────────────────────────────

    using ISOTLVParser_MC = ISOTLVParser<2, 2, true, codec::Encoder::EBCDIC, codec::Encoder::EBCDIC>;
    using ISOTLVParser_VI = ISOTLVParser<2, 1, false, codec::Encoder::BCD, codec::Encoder::BCD>;

} // namespace TNG_NAMESPACE
