#pragma once

// =============================================================================
// Tag-/Length-Policies für ISOTLVParser<TagPolicy, LenPolicy, HAS_TCC, TCC_ENC>
// =============================================================================
//
// Jede Policy ist ein reiner Typ (keine Laufzeit-Instanzen) mit drei
// statischen Methoden ("Policy-Interface"):
//
//   static std::pair<std::size_t, std::size_t> read(buf, offset);
//       -> { dekodierter Wert, Anzahl gelesener Bytes }
//   static std::size_t write(buf, offset, value);
//       -> Anzahl geschriebener Bytes (== required_size(value))
//   static std::size_t required_size(value);
//       -> Anzahl Bytes, die 'value' auf dem Wire belegt
//
// FixedNumericTag<N, Encoder> / FixedNumericLength<N, Encoder> bilden das
// bisherige ISOTLVParser<TAG_BYTES, LEN_BYTES, ...>-Verhalten 1:1 nach
// (feste Byte-Anzahl, numerisch codiert via ASCII/BCD/EBCDIC).
//
// BerTag / BerLength implementieren BER-TLV (ISO/IEC 8825-1 §8.1,
// EMV Book 3 Annex B): Tag- und Length-Feld sind variabel lang, die
// Byte-Anzahl ergibt sich erst aus den Daten selbst.
//
// -----------------------------------------------------------------------------
// C++17/C++20-Kompatibilität
// -----------------------------------------------------------------------------
//   - ISOTLV_CPLUSPLUS:   robuster Sprachversions-Check (MSVC meldet
//                         __cplusplus ohne /Zc:__cplusplus falsch -> Fallback
//                         auf _MSVC_LANG).
//   - ISOTLV_HAS_CONCEPTS: 1 wenn C++20-Concepts verfügbar sind.
//   - ISOTLV_STATIC_ASSERT_TAG_POLICY(T) / _LEN_POLICY(T):
//         Ein Makro, zwei Definitionen. Der Aufrufer (ISOTLVParser) schreibt
//         immer denselben Makro-Aufruf; unter C++20 prüft die Definition
//         automatisch über die Concepts TlvTagPolicy/TlvLenPolicy, unter
//         C++17 über die SFINAE-Detection weiter unten. Ein späterer
//         Umstieg auf "nur noch Concepts" ändert also nur die beiden
//         #define-Zeilen - kein Copy-Paste an den Aufrufstellen nötig.
// =============================================================================

// [stdc++]
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
// [tng]
#include <iso8583/config.h>
#include <iso8583/_codec.hh>

// -----------------------------------------------------------------------------
// Sprachversions-Erkennung
// -----------------------------------------------------------------------------
#if defined(_MSVC_LANG)
#  define ISOTLV_CPLUSPLUS _MSVC_LANG
#else
#  define ISOTLV_CPLUSPLUS __cplusplus
#endif

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && ISOTLV_CPLUSPLUS >= 202002L
#  define ISOTLV_HAS_CONCEPTS 1
#else
#  define ISOTLV_HAS_CONCEPTS 0
#endif

namespace TNG_NAMESPACE {

    // =========================================================================
    // tlv_detail::read_num / write_num
    // Unverändert aus dem ursprünglichen ISOTLVParser übernommen - bilden die
    // Basis für FixedNumericTag/FixedNumericLength.
    // =========================================================================
    namespace tlv_detail {

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

        // ── Laufzeit-Helfer für BER-Fehlerpfade (implementiert in _tlv.cc) ────
        // Wie bei den bestehenden log_*-Helfern in _tlv.cc: Logging lebt in der
        // .cc-Datei, damit Log-Code nicht in jede Template-Instanziierung
        // hineinkopiert wird und Test-TUs ohne _logger.hh nicht scheitern.
        TNG_EXPORT void log_error_ber_tag_overflow(std::size_t offset, std::size_t buf_sz);
        TNG_EXPORT void log_error_ber_length_overflow(std::size_t offset, std::size_t buf_sz);
        TNG_EXPORT void log_error_ber_length_indefinite();
        TNG_EXPORT void log_error_se_length_missing(std::size_t se_num, std::size_t pos, std::size_t buf_sz);

    } // namespace tlv_detail

    // =========================================================================
    // FixedNumericTag<N, Encoder> / FixedNumericLength<N, Encoder>
    // Feste Byte-Anzahl N, numerisch codiert (ASCII/BCD/EBCDIC/BINARY).
    // Bildet exakt das bisherige TAG_BYTES/LEN_BYTES + TAG_ENC/LEN_ENC-
    // Verhalten nach.
    // =========================================================================
    template <std::size_t N, codec::Encoder E>
    struct FixedNumericTag {
        static std::pair<std::size_t, std::size_t> read(
            const std::vector<uint8_t>& buf, std::size_t offset)
        {
            return { tlv_detail::read_num<E, N>(buf, offset), N };
        }
        static std::size_t write(
            std::vector<uint8_t>& buf, std::size_t offset, std::size_t value)
        {
            tlv_detail::write_num<E, N>(buf, offset, value);
            return N;
        }
        static constexpr std::size_t required_size(std::size_t /*value*/) noexcept {
            return N;
        }
    };

    /// Semantisch für das Length-Feld - identisch zu FixedNumericTag, aber
    /// als eigenes Alias gehalten, damit Aufrufstellen selbstdokumentierend
    /// bleiben (ISOTLVParser<FixedNumericTag<2,...>, FixedNumericLength<2,...>>).
    template <std::size_t N, codec::Encoder E>
    using FixedNumericLength = FixedNumericTag<N, E>;

    // =========================================================================
    // BerTag - BER-TLV Tag-Kodierung (ISO/IEC 8825-1 §8.1.2, EMV Book 3 Annex B)
    // =========================================================================
    //   Byte 1:  Bits 8-7 = Klasse, Bit 6 = constructed, Bits 5-1 = Tag-Nummer
    //            (0..30), oder 11111 (0x1F) => Mehrbyte-Form folgt.
    //   Byte 2..n (nur Mehrbyte-Form):
    //            Bit 8 = Fortsetzungsbit (1 = weiteres Byte folgt),
    //            Bits 7-1 = 7 Bit der Tag-Nummer (Big-Endian, Base-128).
    //
    // Der "Tag-Wert" wird - wie in der Praxis üblich (z.B. EMV-Tag "9F26") -
    // als Big-Endian-Verkettung der ROHEN Tag-Bytes behandelt, nicht als
    // dekomponierte Zahl. Damit ist z.B. Tag 0x9F26 identisch zu dem, was man
    // in einer EMV-Tag-Tabelle nachschlagen würde, und write() ist exakt die
    // Umkehrung von read() (die Fortsetzungsbits sind bereits Teil des
    // übergebenen Wertes - required_size() leitet sie nicht neu her).
    struct BerTag {
        static std::pair<std::size_t, std::size_t> read(
            const std::vector<uint8_t>& buf, std::size_t offset)
        {
            if (offset >= buf.size()) {
                tlv_detail::log_error_ber_tag_overflow(offset, buf.size());
                return { 0, 0 };
            }

            std::size_t val = buf[offset];
            std::size_t consumed = 1;

            if ((buf[offset] & 0x1Fu) == 0x1Fu) {
                // Mehrbyte-Form: solange Fortsetzungsbit (0x80) gesetzt ist.
                // Begrenzung auf sizeof(std::size_t) Bytes verhindert
                // Überlauf bei kaputten/böswilligen Eingaben.
                for (;;) {
                    const std::size_t pos = offset + consumed;
                    if (pos >= buf.size() || consumed >= sizeof(std::size_t)) {
                        tlv_detail::log_error_ber_tag_overflow(offset, buf.size());
                        return { val, 0 };
                    }
                    const uint8_t b = buf[pos];
                    val = (val << 8) | b;
                    ++consumed;
                    if ((b & 0x80u) == 0)
                        break;
                }
            }
            return { val, consumed };
        }

        static std::size_t write(
            std::vector<uint8_t>& buf, std::size_t offset, std::size_t value)
        {
            const std::size_t n = required_size(value);
            for (std::size_t i = 0; i < n; ++i)
                buf[offset + i] = static_cast<uint8_t>(
                    (value >> (8 * (n - 1 - i))) & 0xFFu);
            return n;
        }

        static constexpr std::size_t required_size(std::size_t value) noexcept {
            std::size_t n = 1;
            while (value > 0xFFu) { value >>= 8; ++n; }
            return n;
        }
    };

    // =========================================================================
    // BerLength - BER-TLV Length-Kodierung (ISO/IEC 8825-1 §8.1.3)
    // =========================================================================
    //   Short Form:  1 Byte, Bit 8 = 0, Bits 7-1 = Länge (0..127).
    //   Long Form:   1 Byte "0x80 | Anzahl-Folgebytes" (1..126), gefolgt von
    //                der Länge als Big-Endian-Zahl.
    //   0x80 allein ("Indefinite Form", nur bei BER, nicht bei DER/EMV) wird
    //                nicht unterstützt und als Fehler behandelt.
    struct BerLength {
        static std::pair<std::size_t, std::size_t> read(
            const std::vector<uint8_t>& buf, std::size_t offset)
        {
            if (offset >= buf.size()) {
                tlv_detail::log_error_ber_length_overflow(offset, buf.size());
                return { 0, 0 };
            }

            const uint8_t b0 = buf[offset];
            if ((b0 & 0x80u) == 0) {
                // Short Form
                return { static_cast<std::size_t>(b0), 1 };
            }

            const std::size_t num_bytes = b0 & 0x7Fu;
            if (num_bytes == 0) {
                // Indefinite Form - in TLV-Kontext (feste SE-Länge nötig)
                // nicht unterstützt.
                tlv_detail::log_error_ber_length_indefinite();
                return { 0, 0 };
            }
            if (num_bytes > sizeof(std::size_t) ||
                offset + 1 + num_bytes > buf.size())
            {
                tlv_detail::log_error_ber_length_overflow(offset, buf.size());
                return { 0, 0 };
            }

            std::size_t len = 0;
            for (std::size_t i = 0; i < num_bytes; ++i)
                len = (len << 8) | buf[offset + 1 + i];
            return { len, 1 + num_bytes };
        }

        static std::size_t write(
            std::vector<uint8_t>& buf, std::size_t offset, std::size_t value)
        {
            if (value < 0x80u) {
                buf[offset] = static_cast<uint8_t>(value);
                return 1;
            }
            const std::size_t n = required_size(value) - 1; // Anzahl Folgebytes
            buf[offset] = static_cast<uint8_t>(0x80u | n);
            for (std::size_t i = 0; i < n; ++i)
                buf[offset + 1 + i] = static_cast<uint8_t>(
                    (value >> (8 * (n - 1 - i))) & 0xFFu);
            return 1 + n;
        }

        static constexpr std::size_t required_size(std::size_t value) noexcept {
            if (value < 0x80u) return 1;
            std::size_t n = 1;
            std::size_t v = value;
            while (v > 0xFFu) { v >>= 8; ++n; }
            return 1 + n;
        }
    };

    // =========================================================================
    // Detection-Idiom (C++17-Fallback): prüft per SFINAE ob T das
    // Policy-Interface { read, write, required_size } mit den erwarteten
    // Signaturen bereitstellt.
    // =========================================================================
    namespace tlv_detail {

        template <typename T, typename = void>
        struct has_policy_interface : std::false_type {};

        template <typename T>
        struct has_policy_interface<T, std::void_t<
            decltype(T::read(std::declval<const std::vector<uint8_t>&>(), std::declval<std::size_t>())),
            decltype(T::write(std::declval<std::vector<uint8_t>&>(), std::declval<std::size_t>(), std::declval<std::size_t>())),
            decltype(T::required_size(std::declval<std::size_t>()))
        >> : std::bool_constant<
            std::is_same_v<
                decltype(T::read(std::declval<const std::vector<uint8_t>&>(), std::declval<std::size_t>())),
                std::pair<std::size_t, std::size_t>> &&
            std::is_same_v<
                decltype(T::write(std::declval<std::vector<uint8_t>&>(), std::declval<std::size_t>(), std::declval<std::size_t>())),
                std::size_t> &&
            std::is_same_v<
                decltype(T::required_size(std::declval<std::size_t>())),
                std::size_t>
        > {};

        // Aktuell identische Anforderungen an Tag- und Length-Policy - als
        // eigene Alias-Templates gehalten, falls sich die Interfaces künftig
        // unterscheiden sollen (z.B. eine Policy die zusätzlich Klasse/
        // Constructed-Flag eines BER-Tags exponiert).
        template <typename T> struct has_tag_policy_interface : has_policy_interface<T> {};
        template <typename T> struct has_len_policy_interface : has_policy_interface<T> {};

    } // namespace tlv_detail

    // =========================================================================
    // C++20-Concepts - nur aktiv wenn ISOTLV_HAS_CONCEPTS. Gleiche
    // Anforderungen wie die SFINAE-Detection oben, aber mit `requires` und
    // besseren Compiler-Fehlermeldungen.
    // =========================================================================
#if ISOTLV_HAS_CONCEPTS
    template <typename T>
    concept TlvTagPolicy = requires(
        const std::vector<uint8_t>& rbuf, std::vector<uint8_t>& wbuf,
        std::size_t offset, std::size_t value)
    {
        { T::read(rbuf, offset) } -> std::same_as<std::pair<std::size_t, std::size_t>>;
        { T::write(wbuf, offset, value) } -> std::same_as<std::size_t>;
        { T::required_size(value) } -> std::same_as<std::size_t>;
    };

    /// Aktuell identische Form wie TlvTagPolicy - eigenes Concept gehalten,
    /// falls Tag- und Length-Anforderungen künftig auseinanderlaufen.
    template <typename T>
    concept TlvLenPolicy = TlvTagPolicy<T>;
#endif

    // =========================================================================
    // ISOTLV_STATIC_ASSERT_TAG_POLICY(T) / ISOTLV_STATIC_ASSERT_LEN_POLICY(T)
    //
    // Immer derselbe Makro-Aufruf in ISOTLVParser; nur die #define-Definition
    // unterscheidet sich zwischen C++17 (SFINAE) und C++20 (Concept). Ein
    // späterer Umstieg auf "Concepts verpflichtend" betrifft daher nur diese
    // beiden Stellen - keine Anpassung an den Aufrufstellen nötig.
    // =========================================================================
#if ISOTLV_HAS_CONCEPTS
#  define ISOTLV_STATIC_ASSERT_TAG_POLICY(T) \
        static_assert(::TNG_NAMESPACE::TlvTagPolicy<T>, \
            #T " erfuellt nicht das TagPolicy-Interface (siehe Concept TlvTagPolicy)")
#  define ISOTLV_STATIC_ASSERT_LEN_POLICY(T) \
        static_assert(::TNG_NAMESPACE::TlvLenPolicy<T>, \
            #T " erfuellt nicht das LenPolicy-Interface (siehe Concept TlvLenPolicy)")
#else
#  define ISOTLV_STATIC_ASSERT_TAG_POLICY(T) \
        static_assert(::TNG_NAMESPACE::tlv_detail::has_tag_policy_interface<T>::value, \
            #T " erfuellt nicht das TagPolicy-Interface: erwartet werden statische " \
            "Methoden read(buf,offset)->pair<size_t,size_t>, " \
            "write(buf,offset,value)->size_t, required_size(value)->size_t")
#  define ISOTLV_STATIC_ASSERT_LEN_POLICY(T) \
        static_assert(::TNG_NAMESPACE::tlv_detail::has_len_policy_interface<T>::value, \
            #T " erfuellt nicht das LenPolicy-Interface: erwartet werden statische " \
            "Methoden read(buf,offset)->pair<size_t,size_t>, " \
            "write(buf,offset,value)->size_t, required_size(value)->size_t")
#endif

} // namespace TNG_NAMESPACE
