#pragma once

// [stdc++]
#include <string>
#include <system_error>
// [iconv]
#include "iconv.h"
// [tng]
#include <iso8583/config.h>


namespace iconv_wrapper
{
    class TNG_EXPORT iconv
    {
    public:
        // Open
        void open(const std::string& fromcode, const std::string& tocode);

        // Close
        void close() noexcept;

        // Convert
        std::string convert(const std::string& in);

        // Convert (with getting the incomplete input / output
        // when an exception occurs)
        std::string& convert(const std::string& in,
            std::string::size_type* pinpos,
            std::string* pout);

        // Get initial sequence
        std::string get_initial_sequence(void);

        // Get initial sequence
        std::string& get_initial_sequence(std::string* pout);

        // Reset conversion state
        void reset(void) const noexcept;

        // Constructor / destructor
        iconv() = default;

        iconv(const std::string& fromcode, const std::string& tocode);

        ~iconv() noexcept;

        // Enable move
        iconv(iconv&&) = default;
        iconv& operator = (iconv&&) = default;

    private:
        // Internal class
        class iconv_const_cast
        {
        public:
            iconv_const_cast(const char** in) noexcept;

            operator char** () const noexcept;
            operator const char** () const noexcept;

        private:
            const char** t;
        };

        // Internal function
        void do_iconv(std::string* pout, const char* inbuf, size_t* pinleft,
            std::string::size_type* pinpos = nullptr);

        // Disable copy
        iconv(iconv const&) = delete;
        iconv& operator = (iconv const&) = delete;

        // Const
        const iconv_t invalid_cd = (iconv_t)-1;

        // Conversion descriptor
        iconv_t convdesc = invalid_cd;
    };
}

