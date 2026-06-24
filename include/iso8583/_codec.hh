#pragma once

// [tng]
#include <iso8583/config.h>
// [stdc++]
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
// [iconv]
#include "_iconv_wrapper.hh"

// ─────────────────────────────────────────────────────────────────────────────
// _codec.hh
//
// Enthält ausschließlich:
//   - Enums (PrefixEncoder, Length, Encoder)
//   - Konstante Lookup-Tabellen (EBCDIC_DIGITS, HEX_EBCDIC, kEbcdicToAscii)
//   - Funktionsdeklarationen (Definitionen in src/_codec.cc)
//
// Erfahrene Anwender die einen eigenen ISOBaseParser schreiben binden
// ausschließlich diesen Header ein.
// ─────────────────────────────────────────────────────────────────────────────

namespace TNG_NAMESPACE {

    // ── Lookup-Tabellen ───────────────────────────────────────────────────────

    // - NullPrefixer
    // - AsciiPrefixer
    // - BcdPrefixer
    // - BinaryPrefixer
    // - EbcdicPrefixer
    // - HexNibblesPrefixer

    /// Array containing the EBCDIC encoded digits (0-9).
    /// This array is used for encoding and decoding digits in the EBCDIC format.
    static inline const std::array<uint8_t, 10> EBCDIC_DIGITS = {
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4,
        0xF5, 0xF6, 0xF7, 0xF8, 0xF9
    };

    /// EBCDIC-Hex-Ziffern 0–F (für HEX_EBCDIC-Encoder)
    static inline const std::array<uint8_t, 16> HEX_EBCDIC_TABLE = {
        0xF0, 0xF1, 0xF2, 0xF3,
        0xF4, 0xF5, 0xF6, 0xF7,
        0xF8, 0xF9, 0xC1, 0xC2,
        0xC3, 0xC4, 0xC5, 0xC6,
    };

    // Generated with Python code:
    // 
    //```py 
    //import string
    //
    //print('static const char kEbcdicToAscii[256] = {');
    //for i in range(256) :
    //	asc = chr(i).encode('latin1').decode('cp500')
    //	if asc not in string.ascii_letters + string.digits + ' <=>()+-*/&|!$#@.,;%_?"' :
    //		asc = '.'
    //		print('    %s,' % hex(ord(asc)))
    //		print('};')
    //```
    // 
    // This will also likely be the fastest method, since the 256-byte table will easily fit in the L1 cache
    /// EBCDIC → ASCII Konvertierungstabelle (256 Einträge, unbekannte Zeichen → '.')
    static const char kEbcdicToAscii[256] = {
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x20, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x3c, 0x28, 0x2b, 0x21,
        0x26, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x24, 0x2a, 0x29, 0x3b, 0x2e,
        0x2d, 0x2f, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2c, 0x25, 0x5f, 0x3e, 0x3f,
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x23, 0x40, 0x2e, 0x3d, 0x22,
        0x2e, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x7c, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x2e, 0x2e, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    };

    // ── Enums ─────────────────────────────────────────────────────────────────

    /// Enum that defines the available encoding schemes for prefixing lengths.
    /// It includes different formats for encoding the length of data fields.
    ///  
    /// Available encoding formats:
    /// - PrefixEncoder::NONE: No encoding.
    /// - PrefixEncoder::ASCII: ASCII-based encoding (characters '0'-'9').
    /// - PrefixEncoder::BCD: Binary-Coded Decimal encoding.
    /// - PrefixEncoder::BINARY: Raw binary encoding (each byte is a data byte).
    /// - PrefixEncoder::EBCDIC: EBCDIC-based encoding (used in mainframes).
    /// - PrefixEncoder::HEX_NIBBLES: Hexadecimal encoding using nibbles (half-bytes).
    enum class TNG_EXPORT PrefixEncoder : int {
        NONE        = -1, ///< Kein Prefix (Fixlänge)
        ASCII       =  0, ///< ASCII-Ziffern '0'-'9'
        BCD         =  1, ///< Binary Coded Decimal
        BINARY      =  2, ///< Rohe Binär-Bytes (Big-Endian)
        EBCDIC      =  3, ///< EBCDIC-Ziffern 0xF0-0xF9
        HEX_NIBBLES =  4, ///< Hex-Nibbles (reserviert)
    };

    /// Enum representing the length format for encoding data lengths.
    /// The length can vary from 1 digit up to 4 digits (L, LL, LLL, LLLL).
    ///
    /// \note Available formats:
    /// - Length::FIX: Fixed length (no prefix).
    /// - Length::L: 1-digit length encoding.
    /// - Length::LL: 2-digit length encoding.
    /// - Length::LLL: 3-digit length encoding.
    /// - Length::LLLL: 4-digit length encoding.
    enum class TNG_EXPORT Length : int {
        UNKNOWN = -1, ///< unknown lenght (gets calculated)
        FIX     = 0u, ///< fixed length (no Prefix)
        L       = 1u, ///< 1-digit length (max.    9)
        LL      = 2u, ///< 2-digit length (max.   99)
        LLL     = 3u, ///< 3-digit length (max.  999)
        LLLL    = 4u, ///< 4-digit length (max. 9999)
    };

    /// Encoding-Methode für Feldinhalt
    enum class TNG_EXPORT Encoder : int {
        ASCII      = 0, ///< ASCII-Text
        BCD        = 1, ///< Binary Coded Decimal (2 Ziffern/Byte)
        BINARY     = 2, ///< Rohe Bytes
        EBCDIC     = 3, ///< EBCDIC-Text
        HEX_EBCDIC = 4, ///< Hex-Nibbles in EBCDIC-Darstellung
    };

    /* ── Prefixer : Declarations ─────────────────────────────────────────────── */


    /// Returns the number of bytes required to encode the length, based on the specified encoding and length format.
    /// The number of bytes depends on both the prefix encoding method and the length format (L, LL, etc.).
    /// 
    /// \tparam encoding The prefix encoding method to use (e.g., ASCII, BCD, BINARY, etc.).
    /// \tparam digits The number of digits (length format, such as L, LL, etc.).
    /// \return The number of bytes required for the encoding.
    template <PrefixEncoder encoding, Length digits>
    static constexpr std::size_t parsed_length() noexcept;

    /// Encodes the length `l` into a byte vector `b` based on the specified encoding and length format.
    /// The encoding format (e.g., ASCII, BCD, EBCDIC) will determine how the length is encoded into the byte array.
    /// 
    /// \param l The length to be encoded.
    /// \param b The byte vector where the encoded length will be stored.
    /// \tparam encoding The prefix encoding method to use (e.g., ASCII, BCD, BINARY, etc.).
    /// \tparam digits The number of digits (length format) for the encoded length (e.g., L = 1, LL = 2).
    template <PrefixEncoder encoding, Length digits>
    static constexpr void encode_length(std::size_t l, std::vector<uint8_t>& b);

    /// Decodes a length encoded in a byte vector `b`, starting from the given offset `o`.
    /// The encoding format (e.g., ASCII, BCD, EBCDIC) will determine how the length is decoded from the byte array.
    /// 
    /// \param b The byte vector containing the encoded length.
    /// \param o The offset from which to start decoding.
    /// \tparam encoding The prefix encoding method to use (e.g., ASCII, BCD, BINARY, etc.).
    /// \tparam digits The number of digits (length format) for the encoded length (e.g., L = 1, LL = 2).
    /// \return The decoded length.
    template <PrefixEncoder encoding, Length digits>
    static constexpr std::size_t decode_length(const std::vector<uint8_t>& b, std::size_t o);

    // ── Encoder: Deklarationen ────────────────────────────────────────────────

    // T = gewünschter Rückgabewert
    // Text = Eingabetyp

    // Converts the byte image into a user-defined data type with the specified encoder.
    // T supports only std::vector<uint8_t> or std::string
    // \param text (byte image) to convert
    // \param offset to convert from
    // \param length of n units to read
    template <typename T, Encoder e>
    static constexpr T as(const std::vector<uint8_t>& text, std::size_t offset, std::size_t length);

    // Returns the number of bytes required to convert a std::vector<uint8_t>
    // Mainly used in cooperation with function 'as<T, Encoder>()'
    template <Encoder e>
    static constexpr std::size_t required_sz_for_as(std::size_t n) noexcept;

    // ── Interne Hilfsfunktionen (nicht Teil der öffentlichen API) ─────────────
    namespace detail {
        /// EBCDIC-Byte in-place nach ASCII konvertieren (nullterminierter String)
        static inline void e2a(char* s) noexcept {
            while (*s) *s++ = kEbcdicToAscii[(unsigned char)*s];
        }
        /// EBCDIC-Bytes nach ASCII konvertieren (Puffer + Länge)
        static inline void e2a_n(char* s, std::size_t n) noexcept {
            for (std::size_t i = 0; i < n; ++i)
                s[i] = kEbcdicToAscii[(unsigned char)s[i]];
        }
    }

}

// Implementierungen (werden in _codec.cc explizit instanziiert)
#include "detail/_codec_impl.hh"
