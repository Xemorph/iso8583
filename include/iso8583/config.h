#ifndef __TNG_CORE_CONFIG_
#define __TNG_CORE_CONFIG_

#pragma once

// We should give the users the ability to change the namespace of this library
#define TNG_NAMESPACE tng
#define TNG_CORE_VERSION   "0.1.1-alpha"

// ── DLL-Visibility ────────────────────────────────────────────────────────
// Beim Bauen der Shared-Library (ISO8583_DLL_EXPORTS gesetzt durch CMake):
//   Windows  → dllexport
//   GCC/Clang→ visibility("default")  (zusammen mit -fvisibility=hidden)
// Beim Einbinden der fertigen DLL:
//   Windows  → dllimport   (schnellerer Aufruf über IAT)
//   GCC/Clang→ visibility("default")
// Bei einer Static-Library oder Header-only-Nutzung: leer.
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ISO8583_DLL_EXPORTS)
#    define TNG_EXPORT __declspec(dllexport)
#  elif defined(ISO8583_DLL)
#    define TNG_EXPORT __declspec(dllimport)
#  else
#    define TNG_EXPORT
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define TNG_EXPORT __attribute__((visibility("default")))
#else
#  define TNG_EXPORT
#endif

// [stdc++]
#include <cinttypes>
#include <optional>
#include <string>
#include <sstream>
#include <cstddef>
#include <type_traits>
// Schlüsseltyp der Bibliothek.
// Als echtes `using` statt `#define`, damit MSVC die Signatur von
// virtual-Overrides korrekt abgleichen kann (Makros werden beim
// Vergleich virtueller Rückgabetypen von MSVC nicht immer aufgelöst).
namespace TNG_NAMESPACE {
    using key_type = int16_t;
}
// Rückwärtskompatibles Makro – zeigt jetzt auf den Namespace-Alias.
// Bestehender Code der TNG_KEY_TYPE verwendet funktioniert unverändert.
#define TNG_KEY_TYPE ::TNG_NAMESPACE::key_type
// A higher step? The users can decide themself which map they gonna use, but the map must have
// a KEY_TYPE and a VALUE_TYPE. Where the VALUE_TYPE is fix 'std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase >'
//#define TNG_MAP_TYPE std::unordered_map< TNG_KEY_TYPE, TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr >


// -------------------------
// -         UTILS         -
// -------------------------

namespace TNG_NAMESPACE {
    template<typename T>
    struct dependent_false : std::false_type {};
}

// [dynamic_bitset]
#include "detail/extern/libpopcnt.hpp"
#define DYNAMIC_BITSET_CAN_USE_LIBPOPCNT true
#define DYNAMIC_BITSET_NO_STD_BITOPS true
#define DYNAMIC_BITSET_NO_NAMESPACE
#include "detail/extern/dynamic_bitset.hpp"

// [nonstd]
#include <fmt/printf.h>

// [enum init helper]
/*
template<typename T> struct map_init_helper {
    T& data;
    map_init_helper(T& d) : data(d) {}
    map_init_helper& operator() (typename T::key_type const& key, typename T::mapped_type const& value) {
        data[key] = value;
        return *this;
    }
};
template<typename T> map_init_helper<T> map_init(T& item) {
    return map_init_helper<T>(item);
}
*/

#endif // !__TNG_CONFIG_
