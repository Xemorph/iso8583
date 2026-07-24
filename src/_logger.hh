#pragma once

// [tng/public]
#include <iso8583/ISOLog.hh>
// [fmt]
#include <fmt/format.h>

namespace TNG_NAMESPACE::log {
    ISOLogger* getExternalLogger();
    Level      getLevel();
}

// ── Interne Log-Makros ────────────────────────────────────────────────────────
// Die Bibliothek verwendet AUSSCHLIESSLICH den ISOLogger-Pfad.
// Kein direktes Quill in der DLL – verhindert DLL-Boundary-Singleton-Konflikte.
//
// Wenn kein ISOLogger gesetzt ist, werden Meldungen still ignoriert.
// Anwender injizieren eine QuillBridge (siehe ISOLog.hh).

#define TNG_LOG_IMPL(LEVEL_ENUM, vfmt, ...)                                       \
    do {                                                                           \
        if (auto* _ext = ::TNG_NAMESPACE::log::getExternalLogger()) {             \
            if (::TNG_NAMESPACE::log::Level::LEVEL_ENUM >=                        \
                ::TNG_NAMESPACE::log::getLevel()) {                         \
                _ext->log(::TNG_NAMESPACE::log::Level::LEVEL_ENUM,               \
                          __FILE__, __LINE__,                                      \
                          fmt::format(vfmt, ##__VA_ARGS__));                       \
            }                                                                      \
        }                                                                          \
    } while(0)

#define TNG_LOG_TRACE(fmt, ...)  TNG_LOG_IMPL(TRACE, fmt, ##__VA_ARGS__)
#define TNG_LOG_DEBUG(fmt, ...)  TNG_LOG_IMPL(DEBUG, fmt, ##__VA_ARGS__)
#define TNG_LOG_INFO(fmt, ...)   TNG_LOG_IMPL(INFO,  fmt, ##__VA_ARGS__)
#define TNG_LOG_WARN(fmt, ...)   TNG_LOG_IMPL(WARN,  fmt, ##__VA_ARGS__)
#define TNG_LOG_ERROR(fmt, ...)  TNG_LOG_IMPL(ERR,   fmt, ##__VA_ARGS__)
