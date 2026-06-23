#include "_logger.hh"
// [quill]
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/FileSink.h>

namespace {
    // Interner Zustand – nur in dieser Translation Unit sichtbar
    ::TNG_NAMESPACE::log::ISOLogger* g_external_logger = nullptr;
    ::TNG_NAMESPACE::log::Level      g_level           = ::TNG_NAMESPACE::log::Level::WARN;
}

namespace TNG_NAMESPACE::log {

    // ── Interner Quill-Logger ─────────────────────────────────────────────────
    quill::Logger* getLogger() {
        static quill::Logger* logger = []() -> quill::Logger* {
            quill::BackendOptions bo;
            bo.thread_name = "tng_log";
            quill::Backend::start(bo);
            auto sink = quill::Frontend::create_or_get_sink<quill::FileSink>("iso8583.log");
            return quill::Frontend::create_or_get_logger("tng", std::move(sink));
        }();
        return logger;
    }

    // ── Interner Accessor für externen Logger ─────────────────────────────────
    ISOLogger* getExternalLogger() {
        return g_external_logger;
    }

    // ── Öffentliche API (aus ISOLog.hh) ──────────────────────────────────────

    void setLevel(Level lvl) {
        g_level = lvl;
        // Quill-Logger ebenfalls synchronisieren (greift wenn kein externer Logger gesetzt)
        constexpr quill::LogLevel mapping[] = {
            quill::LogLevel::TraceL1,  // TRACE
            quill::LogLevel::Debug,    // DEBUG
            quill::LogLevel::Info,     // INFO
            quill::LogLevel::Warning,  // WARN
            quill::LogLevel::Error,    // ERR
            quill::LogLevel::Critical, // CRITICAL
            quill::LogLevel::None,     // OFF
        };
        getLogger()->set_log_level(mapping[static_cast<int>(lvl)]);
    }

    Level getLevel() {
        return g_level;
    }

    void setLogger(ISOLogger* logger) {
        g_external_logger = logger;
    }

} // namespace TNG_NAMESPACE::log
