#include "_pos.hh"
// [stdc++]
#include <algorithm>
#include <cstring>

namespace TNG_NAMESPACE::pos {

    const std::unordered_map<ReadingMethod, const char*> LookupReadingMethods = {
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
        { ReadingMethod::TRACK2_PRESENT, "Track2 data present" }
    };

    const std::unordered_map<VerificationMethod, const char*> LookupVerificationMethods = {
        // Füge hier die Verifikationsmethoden hinzu
    };

    const std::unordered_map<POSEnvironment, const char*> LookupPOSEnvironments = {
        // Füge hier die POS-Umgebungen hinzu
    };

    const std::unordered_map<SecurityCharacteristic, const char*> LookupSecurityCharacteristics = {
        { SecurityCharacteristic::UNKNWON, "Unknown" },
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
        { SecurityCharacteristic::ACQUIRER_MANAGED_POINT_TO_POINT_ENCRYPTION, "Acquirer managed point-to-point encryption" }
    };

    // Konstruktor für POSDataCode
    POSDataCode::POSDataCode(unsigned int read, unsigned int verify, unsigned int env, unsigned int sec) {
        b.resize(16);
        b[0] = (uint8_t)read;
        b[1] = (uint8_t)(read >> 8);
        b[2] = (uint8_t)(read >> 16);
        b[3] = (uint8_t)(read >> 24);

        b[4] = (uint8_t)verify;
        b[5] = (uint8_t)(verify >> 8);
        b[6] = (uint8_t)(verify >> 16);
        b[7] = (uint8_t)(verify >> 24);

        b[8] = (uint8_t)env;
        b[9] = (uint8_t)(env >> 8);
        b[10] = (uint8_t)(env >> 16);
        b[11] = (uint8_t)(env >> 24);
        
        b[12] = (uint8_t)sec;
        b[13] = (uint8_t)(sec >> 8);
        b[14] = (uint8_t)(sec >> 16);
        b[15] = (uint8_t)(sec >> 24);
    }
    POSDataCode::POSDataCode(const std::vector<uint8_t>& input) {
        if (!input.empty()) {
            std::size_t copyLen = std::min(input.size(), std::size_t(16));
            b.resize(copyLen);  // Resize, um die richtige Größe zu haben
            std::memcpy(b.data(), input.data(), copyLen);  // Kopieren der Daten
        }
    }


    bool POSDataCode::hasReadingMethods(unsigned int read) {
        unsigned int i = b[3] << 24 | b[2] << 16 & 0xFF0000 | b[1] << 8 & 0xFF00 | b[0] & 0xFF;
        return ((i & read) == read);
    }
    bool POSDataCode::hasReadingMethod(ReadingMethod read) {
        return hasReadingMethods((unsigned int)read);
    }
    bool POSDataCode::hasVerificationMethods(unsigned int verify) {
        unsigned int i = b[7] << 24 | b[6] << 16 & 0xFF0000 | b[5] << 8 & 0xFF00 | b[4] & 0xFF;
        return ((i & verify) == verify);
    }
    bool POSDataCode::hasVerificationMethod(VerificationMethod verify) {
        return hasVerificationMethods((unsigned int)verify);
    }
    bool POSDataCode::hasPOSEnvironments(unsigned int env) {
        unsigned int i = b[11] << 24 | b[10] << 16 & 0xFF0000 | b[9] << 8 & 0xFF00 | b[8] & 0xFF;
        return ((i & env) == env);
    }
    bool POSDataCode::hasPOSEnvironment(POSEnvironment env) {
        return hasPOSEnvironments((unsigned int)env);
    }
    bool POSDataCode::hasSecurityCharacteristics(unsigned int sec) {
        unsigned int i = b[15] << 24 | b[14] << 16 & 0xFF0000 | b[13] << 8 & 0xFF00 | b[12] & 0xFF;
        return ((i & sec) == sec);
    }
    bool POSDataCode::hasSecurityCharacteristic(SecurityCharacteristic sec) {
        return hasSecurityCharacteristics((unsigned int)sec);
    }
    bool POSDataCode::isEMV() {
        return (hasReadingMethod(ReadingMethod::ICC) || hasReadingMethod(ReadingMethod::CONTACTLESS));
    }
    bool POSDataCode::isManualEntry() {
        return (hasReadingMethod(ReadingMethod::PHYSICAL));
    }
    bool POSDataCode::isSwiped() {
        return (hasReadingMethod(ReadingMethod::MAGNETIC_STRIPE));
    }
    bool POSDataCode::isRecurring() {
        return (hasPOSEnvironment(POSEnvironment::RECURRING));
    }
    bool POSDataCode::isECommerce() {
        return (hasPOSEnvironment(POSEnvironment::E_COMMERCE));
    }
    bool POSDataCode::isCardNotPresent() {
        return (isECommerce() || isManualEntry() || isRecurring());
    }
}