#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <iso8583/iso8583.h>
#include "_parser.hh"
#include "_tlv.hh"
#include "fmt_types.hh"

using namespace TNG_NAMESPACE;

// Hilfsfunktionen
static std::vector<uint8_t> ebcdic(const std::string& s) {
    std::vector<uint8_t> buf(s.size());
    codec::to<codec::Encoder::EBCDIC>(s, buf, 0);
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
    auto msg = std::make_shared< Message >();
    auto consumed = tlv->unparse(msg, ebcdic("P"));
    CHECK(consumed == 1);
    auto tcc = msg->get< OpaqueField >(ISOTLVParser_MC::TCC_KEY);
    REQUIRE(tcc != nullptr);
    CHECK(tcc->value() == "P");
}

TEST_CASE("ISOTLVParser_MC - unparse TCC + single SE", "[tlv][mc][unparse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("P"),
        ebcdic("48"), ebcdic("06"), ebcdic("ABCDEF"),
        });
    auto msg = std::make_shared< Message >();
    CHECK(tlv->unparse(msg, payload) == 1 + 2 + 2 + 6);
    CHECK(msg->get< OpaqueField> (ISOTLVParser_MC::TCC_KEY)->value() == "P");
    auto se48 = msg->get< BinaryField >(48);
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
    auto msg = std::make_shared< Message >();
    tlv->unparse(msg, payload);
    CHECK(msg->get< OpaqueField >(ISOTLVParser_MC::TCC_KEY)->value() == "R");
    CHECK(msg->get< BinaryField >(60)->value() == ebcdic("AAA"));
    CHECK(msg->get< BinaryField >(72)->value() == ebcdic("BBBB"));
}

TEST_CASE("ISOTLVParser_MC - wire_offset tracked with base_offset", "[tlv][mc][wire]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto payload = concat({
        ebcdic("P"),
        ebcdic("48"), ebcdic("04"), ebcdic("TEST"),
        });
    auto msg = std::make_shared< Message >();
    tlv->unparse(msg, payload, 10);

    auto tcc = msg->get< OpaqueField >(ISOTLVParser_MC::TCC_KEY);
    CHECK(tcc->wire_offset() == 10);
    CHECK(tcc->wire_length() == 1);

    auto se48 = msg->get< BinaryField >(48);
    CHECK(se48->wire_offset() == 11);
    CHECK(se48->wire_length() == 2 + 2 + 4);
}

TEST_CASE("ISOTLVParser_MC - parse single SE", "[tlv][mc][parse]") {
    auto tlv = std::make_shared<ISOTLVParser_MC>();
    auto msg = std::make_shared< Message >();

    auto tcc = std::make_shared< OpaqueField >(ISOTLVParser_MC::TCC_KEY);
    tcc->value("P");
    msg->set(tcc);

    auto se72 = std::make_shared< BinaryField >(72);
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
    auto msg = std::make_shared< Message >();

    // Umgekehrt einfügen – Ausgabe muss aufsteigend sein
    auto se72 = std::make_shared< BinaryField >(72);
    se72->value(ebcdic("BB")); msg->set(se72);
    auto se48 = std::make_shared< BinaryField >(48);
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
    auto msg = std::make_shared< Message >();
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

    auto msg = std::make_shared< Message >();
    CHECK(tlv->unparse(msg, payload) == payload.size());
    CHECK_FALSE(msg->has(ISOTLVParser_VI::TCC_KEY));

    auto se21 = msg->get< BinaryField >(21);
    REQUIRE(se21 != nullptr);
    CHECK(se21->value() == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});

    auto se60 = msg->get< BinaryField >(60);
    REQUIRE(se60 != nullptr);
    CHECK(se60->value() == std::vector<uint8_t>{0xCA, 0xFE});
}

TEST_CASE("ISOTLVParser_VI - full roundtrip BCD", "[tlv][vi][roundtrip]") {
    auto tlv = std::make_shared<ISOTLVParser_VI>();
    std::vector<uint8_t> payload = {
        0x00, 0x21, 0x02, 0xAB, 0xCD,
        0x00, 0x60, 0x04, 0x01, 0x02, 0x03, 0x04,
    };
    auto msg = std::make_shared< Message >();
    tlv->unparse(msg, payload);
    CHECK(tlv->parse(msg) == payload);
}

// =============================================================================
// Custom-Spezialisierung zur Compile-Zeit
// =============================================================================

TEST_CASE("ISOTLVParser custom - 1 byte ASCII tag no TCC", "[tlv][custom]") {
    // Hypothetisches Format: TAG=1 Byte ASCII, LEN=2 Byte ASCII, kein TCC
    using MyParser = ISOTLVParser<
        FixedNumericTag<1, codec::Encoder::ASCII>,
        FixedNumericLength<2, codec::Encoder::ASCII>,
        false>;
    auto tlv = std::make_shared<MyParser>();

    std::vector<uint8_t> payload;
    // TAG='5' (0x35), LEN="04", DATA=4 Bytes
    payload.push_back('5');
    payload.push_back('0'); payload.push_back('4');
    payload.push_back(0x01); payload.push_back(0x02);
    payload.push_back(0x03); payload.push_back(0x04);

    auto msg = std::make_shared< Message >();
    CHECK(tlv->unparse(msg, payload) == payload.size());

    // '5' = ASCII 5
    auto se5 = msg->get< BinaryField >(5);
    REQUIRE(se5 != nullptr);
    CHECK(se5->value() == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});
    CHECK(tlv->parse(msg) == payload);
}

TEST_CASE("ISOTLVParser - data_encoding_map hint", "[tlv][data_enc]") {
    // data_encoding_map ist ein Runtime-Hinweis für höhere Schichten
    ISOTLVParser_MC::DataEncodingMap enc_map;
    enc_map[72] = codec::Encoder::BCD;   // SE72-Daten sind BCD
    enc_map[48] = codec::Encoder::EBCDIC;

    auto tlv = std::make_shared<ISOTLVParser_MC>(std::move(enc_map));

    CHECK(tlv->data_encoding_for(72) == std::optional<codec::Encoder>{codec::Encoder::BCD});
    CHECK(tlv->data_encoding_for(48) == std::optional<codec::Encoder>{codec::Encoder::EBCDIC});
    CHECK(tlv->data_encoding_for(99) == std::nullopt);
}

// =============================================================================
// BerTag - isolierte Read/Write-Tests (ISO/IEC 8825-1 §8.1.2)
// =============================================================================

TEST_CASE("BerTag - single byte tag (low 5 bits != 0x1F)", "[tlv][ber][tag]") {
    // 0x95 = EMV "Terminal Verification Results" - low 5 bits = 0x15 (!= 0x1F)
    std::vector<uint8_t> buf{ 0x95, 0xAA, 0xBB };
    auto [tag, consumed] = BerTag::read(buf, 0);
    CHECK(tag == 0x95);
    CHECK(consumed == 1);
    CHECK(BerTag::required_size(0x95) == 1);

    std::vector<uint8_t> out(1, 0x00);
    CHECK(BerTag::write(out, 0, 0x95) == 1);
    CHECK(out == std::vector<uint8_t>{0x95});
}

TEST_CASE("BerTag - two-byte tag, single continuation byte", "[tlv][ber][tag]") {
    // Erstes Byte hat low5 == 0x1F (Mehrbyte-Form), zweites Byte hat Bit 8 = 0
    // (letztes Byte). Wert = Big-Endian-Verkettung 0x1F22.
    std::vector<uint8_t> buf{ 0x1F, 0x22, 0xEE };
    auto [tag, consumed] = BerTag::read(buf, 0);
    CHECK(tag == 0x1F22);
    CHECK(consumed == 2);
    CHECK(BerTag::required_size(0x1F22) == 2);

    std::vector<uint8_t> out(2, 0x00);
    CHECK(BerTag::write(out, 0, 0x1F22) == 2);
    CHECK(out == std::vector<uint8_t>{0x1F, 0x22});
}

TEST_CASE("BerTag - three-byte tag with two continuation bytes", "[tlv][ber][tag]") {
    // Byte1 low5=0x1F (Mehrbyte), Byte2 Bit8=1 (weiter), Byte3 Bit8=0 (Ende).
    std::vector<uint8_t> buf{ 0x1F, 0x81, 0x01 };
    auto [tag, consumed] = BerTag::read(buf, 0);
    CHECK(tag == 0x1F8101);
    CHECK(consumed == 3);
    CHECK(BerTag::required_size(0x1F8101) == 3);

    std::vector<uint8_t> out(3, 0x00);
    CHECK(BerTag::write(out, 0, 0x1F8101) == 3);
    CHECK(out == buf);
}

TEST_CASE("BerTag - realistic EMV tag 9F26 (Application Cryptogram)", "[tlv][ber][tag][emv]") {
    // Reine Byte-Ebene (read/write), unabhängig von ISOMessage/TNG_KEY_TYPE -
    // siehe Hinweis bei "BERTLVParser - EMV-Tags > int16_t" weiter unten.
    std::vector<uint8_t> buf{ 0x9F, 0x26 };
    auto [tag, consumed] = BerTag::read(buf, 0);
    CHECK(tag == 0x9F26);
    CHECK(consumed == 2);

    std::vector<uint8_t> out(2, 0x00);
    CHECK(BerTag::write(out, 0, 0x9F26) == 2);
    CHECK(out == buf);
}

TEST_CASE("BerTag - truncated buffer is reported as error (consumed == 0)", "[tlv][ber][tag][error]") {
    std::vector<uint8_t> buf{ 0x1F, 0x81 }; // Fortsetzungsbit gesetzt, aber kein 3. Byte
    auto [tag, consumed] = BerTag::read(buf, 0);
    CHECK(consumed == 0);
}

// =============================================================================
// BerLength - isolierte Read/Write-Tests (ISO/IEC 8825-1 §8.1.3)
// =============================================================================

TEST_CASE("BerLength - short form (< 0x80)", "[tlv][ber][length]") {
    std::vector<uint8_t> buf{ 0x7F, 0xAA };
    auto [len, consumed] = BerLength::read(buf, 0);
    CHECK(len == 127);
    CHECK(consumed == 1);
    CHECK(BerLength::required_size(127) == 1);

    std::vector<uint8_t> out(1, 0x00);
    CHECK(BerLength::write(out, 0, 127) == 1);
    CHECK(out == std::vector<uint8_t>{0x7F});
}

TEST_CASE("BerLength - long form, single continuation byte (length 128)", "[tlv][ber][length]") {
    // Erste Länge, die NICHT mehr in die Short-Form passt.
    std::vector<uint8_t> buf{ 0x81, 0x80 };
    auto [len, consumed] = BerLength::read(buf, 0);
    CHECK(len == 128);
    CHECK(consumed == 2);
    CHECK(BerLength::required_size(128) == 2);

    std::vector<uint8_t> out(2, 0x00);
    CHECK(BerLength::write(out, 0, 128) == 2);
    CHECK(out == buf);
}

TEST_CASE("BerLength - long form, length 200 (single continuation byte)", "[tlv][ber][length]") {
    std::vector<uint8_t> buf{ 0x81, 0xC8 }; // 0xC8 = 200
    auto [len, consumed] = BerLength::read(buf, 0);
    CHECK(len == 200);
    CHECK(consumed == 2);

    std::vector<uint8_t> out(2, 0x00);
    CHECK(BerLength::write(out, 0, 200) == 2);
    CHECK(out == buf);
}

TEST_CASE("BerLength - long form, two continuation bytes (length 300)", "[tlv][ber][length]") {
    std::vector<uint8_t> buf{ 0x82, 0x01, 0x2C }; // 300 = 0x012C
    auto [len, consumed] = BerLength::read(buf, 0);
    CHECK(len == 300);
    CHECK(consumed == 3);
    CHECK(BerLength::required_size(300) == 3);

    std::vector<uint8_t> out(3, 0x00);
    CHECK(BerLength::write(out, 0, 300) == 3);
    CHECK(out == buf);
}

TEST_CASE("BerLength - indefinite form (0x80) is rejected", "[tlv][ber][length][error]") {
    std::vector<uint8_t> buf{ 0x80, 0x01, 0x02 };
    auto [len, consumed] = BerLength::read(buf, 0);
    CHECK(consumed == 0); // wird als Fehler behandelt, nicht unterstützt
}

// =============================================================================
// BERTLVParser - End-to-End über den generalisierten Parser
// =============================================================================
//
// WICHTIGER HINWEIS zur Tag-Wahl in diesen Tests:
// SE-Werte werden intern als TNG_KEY_TYPE (int16_t, Bereich bis 32767) in der
// ISOMessage abgelegt (siehe tlv_detail::store_se). Reale 2-Byte-EMV-Tags mit
// erstem Byte >= 0x80 (z.B. 9F26, 5F24, 9F36 - praktisch alle "9Fxx"/"5Fxx"-
// Tags) ergeben als Big-Endian-Wert > 32767 und passen NICHT in TNG_KEY_TYPE.
// Das ist unabhängig vom Policy-Pattern - BerTag::read/write funktioniert für
// diese Werte einwandfrei (siehe Tests oben) - sondern eine Einschränkung des
// aktuellen ISO_MAP-Schlüsseltyps. Die Tests hier verwenden daher bewusst
// Tag-Werte, die in int16_t passen, um den Parser end-to-end zu prüfen, OHNE
// diese offene Design-Frage stillschweigend zu umgehen.
TEST_CASE("BERTLVParser - single-byte tag, short-form length roundtrip", "[tlv][ber][roundtrip]") {
    auto tlv = std::make_shared<BERTLVParser>();
    auto msg = std::make_shared<Message>();

    // Tag 0x15 (low5 != 0x1F -> 1 Byte), Länge 3 (Short-Form), Daten
    std::vector<uint8_t> payload{ 0x15, 0x03, 0xAA, 0xBB, 0xCC };

    CHECK(tlv->unparse(msg, payload) == payload.size());

    auto se = msg->get<BinaryField>(0x15);
    REQUIRE(se != nullptr);
    CHECK(se->value() == std::vector<uint8_t>{0xAA, 0xBB, 0xCC});
    CHECK(tlv->parse(msg) == payload);
}

TEST_CASE("BERTLVParser - multi-byte tag, long-form length roundtrip", "[tlv][ber][roundtrip]") {
    auto tlv = std::make_shared<BERTLVParser>();
    auto msg = std::make_shared<Message>();

    // Tag 0x1F22 (2 Bytes), Länge 130 (Long-Form: 0x81 0x82), 130 Datenbytes
    std::vector<uint8_t> data(130, 0x5A);
    std::vector<uint8_t> payload{ 0x1F, 0x22, 0x81, 0x82 };
    payload.insert(payload.end(), data.begin(), data.end());

    CHECK(tlv->unparse(msg, payload) == payload.size());

    auto se = msg->get<BinaryField>(0x1F22);
    REQUIRE(se != nullptr);
    CHECK(se->value() == data);
    CHECK(tlv->parse(msg) == payload);
}

TEST_CASE("BERTLVParser - multiple SEs, mixed tag/length forms", "[tlv][ber][roundtrip]") {
    auto tlv = std::make_shared<BERTLVParser>();
    auto msg = std::make_shared<Message>();

    // SE1: Tag 0x10 (1 Byte), Länge 2 (Short-Form)
    // SE2: Tag 0x1F05 (2 Bytes), Länge 129 (Long-Form, 1 Folgebyte)
    std::vector<uint8_t> se2_data(129, 0x07);
    std::vector<uint8_t> payload{ 0x10, 0x02, 0x01, 0x02 };
    payload.push_back(0x1F); payload.push_back(0x05);
    payload.push_back(0x81); payload.push_back(0x81); // Long-Form: 129 = 0x81
    payload.insert(payload.end(), se2_data.begin(), se2_data.end());

    CHECK(tlv->unparse(msg, payload) == payload.size());

    auto se1 = msg->get<BinaryField>(0x10);
    REQUIRE(se1 != nullptr);
    CHECK(se1->value() == std::vector<uint8_t>{0x01, 0x02});

    auto se2 = msg->get<BinaryField>(0x1F05);
    REQUIRE(se2 != nullptr);
    CHECK(se2->value() == se2_data);

    // Serialisierung liefert SEs sortiert nach Tag-Wert (0x10 < 0x1F05)
    CHECK(tlv->parse(msg) == payload);
}

TEST_CASE("BERTLVParser - truncated SE length is rejected without crash", "[tlv][ber][error]") {
    auto tlv = std::make_shared<BERTLVParser>();
    auto msg = std::make_shared<Message>();

    // Länge kündigt 10 Bytes an, Puffer liefert aber nur 2.
    // Wie beim bisherigen ISOTLVParser werden bei einem SE-Überlauf die
    // bereits gelesenen Tag+Length-Bytes (hier: 2) als konsumiert
    // zurückgegeben - das fehlerhafte SE selbst wird aber NICHT übernommen.
    std::vector<uint8_t> payload{ 0x10, 0x0A, 0x01, 0x02 };
    const auto consumed = tlv->unparse(msg, payload);
    CHECK(consumed == 2);
    CHECK(msg->get<BinaryField>(0x10) == nullptr);
}

// =============================================================================
// ISO8583_BERTLV: voller EMV-Tag-Bereich (int32_t-Keys)
// =============================================================================
//
// Nur relevant/kompiliert wenn die Bibliothek mit -DISO8583_BERTLV=ON (bzw.
// #define ISO8583_BERTLV) gebaut wurde - siehe include/iso8583/config.h.
// Dann ist TNG_KEY_TYPE = int32_t, und reale 2-Byte-EMV-Tags mit erstem Byte
// >= 0x80 (z.B. 9F26 = Application Cryptogram) passen als ISOMessage-Key,
// ohne die in den Tests oben beschriebene int16_t-Einschränkung.
#if defined(ISO8583_BERTLV)
TEST_CASE("BERTLVParser - real EMV tag 9F26 as message key (ISO8583_BERTLV)", "[tlv][ber][bertlv]") {
    auto tlv = std::make_shared<BERTLVParser>();
    auto msg = std::make_shared<Message>();

    // Tag 0x9F26 (2 Bytes, Mehrbyte-Form), Länge 8 (Short-Form), 8 Datenbytes
    // - der klassische EMV-"Application Cryptogram" (ARQC/TC/AAC).
    const std::vector<uint8_t> cryptogram{ 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
    std::vector<uint8_t> payload{ 0x9F, 0x26, 0x08 };
    payload.insert(payload.end(), cryptogram.begin(), cryptogram.end());

    CHECK(tlv->unparse(msg, payload) == payload.size());

    // 0x9F26 = 40742 - würde in int16_t (max. 32767) nicht passen.
    static_assert(0x9F26 > std::numeric_limits<int16_t>::max(),
        "Testannahme: 9F26 liegt außerhalb des int16_t-Bereichs");
    static_assert(sizeof(TNG_KEY_TYPE) >= sizeof(int32_t),
        "ISO8583_BERTLV sollte TNG_KEY_TYPE auf mind. int32_t erweitern");

    auto se = msg->get<BinaryField>(static_cast<TNG_KEY_TYPE>(0x9F26));
    REQUIRE(se != nullptr);
    CHECK(se->value() == cryptogram);
    CHECK(tlv->parse(msg) == payload);
}
#endif
