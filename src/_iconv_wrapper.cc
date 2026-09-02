#include "_iconv_wrapper.hh"
// [tng/internal]
#include "_logger.hh"
// [stdc++]
#include <system_error>

namespace iconv_wrapper
{
    void iconv::open(const std::string& fromcode, const std::string& tocode)
    {
        if (convdesc != invalid_cd)
        {
            close();
        }
        convdesc = iconv_open(tocode.c_str(), fromcode.c_str());
        if (convdesc == invalid_cd)
        {
            const std::system_error err(errno, std::system_category(),
                "iconv_open(\"" + fromcode + "\" -> \"" + tocode + "\") failed");
            TNG_LOG_ERROR("[iconv::open] {}", err.what());
            throw err;
        }
    }

    void iconv::close() noexcept
    {
        if (convdesc != invalid_cd)
        {
            iconv_close(convdesc);
            convdesc = invalid_cd;
        }
    }

    std::string iconv::convert(const std::string& in)
    {
        std::string out(in.size(), '\0');
        convert(in, nullptr, &out);
        return out;
    }

    std::string& iconv::convert(const std::string& in,
        std::string::size_type* pinpos,
        std::string* pout)
    {
        size_t inleft{ in.size() };
        if (inleft)
            do_iconv(pout, &in.at(0), &inleft, pinpos);
        else
            pout->clear();
        return *pout;
    }

    std::string iconv::get_initial_sequence(void)
    {
        std::string out(1, '\0');
        get_initial_sequence(&out);
        return out;
    }

    std::string& iconv::get_initial_sequence(std::string* pout)
    {
        do_iconv(pout, nullptr, nullptr);
        return *pout;
    }

    void iconv::reset(void) const noexcept
    {
        // POSIX/libiconv: der Reset wird ausgelöst, wenn *inbuf UND *outbuf
        // auf NULL zeigen — es also Zeiger AUF Null-Pointer übergeben werden.
        // Der frühere Aufruf iconv(cd, nullptr, nullptr, nullptr, nullptr)
        // übergeben die Null-Pointer selbst und löste den Reset NICHT aus;
        // der zustandsbehaftete IBM-1047-Konverter trug seinen Shift/Escape-
        // Zustand daher von Konvertierung zu Konvertierung mit (Cross-Field-
        // Kontamination, s. issues/a).
        char* pin = nullptr;
        char* pout = nullptr;
        ::iconv(convdesc, &pin, nullptr, &pout, nullptr);
    }

    void iconv::do_iconv(std::string* pout,
        const char* inbuf, size_t* pinleft,
        std::string::size_type* pinpos)
    {
        if (pout->empty())
        {
            pout->resize(1);
        }
        const char* inbuf_tmp{ inbuf };
        char* outbuf{ &pout->at(0) };
        size_t outleft{ pout->size() };
        size_t s;

        // ── Sicherheitsgrenzen (C1, Security-Plan Phase 1) ──────────────────
        // Der E2BIG-Zweig vergrößert pout in jedem Loop-Durchlauf um den
        // Faktor 2. Ohne Wächter könnte ein fehlerhaftes iconv-Ergebnis
        // (E2BIG ohne jeglichen Fortschritt) eine Endlosschleife mit
        // exponentiellem Speicherverbrauch auslösen:
        //  (1) No-Fortschritt: zwei aufeinanderfolgende E2BIG, bei denen
        //      weder die verbrauchte Eingabe noch die produzierte Ausgabe
        //      zugelegt haben.
        //  (2) Hartes Output-Cap: die hier genutzten Konvertierungen
        //      (EBCDIC<->ASCII, IBM-1047) sind 1:1; mehr als das Doppelte
        //      der Eingabe + Reserve ist ein Konverter-Fehler, kein
        //      legitimer Output. (inbuf==nullptr: nur Initialsequenz,
        //      wird durch die Konstante gedeckt.)
        const std::string::size_type base = pout->size();
        const std::string::size_type in_total =
            (inbuf && pinleft) ? static_cast<std::string::size_type>(*pinleft) : 0;
        const std::string::size_type max_out = in_total * 2 + 32;
        bool have_prev = false;
        std::string::size_type produced_prev = 0;
        std::string::size_type consumed_prev = 0;

        while ((s = ::iconv(convdesc,
            iconv_const_cast(&inbuf_tmp), pinleft,
            &outbuf, &outleft)) == static_cast<size_t>(-1))
        {
            if (errno != E2BIG)
            {
                if (pinpos && inbuf)
                {
                    *pinpos = (inbuf_tmp - inbuf);
                }
                pout->resize(outbuf - &pout->at(0));
                // MSVCs strerror() kennt POSIX-Fehlerwerte wie EILSEQ nicht und
                // liefert dort "unknown error". Wir geben die Bedeutung daher
                // explizit mit, statt auf strerror() zu vertrauen.
                // (EILSEQ: 42 auf Darwin/libiconv-Builds, 133 auf glibc)
#ifndef EILSEQ
                constexpr int EILSEQ = 42;
#endif
                const char* hint =
                    (errno == EILSEQ) ? " (EILSEQ: ungueltige oder nicht abbildbare Byte-Sequenz)"
                    : (errno == EINVAL) ? " (EINVAL: ungueltiges Argument)"
                    : "";
                throw std::system_error(errno, std::system_category(),
                    "iconv() failed with errno=" + std::to_string(errno) + hint);
            }
            // Wächter (C1): WERDEN IMMER VOR der Puffervergrößerung
            // ausgewertet (Zeigerdifferenzen wären nach einem realloc
            // des std::string ungültig).
            const std::string::size_type produced =
                static_cast<std::string::size_type>(outbuf - &pout->at(0)) - base;
            const std::string::size_type consumed =
                (inbuf) ? static_cast<std::string::size_type>(inbuf_tmp - inbuf) : 0;

            if (produced > max_out)
            {
                throw std::runtime_error(
                    "iconv(): Ausgabe überschreitet die Sicherheitsgrenze von " +
                    std::to_string(max_out) + " Bytes - Abbruch");
            }
            if (have_prev && produced == produced_prev && consumed == consumed_prev)
            {
                throw std::runtime_error(
                    "iconv(): E2BIG ohne Fortschritt (weder Eingabe verbraucht noch "
                    "Ausgabe produziert) - Abbruch zur Vermeidung einer Endlosschleife");
            }
            produced_prev = produced;
            consumed_prev = consumed;
            have_prev = true;

            const std::string::size_type pos = produced + base;
            pout->resize(pout->size() * 2);
            outbuf = &pout->at(pos);
            outleft = pout->size() - pos;
        }
        pout->resize(outbuf - &pout->at(0));
    }

    // Internal class implementation
    iconv::iconv_const_cast::iconv_const_cast(const char** in) noexcept : t(in) {}

    iconv::iconv_const_cast::operator char** () const noexcept
    {
        return const_cast<char**>(t);
    }

    iconv::iconv_const_cast::operator const char** () const noexcept
    {
        return t;
    }

    iconv::iconv(const std::string& fromcode, const std::string& tocode)
    {
        open(fromcode, tocode);
    }

    iconv::~iconv() noexcept
    {
        close();
    }
}
