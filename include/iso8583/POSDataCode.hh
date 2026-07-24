#pragma once

// =============================================================================
// POSDataCode.hh - lesbare POS-Fähigkeiten statt roher Bytes/Zahlen
// =============================================================================
//
// Viele Kartennetzwerke (z.B. Mastercards "POS Data Code") packen mehrere
// unabhängige Eigenschaften eines Kartenterminals - wie die Karte gelesen
// wurde, wie der Karteninhaber verifiziert wurde, in welcher Umgebung die
// Transaktion stattfand, welche Sicherheitsmerkmale gelten - in ein
// kompaktes Byte-Feld. POSDataCode dekodiert das in benannte, typsichere
// Flags, statt dass Aufrufer einzelne Bits/Zahlen selbst nachschlagen und
// maskieren müssen.
//
// -----------------------------------------------------------------------------
// Verwendung
// -----------------------------------------------------------------------------
//
//   // Aus einem normalen Feld dekodieren (z.B. ein Binärfeld DE61):
//   auto se = msg->get<BinaryField>(61);
//   pos::POSDataCode pdc(se->value());
//
//   if (pdc.isEMV())            { ... }
//   if (pdc.isCardNotPresent()) { ... }
//   if (pdc.hasVerificationMethod(pos::VerificationMethod::ONLINE_PIN)) { ... }
//
//   // Neu aufbauen und wieder in ein Feld zurückschreiben - Flags werden
//   // mit '|' kombiniert, ganz normale, typsichere Enum-Werte:
//   pos::POSDataCode built(
//       pos::ReadingMethod::ICC | pos::ReadingMethod::TRACK2_PRESENT,
//       pos::VerificationMethod::ONLINE_PIN,
//       pos::POSEnvironment::ATTENDED,
//       pos::SecurityCharacteristic::END_TO_END_ENCRYPTION);
//   msg->set(std::make_shared<BinaryField>(61, built.pack()));
//
// POSDataCode ist bewusst KEIN eigener ISOComponent-/Feldtyp (das würde
// Änderungen am generischen ISOComponent<>-Kern sowie am YAML-Spec-Loader
// erfordern) - sondern ein eigenständiger Interpreter für Rohbytes, die ganz
// normal über die bestehende Feld-API gelesen/geschrieben werden. Das deckt
// das eigentliche Ziel ("User kennen nur die Funktion, nicht die Werte")
// bereits vollständig ab, ohne den Kern der Bibliothek anzufassen.
//
// -----------------------------------------------------------------------------
// Warum die Vorgängerversion (_pos.bak/_pos.h.bak) nie nutzbar war
// -----------------------------------------------------------------------------
//   1. `enum class` unterstützt kein `|` ohne überladene Operatoren - jede
//      Flag-Kombination hätte manuelle `static_cast<unsigned int>(...)` an
//      jeder Aufrufstelle gebraucht (siehe operator| unten - jetzt ergänzt).
//   2. Datei war nie in CMakeLists.txt eingebunden UND die .cc-Datei
//      includete eine andere Datei ("_pos.hh") als tatsächlich existierte
//      ("_pos.h.bak").
//   3. Zwei der vier Lookup-Tabellen (VerificationMethod, POSEnvironment)
//      waren leer.
// =============================================================================

// [stdc++]
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
// [iso8583]
#include <iso8583/config.h>

namespace TNG_NAMESPACE {
    namespace pos {

        /// @brief Wie die Karteninformationen gelesen wurden.
        enum class ReadingMethod : unsigned int {
            UNKNOWN = 1,
            CONTACTLESS = 1 << 1,
            PHYSICAL = 1 << 2,
            BARCODE = 1 << 3,
            MAGNETIC_STRIPE = 1 << 4,
            ICC = 1 << 5,
            DATA_ON_FILE = 1 << 6,
            ICC_FAILED = 1 << 11,
            MAGNETIC_STRIPE_FAILED = 1 << 12,
            FALLBACK = 1 << 13,
            TRACK1_PRESENT = 1 << 27,
            TRACK2_PRESENT = 1 << 28,
            /// Byte-Offset dieser Kategorie innerhalb von POSDataCode::pack() -
            /// keine echte Flag, siehe POSDataCode::LENGTH/pack()/unpack().
            OFFSET = 0
        };

        /// @brief Wie der Karteninhaber verifiziert wurde.
        enum class VerificationMethod : unsigned int {
            UNKNOWN = 1,
            NONE = 1 << 1,
            MANUAL_SIGNATURE = 1 << 2,
            ONLINE_PIN = 1 << 3,
            OFFLINE_PIN_IN_CLEAR = 1 << 4,
            OFFLINE_PIN_ENCRYPTED = 1 << 5,
            OFFLINE_DIGITIZED_SIGNATURE_ANALYSIS = 1 << 6,
            OFFLINE_BIOMETRICS = 1 << 7,
            OFFLINE_MANUAL_VERIFICATION = 1 << 8,
            OFFLINE_BIOGRAPHICS = 1 << 9,
            ACCOUNT_BASED_DIGITAL_SIGNATURE = 1 << 10,
            PUBLIC_KEY_BASED_DIGITAL_SIGNATURE = 1 << 11,
            OFFSET = 4
        };

        /// @brief In welcher Umgebung die Transaktion stattfand.
        enum class POSEnvironment : unsigned int {
            UNKNOWN = 1,
            ATTENDED = 1 << 1,
            UNATTENDED = 1 << 2,
            MOTO = 1 << 3,
            E_COMMERCE = 1 << 4,
            M_COMMERCE = 1 << 5,
            RECURRING = 1 << 6,
            STORED_DETAILS = 1 << 7,
            CAT = 1 << 8,
            ATM_ON_BANK = 1 << 9,
            ATM_OFF_BANK = 1 << 10,
            DEFERRED_TRANSACTION = 1 << 11,
            INSTALLMENT_TRANSACTION = 1 << 12,
            OFFSET = 8
        };

        /// @brief Welche Sicherheitsmerkmale für die Übertragung gelten.
        enum class SecurityCharacteristic : unsigned int {
            UNKNOWN = 1,
            PRIVATE_NETWORK = 1 << 1,
            OPEN_NETWORK = 1 << 2,
            CHANNEL_MACING = 1 << 3,
            PASS_THROUGH_MACING = 1 << 4,
            CHANNEL_ENCRYPTION = 1 << 5,
            END_TO_END_ENCRYPTION = 1 << 6,
            PRIVAT_ALG_ENCRYPTION = 1 << 7,
            PKI_ENCRYPTION = 1 << 8,
            PRIVATE_ALG_MACING = 1 << 9,
            STD_ALG_MACING = 1 << 10,
            CARDHOLDER_MANAGED_END_TO_END_ENCRYPTION = 1 << 11,
            CARDHOLDER_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 12,
            MERCHANT_MANAGED_END_TO_END_ENCRYPTION = 1 << 13,
            MERCHANT_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 14,
            ACQUIRER_MANAGED_END_TO_END_ENCRYPTION = 1 << 15,
            ACQUIRER_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 16,
            OFFSET = 12
        };

        // ── Bitweise Operatoren ──────────────────────────────────────────────
        // Das eigentliche Werkzeug, das in der Vorgängerversion fehlte: ohne
        // diese Überladungen kompiliert `ReadingMethod::ICC | ReadingMethod::
        // CONTACTLESS` bei einem `enum class` nicht. Mit ihnen funktioniert
        // das Kombinieren mehrerer Flags genauso natürlich wie bei einem
        // klassischen (unscoped) Flags-Enum, aber weiterhin typsicher -
        // ReadingMethod und VerificationMethod lassen sich z.B. nicht
        // versehentlich vermischen.
#define TNG_POS_DEFINE_FLAG_OPS(EnumType) \
        constexpr EnumType operator|(EnumType a, EnumType b) noexcept { \
            return static_cast<EnumType>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b)); \
        } \
        constexpr EnumType operator&(EnumType a, EnumType b) noexcept { \
            return static_cast<EnumType>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b)); \
        } \
        constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept { \
            a = a | b; \
            return a; \
        }

        TNG_POS_DEFINE_FLAG_OPS(ReadingMethod)
        TNG_POS_DEFINE_FLAG_OPS(VerificationMethod)
        TNG_POS_DEFINE_FLAG_OPS(POSEnvironment)
        TNG_POS_DEFINE_FLAG_OPS(SecurityCharacteristic)
#undef TNG_POS_DEFINE_FLAG_OPS

        // ── Lesbare Bezeichnungen ────────────────────────────────────────────
        // Für describe()/operator<< unten sowie für eigene Diagnose-/Log-
        // Ausgaben der Aufrufer. Hinweis: Die Beschreibungstexte für
        // VerificationMethod/POSEnvironment sind aus den Bezeichnernamen
        // abgeleitet (die Vorgänger-Tabellen waren leer) - bei Bedarf an die
        // exakte Terminologie eures Kartennetzwerks anpassen.
        inline const std::unordered_map<ReadingMethod, const char*> LookupReadingMethods = {
            { ReadingMethod::UNKNOWN, "Unknown" },
            { ReadingMethod::CONTACTLESS, "Info not taken from card" },
            { ReadingMethod::PHYSICAL, "Physical entry" },
            { ReadingMethod::BARCODE, "Bar code" },
            { ReadingMethod::MAGNETIC_STRIPE, "Magnetic Stripe" },
            { ReadingMethod::ICC, "ICC" },
            { ReadingMethod::DATA_ON_FILE, "Data on file" },
            { ReadingMethod::ICC_FAILED, "ICC read but failed" },
            { ReadingMethod::MAGNETIC_STRIPE_FAILED, "Magnetic Stripe read but failed" },
            { ReadingMethod::FALLBACK, "Fallback" },
            { ReadingMethod::TRACK1_PRESENT, "Track1 data present" },
            { ReadingMethod::TRACK2_PRESENT, "Track2 data present" },
        };

        inline const std::unordered_map<VerificationMethod, const char*> LookupVerificationMethods = {
            { VerificationMethod::UNKNOWN, "Unknown" },
            { VerificationMethod::NONE, "No verification performed" },
            { VerificationMethod::MANUAL_SIGNATURE, "Manual signature" },
            { VerificationMethod::ONLINE_PIN, "Online PIN" },
            { VerificationMethod::OFFLINE_PIN_IN_CLEAR, "Offline PIN (cleartext)" },
            { VerificationMethod::OFFLINE_PIN_ENCRYPTED, "Offline PIN (encrypted)" },
            { VerificationMethod::OFFLINE_DIGITIZED_SIGNATURE_ANALYSIS, "Offline digitized signature analysis" },
            { VerificationMethod::OFFLINE_BIOMETRICS, "Offline biometrics" },
            { VerificationMethod::OFFLINE_MANUAL_VERIFICATION, "Offline manual verification" },
            { VerificationMethod::OFFLINE_BIOGRAPHICS, "Offline biographics" },
            { VerificationMethod::ACCOUNT_BASED_DIGITAL_SIGNATURE, "Account-based digital signature" },
            { VerificationMethod::PUBLIC_KEY_BASED_DIGITAL_SIGNATURE, "Public-key-based digital signature" },
        };

        inline const std::unordered_map<POSEnvironment, const char*> LookupPOSEnvironments = {
            { POSEnvironment::UNKNOWN, "Unknown" },
            { POSEnvironment::ATTENDED, "Attended terminal" },
            { POSEnvironment::UNATTENDED, "Unattended terminal" },
            { POSEnvironment::MOTO, "Mail order / telephone order" },
            { POSEnvironment::E_COMMERCE, "E-Commerce" },
            { POSEnvironment::M_COMMERCE, "M-Commerce" },
            { POSEnvironment::RECURRING, "Recurring transaction" },
            { POSEnvironment::STORED_DETAILS, "Stored card details used" },
            { POSEnvironment::CAT, "Cardholder-activated terminal" },
            { POSEnvironment::ATM_ON_BANK, "ATM, on card issuer's network" },
            { POSEnvironment::ATM_OFF_BANK, "ATM, off card issuer's network" },
            { POSEnvironment::DEFERRED_TRANSACTION, "Deferred transaction" },
            { POSEnvironment::INSTALLMENT_TRANSACTION, "Installment transaction" },
        };

        inline const std::unordered_map<SecurityCharacteristic, const char*> LookupSecurityCharacteristics = {
            { SecurityCharacteristic::UNKNOWN, "Unknown" },
            { SecurityCharacteristic::PRIVATE_NETWORK, "Private network" },
            { SecurityCharacteristic::OPEN_NETWORK, "Open network (Internet)" },
            { SecurityCharacteristic::CHANNEL_MACING, "Channel MACing" },
            { SecurityCharacteristic::PASS_THROUGH_MACING, "Pass through MACing" },
            { SecurityCharacteristic::CHANNEL_ENCRYPTION, "Channel encryption" },
            { SecurityCharacteristic::END_TO_END_ENCRYPTION, "End-to-end encryption" },
            { SecurityCharacteristic::PRIVAT_ALG_ENCRYPTION, "Private algorithm encryption" },
            { SecurityCharacteristic::PKI_ENCRYPTION, "PKI encryption" },
            { SecurityCharacteristic::PRIVATE_ALG_MACING, "Private algorithm MACing" },
            { SecurityCharacteristic::STD_ALG_MACING, "Standard algorithm MACing" },
            { SecurityCharacteristic::CARDHOLDER_MANAGED_END_TO_END_ENCRYPTION, "Cardholder managed end-to-end encryption" },
            { SecurityCharacteristic::CARDHOLDER_MANAGED_POINT_TO_POINT_ENCRYPTION, "Cardholder managed point-to-point encryption" },
            { SecurityCharacteristic::MERCHANT_MANAGED_END_TO_END_ENCRYPTION, "Merchant managed end-to-end encryption" },
            { SecurityCharacteristic::MERCHANT_MANAGED_POINT_TO_POINT_ENCRYPTION, "Merchant managed point-to-point encryption" },
            { SecurityCharacteristic::ACQUIRER_MANAGED_END_TO_END_ENCRYPTION, "Acquirer managed end-to-end encryption" },
            { SecurityCharacteristic::ACQUIRER_MANAGED_POINT_TO_POINT_ENCRYPTION, "Acquirer managed point-to-point encryption" },
        };

        /// @brief Dekodiert/baut ein kompaktes POS-Fähigkeiten-Byte-Feld
        /// (4 Kategorien × 4 Bytes, little-endian = 16 Bytes gesamt).
        class POSDataCode {
        public:
            /// Größe des Byte-Feldes (4 Kategorien × 4 Bytes).
            static constexpr std::size_t LENGTH = 16;

            /// @brief Baut einen POSDataCode aus benannten Flags auf.
            /// Mehrere Flags derselben Kategorie werden mit `|` kombiniert,
            /// z.B. `ReadingMethod::ICC | ReadingMethod::TRACK2_PRESENT`.
            POSDataCode(
                ReadingMethod read,
                VerificationMethod verify,
                POSEnvironment env,
                SecurityCharacteristic sec) noexcept
            {
                b_.assign(LENGTH, 0x00);
                packU32LE(offsetOf(ReadingMethod::OFFSET), static_cast<unsigned int>(read));
                packU32LE(offsetOf(VerificationMethod::OFFSET), static_cast<unsigned int>(verify));
                packU32LE(offsetOf(POSEnvironment::OFFSET), static_cast<unsigned int>(env));
                packU32LE(offsetOf(SecurityCharacteristic::OFFSET), static_cast<unsigned int>(sec));
            }

            /// @brief Dekodiert einen POSDataCode aus Rohbytes, z.B.
            /// `msg->get<BinaryField>(61)->value()`.
            /// @throws std::invalid_argument wenn `raw` nicht genau `LENGTH`
            ///         Bytes hat (kein stillschweigendes Auffüllen/Kürzen -
            ///         ein zu kurzer/langer Puffer deutet meist auf ein
            ///         falsches Feld oder einen Framing-Fehler hin).
            explicit POSDataCode(const std::vector<uint8_t>& raw) {
                if (raw.size() != LENGTH)
                    throw std::invalid_argument(
                        "POSDataCode: erwarte genau " + std::to_string(LENGTH) +
                        " Bytes, erhalten " + std::to_string(raw.size()));
                b_ = raw;
            }

            /// @brief Rohbytes für's Zurückschreiben in ein Feld, z.B.
            /// `msg->set(std::make_shared<BinaryField>(61, pdc.pack()));`.
            const std::vector<uint8_t>& pack() const noexcept { return b_; }

            // ── Kategorie-Abfrage (einzeln oder kombiniert via `|`) ─────────────

            ReadingMethod readingMethod() const noexcept {
                return static_cast<ReadingMethod>(unpackU32LE(offsetOf(ReadingMethod::OFFSET)));
            }
            VerificationMethod verificationMethod() const noexcept {
                return static_cast<VerificationMethod>(unpackU32LE(offsetOf(VerificationMethod::OFFSET)));
            }
            POSEnvironment posEnvironment() const noexcept {
                return static_cast<POSEnvironment>(unpackU32LE(offsetOf(POSEnvironment::OFFSET)));
            }
            SecurityCharacteristic securityCharacteristic() const noexcept {
                return static_cast<SecurityCharacteristic>(unpackU32LE(offsetOf(SecurityCharacteristic::OFFSET)));
            }

            /// @brief `true` wenn ALLE angefragten Reading-Method-Flags gesetzt sind.
            bool hasReadingMethod(ReadingMethod read) const noexcept {
                return (readingMethod() & read) == read;
            }
            /// @brief `true` wenn ALLE angefragten Verification-Method-Flags gesetzt sind.
            bool hasVerificationMethod(VerificationMethod verify) const noexcept {
                return (verificationMethod() & verify) == verify;
            }
            /// @brief `true` wenn ALLE angefragten POS-Environment-Flags gesetzt sind.
            bool hasPOSEnvironment(POSEnvironment env) const noexcept {
                return (posEnvironment() & env) == env;
            }
            /// @brief `true` wenn ALLE angefragten Security-Characteristic-Flags gesetzt sind.
            bool hasSecurityCharacteristic(SecurityCharacteristic sec) const noexcept {
                return (securityCharacteristic() & sec) == sec;
            }

            // ── Bequeme, häufig gebrauchte Zusammenfassungen ────────────────────

            bool isEMV() const noexcept {
                return hasReadingMethod(ReadingMethod::ICC) || hasReadingMethod(ReadingMethod::CONTACTLESS);
            }
            bool isManualEntry() const noexcept {
                return hasReadingMethod(ReadingMethod::PHYSICAL);
            }
            bool isSwiped() const noexcept {
                return hasReadingMethod(ReadingMethod::MAGNETIC_STRIPE);
            }
            bool isRecurring() const noexcept {
                return hasPOSEnvironment(POSEnvironment::RECURRING);
            }
            bool isECommerce() const noexcept {
                return hasPOSEnvironment(POSEnvironment::E_COMMERCE);
            }
            bool isCardNotPresent() const noexcept {
                return isECommerce() || hasPOSEnvironment(POSEnvironment::MOTO) || isRecurring();
            }

            /// @brief Lesbare Zusammenfassung aller gesetzten Flags (über die
            /// Lookup-Tabellen oben) - z.B. für Logging/Debug-Ausgaben.
            std::string describe() const {
                std::string out;
                appendMatches(out, "Reading: ", readingMethod(), LookupReadingMethods);
                appendMatches(out, "Verification: ", verificationMethod(), LookupVerificationMethods);
                appendMatches(out, "Environment: ", posEnvironment(), LookupPOSEnvironments);
                appendMatches(out, "Security: ", securityCharacteristic(), LookupSecurityCharacteristics);
                return out;
            }

        private:
            std::vector<uint8_t> b_;

            void packU32LE(std::size_t offset, unsigned int v) noexcept {
                b_[offset + 0] = static_cast<uint8_t>(v);
                b_[offset + 1] = static_cast<uint8_t>(v >> 8);
                b_[offset + 2] = static_cast<uint8_t>(v >> 16);
                b_[offset + 3] = static_cast<uint8_t>(v >> 24);
            }
            unsigned int unpackU32LE(std::size_t offset) const noexcept {
                return static_cast<unsigned int>(b_[offset + 0]) |
                    (static_cast<unsigned int>(b_[offset + 1]) << 8) |
                    (static_cast<unsigned int>(b_[offset + 2]) << 16) |
                    (static_cast<unsigned int>(b_[offset + 3]) << 24);
            }

            template <typename EnumType>
            static std::size_t offsetOf(EnumType offsetMarker) noexcept {
                return static_cast<std::size_t>(offsetMarker);
            }

            template <typename EnumType>
            static void appendMatches(
                std::string& out, const char* label, EnumType value,
                const std::unordered_map<EnumType, const char*>& lookup)
            {
                bool first = true;
                for (const auto& [flag, text] : lookup) {
                    if ((value & flag) != flag)
                        continue;
                    if (first) { out += label; first = false; }
                    else out += ", ";
                    out += text;
                }
                if (!first) out += "; ";
            }
        };

        /// @brief Schreibt POSDataCode::describe() in `os`.
        inline std::ostream& operator<<(std::ostream& os, const POSDataCode& pdc) {
            return os << pdc.describe();
        }

    } // namespace pos
} // namespace TNG_NAMESPACE
