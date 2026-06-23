#include "_iconv_wrapper.hh"

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
            char bfr[80]; bfr[79] = '\n';
            strerror_s(bfr, 80, errno);
            printf("ERROR: %s\n", bfr);
            throw std::system_error(errno, std::system_category());
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
        ::iconv(convdesc, nullptr, nullptr, nullptr, nullptr);
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
                throw std::system_error(errno, std::system_category());
            }
            std::string::size_type pos = (outbuf - &pout->at(0));
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
