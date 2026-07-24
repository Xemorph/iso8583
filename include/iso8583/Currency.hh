#pragma once

// =============================================================================
// Currency.hh - ISO-4217-Währungscodes: Nachschlagen, Formatieren, Parsen
// =============================================================================
//
// -----------------------------------------------------------------------------
// Verwendung
// -----------------------------------------------------------------------------
//
//   auto* eur = currency::findByNumeric(978);       // DE49/DE51/DE102-Wert
//   // oder: currency::findByAlpha("EUR");
//   if (eur) {
//       std::string amt = eur->formatAmountForISOMessage(19.99); // "000000001999"
//       double back = eur->parseAmountFromISOMessage(amt);        // 19.99
//   }
//
// =============================================================================

// [stdc++]
#include <cmath>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
// [iso8583]
#include <iso8583/config.h>

namespace TNG_NAMESPACE {
    namespace currency {

        /// @brief Ein ISO-4217-Währungscode mit Alpha-/Numeric-Darstellung
        /// und der Anzahl Nachkommastellen (Minor Unit).
        class Currency {
        public:
            constexpr Currency(const char* alpha, int numeric, int decimals) noexcept
                : alpha_(alpha), numeric_(numeric), decimals_(decimals)
            {}

            /// @brief 3-stelliger Alpha-Code, z.B. "EUR".
            constexpr const char* alphaCode() const noexcept { return alpha_; }
            /// @brief 3-stelliger numerischer Code, z.B. 978 für EUR - das ist
            /// die Darstellung, die in ISO-8583-Nachrichten (DE49/DE51/DE102)
            /// tatsächlich auf der Wire steht.
            constexpr int isoCode() const noexcept { return numeric_; }
            /// @brief Anzahl Nachkommastellen (ISO-4217 "Minor Unit"). 0 für
            /// Edelmetall-/Fonds-/Testcodes, für die ISO 4217 keine
            /// Nachkommastellen definiert (siehe data/iso4217/README.md).
            constexpr int decimals() const noexcept { return decimals_; }

            /// @brief Formatiert einen Betrag (bereits in kleinster Einheit,
            /// z.B. Cent) als nullgepolsterte ISO-8583-Ziffernfolge fester
            /// Breite (Standard: 12 Stellen, wie DE4 "Amount, Transaction").
            /// @throws std::invalid_argument bei negativen Beträgen (ISO 8583
            ///         Betragsfelder haben kein Vorzeichen) oder wenn der Betrag
            ///         nicht in `width` Stellen passt.
            std::string formatAmountForISOMessage(long long minorUnits, std::size_t width = 12) const {
                if (minorUnits < 0)
                    throw std::invalid_argument(
                        "Currency::formatAmountForISOMessage: negative Beträge werden "
                        "nicht unterstützt (ISO-8583-Betragsfelder sind vorzeichenlos)");
                std::string s = std::to_string(minorUnits);
                if (s.size() > width)
                    throw std::invalid_argument(
                        "Currency::formatAmountForISOMessage: Betrag " + s +
                        " passt nicht in " + std::to_string(width) + " Stellen");
                return std::string(width - s.size(), '0') + s;
            }

            /// @brief Komfort-Überladung für `double`-Beträge in Hauptwährungseinheit
            /// (z.B. 19.99 statt 1999 Cent). Nutzt intern die Integer-Überladung
            /// oben - für Finanzcode ohne jede Fließkomma-Unsicherheit empfiehlt
            /// sich aber die Minor-Units-Variante direkt.
            std::string formatAmountForISOMessage(double amount, std::size_t width = 12) const {
                const double scaled = amount * std::pow(10.0, decimals_);
                const long long minorUnits = static_cast<long long>(std::llround(scaled));
                return formatAmountForISOMessage(minorUnits, width);
            }

            /// @brief Kehrt formatAmountForISOMessage(long long) um: liefert den
            /// Betrag in kleinster Einheit (z.B. Cent) als Integer - ohne jede
            /// Fließkomma-Rundung.
            /// @throws std::invalid_argument bei leerem String oder Nicht-Ziffern.
            long long parseAmountMinorUnitsFromISOMessage(std::string_view isoAmount) const {
                if (isoAmount.empty())
                    throw std::invalid_argument("Currency::parseAmountMinorUnitsFromISOMessage: leerer String");
                for (const char c : isoAmount)
                    if (c < '0' || c > '9')
                        throw std::invalid_argument(
                            "Currency::parseAmountMinorUnitsFromISOMessage: ungültige Eingabe (keine Ziffer)");
                return std::stoll(std::string(isoAmount));
            }

            /// @brief Komfort-Überladung: liefert den Betrag als `double` in
            /// Hauptwährungseinheit (z.B. 19.99 statt 1999 Cent).
            double parseAmountFromISOMessage(std::string_view isoAmount) const {
                const long long minorUnits = parseAmountMinorUnitsFromISOMessage(isoAmount);
                return static_cast<double>(minorUnits) / std::pow(10.0, decimals_);
            }

            friend std::ostream& operator<<(std::ostream& os, const Currency& c) {
                return os << c.alpha_ << " (" << c.numeric_ << ", " << c.decimals_ << " decimals)";
            }

        private:
            const char* alpha_;
            int numeric_;
            int decimals_;
        };

    } // namespace currency
} // namespace TNG_NAMESPACE

// Generierte Tabelle (siehe scripts/generate_currency_table.py). Muss NACH
// der Klassendefinition oben eingebunden werden - das Fragment selbst
// inkludiert diese Datei nicht noch einmal (vermeidet einen zirkulären
// Include, siehe Kommentar dort).
#include "detail/_currency_table.hh"

namespace TNG_NAMESPACE {
    namespace currency {

        /// @brief Sucht eine Währung über den 3-stelligen Alpha-Code (z.B. "EUR").
        /// @return Zeiger in die statische Tabelle, oder `nullptr` wenn nicht gefunden.
        inline const Currency* findByAlpha(std::string_view alpha3) noexcept {
            for (const auto& c : detail::kCurrencyTable)
                if (alpha3 == c.alphaCode())
                    return &c;
            return nullptr;
        }

        /// @brief Sucht eine Währung über den numerischen ISO-4217-Code (z.B. 978
        /// für EUR) - das ist die Darstellung, die in ISO-8583-Nachrichten
        /// (DE49/DE51/DE102) tatsächlich auf der Wire steht.
        /// @return Zeiger in die statische Tabelle, oder `nullptr` wenn nicht gefunden.
        inline const Currency* findByNumeric(int isoNumeric) noexcept {
            for (const auto& c : detail::kCurrencyTable)
                if (isoNumeric == c.isoCode())
                    return &c;
            return nullptr;
        }

        /// @brief Alle bekannten Währungen (z.B. zum Aufbau einer Auswahlliste).
        inline const std::array<Currency, detail::kCurrencyTableSize>& allCurrencies() noexcept {
            return detail::kCurrencyTable;
        }

    } // namespace currency
} // namespace TNG_NAMESPACE
