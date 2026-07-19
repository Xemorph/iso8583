#pragma once

// Diese Datei enthält die Template-Implementierungen für _codec.hh.
// Sie wird ausschließlich von zwei Stellen eingebunden:
//   1. _codec.hh  (am Ende, damit normale Aufrufer die Definitionen sehen)
//   2. _codec.cc  (für die expliziten Instanziierungen)
//
// Direkt einbinden ist nicht vorgesehen.

namespace TNG_NAMESPACE {

    // -------------------------------------------------------------------------
    // parsed_length<encoding, digits>
    // -------------------------------------------------------------------------
    template <PrefixEncoder encoding, Length digits>
    static constexpr std::size_t parsed_length() noexcept {
        if constexpr (PrefixEncoder::NONE   == encoding) return 0u;
        else if constexpr (PrefixEncoder::ASCII  == encoding) return (std::size_t)((int)digits);
        else if constexpr (PrefixEncoder::BCD    == encoding) return (std::size_t)(((int)digits + 1) >> 1);
        else if constexpr (PrefixEncoder::BINARY == encoding) return (std::size_t)((int)digits);
        else if constexpr (PrefixEncoder::EBCDIC == encoding) return (std::size_t)((int)digits);
        else static_assert(dependent_false<decltype(encoding)>::value, "unknown PrefixEncoder");
    }

    // -------------------------------------------------------------------------
    // encode_length<encoding, digits>
    // -------------------------------------------------------------------------
    template <PrefixEncoder encoding, Length digits>
    static constexpr void encode_length(std::size_t l, std::vector<uint8_t>& b) {
        if constexpr (PrefixEncoder::NONE == encoding) {
            return;
        }
        else if constexpr (PrefixEncoder::ASCII == encoding) {
            constexpr std::size_t len = parsed_length<encoding, digits>();
            if (b.size() < len) b.resize(len, 0);
            for (std::size_t i = len; i-- > 0;) {
                b[i] = (uint8_t)('0' + l % 10);
                l /= 10;
            }
        }
        else if constexpr (PrefixEncoder::BCD == encoding) {
            constexpr std::size_t len = parsed_length<encoding, digits>();
            if (b.size() < len) b.resize(len, 0);
            for (std::size_t i = len; i-- > 0;) {
                const int pair = (int)(l % 100);
                l /= 100;
                b[i] = (uint8_t)((pair / 10 << 4) | (pair % 10));
            }
        }
        else if constexpr (PrefixEncoder::BINARY == encoding) {
            constexpr std::size_t len = parsed_length<encoding, digits>();
            if (b.size() < len) b.resize(len, 0);
            for (std::ptrdiff_t i = (std::ptrdiff_t)len - 1; i >= 0; --i) {
                b[(std::size_t)i] = (uint8_t)(l & 0xFF);
                l >>= 8;
            }
        }
        else if constexpr (PrefixEncoder::EBCDIC == encoding) {
            constexpr std::size_t len = parsed_length<encoding, digits>();
            if (b.size() < len) b.resize(len, 0);
            for (std::ptrdiff_t i = (std::ptrdiff_t)len - 1; i >= 0; --i) {
                b[(std::size_t)i] = EBCDIC_DIGITS[l % 10];
                l /= 10;
            }
        }
        else static_assert(dependent_false<decltype(encoding)>::value, "unknown PrefixEncoder");
    }

    // -------------------------------------------------------------------------
    // decode_length<encoding, digits>
    // -------------------------------------------------------------------------
    template <PrefixEncoder encoding, Length digits>
    static constexpr std::size_t decode_length(const std::vector<uint8_t>& b, std::size_t o) {
        std::size_t l = 0u;
        if constexpr (PrefixEncoder::NONE == encoding) {
            return 0u;
        }
        else if constexpr (PrefixEncoder::ASCII == encoding) {
            for (std::size_t i = 0; i < (std::size_t)digits; ++i)
                l = l * 10 + (b.at(o + i) - (uint8_t)'0');
        }
        else if constexpr (PrefixEncoder::BCD == encoding) {
            const std::size_t bytes = parsed_length<encoding, digits>();
            for (std::size_t i = 0; i < bytes; ++i)
                l = l * 100
                    + ((b.at(o + i) >> 4) & 0x0F) * 10
                    +  (b.at(o + i)       & 0x0F);
        }
        else if constexpr (PrefixEncoder::BINARY == encoding) {
            for (std::size_t i = 0; i < (std::size_t)digits; ++i)
                l = l * 256 + (b.at(o + i) & 0xFF);
        }
        else if constexpr (PrefixEncoder::EBCDIC == encoding) {
            for (std::size_t i = 0; i < (std::size_t)digits; ++i)
                l = l * 10 + (b.at(o + i) & 0x0F);
        }
        else static_assert(dependent_false<decltype(encoding)>::value, "unknown PrefixEncoder");
        return l;
    }

    // -------------------------------------------------------------------------
    // required_sz_for_as<e>
    // -------------------------------------------------------------------------
    template <Encoder e>
    static constexpr std::size_t required_sz_for_as(std::size_t n) noexcept {
        if constexpr (Encoder::BINARY == e) return n;
        else if constexpr (Encoder::BCD == e) return (n + 1) / 2;
        else if constexpr (Encoder::HEX_EBCDIC == e) return  (n * 2);
        else if constexpr (Encoder::EBCDIC     == e) return n;
        else if constexpr (Encoder::ASCII      == e) return n;
        else static_assert(dependent_false<decltype(e)>::value, "unknown Encoder");
    }

    // -------------------------------------------------------------------------
    // as<T, e>
    // -------------------------------------------------------------------------
    template <typename T, Encoder e>
    static constexpr T as(const std::vector<uint8_t>& text, std::size_t offset, std::size_t length) {
        if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            // -- Binärer Rückgabepfad -----------------------------------------
            if constexpr (Encoder::HEX_EBCDIC == e) {
                std::vector<uint8_t> d(length, 0x00);
                for (std::size_t i = 0; i < length; ++i) {
                    const uint8_t hi = text[offset + i * 2];
                    const uint8_t lo = text[offset + i * 2 + 1];
                    const int h = hi < 0xF0u ? 10 + hi - 0xC1 : hi - 0xF0;
                    const int lv = lo < 0xF0u ? 10 + lo - 0xC1 : lo - 0xF0;
                    d[i] = (uint8_t)(h << 4 | lv);
                }
                return d;
            }
            else if constexpr (Encoder::BINARY == e) {
                return std::vector<uint8_t>(text.begin() + offset,
                                            text.begin() + offset + length);
            }
            else static_assert(dependent_false<decltype(e)>::value,
                "as<vector<uint8_t>, E>: unsupported encoder");
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            // -- String-Rückgabepfad ------------------------------------------
            if constexpr (Encoder::EBCDIC == e) {
                std::string data(text.begin() + offset, text.begin() + offset + length);
#if ENABLE_ICONV
                iconv_wrapper::iconv enc;
                enc.open("IBM-1047", "");
                std::string res(enc.convert(data));
                enc.close();
                return res;
#else
                detail::e2a_n(data.data(), data.size());
                return data;
#endif
            }
            else if constexpr (Encoder::BCD == e) {
                std::string data;
                data.reserve(length);
                for (std::size_t i = 0; i < length; ++i) {
                    const int shift = (i & 1) == 1 ? 0 : 4;
                    char c = (char)(((text.at(offset + (i >> 1)) >> shift) & 0x0F) + '0');
                    data.push_back(c);
                }
                return data;
            }
            else if constexpr (Encoder::ASCII == e) {
                return std::string(text.begin() + offset, text.begin() + offset + length);
            }
            else static_assert(dependent_false<decltype(e)>::value,
                "as<string, E>: unsupported encoder");
        }
        else static_assert(dependent_false<T>::value,
            "as<T,E>: T must be std::string or std::vector<uint8_t>");
    }

    // -------------------------------------------------------------------------
    // to<e, T>
    // Exact inverse of as<T, e>().
    // Writes the encoded representation of `value` into b[offset..].
    // b must already be sized; only the bytes at [offset, offset+required) are written.
    // -------------------------------------------------------------------------
    template <Encoder e, typename T>
    static constexpr void to(const T& value, std::vector<uint8_t>& b, std::size_t offset) {

        if constexpr (std::is_same_v<T, std::string>) {
            // ── String-Encoding ───────────────────────────────────────────────
            if constexpr (Encoder::ASCII == e) {
                // 1:1 copy – ASCII string bytes go directly into the buffer
                for (std::size_t i = 0; i < value.size(); ++i)
                    b[offset + i] = static_cast<uint8_t>(value[i]);
            }
            else if constexpr (Encoder::EBCDIC == e) {
                // ASCII → EBCDIC: invert the kEbcdicToAscii lookup table.
                // We build a reverse table lazily at compile-time (constexpr).
                // For runtime use, a direct iconv call is more accurate for
                // non-digit characters; for digits and common alphanumerics
                // the reverse-table approach matches the decoder exactly.
#if ENABLE_ICONV
                std::string src(value);
                iconv_wrapper::iconv enc;
                enc.open("", "IBM-1047");   // ASCII → EBCDIC
                std::string res(enc.convert(src));
                enc.close();
                for (std::size_t i = 0; i < res.size(); ++i)
                    b[offset + i] = static_cast<uint8_t>(res[i]);
#else
                // Use the class-level kAsciiToEbcdic table (defined in _codec.hh).
                for (std::size_t i = 0; i < value.size(); ++i)
                    b[offset + i] = kAsciiToEbcdic[static_cast<unsigned char>(value[i])];
#endif
            }
            else if constexpr (Encoder::BCD == e) {
                // Digits → BCD packed (2 digits per byte).
                // Mirrors as<string, BCD>: index i maps to nibble at byte i/2.
                const std::size_t bytes = required_sz_for_as<e>(value.size());
                for (std::size_t i = 0; i < bytes; ++i)
                    b[offset + i] = 0x00;
                for (std::size_t i = 0; i < value.size(); ++i) {
                    const uint8_t digit = static_cast<uint8_t>(value[i] - '0');
                    const int shift = (i & 1) == 0 ? 4 : 0; // MSN first (mirrors as<>)
                    b[offset + (i >> 1)] |= static_cast<uint8_t>(digit << shift);
                }
            }
            else static_assert(dependent_false<decltype(e)>::value,
                "to<E, string>: unsupported encoder");
        }
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            // ── Binary-Encoding ───────────────────────────────────────────────
            if constexpr (Encoder::BINARY == e) {
                // Raw copy – mirrors as<vector<uint8_t>, BINARY>
                for (std::size_t i = 0; i < value.size(); ++i)
                    b[offset + i] = value[i];
            }
            else if constexpr (Encoder::HEX_EBCDIC == e) {
                // Each input byte → two EBCDIC hex-digit bytes.
                // Mirrors as<vector<uint8_t>, HEX_EBCDIC>:
                //   decode: (hi,lo) EBCDIC pair → nibbles → byte
                //   encode: byte   → high nibble → HEX_EBCDIC_TABLE[h]
                //                  → low  nibble → HEX_EBCDIC_TABLE[l]
                for (std::size_t i = 0; i < value.size(); ++i) {
                    const uint8_t byte = value[i];
                    b[offset + i * 2] = HEX_EBCDIC_TABLE[(byte >> 4) & 0x0F];
                    b[offset + i * 2 + 1] = HEX_EBCDIC_TABLE[byte & 0x0F];
                }
            }
            else static_assert(dependent_false<decltype(e)>::value,
                "to<E, vector<uint8_t>>: unsupported encoder");
        }
        else static_assert(dependent_false<T>::value,
            "to<E,T>: T must be std::string or std::vector<uint8_t>");
    }

} // namespace TNG_NAMESPACE
