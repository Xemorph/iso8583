#pragma once

// =============================================================================
// ISOUtils.hh - kleine, header-only Hilfsfunktionen rund um ISO-8583
// =============================================================================
//
// makeBitmap() baut die rohen Wire-Bytes einer primären/sekundären/tertiären
// Bitmap (8/16/24 Bytes) aus einer Menge von DE-Nummern - z.B. um in Tests
// oder Tools synthetische Nachrichten Byte für Byte zusammenzusetzen, ohne
// die interne dynamic_bitset<>-Logik der Bibliothek (siehe
// ISOMessage::recalcBitmap() / ISOBitmapFieldParser in src/_parser.hh) selbst
// nachbauen zu müssen. Die Bit-Platzierung ist exakt deckungsgleich mit dem,
// was die Bibliothek selbst für eine äquivalente Menge gesetzter Felder
// erzeugen würde (empirisch über die Testsuite abgesichert, siehe
// tests/test_e2e_full_message.cc).
//
// [stdc++]
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>
// [iso8583]
#include <iso8583/config.h>
#include <iso8583/ISOMessage.hh>

namespace TNG_NAMESPACE {

    /// @brief Utility functions for working with `Message` objects.
	namespace utils {

        /// @brief Flattens all leaf fields of a message into a `string → string` map.
        ///
        /// Keys use dot-notation (`"2"`, `"48.72.1"`).  Values are
        /// `readable_value()` strings.  Composite fields (nested messages) are
        /// not included; only their leaf children appear.
        ///
        /// @param msg  The message to flatten.
        /// @return     Map from dot-notation DE key to string value.
        TNG_EXPORT std::map<std::string, std::string> flatten(const Message& msg);

        /// @brief Returns the field value or throws if the field is absent or wrong type.
        ///
        /// Use when the field **must** be present according to the spec.
        ///
        /// @code
        ///   std::string pan = iso8583::utils::getOrThrow<OpaqueField>(msg, 2);
        /// @endcode
        ///
        /// @tparam T   Concrete field type.
        /// @param  msg Reference to the message.
        /// @param  key DE number.
        /// @return     The field's value (by copy).
        /// @throws std::out_of_range if the field is missing or has the wrong type.
        template <typename T>
        auto getOrThrow(Message& msg, TNG_KEY_TYPE key)
            -> decltype(std::declval<T>().value())
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt)
                throw std::out_of_range(
                    "DE" + std::to_string(key) + " missing or wrong type");
            return (*opt)->value();
        }

        /// @brief Returns the field value or a caller-supplied default.
        ///
        /// Use for optional fields where a sensible default exists.
        ///
        /// @code
        ///   auto currency = iso8583::utils::getOrDefault<OpaqueField>(msg, 49, "978");
        /// @endcode
        ///
        /// @tparam T            Concrete field type.
        /// @param  msg          Reference to the message.
        /// @param  key          DE number.
        /// @param  defaultValue Value to return when the field is absent.
        /// @return              The field's value, or `defaultValue`.
        template <typename T, typename Default>
        auto getOrDefault(Message& msg, TNG_KEY_TYPE key, Default&& defaultValue)
            -> std::decay_t<decltype(std::declval<T>().value())>
        {
            using ValueType = std::decay_t<decltype(std::declval<T>().value())>;
            auto opt = msg.tryGet<T>(key);
            if (!opt) return ValueType(std::forward<Default>(defaultValue));
            return (*opt)->value();
        }


        /// @brief Calls `fn` with the field value if the field is present.
        ///
        /// Returns `true` when the field was found and `fn` was called.
        /// Avoids the nested `if (auto opt = ...) { ... }` pattern.
        ///
        /// @code
        ///   iso8583::utils::ifPresent<OpaqueField>(msg, 11, [](const std::string& stan) {
        ///       log("STAN: {}", stan);
        ///   });
        /// @endcode
        ///
        /// @tparam T   Concrete field type.
        /// @tparam Fn  Callable with signature `void(const ValueType&)`.
        /// @param  msg Reference to the message.
        /// @param  key DE number.
        /// @param  fn  Callback invoked with the field value.
        /// @return     `true` if the field was found, `false` otherwise.
        template <typename T, typename Fn>
        bool ifPresent(Message& msg, TNG_KEY_TYPE key, Fn&& fn)
        {
            auto opt = msg.tryGet<T>(key);
            if (!opt) return false;
            std::forward<Fn>(fn)((*opt)->value());
            return true;
        }

        /// @brief Baut eine wire-korrekte ISO-8583-Bitmap (8, 16 oder 24 Bytes)
        /// für die angegebenen Data-Element-Nummern.
        ///
        /// Unterstützt DE 2..192. Bit `N` (1-indiziert) landet an
        /// `byte[(N-1)/8]`, Bit `7-((N-1)%8)` (MSB-first) - identisch zur
        /// Interpretation, die `ISOBaseParser::unparse()` beim Dekodieren
        /// verwendet (siehe `byte2bitset` in `src/_parser.hh`) und die
        /// `iso8583::Message::recalcBitmap()` beim Serialisieren erzeugt.
        ///
        /// Die Extension-Präsenzbits werden automatisch gesetzt, exakt wie
        /// die Bibliothek selbst es tut (`ISOBitmapFieldParser::parse()`,
        /// `if (bits > 64) d[0] |= 0x80; if (bits > 128) d[8] |= 0x80;`):
        ///   - Byte 0, Bit 7 (0x80) = 1, sobald ein DE > 64 angefordert wird
        ///     (→ 16-Byte-Bitmap, "Secondary Bitmap Present").
        ///   - Byte 8, Bit 7 (0x80) = 1, sobald ein DE > 128 angefordert wird
        ///     (→ 24-Byte-Bitmap, "Tertiary Bitmap Present").
        ///
        /// DE1, DE65 und DE129 sind reservierte Indikator-Positionen (siehe
        /// oben) und werden daher explizit abgelehnt, statt sie stillschweigend
        /// als reguläre Felder zu behandeln.
        ///
        /// @param des  DE-Nummern (beliebige Reihenfolge, Duplikate erlaubt).
        /// @return     8, 16 oder 24 rohe Bytes, je nach höchster angeforderter DE.
        /// @throws std::invalid_argument  wenn eine DE außerhalb [2, 192] liegt
        ///         oder eine reservierte Indikator-Position (1, 65, 129) ist.
        inline std::vector<uint8_t> makeBitmap(const std::vector<TNG_KEY_TYPE>& des) {
            TNG_KEY_TYPE max_de = 0;

            for (const auto de : des) {
                if (de == 1 || de == 65 || de == 129)
                    throw std::invalid_argument(
                        "makeBitmap: DE" + std::to_string(de) +
                        " ist eine reservierte Bitmap-Extension-Indikator-Position "
                        "und wird automatisch verwaltet - nicht manuell angeben");
                if (de < 2 || de > 192)
                    throw std::invalid_argument(
                        "makeBitmap: DE" + std::to_string(de) +
                        " liegt außerhalb des gültigen Bereichs [2, 192]");
                max_de = std::max(max_de, de);
            }

            const std::size_t bytes = (max_de > 128) ? 24 : (max_de > 64) ? 16 : 8;
            std::vector<uint8_t> bmp(bytes, 0x00);

            for (const auto de : des) {
                const std::size_t p = static_cast<std::size_t>(de) - 1; // 0-indiziert
                const std::size_t byte_idx = p / 8;
                const std::size_t bit_idx = 7 - (p % 8);
                bmp[byte_idx] |= static_cast<uint8_t>(1u << bit_idx);
            }

            if (bytes >= 16) bmp[0] |= 0x80u; // Secondary Bitmap Present
            if (bytes >= 24) bmp[8] |= 0x80u; // Tertiary Bitmap Present

            return bmp;
        }

        /// @overload
        /// Bequeme Variante für Brace-Init-Listen: `makeBitmap({2, 3, 4, 55})`.
        inline std::vector<uint8_t> makeBitmap(std::initializer_list<TNG_KEY_TYPE> des) {
            return makeBitmap(std::vector<TNG_KEY_TYPE>(des));
        }

        /// @overload
        /// Leitet die DE-Menge direkt aus einer bereits befüllten `ISOMessage`
        /// ab (`ISOMessage::keys()`) - erspart das manuelle Nachführen einer
        /// Liste beim Aufbau von Testnachrichten. Praktisch z.B. um zuerst
        /// eine Nachricht Feld für Feld zu befüllen und daraus direkt die
        /// passende Wire-Bitmap für einen von Hand zusammengesetzten Puffer
        /// abzuleiten, ohne selbst eine dynamic_bitset<> anzufassen.
        ///
        /// @note Nicht `const iso8583::Message&`, da `iso8583::Message::keys()` aus
        ///       demselben Grund nicht `const` ist wie `get()`/`tryGet()`:
        ///       das interne Read-Lock (`d_lock_`) ist nicht `mutable`.
        ///
        /// @note Schlüssel `0` (MTI, nur bei Root-Nachrichten gesetzt) wird
        ///       zusätzlich zu den bereits von `keys()` ausgeschlossenen
        ///       Sentinels `-1`/`-2` herausgefiltert - die MTI ist niemals
        ///       Teil der Bitmap, unabhängig davon, ob `msg` eine Root- oder
        ///       eine (TLV-)Sub-Message ist.
        inline std::vector<uint8_t> makeBitmap(Message& msg) {
            auto ks = msg.keys();
            ks.erase(std::remove(ks.begin(), ks.end(), Message::MTI_KEY), ks.end());
            return makeBitmap(ks);
        }
	}
}

/// @deprecated Verwende iso8583::utils:: statt ISOUtils::
namespace ISOUtils = ::TNG_NAMESPACE::utils;