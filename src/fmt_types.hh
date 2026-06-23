#pragma once

// [tng]
#include "_parser.hh"

namespace TNG_NAMESPACE {

    // ── Sonder-Typen ────────────────────────────────────────────────────────
    using IF_NOP      = ISOFieldParser< std::nullptr_t, Length::FIX,  PrefixEncoder::NONE,   Encoder::BINARY, Padder::NONE >;
    using IF_UNUSED   = ISOFieldParser< ::TNG_NAMESPACE::UNUSED, Length::FIX, PrefixEncoder::NONE, Encoder::BINARY, Padder::NONE >;

    // ── Bitmap ───────────────────────────────────────────────────────────────
    using IFB_BITMAP    = ISOBitmapFieldParser;

    // ── BINARY (rohe Bytes, kein Encoding) ──────────────────────────────────
    using IF_BINARY      = ISOBinaryFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::BINARY >;
    using IF_LBINARY     = ISOBinaryFieldParser< Length::L,    PrefixEncoder::BINARY, Encoder::BINARY >;
    using IF_LLBINARY    = ISOBinaryFieldParser< Length::LL,   PrefixEncoder::BINARY, Encoder::BINARY >;
    using IF_LLLBINARY   = ISOBinaryFieldParser< Length::LLL,  PrefixEncoder::BINARY, Encoder::BINARY >;
    using IF_LLLLBINARY  = ISOBinaryFieldParser< Length::LLLL, PrefixEncoder::BINARY, Encoder::BINARY >;

    // ── ASCII ────────────────────────────────────────────────────────────────
    // Numerisch (rechts-ausgerichtet, Null-gefüllt)
    using IFA_NUMERIC    = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::ASCII, Padder::LEFT_ZERO >;
    // Alphabetisch/alphanumerisch (links-ausgerichtet, Leerzeichen-gefüllt)
    using IFA_CHAR       = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::ASCII, Padder::RIGHT_T_SPACE >;
    using IFA_NOPAD_CHAR = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::ASCII >;
    // Variable Länge (ASCII Length-Prefix)
    using IFA_LCHAR      = ISOOpaqueFieldParser< Length::L,    PrefixEncoder::ASCII,  Encoder::ASCII >;
    using IFA_LLCHAR     = ISOOpaqueFieldParser< Length::LL,   PrefixEncoder::ASCII,  Encoder::ASCII >;
    using IFA_LLLCHAR    = ISOOpaqueFieldParser< Length::LLL,  PrefixEncoder::ASCII,  Encoder::ASCII >;
    using IFA_LLLLCHAR   = ISOOpaqueFieldParser< Length::LLLL, PrefixEncoder::ASCII,  Encoder::ASCII >;
    // Numerisch variable Länge
    using IFA_LNUM       = ISOOpaqueFieldParser< Length::L,    PrefixEncoder::ASCII,  Encoder::ASCII >;
    using IFA_LLNUM      = ISOOpaqueFieldParser< Length::LL,   PrefixEncoder::ASCII,  Encoder::ASCII >;
    // Binary variable Länge (ASCII Length-Prefix)
    using IFA_LBINARY    = ISOBinaryFieldParser< Length::L,    PrefixEncoder::ASCII,  Encoder::BINARY >;
    using IFA_LLBINARY   = ISOBinaryFieldParser< Length::LL,   PrefixEncoder::ASCII,  Encoder::BINARY >;
    using IFA_LLLBINARY  = ISOBinaryFieldParser< Length::LLL,  PrefixEncoder::ASCII,  Encoder::BINARY >;

    // ── BCD (Binary Coded Decimal) ───────────────────────────────────────────
    // Numerisch, gepackt (2 Ziffern pro Byte)
    using IFB_NUMERIC    = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::BCD,   Padder::LEFT_ZERO >;
    // Variable Länge (BCD Length-Prefix)
    using IFB_LCHAR      = ISOOpaqueFieldParser< Length::L,    PrefixEncoder::BCD,    Encoder::BCD >;
    using IFB_LLCHAR     = ISOOpaqueFieldParser< Length::LL,   PrefixEncoder::BCD,    Encoder::BCD >;
    using IFB_LLLCHAR    = ISOOpaqueFieldParser< Length::LLL,  PrefixEncoder::BCD,    Encoder::BCD >;
    // Binary variable Länge (BCD Length-Prefix)
    using IFB_LBINARY    = ISOBinaryFieldParser< Length::L,    PrefixEncoder::BCD,    Encoder::BINARY >;
    using IFB_LLBINARY   = ISOBinaryFieldParser< Length::LL,   PrefixEncoder::BCD,    Encoder::BINARY >;
    using IFB_LLLBINARY  = ISOBinaryFieldParser< Length::LLL,  PrefixEncoder::BCD,    Encoder::BINARY >;

    // ── EBCDIC ───────────────────────────────────────────────────────────────
    using IFE_BINARY     = ISOBinaryFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::HEX_EBCDIC >;
    using IFE_LBINARY    = ISOBinaryFieldParser< Length::L,    PrefixEncoder::EBCDIC, Encoder::BINARY >;
    using IFE_LLBINARY   = ISOBinaryFieldParser< Length::LL,   PrefixEncoder::EBCDIC, Encoder::BINARY >;
    using IFE_LLLBINARY  = ISOBinaryFieldParser< Length::LLL,  PrefixEncoder::EBCDIC, Encoder::BINARY >;
    using IFE_LLLLBINARY = ISOBinaryFieldParser< Length::LLLL, PrefixEncoder::EBCDIC, Encoder::BINARY >;
    using IFE_NUMERIC    = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::EBCDIC, Padder::LEFT_ZERO >;
    using IFE_LNUM       = ISOOpaqueFieldParser< Length::L,    PrefixEncoder::EBCDIC, Encoder::EBCDIC >;
    using IFE_CHAR       = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::EBCDIC, Padder::RIGHT_T_SPACE >;
    using IFE_NOPAD_CHAR = ISOOpaqueFieldParser< Length::FIX,  PrefixEncoder::NONE,   Encoder::EBCDIC >;
    using IFE_LCHAR      = ISOOpaqueFieldParser< Length::L,    PrefixEncoder::EBCDIC, Encoder::EBCDIC >;
    using IFE_LLCHAR     = ISOOpaqueFieldParser< Length::LL,   PrefixEncoder::EBCDIC, Encoder::EBCDIC >;
    using IFE_LLLCHAR    = ISOOpaqueFieldParser< Length::LLL,  PrefixEncoder::EBCDIC, Encoder::EBCDIC >;

    // ── Aliase für Rückwärtskompatibilität ───────────────────────────────────
    using IFEMC_TCC = IFE_CHAR;

}
