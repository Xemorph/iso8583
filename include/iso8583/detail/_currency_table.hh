#pragma once

// =============================================================================
// _currency_table.hh - AUTOMATISCH GENERIERT, NICHT VON HAND BEARBEITEN.
// =============================================================================
//
// Erzeugt von scripts/generate_currency_table.py aus data/iso4217/codes-all.csv
// (178 eindeutige, aktive Währungscodes)
// Generiert am: 2026-07-23
//
// Quelle: https://github.com/datasets/currency-codes (Public Domain), abgeleitet
// aus den offiziellen SIX-Group-ISO-4217-XML-Tabellen. Siehe data/iso4217/README.md
// für Provenienz und den (bewusst manuellen, nicht build-automatischen)
// Aktualisierungsprozess.
//
// Zum Aktualisieren: scripts/generate_currency_table.py erneut ausführen,
// NICHT diese Datei von Hand anpassen.
//
// WICHTIG: Reines Fragment, kein eigenständig inkludierbarer Header - wird
// ausschließlich von include/iso8583/Currency.hh eingebunden (analog zu
// detail/_codec_impl.hh, das ebenfalls nur von _codec.hh aus benutzt wird).
// Setzt voraus, dass `class Currency` bereits sichtbar ist - kein eigenes
// #include <iso8583/Currency.hh> hier, um einen zirkulären Include zu vermeiden.
// =============================================================================

// [stdc++]
#include <array>

namespace TNG_NAMESPACE {
    namespace currency {
        namespace detail {

            inline constexpr std::size_t kCurrencyTableSize = 178;

            inline constexpr std::array<Currency, kCurrencyTableSize> kCurrencyTable = {{
                Currency("AED", 784, 2), // UAE Dirham
                Currency("AFN", 971, 2), // Afghani
                Currency("ALL", 8, 2), // Lek
                Currency("AMD", 51, 2), // Armenian Dram
                Currency("AOA", 973, 2), // Kwanza
                Currency("ARS", 32, 2), // Argentine Peso
                Currency("AUD", 36, 2), // Australian Dollar
                Currency("AWG", 533, 2), // Aruban Florin
                Currency("AZN", 944, 2), // Azerbaijan Manat
                Currency("BAM", 977, 2), // Convertible Mark
                Currency("BBD", 52, 2), // Barbados Dollar
                Currency("BDT", 50, 2), // Taka
                Currency("BHD", 48, 3), // Bahraini Dinar
                Currency("BIF", 108, 0), // Burundi Franc
                Currency("BMD", 60, 2), // Bermudian Dollar
                Currency("BND", 96, 2), // Brunei Dollar
                Currency("BOB", 68, 2), // Boliviano
                Currency("BOV", 984, 2), // Mvdol
                Currency("BRL", 986, 2), // Brazilian Real
                Currency("BSD", 44, 2), // Bahamian Dollar
                Currency("BTN", 64, 2), // Ngultrum
                Currency("BWP", 72, 2), // Pula
                Currency("BYN", 933, 2), // Belarusian Ruble
                Currency("BZD", 84, 2), // Belize Dollar
                Currency("CAD", 124, 2), // Canadian Dollar
                Currency("CDF", 976, 2), // Congolese Franc
                Currency("CHE", 947, 2), // WIR Euro
                Currency("CHF", 756, 2), // Swiss Franc
                Currency("CHW", 948, 2), // WIR Franc
                Currency("CLF", 990, 4), // Unidad de Fomento
                Currency("CLP", 152, 0), // Chilean Peso
                Currency("CNY", 156, 2), // Yuan Renminbi
                Currency("COP", 170, 2), // Colombian Peso
                Currency("COU", 970, 2), // Unidad de Valor Real
                Currency("CRC", 188, 2), // Costa Rican Colon
                Currency("CUP", 192, 2), // Cuban Peso
                Currency("CVE", 132, 2), // Cabo Verde Escudo
                Currency("CZK", 203, 2), // Czech Koruna
                Currency("DJF", 262, 0), // Djibouti Franc
                Currency("DKK", 208, 2), // Danish Krone
                Currency("DOP", 214, 2), // Dominican Peso
                Currency("DZD", 12, 2), // Algerian Dinar
                Currency("EGP", 818, 2), // Egyptian Pound
                Currency("ERN", 232, 2), // Nakfa
                Currency("ETB", 230, 2), // Ethiopian Birr
                Currency("EUR", 978, 2), // Euro
                Currency("FJD", 242, 2), // Fiji Dollar
                Currency("FKP", 238, 2), // Falkland Islands Pound
                Currency("GBP", 826, 2), // Pound Sterling
                Currency("GEL", 981, 2), // Lari
                Currency("GHS", 936, 2), // Ghana Cedi
                Currency("GIP", 292, 2), // Gibraltar Pound
                Currency("GMD", 270, 2), // Dalasi
                Currency("GNF", 324, 0), // Guinean Franc
                Currency("GTQ", 320, 2), // Quetzal
                Currency("GYD", 328, 2), // Guyana Dollar
                Currency("HKD", 344, 2), // Hong Kong Dollar
                Currency("HNL", 340, 2), // Lempira
                Currency("HTG", 332, 2), // Gourde
                Currency("HUF", 348, 2), // Forint
                Currency("IDR", 360, 2), // Rupiah
                Currency("ILS", 376, 2), // New Israeli Sheqel
                Currency("INR", 356, 2), // Indian Rupee
                Currency("IQD", 368, 3), // Iraqi Dinar
                Currency("IRR", 364, 2), // Iranian Rial
                Currency("ISK", 352, 0), // Iceland Krona
                Currency("JMD", 388, 2), // Jamaican Dollar
                Currency("JOD", 400, 3), // Jordanian Dinar
                Currency("JPY", 392, 0), // Yen
                Currency("KES", 404, 2), // Kenyan Shilling
                Currency("KGS", 417, 2), // Som
                Currency("KHR", 116, 2), // Riel
                Currency("KMF", 174, 0), // Comorian Franc
                Currency("KPW", 408, 2), // North Korean Won
                Currency("KRW", 410, 0), // Won
                Currency("KWD", 414, 3), // Kuwaiti Dinar
                Currency("KYD", 136, 2), // Cayman Islands Dollar
                Currency("KZT", 398, 2), // Tenge
                Currency("LAK", 418, 2), // Lao Kip
                Currency("LBP", 422, 2), // Lebanese Pound
                Currency("LKR", 144, 2), // Sri Lanka Rupee
                Currency("LRD", 430, 2), // Liberian Dollar
                Currency("LSL", 426, 2), // Loti
                Currency("LYD", 434, 3), // Libyan Dinar
                Currency("MAD", 504, 2), // Moroccan Dirham
                Currency("MDL", 498, 2), // Moldovan Leu
                Currency("MGA", 969, 2), // Malagasy Ariary
                Currency("MKD", 807, 2), // Denar
                Currency("MMK", 104, 2), // Kyat
                Currency("MNT", 496, 2), // Tugrik
                Currency("MOP", 446, 2), // Pataca
                Currency("MRU", 929, 2), // Ouguiya
                Currency("MUR", 480, 2), // Mauritius Rupee
                Currency("MVR", 462, 2), // Rufiyaa
                Currency("MWK", 454, 2), // Malawi Kwacha
                Currency("MXN", 484, 2), // Mexican Peso
                Currency("MXV", 979, 2), // Mexican Unidad de Inversion (UDI)
                Currency("MYR", 458, 2), // Malaysian Ringgit
                Currency("MZN", 943, 2), // Mozambique Metical
                Currency("NAD", 516, 2), // Namibia Dollar
                Currency("NGN", 566, 2), // Naira
                Currency("NIO", 558, 2), // Cordoba Oro
                Currency("NOK", 578, 2), // Norwegian Krone
                Currency("NPR", 524, 2), // Nepalese Rupee
                Currency("NZD", 554, 2), // New Zealand Dollar
                Currency("OMR", 512, 3), // Rial Omani
                Currency("PAB", 590, 2), // Balboa
                Currency("PEN", 604, 2), // Sol
                Currency("PGK", 598, 2), // Kina
                Currency("PHP", 608, 2), // Philippine Peso
                Currency("PKR", 586, 2), // Pakistan Rupee
                Currency("PLN", 985, 2), // Zloty
                Currency("PYG", 600, 0), // Guarani
                Currency("QAR", 634, 2), // Qatari Rial
                Currency("RON", 946, 2), // Romanian Leu
                Currency("RSD", 941, 2), // Serbian Dinar
                Currency("RUB", 643, 2), // Russian Ruble
                Currency("RWF", 646, 0), // Rwanda Franc
                Currency("SAR", 682, 2), // Saudi Riyal
                Currency("SBD", 90, 2), // Solomon Islands Dollar
                Currency("SCR", 690, 2), // Seychelles Rupee
                Currency("SDG", 938, 2), // Sudanese Pound
                Currency("SEK", 752, 2), // Swedish Krona
                Currency("SGD", 702, 2), // Singapore Dollar
                Currency("SHP", 654, 2), // Saint Helena Pound
                Currency("SLE", 925, 2), // Leone
                Currency("SOS", 706, 2), // Somali Shilling
                Currency("SRD", 968, 2), // Surinam Dollar
                Currency("SSP", 728, 2), // South Sudanese Pound
                Currency("STN", 930, 2), // Dobra
                Currency("SVC", 222, 2), // El Salvador Colon
                Currency("SYP", 760, 2), // Syrian Pound
                Currency("SZL", 748, 2), // Lilangeni
                Currency("THB", 764, 2), // Baht
                Currency("TJS", 972, 2), // Somoni
                Currency("TMT", 934, 2), // Turkmenistan New Manat
                Currency("TND", 788, 3), // Tunisian Dinar
                Currency("TOP", 776, 2), // Pa’anga
                Currency("TRY", 949, 2), // Turkish Lira
                Currency("TTD", 780, 2), // Trinidad and Tobago Dollar
                Currency("TWD", 901, 2), // New Taiwan Dollar
                Currency("TZS", 834, 2), // Tanzanian Shilling
                Currency("UAH", 980, 2), // Hryvnia
                Currency("UGX", 800, 0), // Uganda Shilling
                Currency("USD", 840, 2), // US Dollar
                Currency("USN", 997, 2), // US Dollar (Next day)
                Currency("UYI", 940, 0), // Uruguay Peso en Unidades Indexadas (UI)
                Currency("UYU", 858, 2), // Peso Uruguayo
                Currency("UYW", 927, 4), // Unidad Previsional
                Currency("UZS", 860, 2), // Uzbekistan Sum
                Currency("VED", 926, 2), // Bolívar Soberano
                Currency("VES", 928, 2), // Bolívar Soberano
                Currency("VND", 704, 0), // Dong
                Currency("VUV", 548, 0), // Vatu
                Currency("WST", 882, 2), // Tala
                Currency("XAD", 396, 2), // Arab Accounting Dinar
                Currency("XAF", 950, 0), // CFA Franc BEAC
                Currency("XAG", 961, 0), // Silver
                Currency("XAU", 959, 0), // Gold
                Currency("XBA", 955, 0), // Bond Markets Unit European Composite Unit (EURCO)
                Currency("XBB", 956, 0), // Bond Markets Unit European Monetary Unit (E.M.U.-6)
                Currency("XBC", 957, 0), // Bond Markets Unit European Unit of Account 9 (E.U.A.-9)
                Currency("XBD", 958, 0), // Bond Markets Unit European Unit of Account 17 (E.U.A.-17)
                Currency("XCD", 951, 2), // East Caribbean Dollar
                Currency("XCG", 532, 2), // Caribbean Guilder
                Currency("XDR", 960, 0), // SDR (Special Drawing Right)
                Currency("XOF", 952, 0), // CFA Franc BCEAO
                Currency("XPD", 964, 0), // Palladium
                Currency("XPF", 953, 0), // CFP Franc
                Currency("XPT", 962, 0), // Platinum
                Currency("XSU", 994, 0), // Sucre
                Currency("XTS", 963, 0), // Codes specifically reserved for testing purposes
                Currency("XUA", 965, 0), // ADB Unit of Account
                Currency("XXX", 999, 0), // The codes assigned for transactions where no currency is involved
                Currency("YER", 886, 2), // Yemeni Rial
                Currency("ZAR", 710, 2), // Rand
                Currency("ZMW", 967, 2), // Zambian Kwacha
                Currency("ZWG", 924, 2), // Zimbabwe Gold
            }};

        } // namespace detail
    } // namespace currency
} // namespace TNG_NAMESPACE
