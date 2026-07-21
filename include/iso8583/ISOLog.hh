#pragma once

/// @file ISOLog.hh
/// @brief Public logging API for libiso8583.
///
/// The library emits internal diagnostic messages through this interface.
/// By default all messages are suppressed (level WARN, no external logger set).
///
/// @par Integration options (choose one)
///
/// **Option 1 – Level only (suppress noise):**
/// @code
///   iso8583::log::setLevel(iso8583::log::Level::DEBUG);   // see library internals
///   iso8583::log::setLevel(iso8583::log::Level::OFF);      // silence all output
/// @endcode
///
/// **Option 2 – Custom logger (no Quill dependency):**
/// @code
///   class MyLogger : public iso8583::log::ISOLogger {
///   public:
///       void log(iso8583::log::Level level,
///                std::string_view file, int line,
///                std::string_view message) override {
///           fmt::print("[iso8583] {}\n", message);
///       }
///   };
///   static MyLogger g_iso_logger;   // must outlive all library calls
///   iso8583::log::setLogger(&g_iso_logger);
/// @endcode
///
/// **Option 3 – Quill bridge (recommended for Quill users):**
///
/// Because libiso8583 is a DLL, Quill's per-process singleton is split across
/// the DLL boundary.  Log macros called inside the DLL write to the DLL's own
/// Quill queue, which the EXE's backend never reads.
///
/// The solution is `QuillBridge`: a header-only `ISOLogger` that calls Quill
/// macros **in the caller's compilation unit** (EXE side), keeping everything
/// in the correct singleton.
///
/// @code
///   // Include order matters – quill headers before ISOLog.hh
///   #include <quill/Frontend.h>
///   #include <quill/LogMacros.h>
///   #include <iso8583/ISOLog.hh>    // QuillBridge is activated by QUILL_VERSION
///
///   quill::Backend::start();
///   auto* myLog = quill::Frontend::create_or_get_logger("myapp", mySink);
///
///   static iso8583::log::QuillBridge bridge(myLog);
///   iso8583::log::setLogger(&bridge);
///   iso8583::log::setLevel(iso8583::log::Level::DEBUG);
/// @endcode
///
/// **Option 4 – Silence:**
/// @code
///   iso8583::log::setLevel(iso8583::log::Level::OFF);
/// @endcode
///
/// @note The caller is responsible for the lifetime of any injected logger.
///       The logger must remain valid until the library is no longer used.

#include "config.h"
#include <string_view>

namespace TNG_NAMESPACE::log {

    // ── Log-Level ─────────────────────────────────────────────────────────────

    /// @brief Minimum severity levels understood by libiso8583.
    ///
    /// Only messages at or above the level set via @ref setLevel are forwarded
    /// to the active @ref ISOLogger.  The numeric values mirror Quill's ordering.
    enum class TNG_EXPORT Level {
        TRACE = 0, ///< Very verbose internal tracing (parsing loops, byte offsets).
        DEBUG = 1, ///< Useful for integration debugging (field values, parser decisions).
        INFO = 2, ///< Normal operational events (spec loaded, connection established).
        WARN = 3, ///< Recoverable anomalies (unknown format/encoding combination). **Default.**
        ERR = 4, ///< Errors that affect correctness (parse failure, null component).
        CRITICAL = 5, ///< Unrecoverable errors.
        OFF = 6, ///< Disables all output.
    };

    // ── Logger interface ─────────────────────────────────────────────────────

    /// @brief Abstract base class for custom logger implementations.
    ///
    /// Implement this interface and pass an instance to @ref setLogger to route
    /// all library log output into your own logging infrastructure (spdlog,
    /// std::cout, a UI panel, etc.).  The library has no Quill dependency in
    /// its public headers.
    class TNG_EXPORT ISOLogger {
    public:
        virtual ~ISOLogger() = default;

        /// @brief Called for every log message that passes the active level filter.
        ///
        /// @param level    Severity of this message.
        /// @param file     Source file where the log call originated (`__FILE__`).
        /// @param line     Source line (`__LINE__`).
        /// @param message  Pre-formatted message string (already has all arguments
        ///                 substituted via `fmt::format`).
        virtual void log(Level level,
            std::string_view file,
            int line,
            std::string_view message) = 0;
    };

    // ── Configuration ────────────────────────────────────────────────────────

    /// @brief Sets the minimum log level.  Messages below this level are dropped.
    ///
    /// Default: `Level::WARN`.
    /// @param lvl New minimum level.
    TNG_EXPORT void setLevel(Level lvl);

    /// @brief Returns the currently active minimum log level.
    TNG_EXPORT Level getLevel();

    /// @brief Injects a custom logger.
    ///
    /// Replaces any previously set logger.  Pass `nullptr` to remove the
    /// custom logger (messages are then silently dropped unless a Quill
    /// override was set via @ref setQuillLogger).
    ///
    /// @param logger Pointer to your @ref ISOLogger implementation.
    ///               Must remain valid until the library is no longer used.
    TNG_EXPORT void setLogger(ISOLogger* logger);

    /// @brief Injects a `quill::Logger*` as a `void*` to avoid a public Quill dependency.
    ///
    /// Cast your `quill::Logger*` to `void*` at the call site:
    /// @code
    ///   iso8583::log::setQuillLogger(static_cast<void*>(myQuillLogger));
    /// @endcode
    ///
    /// @warning Due to Quill's DLL-boundary singleton problem this function
    ///          may not work as expected when libiso8583 is loaded as a DLL.
    ///          Prefer @ref QuillBridge via @ref setLogger in that case.
    ///
    /// @param quillLoggerPtr  `quill::Logger*` cast to `void*`.
    ///                        Pass `nullptr` to clear the override.
    TNG_EXPORT void setQuillLogger(void* quillLoggerPtr);

} // namespace TNG_NAMESPACE::log


// ── QuillBridge (header-only, opt-in) ────────────────────────────────────────
//
// Activated automatically when <quill/LogMacros.h> has been included before
// this header (QUILL_VERSION is then defined).
//
// QuillBridge solves the DLL-singleton problem by expanding Quill macros in
// the *caller's* compilation unit (typically the EXE) rather than inside the
// DLL.  The result is that library log messages reach the same Quill queue and
// backend that the application itself uses.

#if defined(QUILL_VERSION)
#include <quill/LogMacros.h>

namespace TNG_NAMESPACE::log {

    /// @brief Header-only ISOLogger that forwards to a `quill::Logger`.
    ///
    /// @par Why this class exists
    ///
    /// libiso8583 is shipped as a DLL.  Quill manages thread-local SPSC queues
    /// through a per-process singleton stored in a function-local static.  When
    /// both the DLL and the host EXE link Quill statically, each binary has its
    /// own singleton.  Log calls made **inside the DLL** enqueue to the DLL's
    /// singleton; the backend thread (running in the EXE) only drains the EXE's
    /// singleton – so DLL messages are permanently lost.
    ///
    /// `QuillBridge` is compiled into the **EXE** (it is header-only) and
    /// therefore expands `QUILL_LOG_*` macros in the correct Quill singleton.
    ///
    /// @par Usage
    /// @code
    ///   // Must come BEFORE ISOLog.hh to activate QuillBridge
    ///   #include <quill/LogMacros.h>
    ///   #include <iso8583/ISOLog.hh>
    ///
    ///   static iso8583::log::QuillBridge bridge(myQuillLogger);
    ///   iso8583::log::setLogger(&bridge);
    ///   iso8583::log::setLevel(iso8583::log::Level::DEBUG);
    /// @endcode
    class QuillBridge final : public ISOLogger {
        quill::Logger* logger_;

    public:
        /// @brief Constructs the bridge around an existing `quill::Logger`.
        /// @param logger  The Quill logger to forward messages to.  Must not be null.
        explicit QuillBridge(quill::Logger* logger) : logger_(logger) {}

        void log(Level level,
            std::string_view /*file*/,
            int /*line*/,
            std::string_view message) override
        {
            if (!logger_) return;
            // Diese Makros werden im Kontext des Aufrufers expandiert (tng.exe),
            // nicht im DLL-Kontext – damit korrekter Quill-Singleton.
            const std::string msg(message);
            switch (level) {
            case Level::TRACE:    QUILL_LOG_TRACE_L1(logger_, "{}", msg); break;
            case Level::DEBUG:    QUILL_LOG_DEBUG(logger_, "{}", msg); break;
            case Level::INFO:     QUILL_LOG_INFO(logger_, "{}", msg); break;
            case Level::WARN:     QUILL_LOG_WARNING(logger_, "{}", msg); break;
            case Level::ERR:      QUILL_LOG_ERROR(logger_, "{}", msg); break;
            case Level::CRITICAL: QUILL_LOG_CRITICAL(logger_, "{}", msg); break;
            default: break;
            }
        }
    };

} // namespace TNG_NAMESPACE::log
#endif // defined(QUILL_VERSION)
