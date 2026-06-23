#pragma once

/**
 * @file ISOLog.hh
 * @brief Öffentliche Logging-API für libiso8583.
 *
 * Ermöglicht dem Anwender das Logging der Bibliothek zu kontrollieren:
 *
 *  1. Log-Level setzen:
 *     @code
 *     tng::log::setLevel(tng::log::Level::DEBUG);
 *     @endcode
 *
 *  2. Eigenen Logger injizieren (ohne Quill-Abhängigkeit):
 *     @code
 *     class MyLogger : public tng::log::ISOLogger {
 *     public:
 *         void log(tng::log::Level level,
 *                  std::string_view file, int line,
 *                  std::string_view message) override
 *         {
 *             std::cout << "[iso8583] " << message << "\n";
 *         }
 *     };
 *
 *     MyLogger logger;
 *     tng::log::setLogger(&logger);
 *     @endcode
 *
 *  3. Logging vollständig deaktivieren:
 *     @code
 *     tng::log::setLevel(tng::log::Level::OFF);
 *     @endcode
 *
 * @note Der Anwender ist für die Lebensdauer des übergebenen Loggers
 *       verantwortlich. Der Logger muss gültig bleiben solange die
 *       Bibliothek verwendet wird.
 */

// [tng/config]
#include "config.h"
// [stdc++]
#include <string_view>

namespace TNG_NAMESPACE::log {

    // ── Log-Level ─────────────────────────────────────────────────────────────
    enum class TNG_EXPORT Level {
        TRACE    = 0,
        DEBUG    = 1,
        INFO     = 2,
        WARN     = 3,
        ERR      = 4,
        CRITICAL = 5,
        OFF      = 6,
    };

    // ── Logger-Interface ──────────────────────────────────────────────────────
    /**
     * @brief Abstrakte Basisklasse für eigene Logger-Implementierungen.
     *
     * Implementiere diese Klasse und übergib eine Instanz an `setLogger()`
     * um das Logging der Bibliothek in die eigene Log-Infrastruktur zu leiten.
     * Die Bibliothek hat keine Quill-Abhängigkeit in ihrer öffentlichen API.
     */
    class TNG_EXPORT ISOLogger {
    public:
        virtual ~ISOLogger() = default;

        /**
         * @brief Wird für jede Log-Meldung aufgerufen.
         *
         * @param level   Log-Level der Meldung
         * @param file    Quelldatei (kann leer sein)
         * @param line    Zeilennummer (kann 0 sein)
         * @param message Formatierte Log-Meldung
         */
        virtual void log(Level level,
                         std::string_view file,
                         int line,
                         std::string_view message) = 0;
    };

    // ── Konfiguration ─────────────────────────────────────────────────────────

    /**
     * @brief Setzt den minimalen Log-Level.
     *
     * Meldungen unterhalb dieses Levels werden nicht weitergegeben.
     * Standard: Level::WARN
     */
    TNG_EXPORT void setLevel(Level lvl);

    /**
     * @brief Gibt den aktuellen Log-Level zurück.
     */
    TNG_EXPORT Level getLevel();

    /**
     * @brief Injiziert einen eigenen Logger.
     *
     * Ersetzt den internen Quill-Logger. Der Anwender ist für die
     * Lebensdauer des Loggers verantwortlich.
     * Mit `nullptr` wird auf den internen Quill-Logger zurückgeschaltet.
     *
     * @param logger Zeiger auf die Logger-Implementierung (darf nullptr sein)
     */
    TNG_EXPORT void setLogger(ISOLogger* logger);

} // namespace TNG_NAMESPACE::log
