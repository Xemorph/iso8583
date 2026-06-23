#pragma once

// [quill] - nur intern, nie öffentlich exponiert
#include <quill/Logger.h>
#include <quill/LogMacros.h>
// [tng/public]
#include <iso8583/ISOLog.hh>

namespace TNG_NAMESPACE::log {

    // Interner Quill-Logger – wird von den TNG_LOG_*-Makros verwendet.
    // Nicht öffentlich: Anwender interagieren ausschließlich über ISOLog.hh.
    quill::Logger* getLogger();

    // Interne Hilfsfunktion: gibt den externen ISOLogger zurück (oder nullptr).
    ISOLogger* getExternalLogger();

}

// ── Interne Log-Makros ────────────────────────────────────────────────────────
// Leiten an den externen ISOLogger weiter wenn gesetzt,
// sonst an Quill.
#define TNG_LOG_IMPL(LEVEL, QUILL_MACRO, vfmt, ...) \
    do { \
        if (auto* _ext = ::TNG_NAMESPACE::log::getExternalLogger()) { \
            if (::TNG_NAMESPACE::log::Level::LEVEL >= ::TNG_NAMESPACE::log::getLevel()) { \
                _ext->log(::TNG_NAMESPACE::log::Level::LEVEL, __FILE__, __LINE__, fmt::format(vfmt, ##__VA_ARGS__)); \
            } \
        } else { \
            QUILL_MACRO(::TNG_NAMESPACE::log::getLogger(), vfmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define TNG_LOG_TRACE(fmt, ...)  TNG_LOG_IMPL(TRACE,    LOG_TRACE_L1, fmt, ##__VA_ARGS__)
#define TNG_LOG_DEBUG(fmt, ...)  TNG_LOG_IMPL(DEBUG,    LOG_DEBUG,    fmt, ##__VA_ARGS__)
#define TNG_LOG_INFO(fmt, ...)   TNG_LOG_IMPL(INFO,     LOG_INFO,     fmt, ##__VA_ARGS__)
#define TNG_LOG_WARN(fmt, ...)   TNG_LOG_IMPL(WARN,     LOG_WARNING,  fmt, ##__VA_ARGS__)
#define TNG_LOG_ERROR(fmt, ...)  TNG_LOG_IMPL(ERR,      LOG_ERROR,    fmt, ##__VA_ARGS__)
