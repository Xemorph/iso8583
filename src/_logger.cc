#include "_logger.hh"
// [quill]
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>

static quill::Logger*& loggerRef() {
    static quill::Logger* instance = nullptr;
    return instance;
}

namespace TNG_NAMESPACE::log {

    // === Interner Singleton-Logger ==========================================

    quill::Logger* getLogger() {
        if (loggerRef())
            return loggerRef();  // externer Logger hat Vorrang

        static quill::Logger* logger = []() -> quill::Logger* {
            // Backend einmalig starten
            quill::BackendOptions bo;
            bo.thread_name = "tng_log";
            quill::Backend::start(bo);

            // File-Sink als Default
            auto sink = quill::Frontend::create_or_get_sink<quill::FileSink>("dll.log");
            return quill::Frontend::create_or_get_logger("tng", std::move(sink));
            }();
        return logger;
    }

    // === Public API =========================================================

    void setLevel(Level lvl) {
        constexpr quill::LogLevel mapping[] = {
            quill::LogLevel::TraceL1,   // TRACE
            quill::LogLevel::Debug,     // DEBUG
            quill::LogLevel::Info,      // INFO
            quill::LogLevel::Warning,   // WARN
            quill::LogLevel::Error,     // ERR
            quill::LogLevel::Critical,  // CRITICAL
            quill::LogLevel::None,      // OFF
        };
        getLogger()->set_log_level(mapping[static_cast<int>(lvl)]);
        TNG_LOG_INFO("Current level {}", static_cast<int>(getLogger()->get_log_level()));
    }

    void setExternalLogger(quill::Logger* logger) {
        loggerRef() = logger;
    }


    // === Template-Implementierungen =========================================

    /*
    template<typename... Args>
    void trace(std::string_view fmt, Args&&... args) {
        LOG_TRACE_L1(getLogger(), fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void debug(std::string_view fmt, Args&&... args) {
        LOG_DEBUG(getLogger(), fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void info(std::string_view fmt, Args&&... args) {
        LOG_INFO(getLogger(), fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void warn(std::string_view fmt, Args&&... args) {
        LOG_WARNING(getLogger(), fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void error(std::string_view fmt, Args&&... args) {
        LOG_ERROR(getLogger(), fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void critical(std::string_view fmt, Args&&... args) {
        LOG_CRITICAL(getLogger(), fmt, std::forward<Args>(args)...);
    }

    template void trace(std::string_view);
    template void debug(std::string_view);
    template void info(std::string_view);
    template void warn(std::string_view);
    template void error(std::string_view);
    template void critical(std::string_view);

    template void trace(std::string_view, std::size_t const&);
    template void warn(std::string_view, std::size_t const&, std::size_t const&);
    template void error(std::string_view, std::size_t const&, std::size_t const&);
    */

}