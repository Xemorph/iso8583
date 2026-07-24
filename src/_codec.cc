// _codec.cc
// Explizite Template-Instanziierungen für alle in fmt_types.hh verwendeten
// PrefixEncoder×Length×Encoder-Kombinationen.
//
// Vorteil: Die Implementierung wird nur einmal kompiliert (hier),
// nicht in jeder Translation Unit die _codec.hh einbindet.
// Der Linker findet genau eine Definition pro Kombination.
//
// Neue Kombinationen: in fmt_types.hh einen neuen Typ anlegen und hier
// die entsprechenden Instanziierungen ergänzen.

// _codec_impl.hh einbinden BEVOR die expliziten Instanziierungen,
// damit die Definitionen sichtbar sind.
#define CODEC_IMPL_SOURCE
#include <iso8583/_codec.hh>

namespace TNG_NAMESPACE::codec {

#if ENABLE_ICONV
    namespace detail {

        // Ein iconv_t-Deskriptor pro Thread und Richtung wird einmalig geöffnet
        // und über alle nachfolgenden Aufrufe hinweg wiederverwendet, statt bei
        // JEDEM einzelnen EBCDIC-Feld iconv_open()/iconv_close() neu aufzurufen.
        // Micro-Benchmark (100.000 Konvertierungen, glibc, x86_64):
        //   iconv_open()+convert()+iconv_close() pro Aufruf:  ~0.44 us/Aufruf
        //   wiederverwendeter Deskriptor + reset() davor:      ~0.08 us/Aufruf
        // -> Faktor ~5x, da iconv_open() ein Gconv-Modul-Lookup durchführt statt
        // nur einen billigen Zähler zu inkrementieren. reset() vor jeder
        // Konvertierung ist dagegen kein Syscall (nur iconv() mit Null-Puffern)
        // und schützt vorsorglich vor Shift-State-Resten - für EBCDIC
        // (zustandslos) zwar nicht nötig, aber robuster, falls die Zielcodepage
        // jemals gegen eine zustandsbehaftete getauscht wird.
        std::string ebcdic_to_ascii_cached(const std::string& data) {
            thread_local iconv_wrapper::iconv enc("IBM-1047", "");
            enc.reset();
            return enc.convert(data);
        }

        std::string ascii_to_ebcdic_cached(const std::string& data) {
            thread_local iconv_wrapper::iconv enc("", "IBM-1047");
            enc.reset();
            return enc.convert(data);
        }

    } // namespace detail
#endif

// -----------------------------------------------------------------------------
// parsed_length
// -----------------------------------------------------------------------------
#define INST_PARSED(PE, L) \
    template std::size_t parsed_length<PrefixEncoder::PE, Length::L>() noexcept;

// NONE (nur FIX relevant, der Rest wird nicht gebraucht)
INST_PARSED(NONE,   FIX)

// ASCII
INST_PARSED(ASCII,  L)
INST_PARSED(ASCII,  LL)
INST_PARSED(ASCII,  LLL)
INST_PARSED(ASCII,  LLLL)

// BCD
INST_PARSED(BCD,    L)
INST_PARSED(BCD,    LL)
INST_PARSED(BCD,    LLL)
INST_PARSED(BCD,    LLLL)

// BINARY
INST_PARSED(BINARY, L)
INST_PARSED(BINARY, LL)
INST_PARSED(BINARY, LLL)
INST_PARSED(BINARY, LLLL)
INST_PARSED(BINARY, UNKNOWN)

// EBCDIC
INST_PARSED(EBCDIC, L)
INST_PARSED(EBCDIC, LL)
INST_PARSED(EBCDIC, LLL)
INST_PARSED(EBCDIC, LLLL)
INST_PARSED(EBCDIC, UNKNOWN)

#undef INST_PARSED

// -----------------------------------------------------------------------------
// encode_length
// -----------------------------------------------------------------------------
#define INST_ENC(PE, L) \
    template void encode_length<PrefixEncoder::PE, Length::L>(std::size_t, std::vector<uint8_t>&);

INST_ENC(NONE,   FIX)
INST_ENC(ASCII,  L)   INST_ENC(ASCII,  LL)   INST_ENC(ASCII,  LLL)   INST_ENC(ASCII,  LLLL)
INST_ENC(BCD,    L)   INST_ENC(BCD,    LL)   INST_ENC(BCD,    LLL)   INST_ENC(BCD,    LLLL)
INST_ENC(BINARY, L)   INST_ENC(BINARY, LL)   INST_ENC(BINARY, LLL)   INST_ENC(BINARY, LLLL)
INST_ENC(EBCDIC, L)   INST_ENC(EBCDIC, LL)   INST_ENC(EBCDIC, LLL)   INST_ENC(EBCDIC, LLLL)

#undef INST_ENC

// -----------------------------------------------------------------------------
// decode_length
// -----------------------------------------------------------------------------
#define INST_DEC(PE, L) \
    template std::size_t decode_length<PrefixEncoder::PE, Length::L>(const std::vector<uint8_t>&, std::size_t);

INST_DEC(NONE,   FIX)
INST_DEC(ASCII,  L)   INST_DEC(ASCII,  LL)   INST_DEC(ASCII,  LLL)   INST_DEC(ASCII,  LLLL)
INST_DEC(BCD,    L)   INST_DEC(BCD,    LL)   INST_DEC(BCD,    LLL)   INST_DEC(BCD,    LLLL)
INST_DEC(BINARY, L)   INST_DEC(BINARY, LL)   INST_DEC(BINARY, LLL)   INST_DEC(BINARY, LLLL)
INST_DEC(EBCDIC, L)   INST_DEC(EBCDIC, LL)   INST_DEC(EBCDIC, LLL)   INST_DEC(EBCDIC, LLLL)

#undef INST_DEC

// -----------------------------------------------------------------------------
// required_sz_for_as
// -----------------------------------------------------------------------------
template std::size_t required_sz_for_as<Encoder::ASCII>  (std::size_t) noexcept;
template std::size_t required_sz_for_as<Encoder::BCD>    (std::size_t) noexcept;
template std::size_t required_sz_for_as<Encoder::BINARY> (std::size_t) noexcept;
template std::size_t required_sz_for_as<Encoder::EBCDIC> (std::size_t) noexcept;

// -----------------------------------------------------------------------------
// as<T, Encoder>
// -----------------------------------------------------------------------------
#define INST_AS_STR(E) \
    template std::string as<std::string, Encoder::E>(const std::vector<uint8_t>&, std::size_t, std::size_t);
#define INST_AS_BIN(E) \
    template std::vector<uint8_t> as<std::vector<uint8_t>, Encoder::E>(const std::vector<uint8_t>&, std::size_t, std::size_t);

INST_AS_STR(ASCII)
INST_AS_STR(BCD)
INST_AS_STR(EBCDIC)

INST_AS_BIN(BINARY)
INST_AS_BIN(HEX_EBCDIC)

#undef INST_AS_STR
#undef INST_AS_BIN

// -----------------------------------------------------------------------------
// to<Encoder, T>
// -----------------------------------------------------------------------------
#define INST_STR_TO(E) \
    template void to<Encoder::E, std::string>(const std::string& value, std::vector<uint8_t>& b, std::size_t offset);
#define INST_BIN_TO(E) \
    template void to<Encoder::E, std::vector<uint8_t>>(const std::vector<uint8_t>& value, std::vector<uint8_t>& b, std::size_t offset);

INST_STR_TO(ASCII)
INST_STR_TO(EBCDIC)
INST_STR_TO(BCD)

INST_BIN_TO(BINARY)
INST_BIN_TO(HEX_EBCDIC)

#undef INST_STR_TO
#undef INST_BIN_TO

} // namespace TNG_NAMESPACE
