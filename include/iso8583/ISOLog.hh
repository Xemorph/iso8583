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
 * ── Option 3: Eigenen Quill-Logger injizieren (wenn Quill verfügbar) ──────
 * @code
 *   #include <iso8583/ISOLog.hh>    // vor dem Quill-Include
 *   #define ISO8583_ENABLE_QUILL_INJECTION
 *   #include <quill/Logger.h>
 *
 *   quill::Logger* myLogger = quill::Frontend::create_or_get_logger(...);
 *   tng::log::setQuillLogger(myLogger);
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

} // namespace TNG_NAMESPACE::log

// ── Optionale Quill-Direktinjektion ───────────────────────────────────────────
// Nur verfügbar wenn der Anwender Quill bereits eingebunden hat.
// Muss VOR diesem Header definiert werden:
//   #define ISO8583_ENABLE_QUILL_INJECTION
//   #include <quill/Logger.h>
//   #include <iso8583/ISOLog.hh>
#if defined(ISO8583_ENABLE_QUILL_INJECTION) && defined(QUILL_VERSION)
namespace TNG_NAMESPACE::log {
    /**
     * @brief Injiziert einen eigenen Quill-Logger direkt.
     *
     * Effizienter als ISOLogger-Bridge da keine fmt::format()-Konvertierung
     * nötig ist – Quill formatiert lazy im Backend-Thread.
     *
     * Beispiel:
     * @code
     *   #define ISO8583_ENABLE_QUILL_INJECTION
     *   #include <quill/Logger.h>
     *   #include <iso8583/ISOLog.hh>
     *
     *   // Eigenen Logger mit File + Console Sink:
     *   auto file_sink    = quill::Frontend::create_or_get_sink<quill::FileSink>("app.log");
     *   auto stdout_sink  = quill::Frontend::create_or_get_sink<quill::ConsoleSink>();
     *   quill::Logger* my = quill::Frontend::create_or_get_logger(
     *       "myapp", { file_sink, stdout_sink });
     *
     *   tng::log::setQuillLogger(my);
     * @endcode
     *
     * @param logger Quill-Logger (nullptr = zurück zum internen Logger)
     */
    TNG_EXPORT void setQuillLogger(quill::Logger* logger);
}
#endif