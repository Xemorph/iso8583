#include "_utils.hh"
// [stdc++]
#include <cstddef>
#include <sstream>
#include <type_traits>

std::string TNG_NAMESPACE::utils::getSetBMPFieldsCSV(const dynamic_bitset<>& bitset) {
    std::ostringstream oss;
    bool first = true;

    for (std::size_t i = 0; i < bitset.size(); ++i) {
        if (bitset[i]) {
            if (!first) oss << ",";
            oss << (i);
            first = false;
        }
    }

    return oss.str();
}

template<char MaskChar>
std::string TNG_NAMESPACE::utils::protect(const std::string& s) {
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
// Explizite Instanziierungen
template std::string TNG_NAMESPACE::utils::protect<'_'>(const std::string&);
template std::string TNG_NAMESPACE::utils::protect<'*'>(const std::string&);

/*
template <typename T>
T* TNG_NAMESPACE::utils::unwrap(const std::optional<std::shared_ptr<T>>& opt) {
    return opt ? opt->get() : nullptr;
}
*/

template <typename FieldPtr, typename EnumType, typename MapType>
void TNG_NAMESPACE::utils::trySetExplanation(const FieldPtr& field, const MapType& map, EnumType key) {
    static_assert(std::is_same_v<decltype(map.find(key)), typename MapType::const_iterator>,
        "MapType must support .find(key)");

    if (!field) return;
    if (const auto it = map.find(key); it != map.end()) {
        field->explanation(it->second);
    }
}

template <typename FieldPtr, typename EnumType, typename MapType, typename TransformFn>
void TNG_NAMESPACE::utils::trySetExplanationTransformed(const FieldPtr& field, const MapType& map, TransformFn&& transform) {
    if (!field) return;
    try {
        int base = std::stoi(field->value());
        EnumType key = transform(base);
        if (auto it = map.find(key); it != map.end()) {
            field->explanation(it->second);
        }
    }
    catch (...) {
        // Optional: Fehlerbehandlung
    }
}