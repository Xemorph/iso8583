#pragma once

// =============================================================================
// POSDataCode.hh - lesbare POS-Fähigkeiten statt roher Bytes/Zahlen
// =============================================================================
//
// pos::POSDataCode gibt einem ISO-8583-Data-Code, der als rohe Bytes
// übertragen wird (z. B. DE 61 / POS-Data-Code), eine lesbare Interpretation —
// ohne ein neues ISOComponent- oder Feldtyp-Subsystem zu sein. Er liest dieselben
// rohen Bytes, die BinaryField & Co. hergeben, interpretiert sie als vier
// 32-Bit-Flag-Wörter und kann das Ergebnis wieder packen.
//
// Verwendung (lesen, z. B. DE 61):
//
//     auto se   = msg->get<BinaryField>(61);
//     pos::POSDataCode pdc(se->value());
//     if (pdc.hasReadingMethod(pos::ReadingMethod::ICC)) { /* EMV */ }
//     log() << pdc;   // "Reading: ICC; Verification: Online PIN; ..."
//
// Verwendung (schreiben):
//
//     pos::POSDataCode built(pos::ReadingMethod::ICC | pos::ReadingMethod::TRACK2_PRESENT,
//                            pos::VerificationMethod::ONLINE_PIN,
//                            pos::POSEnvironment::ATTENDED,
//                            pos::SecurityCharacteristic::END_TO_END_ENCRYPTION);
//     msg->set(std::make_shared<BinaryField>(61, built.pack()));
//
// Draht-Layout (fix, 16 Byte):
//
//     Offset    Größe   Inhalt
//     [0..3]     4B     ReadingMethod          (little-endian u32)
//     [4..7]     4B     VerificationMethod     (little-endian u32)
//     [8..11]    4B     POSEnvironment         (little-endian u32)
//     [12..15]   4B     SecurityCharacteristic (little-endian u32)
//
// Die Byte-Reihenfolge INNERHALB eines 32-Bit-Worts ist LITTLE-ENDIAN. Das ist
// eine lokale Konvention dieses Data-Code-Layouts — ISO-8583-Elemente sind
// üblicherweise BIG-endian; die Konvention darf daher nicht ungeprüft auf
// andere DEs übertragen werden.
//
// Warum diese Datei und nicht der Vorgänger (siehe _pos.bak / _pos.h.bak)?
//   1. Die Flag-Enums des Vorgängers hatten keine operator|, sodass Flags
//      wie ReadingMethod::ICC | ReadingMethod::TRACK2_PRESENT nicht
//      kombinierbar waren. Hier bekommen die Enums den vollen Satz an
//      Flag-Operatoren (siehe TNG_POS_DEFINE_FLAG_OPS unten).
//   2. Der Vorgänger war nie in der CMakeLists.txt und sein .cc
//      #include-ten den falschen Header-Namen. Diese Datei ist
//      header-only umgesetzt — kein .cc, keine CMake-Änderung.
//   3. Zwei der vier Lookup-Tabellen des Vorgängers waren leer, deshalb
//      lieferte describe()/operator<< für die Kategorien gar nichts. Hier
//      sind alle vier Tabellen vollständig (die Texte sind aus den Namen
//      abgeleitet und bei Bedarf den exakten Netzwerk-Termini anzupassen).
// =============================================================================

#include <cstdint>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <iso8583/config.h>

namespace TNG_NAMESPACE
{

namespace pos
{

// ---------------------------------------------------------------------------
// Flag-Enums (je eine pro Kategorie)
//
// Jedes Enum ist eine scoped Bitmaske über einem 32-Bit-Wort. UNKNOWN ist
// kein "echtes" Flag, sondern ein Pseudowert für "nichts Weiteres angegeben".
// Die Bit-Positionen 16..31 sind aktuell nicht vergeben; das lässt
// Erweiterungs-Spielraum, ohne das Layout zu ändern.
// ---------------------------------------------------------------------------

/// Lesen: Wie wurde die Zahlungsquelle eingelesen?
enum class ReadingMethod : unsigned int
{
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
    TRACK2_PRESENT = 1 << 28
};

/// Verifikation: Wie wurde der Karteninhaber verifiziert?
enum class VerificationMethod : unsigned int
{
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
    PUBLIC_KEY_BASED_DIGITAL_SIGNATURE = 1 << 11
};

/// Umgebung: Wo/wie wird die Transaktion ausgeführt?
enum class POSEnvironment : unsigned int
{
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
    INSTALLMENT_TRANSACTION = 1 << 12
};

/// Sicherheit: Wie ist der Kanal geschützt?
enum class SecurityCharacteristic : unsigned int
{
    UNKNOWN = 1,
    PRIVATE_NETWORK = 1 << 1,
    OPEN_NETWORK = 1 << 2,
    CHANNEL_MACING = 1 << 3,
    PASS_THROUGH_MACING = 1 << 4,
    CHANNEL_ENCRYPTION = 1 << 5,
    END_TO_END_ENCRYPTION = 1 << 6,
    PRIVATE_ALG_ENCRYPTION = 1 << 7, // vormals: PRIVAT_ALG_ENCRYPTION (Typo)
    PKI_ENCRYPTION = 1 << 8,
    PRIVATE_ALG_MACING = 1 << 9,
    STD_ALG_MACING = 1 << 10,
    CARDHOLDER_MANAGED_END_TO_END_ENCRYPTION = 1 << 11,
    CARDHOLDER_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 12,
    MERCHANT_MANAGED_END_TO_END_ENCRYPTION = 1 << 13,
    MERCHANT_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 14,
    ACQUIRER_MANAGED_END_TO_END_ENCRYPTION = 1 << 15,
    ACQUIRER_MANAGED_POINT_TO_POINT_ENCRYPTION = 1 << 16
};

// ---------------------------------------------------------------------------
// Flag-Operatoren
//
// Die Enums sind scoped (enum class) und können daher nicht implizit zu int
// gewandelt werden — operator|, &, ^, ~ sowie die Compound-Zuweisungen
// müssen hier für jedes Flag-Enum definiert werden. (Der Vorgänger hatte
// genau das nicht, deshalb war das Kombinieren von Flags unmöglich.)
// ---------------------------------------------------------------------------

#define TNG_POS_DEFINE_FLAG_OPS(EnumType)                                                                              \
    constexpr EnumType operator|(EnumType a, EnumType b) noexcept                                                      \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));                     \
    }                                                                                                                  \
    constexpr EnumType operator&(EnumType a, EnumType b) noexcept                                                      \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b));                     \
    }                                                                                                                  \
    constexpr EnumType operator^(EnumType a, EnumType b) noexcept                                                      \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<unsigned int>(a) ^ static_cast<unsigned int>(b));                     \
    }                                                                                                                  \
    constexpr EnumType operator~(EnumType a) noexcept                                                                  \
    {                                                                                                                  \
        return static_cast<EnumType>(~static_cast<unsigned int>(a));                                                   \
    }                                                                                                                  \
    constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept                                                   \
    {                                                                                                                  \
        a = a | b;                                                                                                     \
        return a;                                                                                                      \
    }                                                                                                                  \
    constexpr EnumType& operator&=(EnumType& a, EnumType b) noexcept                                                   \
    {                                                                                                                  \
        a = a & b;                                                                                                     \
        return a;                                                                                                      \
    }                                                                                                                  \
    constexpr EnumType& operator^=(EnumType& a, EnumType b) noexcept                                                   \
    {                                                                                                                  \
        a = a ^ b;                                                                                                     \
        return a;                                                                                                      \
    }

TNG_POS_DEFINE_FLAG_OPS(ReadingMethod)
TNG_POS_DEFINE_FLAG_OPS(VerificationMethod)
TNG_POS_DEFINE_FLAG_OPS(POSEnvironment)
TNG_POS_DEFINE_FLAG_OPS(SecurityCharacteristic)

#undef TNG_POS_DEFINE_FLAG_OPS

// ---------------------------------------------------------------------------
// Lookup-Tabellen: Enumerator -> lesbarer Text (für describe()/operator<<)
//
// std::map statt std::unordered_map: Die Iterations-Reihenfolge ist dadurch
// deterministisch (steigende Bit-Position), das macht die Ausgabe von
// describe() test- und vergleichbar.
// ---------------------------------------------------------------------------

inline const std::map<ReadingMethod, const char*> LookupReadingMethods = {
    {ReadingMethod::UNKNOWN, "Unknown"},
    {ReadingMethod::CONTACTLESS, "Contactless"},
    {ReadingMethod::PHYSICAL, "Physical"},
    {ReadingMethod::BARCODE, "Barcode"},
    {ReadingMethod::MAGNETIC_STRIPE, "Magnetic Stripe"},
    {ReadingMethod::ICC, "ICC"},
    {ReadingMethod::DATA_ON_FILE, "Data on File"},
    {ReadingMethod::ICC_FAILED, "ICC Failed"},
    {ReadingMethod::MAGNETIC_STRIPE_FAILED, "Magnetic Stripe Failed"},
    {ReadingMethod::FALLBACK, "Fallback"},
    {ReadingMethod::TRACK1_PRESENT, "Track 1 present"},
    {ReadingMethod::TRACK2_PRESENT, "Track 2 present"}};

inline const std::map<VerificationMethod, const char*> LookupVerificationMethods = {
    {VerificationMethod::UNKNOWN, "Unknown"},
    {VerificationMethod::NONE, "None"},
    {VerificationMethod::MANUAL_SIGNATURE, "Manual Signature"},
    {VerificationMethod::ONLINE_PIN, "Online PIN"},
    {VerificationMethod::OFFLINE_PIN_IN_CLEAR, "Offline PIN in Clear"},
    {VerificationMethod::OFFLINE_PIN_ENCRYPTED, "Offline PIN encrypted"},
    {VerificationMethod::OFFLINE_DIGITIZED_SIGNATURE_ANALYSIS, "Offline digitized signature analysis"},
    {VerificationMethod::OFFLINE_BIOMETRICS, "Offline biometrics"},
    {VerificationMethod::OFFLINE_MANUAL_VERIFICATION, "Offline manual verification"},
    {VerificationMethod::OFFLINE_BIOGRAPHICS, "Offline biographics"},
    {VerificationMethod::ACCOUNT_BASED_DIGITAL_SIGNATURE, "Account-based digital signature"},
    {VerificationMethod::PUBLIC_KEY_BASED_DIGITAL_SIGNATURE, "Public-key-based digital signature"}};

inline const std::map<POSEnvironment, const char*> LookupPOSEnvironments = {
    {POSEnvironment::UNKNOWN, "Unknown"},
    {POSEnvironment::ATTENDED, "Attended"},
    {POSEnvironment::UNATTENDED, "Unattended"},
    {POSEnvironment::MOTO, "MOTO"},
    {POSEnvironment::E_COMMERCE, "E-commerce"},
    {POSEnvironment::M_COMMERCE, "M-commerce"},
    {POSEnvironment::RECURRING, "Recurring"},
    {POSEnvironment::STORED_DETAILS, "Stored details"},
    {POSEnvironment::CAT, "Cardholder-activated transaction (CAT)"},
    {POSEnvironment::ATM_ON_BANK, "ATM (on-bank)"},
    {POSEnvironment::ATM_OFF_BANK, "ATM (off-bank)"},
    {POSEnvironment::DEFERRED_TRANSACTION, "Deferred transaction"},
    {POSEnvironment::INSTALLMENT_TRANSACTION, "Installment transaction"}};

inline const std::map<SecurityCharacteristic, const char*> LookupSecurityCharacteristics = {
    {SecurityCharacteristic::UNKNOWN, "Unknown"},
    {SecurityCharacteristic::PRIVATE_NETWORK, "Private network"},
    {SecurityCharacteristic::OPEN_NETWORK, "Open network"},
    {SecurityCharacteristic::CHANNEL_MACING, "Channel Macing"},
    {SecurityCharacteristic::PASS_THROUGH_MACING, "Pass-through Macing"},
    {SecurityCharacteristic::CHANNEL_ENCRYPTION, "Channel encryption"},
    {SecurityCharacteristic::END_TO_END_ENCRYPTION, "End-to-end encryption"},
    {SecurityCharacteristic::PRIVATE_ALG_ENCRYPTION, "Private-alg encryption"},
    {SecurityCharacteristic::PKI_ENCRYPTION, "PKI encryption"},
    {SecurityCharacteristic::PRIVATE_ALG_MACING, "Private-alg Macing"},
    {SecurityCharacteristic::STD_ALG_MACING, "Standard-alg Macing"},
    {SecurityCharacteristic::CARDHOLDER_MANAGED_END_TO_END_ENCRYPTION, "Cardholder-managed E2E encryption"},
    {SecurityCharacteristic::CARDHOLDER_MANAGED_POINT_TO_POINT_ENCRYPTION, "Cardholder-managed P2P encryption"},
    {SecurityCharacteristic::MERCHANT_MANAGED_END_TO_END_ENCRYPTION, "Merchant-managed E2E encryption"},
    {SecurityCharacteristic::MERCHANT_MANAGED_POINT_TO_POINT_ENCRYPTION, "Merchant-managed P2P encryption"},
    {SecurityCharacteristic::ACQUIRER_MANAGED_END_TO_END_ENCRYPTION, "Acquirer-managed E2E encryption"},
    {SecurityCharacteristic::ACQUIRER_MANAGED_POINT_TO_POINT_ENCRYPTION, "Acquirer-managed P2P encryption"}};

// ---------------------------------------------------------------------------
// pos::POSDataCode
// ---------------------------------------------------------------------------

class POSDataCode
{
public:
    /// Gesamtgröße des gepackten Data-Codes in Bytes (4 Kategorien × 4 Bytes).
    static constexpr std::size_t LENGTH = 16;

    /// Baut aus vier Flag-Kombinationen den 16-Byte-Data-Code.
    POSDataCode(ReadingMethod reading = ReadingMethod::UNKNOWN,
                VerificationMethod verification = VerificationMethod::UNKNOWN,
                POSEnvironment environment = POSEnvironment::UNKNOWN,
                SecurityCharacteristic security = SecurityCharacteristic::UNKNOWN)
    {
        b_.assign(LENGTH, 0);
        packU32LE(offsetOf<ReadingMethod>(), static_cast<unsigned int>(reading));
        packU32LE(offsetOf<VerificationMethod>(), static_cast<unsigned int>(verification));
        packU32LE(offsetOf<POSEnvironment>(), static_cast<unsigned int>(environment));
        packU32LE(offsetOf<SecurityCharacteristic>(), static_cast<unsigned int>(security));
    }

    /// Baut einen Data-Code aus rohen Bytes. Erwartet exakt LENGTH Bytes.
    explicit POSDataCode(const std::vector<uint8_t>& raw)
    {
        if (raw.size() != LENGTH) {
            throw std::invalid_argument("POSDataCode requires exactly " + std::to_string(LENGTH) +
                                        " raw bytes, got " + std::to_string(raw.size()));
        }
        b_ = raw;
    }

    /// Rückgabe der gepackten 16 Bytes (z. B. für BinaryField).
    const std::vector<uint8_t>& pack() const noexcept
    {
        return b_;
    }

    // ---- einzelne Kategorien als ganzer Wert -----------------------------

    ReadingMethod readingMethod() const
    {
        return static_cast<ReadingMethod>(unpackU32LE(offsetOf<ReadingMethod>()));
    }
    VerificationMethod verificationMethod() const
    {
        return static_cast<VerificationMethod>(unpackU32LE(offsetOf<VerificationMethod>()));
    }
    POSEnvironment posEnvironment() const
    {
        return static_cast<POSEnvironment>(unpackU32LE(offsetOf<POSEnvironment>()));
    }
    SecurityCharacteristic securityCharacteristic() const
    {
        return static_cast<SecurityCharacteristic>(unpackU32LE(offsetOf<SecurityCharacteristic>()));
    }

    // ---- Flag-Prüfung (true, wenn ALLE Bits von f gesetzt sind) ----------

    bool hasReadingMethod(ReadingMethod f) const
    {
        const auto value = static_cast<unsigned int>(readingMethod());
        return (value & static_cast<unsigned int>(f)) == static_cast<unsigned int>(f);
    }
    bool hasVerificationMethod(VerificationMethod f) const
    {
        const auto value = static_cast<unsigned int>(verificationMethod());
        return (value & static_cast<unsigned int>(f)) == static_cast<unsigned int>(f);
    }
    bool hasPOSEnvironment(POSEnvironment f) const
    {
        const auto value = static_cast<unsigned int>(posEnvironment());
        return (value & static_cast<unsigned int>(f)) == static_cast<unsigned int>(f);
    }
    bool hasSecurityCharacteristic(SecurityCharacteristic f) const
    {
        const auto value = static_cast<unsigned int>(securityCharacteristic());
        return (value & static_cast<unsigned int>(f)) == static_cast<unsigned int>(f);
    }

    // ---- Komfort-Methoden ------------------------------------------------

    /// EMV / Chip: ICC-Lese (oder Contactless-Chip).
    bool isEMV() const
    {
        return hasReadingMethod(ReadingMethod::ICC) || hasReadingMethod(ReadingMethod::CONTACTLESS);
    }
    /// Kartennummer manuell eingegeben.
    bool isManualEntry() const
    {
        return hasReadingMethod(ReadingMethod::PHYSICAL);
    }
    /// Magnetbahn gelesen.
    bool isSwiped() const
    {
        return hasReadingMethod(ReadingMethod::MAGNETIC_STRIPE);
    }
    bool isRecurring() const
    {
        return hasPOSEnvironment(POSEnvironment::RECURRING);
    }
    bool isECommerce() const
    {
        return hasPOSEnvironment(POSEnvironment::E_COMMERCE);
    }
    /// Karte an der Transaktion nicht physisch anwesend.
    bool isCardNotPresent() const
    {
        return isECommerce() || hasPOSEnvironment(POSEnvironment::MOTO) || isRecurring();
    }

    // ---- Ausgabe ---------------------------------------------------------

    /// Lesbare Zusammenfassung aller gesetzten Flags, z. B.
    /// "Reading: ICC, Track 2 present; Verification: Online PIN; Environment:
    /// Attended; Security: End-to-end encryption". Kategorien ohne gesetzte
    /// Bits liefern "<label>: none".
    std::string describe() const
    {
        const std::vector<std::string> parts = {
            matchesText(readingMethod(), LookupReadingMethods, "Reading"),
            matchesText(verificationMethod(), LookupVerificationMethods, "Verification"),
            matchesText(posEnvironment(), LookupPOSEnvironments, "Environment"),
            matchesText(securityCharacteristic(), LookupSecurityCharacteristics, "Security")};
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i) out += "; ";
            out += parts[i];
        }
        return out;
    }

private:
    /// Interner Speicher des Data-Codes (exakt LENGTH Bytes; nach der
    /// Konstruierung keine Re-Allokation — pack() liefert einen stabilen
    /// Verweis und ist allokationsfrei).
    std::vector<uint8_t> b_;

    // ---- little-endian 32-Bit-Wörter packen/lesen -------------------------

    void packU32LE(std::size_t offset, unsigned int value) noexcept
    {
        b_[offset + 0] = static_cast<uint8_t>((value >> 0) & 0xFF);
        b_[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        b_[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        b_[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    unsigned int unpackU32LE(std::size_t offset) const noexcept
    {
        return (static_cast<unsigned int>(b_[offset + 0])) |
               (static_cast<unsigned int>(b_[offset + 1]) << 8) |
               (static_cast<unsigned int>(b_[offset + 2]) << 16) |
               (static_cast<unsigned int>(b_[offset + 3]) << 24);
    }

    /// Byte-Offset einer Kategorie — Implementationsdetail. (Die Pseudo-
    /// Enumerator <Name>::OFFSET des Vorgängers ist entfernt: Offset 0/4/8/12
    /// ist kein Flag und sollte nicht in der Flag-Semantik auftauchen. Das
    /// Draht-Layout bleibt unverändert.)
    template <typename EnumType>
    static std::size_t offsetOf() noexcept
    {
        if constexpr (std::is_same_v<EnumType, ReadingMethod>)
        {
            return 0;
        }
        else if constexpr (std::is_same_v<EnumType, VerificationMethod>)
        {
            return 4;
        }
        else if constexpr (std::is_same_v<EnumType, POSEnvironment>)
        {
            return 8;
        }
        else
        {
            return 12; // SecurityCharacteristic
        }
    }

    /// Liefert den Lookup-Text für alle in value gesetzten Flags (steigende
    /// Bit-Reihenfolge, durch ", " getrennt). Keine gesetzten Flags ->
    /// "<label>: none".
    template <typename EnumType>
    static std::string matchesText(EnumType value,
                                   const std::map<EnumType, const char*>& table,
                                   const char* label)
    {
        const auto bitsInValue = static_cast<unsigned int>(value);
        std::string text;
        for (const auto& entry : table)
        {
            const auto bits = static_cast<unsigned int>(entry.first);
            if (bits == 0u || (bitsInValue & bits) != bits)
            {
                continue;
            }
            if (!text.empty())
            {
                text += ", ";
            }
            text += entry.second;
        }
        std::string result = std::string(label) + ": ";
        if (text.empty())
        {
            result += "none";
        }
        else
        {
            result += text;
        }
        return result;
    }
};

inline std::ostream& operator<<(std::ostream& os, const POSDataCode& pdc)
{
    os << pdc.describe();
    return os;
}

} // namespace pos

} // namespace TNG_NAMESPACE