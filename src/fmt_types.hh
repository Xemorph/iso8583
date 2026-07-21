#pragma once

// [tng]
#include "_parser.hh"

namespace TNG_NAMESPACE {

    // -- Sonder-Typen --------------------------------------------------------
    using IF_NOP      = ISOFieldParser< std::nullptr_t, codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BINARY, codec::Padder::NONE >;
    using IF_UNUSED   = ISOFieldParser< ::TNG_NAMESPACE::UNUSED, codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BINARY, codec::Padder::NONE >;
    using IF_CONSUMER = ISOConsumer;

    using IF_REMAINING = ISORemainderFieldParser< std::vector<uint8_t>, codec::Encoder::BINARY >;
    using IFE_REMAINING = ISORemainderFieldParser< std::string, codec::Encoder::EBCDIC >;

    // -- Bitmap ---------------------------------------------------------------
    using IFB_BITMAP    = ISOBitmapFieldParser;

    // -- BINARY (rohe Bytes, kein Encoding) ----------------------------------
    using IF_BINARY      = ISOBinaryFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BINARY >;
    using IF_LBINARY     = ISOBinaryFieldParser< codec::Length::L, codec::PrefixEncoder::BINARY, codec::Encoder::BINARY >;
    using IF_LLBINARY    = ISOBinaryFieldParser< codec::Length::LL, codec::PrefixEncoder::BINARY, codec::Encoder::BINARY >;
    using IF_LLLBINARY   = ISOBinaryFieldParser< codec::Length::LLL, codec::PrefixEncoder::BINARY, codec::Encoder::BINARY >;
    using IF_LLLLBINARY  = ISOBinaryFieldParser< codec::Length::LLLL, codec::PrefixEncoder::BINARY, codec::Encoder::BINARY >;

    // -- ASCII ----------------------------------------------------------------
    // Numerisch (rechts-ausgerichtet, Null-gefüllt)
    using IFA_NUMERIC    = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::ASCII, codec::Padder::LEFT_ZERO >;
    // Alphabetisch/alphanumerisch (links-ausgerichtet, Leerzeichen-gefüllt)
    using IFA_CHAR       = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::ASCII, codec::Padder::RIGHT_T_SPACE >;
    using IFA_NOPAD_CHAR = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::ASCII >;
    // Variable Länge (ASCII Length-Prefix)
    using IFA_LCHAR      = ISOOpaqueFieldParser< codec::Length::L, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    using IFA_LLCHAR     = ISOOpaqueFieldParser< codec::Length::LL, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    using IFA_LLLCHAR    = ISOOpaqueFieldParser< codec::Length::LLL, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    using IFA_LLLLCHAR   = ISOOpaqueFieldParser< codec::Length::LLLL, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    // Numerisch variable Länge
    using IFA_LNUM       = ISOOpaqueFieldParser< codec::Length::L, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    using IFA_LLNUM      = ISOOpaqueFieldParser< codec::Length::LL, codec::PrefixEncoder::ASCII, codec::Encoder::ASCII >;
    // Binary variable Länge (ASCII Length-Prefix)
    using IFA_LBINARY    = ISOBinaryFieldParser< codec::Length::L, codec::PrefixEncoder::ASCII, codec::Encoder::BINARY >;
    using IFA_LLBINARY   = ISOBinaryFieldParser< codec::Length::LL, codec::PrefixEncoder::ASCII, codec::Encoder::BINARY >;
    using IFA_LLLBINARY  = ISOBinaryFieldParser< codec::Length::LLL, codec::PrefixEncoder::ASCII, codec::Encoder::BINARY >;

    // -- BCD (Binary Coded Decimal) -------------------------------------------
    // Numerisch, gepackt (2 Ziffern pro Byte)
    using IFB_NUMERIC    = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::BCD, codec::Padder::LEFT_ZERO >;
    // Variable Länge (BCD Length-Prefix)
    using IFB_LCHAR      = ISOOpaqueFieldParser< codec::Length::L, codec::PrefixEncoder::BCD, codec::Encoder::BCD >;
    using IFB_LLCHAR     = ISOOpaqueFieldParser< codec::Length::LL, codec::PrefixEncoder::BCD, codec::Encoder::BCD >;
    using IFB_LLLCHAR    = ISOOpaqueFieldParser< codec::Length::LLL, codec::PrefixEncoder::BCD, codec::Encoder::BCD >;
    // Binary variable Länge (BCD Length-Prefix)
    using IFB_LBINARY    = ISOBinaryFieldParser< codec::Length::L, codec::PrefixEncoder::BCD, codec::Encoder::BINARY >;
    using IFB_LLBINARY   = ISOBinaryFieldParser< codec::Length::LL, codec::PrefixEncoder::BCD, codec::Encoder::BINARY >;
    using IFB_LLLBINARY  = ISOBinaryFieldParser< codec::Length::LLL, codec::PrefixEncoder::BCD, codec::Encoder::BINARY >;

    // -- EBCDIC ---------------------------------------------------------------
    using IFE_BINARY     = ISOBinaryFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::HEX_EBCDIC >;
    using IFE_LBINARY    = ISOBinaryFieldParser< codec::Length::L, codec::PrefixEncoder::EBCDIC, codec::Encoder::BINARY >;
    using IFE_LLBINARY   = ISOBinaryFieldParser< codec::Length::LL, codec::PrefixEncoder::EBCDIC, codec::Encoder::BINARY >;
    using IFE_LLLBINARY  = ISOBinaryFieldParser< codec::Length::LLL, codec::PrefixEncoder::EBCDIC, codec::Encoder::BINARY >;
    using IFE_LLLLBINARY = ISOBinaryFieldParser< codec::Length::LLLL, codec::PrefixEncoder::EBCDIC, codec::Encoder::BINARY >;
    using IFE_NUMERIC    = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::EBCDIC, codec::Padder::LEFT_ZERO >;
    using IFE_LNUM       = ISOOpaqueFieldParser< codec::Length::L, codec::PrefixEncoder::EBCDIC, codec::Encoder::EBCDIC >;
    using IFE_CHAR       = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::EBCDIC, codec::Padder::RIGHT_T_SPACE >;
    using IFE_NOPAD_CHAR = ISOOpaqueFieldParser< codec::Length::FIX, codec::PrefixEncoder::NONE, codec::Encoder::EBCDIC >;
    using IFE_LCHAR      = ISOOpaqueFieldParser< codec::Length::L, codec::PrefixEncoder::EBCDIC, codec::Encoder::EBCDIC >;
    using IFE_LLCHAR     = ISOOpaqueFieldParser< codec::Length::LL, codec::PrefixEncoder::EBCDIC, codec::Encoder::EBCDIC >;
    using IFE_LLLCHAR    = ISOOpaqueFieldParser< codec::Length::LLL, codec::PrefixEncoder::EBCDIC, codec::Encoder::EBCDIC >;

    // -- Aliase für Rückwärtskompatibilität -----------------------------------
    using IFEMC_TCC = IFE_CHAR;

}
