#include <catch2/catch_test_macros.hpp>

#include <iso8583/iso8583.h>
#include "_parser.hh"
#include "_tlv.hh"
#include "fmt_types.hh"

using namespace TNG_NAMESPACE;

// Hilfsfunktionen
static std::vector<uint8_t> ebcdic(const std::string& s) {
    std::vector<uint8_t> buf(s.size());
    ::tng::to<Encoder::EBCDIC>(s, buf, 0);
    return buf;
}
static std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

// =============================================================================
// Mastercard-Spezialisierung (ISOTLVParser_MC)
// =============================================================================

TEST_CASE("ISOTLVParser_MC - unparse TCC only", "[tlv][mc][unparse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto msg = std::make_shared<ISOMessage>();
    auto consumed = tlv->unparse(msg, ebcdic("P"));
    CHECK(consumed == 1);
    auto tcc = msg->get<ISOOpaqueField>(ISOTLVParser_MC::TCC_KEY);
    REQUIRE(tcc != nullptr);
    CHECK(tcc->value() == "P");
}

TEST_CASE("ISOTLVParser_MC - unparse TCC + single SE", "[tlv][mc][unparse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("P"),
        ebcdic("48"), ebcdic("06"), ebcdic("ABCDEF"),
        });
    auto msg = std::make_shared<ISOMessage>();
    CHECK(tlv->unparse(msg, payload) == 1 + 2 + 2 + 6);
    CHECK(msg->get<ISOOpaqueField>(ISOTLVParser_MC::TCC_KEY)->value() == "P");
    auto se48 = msg->get<ISOBinaryField>(48);
    REQUIRE(se48 != nullptr);
    CHECK(se48->value() == ebcdic("ABCDEF"));
}

TEST_CASE("ISOTLVParser_MC - unparse multiple SEs", "[tlv][mc][unparse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("R"),
        ebcdic("60"), ebcdic("03"), ebcdic("AAA"),
        ebcdic("72"), ebcdic("04"), ebcdic("BBBB"),
        });
    auto msg = std::make_shared<ISOMessage>();
    tlv->unparse(msg, payload);
    CHECK(msg->get<ISOOpaqueField>(ISOTLVParser_MC::TCC_KEY)->value() == "R");
    CHECK(msg->get<ISOBinaryField>(60)->value() == ebcdic("AAA"));
    CHECK(msg->get<ISOBinaryField>(72)->value() == ebcdic("BBBB"));
}

TEST_CASE("ISOTLVParser_MC - wire_offset tracked with base_offset", "[tlv][mc][wire]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("P"),
        ebcdic("48"), ebcdic("04"), ebcdic("TEST"),
        });
    auto msg = std::make_shared<ISOMessage>();
    tlv->unparse(msg, payload, 10);

    auto tcc = msg->get<ISOOpaqueField>(ISOTLVParser_MC::TCC_KEY);
    CHECK(tcc->wire_offset() == 10);
    CHECK(tcc->wire_length() == 1);

    auto se48 = msg->get<ISOBinaryField>(48);
    CHECK(se48->wire_offset() == 11);
    CHECK(se48->wire_length() == 2 + 2 + 4);
}

TEST_CASE("ISOTLVParser_MC - parse single SE", "[tlv][mc][parse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto msg = std::make_shared<ISOMessage>();

    auto tcc = std::make_shared<ISOOpaqueField>(ISOTLVParser_MC::TCC_KEY);
    tcc->value("P");
    msg->set(tcc);

    auto se72 = std::make_shared<ISOBinaryField>(72);
    se72->value(ebcdic("HELLO"));
    msg->set(se72);

    auto out = tlv->parse(msg);
    REQUIRE(out.size() == 1 + 2 + 2 + 5);
    CHECK(out[0] == ebcdic("P")[0]);
    CHECK(std::vector<uint8_t>(out.begin() + 1, out.begin() + 3) == ebcdic("72"));
    CHECK(std::vector<uint8_t>(out.begin() + 3, out.begin() + 5) == ebcdic("05"));
    CHECK(std::vector<uint8_t>(out.begin() + 5, out.end()) == ebcdic("HELLO"));
}

TEST_CASE("ISOTLVParser_MC - parse SEs sorted ascending", "[tlv][mc][parse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto msg = std::make_shared<ISOMessage>();

    // Umgekehrt einfügen – Ausgabe muss aufsteigend sein
    auto se72 = std::make_shared<ISOBinaryField>(72);
    se72->value(ebcdic("BB")); msg->set(se72);
    auto se48 = std::make_shared<ISOBinaryField>(48);
    se48->value(ebcdic("AA")); msg->set(se48);

    auto out = tlv->parse(msg);
    REQUIRE(out.size() == 2 * (2 + 2 + 2) +1);
    CHECK(std::vector<uint8_t>(out.begin() + 1, out.begin() + 3) == ebcdic("48"));
    CHECK(std::vector<uint8_t>(out.begin() + 7, out.begin() + 9) == ebcdic("72"));
}

TEST_CASE("ISOTLVParser_MC - full roundtrip", "[tlv][mc][roundtrip]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("T"),
        ebcdic("22"), ebcdic("03"), {0x01, 0x02, 0x03},
        ebcdic("48"), ebcdic("06"), {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE},
        ebcdic("72"), ebcdic("02"), {0xAB, 0xCD},
        });
    auto msg = std::make_shared<ISOMessage>();
    tlv->unparse(msg, payload);
    CHECK(tlv->parse(msg) == payload);
}

// =============================================================================
// Visa-Spezialisierung (ISOTLVParser_VI): BCD TAG+LEN, kein TCC
// =============================================================================

TEST_CASE("ISOTLVParser_VI - unparse BCD tag and length", "[tlv][vi][unparse]") {
    auto tlv = std::make_shared<ISOTLVParser_VI>();

    std::vector<uint8_t> payload = {
        0x00, 0x21,              // TAG BCD = 21
        0x04,                    // LEN BCD = 4
        0xDE, 0xAD, 0xBE, 0xEF,
        0x00, 0x60,              // TAG BCD = 60
        0x02,                    // LEN BCD = 2
        0xCA, 0xFE,
    };

    auto msg = std::make_shared<ISOMessage>();
    CHECK(tlv->unparse(msg, payload) == payload.size());
    CHECK_FALSE(msg->has(ISOTLVParser_VI::TCC_KEY));

    auto se21 = msg->get<ISOBinaryField>(21);
    REQUIRE(se21 != nullptr);
    CHECK(se21->value() == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});

    auto se60 = msg->get<ISOBinaryField>(60);
    REQUIRE(se60 != nullptr);
    CHECK(se60->value() == std::vector<uint8_t>{0xCA, 0xFE});
}

TEST_CASE("ISOTLVParser_VI - full roundtrip BCD", "[tlv][vi][roundtrip]") {
    auto tlv = std::make_shared<ISOTLVParser_VI>();
    std::vector<uint8_t> payload = {
        0x00, 0x21, 0x02, 0xAB, 0xCD,
        0x00, 0x60, 0x04, 0x01, 0x02, 0x03, 0x04,
    };
    auto msg = std::make_shared<ISOMessage>();
    tlv->unparse(msg, payload);
    CHECK(tlv->parse(msg) == payload);
}

// =============================================================================
// Custom-Spezialisierung zur Compile-Zeit
// =============================================================================

TEST_CASE("ISOTLVParser custom - 1 byte ASCII tag no TCC", "[tlv][custom]") {
    // Hypothetisches Format: TAG=1 Byte ASCII, LEN=2 Byte ASCII, kein TCC
    using MyParser = ISOTLVParser<1, 2, false, Encoder::ASCII, Encoder::ASCII>;
    auto tlv = std::make_shared<MyParser>();

    std::vector<uint8_t> payload;
    // TAG='5' (0x35), LEN="04", DATA=4 Bytes
    payload.push_back('5');
    payload.push_back('0'); payload.push_back('4');
    payload.push_back(0x01); payload.push_back(0x02);
    payload.push_back(0x03); payload.push_back(0x04);

    auto msg = std::make_shared<ISOMessage>();
    CHECK(tlv->unparse(msg, payload) == payload.size());

    // '5' = ASCII 5
    auto se5 = msg->get<ISOBinaryField>(5);
    REQUIRE(se5 != nullptr);
    CHECK(se5->value() == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});
    CHECK(tlv->parse(msg) == payload);
}

TEST_CASE("ISOTLVParser - data_encoding_map hint", "[tlv][data_enc]") {
    // data_encoding_map ist ein Runtime-Hinweis für höhere Schichten
    ISOTLVParser_MC::DataEncodingMap enc_map;
    enc_map[72] = Encoder::BCD;   // SE72-Daten sind BCD
    enc_map[48] = Encoder::EBCDIC;

    auto tlv = std::make_shared<ISOTLVParser_MC>(std::move(enc_map));

    CHECK(tlv->data_encoding_for(72) == std::optional<Encoder>{Encoder::BCD});
    CHECK(tlv->data_encoding_for(48) == std::optional<Encoder>{Encoder::EBCDIC});
    CHECK(tlv->data_encoding_for(99) == std::nullopt);
}
