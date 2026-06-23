#pragma once

// [stdc++]
#include <string_view>
// [quill]
#include <quill/Logger.h>
#include <quill/LogMacros.h>
// [tng/config]
#include <iso8583/config.h>

namespace TNG_NAMESPACE::log {

    enum class Level { TRACE, DEBUG, INFO, WARN, ERR, CRITICAL, OFF };

    // Vom Aufrufer der DLL konfigurierbar
    TNG_EXPORT void setLevel(Level lvl);

    // Aufrufer kann eigenen Quill-Logger injizieren –
    // z.B. um in die eigene Log-Infrastruktur zu integrieren
    TNG_EXPORT void setExternalLogger(quill::Logger* logger);

    quill::Logger* getLogger();

#define TNG_LOG_TRACE(fmt, ...) LOG_TRACE_L1(::TNG_NAMESPACE::log::getLogger(), fmt, ##__VA_ARGS__)
#define TNG_LOG_DEBUG(fmt, ...) LOG_DEBUG(::TNG_NAMESPACE::log::getLogger(), fmt, ##__VA_ARGS__)
#define TNG_LOG_INFO(fmt, ...) LOG_INFO(::TNG_NAMESPACE::log::getLogger(), fmt, ##__VA_ARGS__)
#define TNG_LOG_WARN(fmt, ...)  LOG_WARNING(::TNG_NAMESPACE::log::getLogger(), fmt, ##__VA_ARGS__)
#define TNG_LOG_ERROR(fmt, ...) LOG_ERROR(::TNG_NAMESPACE::log::getLogger(), fmt, ##__VA_ARGS__)

}