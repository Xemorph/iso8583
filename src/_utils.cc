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

// protect<>() ist jetzt header-only implementiert (siehe _utils.hh) - kein
// Export-/Instanziierungs-Ärger mehr über DLL-/SO-Grenzen hinweg.

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