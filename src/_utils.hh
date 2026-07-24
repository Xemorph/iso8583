#pragma once

// [stdc++]
#include <optional>
#include <string>
#include <type_traits>
// [tng]
#include <iso8583/config.h>

// -------------------------
// -         UTILS         -
// -------------------------

namespace TNG_NAMESPACE {

    namespace utils {
        
        std::string getSetBMPFieldsCSV(const dynamic_bitset<>& bitset);

        // Header-only (statt extern template + Instanziierung in _utils.cc):
        // Als Out-of-Line-Template ohne TNG_EXPORT ließ sich protect<>() zwar
        // innerhalb der eigenen .so/.dll problemlos aufrufen, aber nicht mehr
        // von außerhalb (z.B. direkt aus einem Test-Executable) - auf GCC/Clang
        // ließ sich das noch mit einem -fvisibility=default-Override für
        // _utils.cc kaschieren, auf MSVC gibt es dafür kein Äquivalent
        // (LNK2019 unresolved external, da nichts hier __declspec(dllexport)
        // trägt). Header-only umgeht das Problem grundsätzlich auf beiden
        // Plattformen, statt es nur für GCC zu flicken.
        template<char MaskChar = '_'>
        std::string protect(const std::string& s) {
            static_assert(std::is_same<decltype(MaskChar), char>::value, "The masking char (MaskChar) must be a typeof char");

            // Disallow newline or null character as mask (compile-time validation)
            static_assert(MaskChar != '\0', "Mask character cannot be null");
            static_assert(MaskChar != '\n', "Mask character cannot be newline");

            const std::size_t len = s.length();
            if (len <= 4) {
                return std::string(len, MaskChar);
            }

            std::string result = s;

            // Find separator: either '^' or '='
            std::size_t sepPos = s.find('^');
            if (sepPos == std::string::npos) {
                sepPos = s.find('=');
            }

            if (sepPos == std::string::npos) {
                sepPos = len;
            }

            if (sepPos < 6) {
                return s;
            }

            const std::size_t lastDigitStart = sepPos >= 4 ? sepPos - 4 : 6;

            // Mask middle section, excluding BIN and last 4 digits
            for (std::size_t i = 6; i < lastDigitStart; ++i) {
                result[i] = MaskChar;
            }

            // Mask characters after separator (excluding '=' and '^')
            if (sepPos < len) {
                for (std::size_t i = sepPos + 1; i < len; ++i) {
                    if (result[i] != '=' && result[i] != '^') {
                        result[i] = MaskChar;
                    }
                }
            }

            return result;
        }

        template <typename T>
        T* unwrap(const std::optional<std::shared_ptr<T>>& opt) {
            return opt ? opt->get() : nullptr;
        }

        template <typename FieldPtr, typename EnumType, typename MapType>
        void trySetExplanation(const FieldPtr& field, const MapType& map, EnumType key);

        template <typename FieldPtr, typename EnumType, typename MapType, typename TransformFn>
        void trySetExplanationTransformed(const FieldPtr& field, const MapType& map, TransformFn&& transform);

    }
}
