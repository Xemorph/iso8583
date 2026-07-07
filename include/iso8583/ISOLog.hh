#pragma once

/**
 * @file ISOLog.hh
 * @brief Öffentliche Logging-API für libiso8583.
 *
 * Ermöglicht dem Anwender das Logging der Bibliothek zu kontrollieren.
 *
 * ── Option 1: Log-Level setzen ────────────────────────────────────────────
 * @code
 *   tng::log::setLevel(tng::log::Level::DEBUG);
 * @endcode
 *
 * ── Option 2: Eigenen Logger injizieren (ohne Quill-Abhängigkeit) ─────────
 * @code
 *   class MyLogger : public tng::log::ISOLogger {
 *   public:
 *       void log(tng::log::Level level,
 *                std::string_view file, int line,
 *                std::string_view message) override
 *       {
 *           std::cout << "[iso8583] " << message << "\n";
 *       }
 *   };
 *
 *   static MyLogger logger;          // Lebensdauer beachten!
 *   tng::log::setLogger(&logger);
 * @endcode
 *
 * ── Option 3: Eigenen Quill-Logger injizieren ────────────────────────────
 * @code
 *   #include <iso8583/ISOLog.hh>
 *   #include <quill/Frontend.h>
 *   #include <quill/sinks/ConsoleSink.h>
 *
 *   quill::Backend::start();
 *   auto sink   = quill::Frontend::create_or_get_sink<quill::ConsoleSink>();
 *   auto* myLog = quill::Frontend::create_or_get_logger("myapp", std::move(sink));
 *   tng::log::setQuillLogger(static_cast<void*>(myLog));
 * @endcode
 *
 * ── Option 4: Logging deaktivieren ────────────────────────────────────────
 * @code
 *   tng::log::setLevel(tng::log::Level::OFF);
 * @endcode
 *
 * @note Der Anwender ist für die Lebensdauer des übergebenen Loggers
 *       verantwortlich. Der Logger muss gültig bleiben solange die
 *       Bibliothek verwendet wird.
 */

#include "config.h"
#include <string_view>

namespace TNG_NAMESPACE::log {

    // ── Log-Level ─────────────────────────────────────────────────────────────
    enum class TNG_EXPORT Level {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERR = 4,
        CRITICAL = 5,
        OFF = 6,
    };

    // ── Logger-Interface (ohne externe Abhängigkeiten) ────────────────────────
    /**
     * @brief Abstrakte Basisklasse für eigene Logger-Implementierungen.
     *
     * Implementiere diese Klasse und übergib eine Instanz an `setLogger()`
     * um das Logging der Bibliothek in die eigene Log-Infrastruktur zu leiten.
     */
    class TNG_EXPORT ISOLogger {
    public:
        virtual ~ISOLogger() = default;

        /**
         * @brief Wird für jede Log-Meldung aufgerufen.
         *
         * @param level   Log-Level der Meldung
         * @param file    Quelldatei
         * @param line    Zeilennummer
         * @param message Formatierte Log-Meldung
         */
        virtual void log(Level level,
            std::string_view file,
            int line,
            std::string_view message) = 0;
    };

    // ── Konfiguration ─────────────────────────────────────────────────────────

    /** Setzt den minimalen Log-Level. Standard: Level::WARN */
    TNG_EXPORT void setLevel(Level lvl);

    /** Gibt den aktuellen Log-Level zurück. */
    TNG_EXPORT Level getLevel();

    /**
     * @brief Injiziert einen eigenen Logger (ohne Quill-Abhängigkeit).
     *
     * Mit `nullptr` wird auf den internen Quill-Logger zurückgeschaltet.
     * Der Anwender ist für die Lebensdauer des Loggers verantwortlich.
     */
    TNG_EXPORT void setLogger(ISOLogger* logger);

    /**
     * @brief Injiziert einen eigenen Quill-Logger direkt.
     *
     * Nimmt einen `void*` entgegen um die Quill-Abhängigkeit aus dem
     * öffentlichen Header zu entfernen. Der Aufrufer übergibt seinen
     * `quill::Logger*` als `void*` – die Bibliothek castet intern zurück.
     *
     * Nutzer die kein Quill verwenden müssen diesen Header nicht einbinden
     * und erhalten keine transitive Quill-Abhängigkeit.
     *
     * Voraussetzung: Quill-Backend muss bereits gestartet sein.
     * Mit nullptr wird auf den internen Quill-Logger zurückgeschaltet.
     *
     * Beispiel:
     * @code
     *   #include <iso8583/ISOLog.hh>
     *   #include <quill/Frontend.h>
     *   #include <quill/sinks/ConsoleSink.h>
     *
     *   quill::Backend::start();
     *   auto sink   = quill::Frontend::create_or_get_sink<quill::ConsoleSink>();
     *   quill::Logger* myLog = quill::Frontend::create_or_get_logger("myapp", std::move(sink));
     *
     *   tng::log::setLevel(tng::log::Level::DEBUG);
     *   tng::log::setQuillLogger(static_cast<void*>(myLog));
     * @endcode
     */
    TNG_EXPORT void setQuillLogger(void* quillLoggerPtr);

} // namespace TNG_NAMESPACE::log


// ── QuillBridge ───────────────────────────────────────────────────────────────
// Header-only ISOLogger-Implementierung die an einen quill::Logger weiterleitet.
//
// WARUM QuillBridge statt setQuillLogger():
//   iso8583 ist eine DLL. Quill verwendet pro Prozess einen Singleton für die
//   Thread-Queue-Verwaltung. Wenn iso8583.dll Quill-Makros direkt aufruft,
//   landen sie im DLL-eigenen Singleton – nicht im Singleton von tng.exe.
//   Das Backend liest nur den tng.exe-Singleton → DLL-Nachrichten verschwinden.
//
//   QuillBridge wird im Kontext von tng.exe kompiliert (Header-only) und
//   ruft Quill-Makros dort auf → korrekter Singleton → Nachrichten erscheinen.
//
// Verwendung:
//   #include <iso8583/ISOLog.hh>
//   #include <quill/LogMacros.h>
//
//   static tng::log::QuillBridge bridge(myQuillLogger);
//   tng::log::setLogger(&bridge);
//   tng::log::setLevel(tng::log::Level::DEBUG);

#if defined(QUILL_VERSION)
#include <quill/LogMacros.h>

namespace TNG_NAMESPACE::log {

    class QuillBridge final : public ISOLogger {
        quill::Logger* logger_;

    public:
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
