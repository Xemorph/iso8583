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
//
// Der DE-Schlüsseltyp ist konfigurierbar:
//
//   Standard (nichts definiert):        int16_t  (-32768 … 32767)
//   -DISO8583_BERTLV / #define ISO8583_BERTLV:
//                                        int32_t  - nötig für volle BER-TLV/
//                                        EMV-Tag-Unterstützung, da reale
//                                        2-Byte-EMV-Tags mit erstem Byte
//                                        >= 0x80 (z.B. 9F26, 5F24) als
//                                        Big-Endian-Wert > 32767 ergeben und
//                                        nicht in int16_t passen (siehe
//                                        BerTag in src/_tlv_policy.hh).
//   -DISO8583_KEY_TYPE=<Typ>:            frei definierbar, überstimmt auch
//                                        ISO8583_BERTLV - z.B. int64_t falls
//                                        eigene Tag-Schemata noch größere
//                                        Werte benötigen.
//
// WICHTIG: Dies ist eine ABI-relevante Einstellung (TNG_KEY_TYPE fließt u.a.
// in die virtuelle Signatur von ISOComponentPtrBase::key() ein). Wird die
// Bibliothek als Shared Library gebaut, MUSS jeder Konsument mit demselben
// Wert übersetzt werden - deshalb setzt CMakeLists.txt die Definition PUBLIC
// auf dem Target `iso8583`, sodass sie über find_package(iso8583)/
// target_link_libraries automatisch an Konsumenten weitergereicht wird.
// Bei manueller Einbindung (ohne CMake-Target) muss das Makro von Hand in
// Bibliothek UND allen Konsumenten identisch gesetzt werden.

#if defined(ISO8583_KEY_TYPE)
   // Explizite Überschreibung hat immer Vorrang.
#elif defined(ISO8583_BERTLV)
#  define ISO8583_KEY_TYPE int32_t
#else
#  define ISO8583_KEY_TYPE int16_t
#endif

namespace TNG_NAMESPACE {
    /// @brief The integer type used as the DE (data element) key throughout the library.
    ///
    /// Defined as a `using` alias rather than a `#define` so that MSVC can
    /// correctly match virtual override signatures (macros are not resolved
    /// when comparing virtual return types on MSVC).
    ///
    /// Standardmäßig `int16_t` (Bereich -32768…32767). Wird `ISO8583_BERTLV`
    /// definiert (oder `ISO8583_KEY_TYPE` explizit gesetzt), ist der Typ
    /// stattdessen `int32_t` (bzw. der explizit gewählte Typ) - siehe oben.
    /// Special value −1 is used for the root `ISOMessage` that is not itself
    /// a sub-field, and −2 is used internally for TLV TCC fields.
    using key_type = ISO8583_KEY_TYPE;
}
/// @brief Backward-compatible macro alias for `tng::key_type`.
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


#endif // !__TNG_CORE_CONFIG_
