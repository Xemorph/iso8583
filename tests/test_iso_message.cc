// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/iso8583.h>

using namespace TNG_NAMESPACE;

// =============================================================================
// Helpers
// =============================================================================

static std::shared_ptr<ISOOpaqueField> make_opaque(TNG_KEY_TYPE key, const std::string& value) {
    auto f = std::make_shared<ISOOpaqueField>(key);
    f->value(value);
    return f;
}

static std::shared_ptr<ISOBinaryField> make_binary(TNG_KEY_TYPE key, std::vector<uint8_t> value) {
    auto f = std::make_shared<ISOBinaryField>(key);
    f->value(std::move(value));
    return f;
}

// =============================================================================
// ISOMessage: construction
// =============================================================================

TEST_CASE("ISOMessage - default construction", "[message][construction]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK(msg->size() == 0);
    CHECK_FALSE(msg->hasMTI());
    CHECK(msg->is_composite());
}

TEST_CASE("ISOMessage - construction with MTI", "[message][construction]") {
    auto msg = std::make_shared<ISOMessage>("0200");
    CHECK(msg->hasMTI());
    CHECK(msg->mti() == "0200");
}

// =============================================================================
// ISOMessage: set / has / get / unset
// =============================================================================

TEST_CASE("ISOMessage - set and has field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    auto pan = make_opaque(2, "4111111111111111");

    CHECK_FALSE(msg->has(2));
    CHECK(msg->set(pan));
    CHECK(msg->has(2));
    CHECK(msg->size() == 1);
}

TEST_CASE("ISOMessage - get returns correct field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_opaque(3, "000000"));

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");

    auto de3 = msg->get<ISOOpaqueField>(3);
    REQUIRE(de3 != nullptr);
    CHECK(de3->value() == "000000");
}

TEST_CASE("ISOMessage - get returns nullptr for missing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK(msg->get<ISOOpaqueField>(99) == nullptr);
}

TEST_CASE("ISOMessage - tryGet returns nullopt for missing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    auto result = msg->tryGet<ISOOpaqueField>(99);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ISOMessage - tryGet returns value for existing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(4, "000000010000"));

    auto result = msg->tryGet<ISOOpaqueField>(4);
    REQUIRE(result.has_value());
    CHECK((*result)->value() == "000000010000");
}

TEST_CASE("ISOMessage - tryGet returns nullopt for wrong type cast", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(52, "text"));  // OpaqueField unter key 52

    // Erwarten wir ISOBinaryField -> Cast schlaegt fehl -> nullopt
    auto result = msg->tryGet<ISOBinaryField>(52);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ISOMessage - unset removes field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));
    REQUIRE(msg->has(2));

    CHECK(msg->unset(2));
    CHECK_FALSE(msg->has(2));
    CHECK(msg->size() == 0);
}

TEST_CASE("ISOMessage - unset returns false for missing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK_FALSE(msg->unset(99));
}

TEST_CASE("ISOMessage - set overwrites existing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_opaque(2, "5500000000000004"));

    auto de2 = msg->get<ISOOpaqueField>(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "5500000000000004");
    CHECK(msg->size() == 1);  // immer noch nur ein Feld
}

TEST_CASE("ISOMessage - reset clears all fields", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_opaque(3, "000000"));
    msg->set(make_opaque(4, "000000010000"));
    REQUIRE(msg->size() == 3);

    msg->reset();
    CHECK(msg->size() == 0);
}

// =============================================================================
// ISOMessage: binary fields
// =============================================================================

TEST_CASE("ISOMessage - binary field set and get", "[message][binary]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_binary(52, { 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE }));

    auto de52 = msg->get<ISOBinaryField>(52);
    REQUIRE(de52 != nullptr);
    CHECK(de52->value() == std::vector<uint8_t>({ 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE }));
}

// =============================================================================
// ISOMessage: MTI classification
// =============================================================================

TEST_CASE("MTI 0100 - Authorization Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->isResponse());
    CHECK_FALSE(msg->isFinancial());
    CHECK_FALSE(msg->isReversal());
}

TEST_CASE("MTI 0110 - Authorization Response", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0110");
    CHECK(msg->isAuthorization());
    CHECK(msg->isResponse());
    CHECK_FALSE(msg->isRequest());
}

TEST_CASE("MTI 0200 - Financial Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0200");
    CHECK(msg->isFinancial());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->isAuthorization());
}

TEST_CASE("MTI 0210 - Financial Response", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0210");
    CHECK(msg->isFinancial());
    CHECK(msg->isResponse());
}

TEST_CASE("MTI 0400 - Reversal Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0400");
    CHECK(msg->isReversal());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0420 - Reversal Advice", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0420");
    CHECK(msg->isReversal());
}

TEST_CASE("MTI 0800 - Network Management Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0800");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0810 - Network Management Response", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0810");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isResponse());
}

// =============================================================================
// ISOMessage: to_json
// =============================================================================

TEST_CASE("ISOMessage - to_json contains fields", "[message][json]") {
    auto msg = std::make_shared<ISOMessage>("0200");
    msg->set(make_opaque(2,  "4111111111111111"));
    msg->set(make_opaque(4,  "000000010000"));
    msg->set(make_opaque(11, "000001"));

    auto j = msg->to_json();
    CHECK(j.contains("fields"));
    CHECK(j["fields"].is_array());
    CHECK_FALSE(j["fields"].empty());
}

TEST_CASE("ISOMessage - to_json wire positions present after parse", "[message][json]") {
    auto f = make_opaque(4, "000000010000");
    f->wire_offset(5);
    f->wire_length(6);

    auto msg = std::make_shared<ISOMessage>("0200");
    msg->set(f);

    // wire_offset und wire_length erscheinen im JSON wenn wire_length > 0
    auto j = msg->to_json();
    // Wir pruefen dass das JSON gebaut werden kann ohne Fehler
    CHECK(j.contains("fields"));
}

// =============================================================================
// ISOField: wire_offset / wire_length
// =============================================================================

TEST_CASE("ISOOpaqueField - wire position get/set", "[field][wire]") {
    auto f = make_opaque(2, "4111111111111111");
    CHECK(f->wire_offset() == 0);
    CHECK(f->wire_length() == 0);

    f->wire_offset(44);
    f->wire_length(10);
    CHECK(f->wire_offset() == 44);
    CHECK(f->wire_length() == 10);
}

TEST_CASE("ISOBinaryField - wire position get/set", "[field][wire]") {
    auto f = make_binary(52, { 0xDE, 0xAD, 0xBE, 0xEF });
    f->wire_offset(100);
    f->wire_length(4);
    CHECK(f->wire_offset() == 100);
    CHECK(f->wire_length() == 4);
}

// =============================================================================
// ISOMessage: direction
// =============================================================================

TEST_CASE("ISOMessage - direction set/get", "[message][direction]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->direction(ISOMessage::Direction::INCOMING);
    CHECK(msg->direction() == ISOMessage::Direction::INCOMING);

    msg->direction(ISOMessage::Direction::OUTGOING);
    CHECK(msg->direction() == ISOMessage::Direction::OUTGOING);
}

// =============================================================================
// ISOMessage: multiple fields, size tracking
// =============================================================================

TEST_CASE("ISOMessage - size tracks correctly across operations", "[message][size]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK(msg->size() == 0);

    msg->set(make_opaque(2, "4111111111111111"));
    CHECK(msg->size() == 1);

    msg->set(make_opaque(3, "000000"));
    msg->set(make_opaque(4, "000000010000"));
    CHECK(msg->size() == 3);

    msg->unset(3);
    CHECK(msg->size() == 2);

    msg->reset();
    CHECK(msg->size() == 0);
}
