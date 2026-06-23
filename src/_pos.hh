#pragma once
// [stdc++]
#include <vector>
#include <unordered_map>
// [tng]
#include <iso8583/config.h>

namespace TNG_NAMESPACE {

    namespace pos {

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
            OFFSET = 0
        };

        enum class VerificationMethod : unsigned int {
            UNKNWON = 1,
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

        enum class POSEnvironment : unsigned int {
            UNKNWON = 1,
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

        enum class SecurityCharacteristic : unsigned int {
            UNKNWON = 1,
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

        extern const std::unordered_map<ReadingMethod, const char*> LookupReadingMethods;
        extern const std::unordered_map<VerificationMethod, const char*> LookupVerificationMethods;
        extern const std::unordered_map<POSEnvironment, const char*> LookupPOSEnvironments;
        extern const std::unordered_map<SecurityCharacteristic, const char*> LookupSecurityCharacteristics;

        class POSDataCode {
        private:
            std::vector<uint8_t> b;
        public:
            POSDataCode(unsigned int read, unsigned int verify, unsigned int env, unsigned int sec);
            POSDataCode(const std::vector<uint8_t>& b);

            bool hasReadingMethods(unsigned int read);
            bool hasReadingMethod(ReadingMethod read);
            bool hasVerificationMethods(unsigned int verify);
            bool hasVerificationMethod(VerificationMethod verify);
            bool hasPOSEnvironments(unsigned int env);
            bool hasPOSEnvironment(POSEnvironment env);
            bool hasSecurityCharacteristics(unsigned int sec);
            bool hasSecurityCharacteristic(SecurityCharacteristic sec);
            bool isEMV();
            bool isManualEntry();
            bool isSwiped();
            bool isRecurring();
            bool isECommerce();
            bool isCardNotPresent();
        };
    }
}