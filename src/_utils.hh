#pragma once

// [stdc++]
#include <optional>
#include <string>
// [tng]
#include <iso8583/config.h>

// -------------------------
// -         UTILS         -
// -------------------------

namespace TNG_NAMESPACE {

    namespace utils {
        
        std::string getSetBMPFieldsCSV(const dynamic_bitset<>& bitset);

        template<char MaskChar = '_'>
        std::string protect(const std::string& s);

        extern template std::string protect<'_'>(const std::string&);
        extern template std::string protect<'#'>(const std::string&);

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
