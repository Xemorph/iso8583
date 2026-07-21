#ifndef __TNG_CORE_CONFIG_
#define __TNG_CORE_CONFIG_

#pragma once

/// @file config.h
/// @brief Library-wide configuration: namespace, version, DLL visibility, key type.
///
/// This header is included transitively by all other public headers.
/// Applications do not normally need to include it directly.

// ── Namespace ────────────────────────────────────────────────────────────────

/// @brief The C++ namespace used by libiso8583.
///
/// All public types live in this namespace (`iso8583::ISOMessage`, `iso8583::log::Level`, …).
/// The macro allows downstream consumers to rename the namespace if required,
/// though the default `tng` is strongly recommended.
#define TNG_NAMESPACE iso8583

/// @brief Library version string in `MAJOR.MINOR.PATCH-STAGE` format.
#define TNG_CORE_VERSION   "0.1.1-alpha"

// ── DLL visibility ───────────────────────────────────────────────────────────
//
// When building libiso8583 as a shared library, CMake sets ISO8583_DLL_EXPORTS.
// When *consuming* the shared library, define ISO8583_DLL before including any
// header so that MSVC generates faster IAT-based import stubs.
//
// | Scenario                  | Windows macro      | GCC/Clang attribute  |
// |---------------------------|--------------------|----------------------|
// | Building the DLL          | __declspec(export) | visibility("default")|
// | Consuming the DLL         | __declspec(import) | visibility("default")|
// | Static lib / header-only  | (empty)            | (empty)              |

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

// ── Standard headers ─────────────────────────────────────────────────────────

// [stdc++]
#include <cinttypes>
#include <optional>
#include <string>
#include <sstream>
#include <cstddef>
#include <type_traits>

// ── Key type ─────────────────────────────────────────────────────────────────

namespace TNG_NAMESPACE {
    /// @brief The integer type used as the DE (data element) key throughout the library.
    ///
    /// Defined as a `using` alias rather than a `#define` so that MSVC can
    /// correctly match virtual override signatures (macros are not resolved
    /// when comparing virtual return types on MSVC).
    ///
    /// Range: −32 768 … 32 767.  Special value −1 is used for the root
    /// `ISOMessage` that is not itself a sub-field.
    using key_type = int16_t;
}
/// @brief Backward-compatible macro alias for `tng::key_type` (`int16_t`).
///
/// Existing code that uses `TNG_KEY_TYPE` continues to work unchanged.
#define TNG_KEY_TYPE ::TNG_NAMESPACE::key_type

// A higher step? The users can decide themself which map they gonna use, but the map must have
// a KEY_TYPE and a VALUE_TYPE. Where the VALUE_TYPE is fix 'std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase >'
//#define TNG_MAP_TYPE std::unordered_map< TNG_KEY_TYPE, TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr >


// ── Template helper ──────────────────────────────────────────────────────────

namespace TNG_NAMESPACE {
    /// @brief Dependent false for use in `static_assert` inside `if constexpr` branches.
    ///
    /// Allows writing a `static_assert` that fires only when an unsupported
    /// template instantiation is actually compiled:
    /// @code
    ///   static_assert(dependent_false<T>::value, "Unsupported type T");
    /// @endcode
    template<typename T>
    struct dependent_false : std::false_type {};
}

// ── Third-party: ─────────────────────────────────────────────────────────────

// [dynamic_bitset]
#include "detail/extern/libpopcnt.hpp"
#define DYNAMIC_BITSET_CAN_USE_LIBPOPCNT true
#define DYNAMIC_BITSET_NO_STD_BITOPS true
#define DYNAMIC_BITSET_NO_NAMESPACE
#include "detail/extern/dynamic_bitset.hpp"

// [nonstd]
#include <fmt/printf.h>


#endif // !__TNG_CORE_CONFIG_
