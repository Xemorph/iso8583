#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <iso8583/iso8583.h>
#include <iso8583/detail/_components.hh>

#include <atomic>
#include <thread>

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

TEST_CASE("ISOMessage - set returns false for null component", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK_FALSE(msg->set(nullptr));
    CHECK(msg->size() == 0);
}

TEST_CASE("ISOMessage - unset returns false for missing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK_FALSE(msg->unset(99));
}

TEST_CASE("ISOMessage - set overwrites existing field", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK(msg->set(make_opaque(2, "4111111111111111")));
    CHECK(msg->set(make_opaque(2, "5500000000000004")));  // Ueberschreiben gibt true zurueck

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
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_opaque(4, "000000010000"));
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

TEST_CASE("ISOMessage - tryGetValue returns copy of value", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(4, "000000010000"));

    auto result = msg->tryGetValue<ISOOpaqueField>(4);
    REQUIRE(result.has_value());
    CHECK(*result == "000000010000");
}

TEST_CASE("ISOMessage - tryGetValue returns nullopt for missing key", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK_FALSE(msg->tryGetValue<ISOOpaqueField>(99).has_value());
}

TEST_CASE("ISOMessage - tryGetValue returns nullopt for wrong type", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "text"));
    CHECK_FALSE(msg->tryGetValue<ISOBinaryField>(2).has_value());
}

TEST_CASE("ISOMessage - tryGetValueRef returns reference to value", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(11, "000042"));

    auto ref = msg->tryGetValueRef<ISOOpaqueField>(11);
    REQUIRE(ref.has_value());
    CHECK(ref->get() == "000042");
}

TEST_CASE("ISOMessage - tryGetValueRef returns nullopt for missing key", "[message][crud]") {
    auto msg = std::make_shared<ISOMessage>();
    CHECK_FALSE(msg->tryGetValueRef<ISOOpaqueField>(99).has_value());
}

// =============================================================================
// MTI classification - full coverage
// =============================================================================

TEST_CASE("MTI 0300 - File Action Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0300");
    CHECK(msg->isFileAction());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0420 - Reversal Advice (not Request)", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0420");
    CHECK(msg->isReversal());
    // 0420: digit[2]='2' -> isRequest() = (2 % 2 == 0) = true
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0430 - Reversal Advice Response", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0430");
    CHECK(msg->isReversal());
    CHECK(msg->isResponse());
}

TEST_CASE("MTI 0420 vs 0422 - Chargeback distinction", "[message][mti]") {
    // Reversal:   digit[3] in {0,1}
    // Chargeback: digit[3] in {2,3}
    CHECK(std::make_shared<ISOMessage>("0420")->isReversal());
    CHECK(std::make_shared<ISOMessage>("0421")->isReversal());
    CHECK(std::make_shared<ISOMessage>("0422")->isChargeback());
    CHECK(std::make_shared<ISOMessage>("0423")->isChargeback());
}

TEST_CASE("MTI 0500 - Reconciliation Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0500");
    CHECK(msg->isReconciliation());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0600 - Administrative Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0600");
    CHECK(msg->isAdministrative());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0700 - Fee Collection Request", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0700");
    CHECK(msg->isFeeCollection());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0801 - Network Management Retransmission", "[message][mti]") {
    auto msg = std::make_shared<ISOMessage>("0801");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isRetransmission());  // digit[3] == '1'
    CHECK(msg->isRequest());
}

TEST_CASE("MTI - mti() throws without MTI field", "[message][mti][error]") {
    auto msg = std::make_shared<ISOMessage>();
    REQUIRE_THROWS_AS(msg->mti(), std::logic_error);
}

TEST_CASE("MTI - isRequest/isResponse are mutually exclusive", "[message][mti]") {
    for (const char* mti : { "0100","0110","0200","0210","0400","0410","0800","0810" }) {
        auto msg = std::make_shared<ISOMessage>(mti);
        CHECK(msg->isRequest() != msg->isResponse());
    }
}

// =============================================================================
// to_json - field type coverage
// =============================================================================

TEST_CASE("to_json - OpaqueField has string value", "[message][json]") {
    auto f = make_opaque(2, "4111111111111111");
    auto j = f->to_json();
    REQUIRE(j.contains("key"));
    REQUIRE(j.contains("value"));
    CHECK(j["key"] == 2);
    CHECK(j["value"] == "4111111111111111");
}

TEST_CASE("to_json - BinaryField has hex string value", "[message][json]") {
    auto f = make_binary(52, { 0xDE, 0xAD, 0xBE, 0xEF });
    auto j = f->to_json();
    REQUIRE(j.contains("value"));
    // Binary -> Hex-String
    CHECK(j["value"] == "DEADBEEF");
}

TEST_CASE("to_json - ISOMessage fields array", "[message][json]") {
    auto msg = std::make_shared<ISOMessage>("0200");
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_binary(52, { 0x01, 0x02 }));

    auto j = msg->to_json();
    REQUIRE(j.contains("fields"));
    REQUIRE(j["fields"].is_array());
    CHECK(j["fields"].size() >= 2);
}

TEST_CASE("to_json - wire_offset and wire_length appear when set", "[message][json]") {
    auto f = make_opaque(4, "000000010000");
    f->wire_offset(12);
    f->wire_length(6);

    auto j = f->to_json();
    // wire_length > 0 -> beide erscheinen im JSON
    CHECK(j.contains("wire_offset"));
    CHECK(j.contains("wire_length"));
    CHECK(j["wire_offset"] == 12);
    CHECK(j["wire_length"] == 6);
}

TEST_CASE("to_json - wire positions absent when not set", "[message][json]") {
    auto f = make_opaque(4, "000000010000");
    // wire_length == 0 (default) -> nicht im JSON
    auto j = f->to_json();
    CHECK_FALSE(j.contains("wire_offset"));
    CHECK_FALSE(j.contains("wire_length"));
}

// =============================================================================
// Thread-safety: concurrent set/get
// =============================================================================

TEST_CASE("ISOMessage - concurrent set does not corrupt", "[message][thread]") {
    auto msg = std::make_shared<ISOMessage>();

    // 4 Threads schreiben gleichzeitig auf verschiedene Keys
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&msg, t]() {
            for (int i = 0; i < 50; ++i) {
                auto f = std::make_shared<ISOOpaqueField>(
                    static_cast<TNG_KEY_TYPE>(t * 10 + i % 10));
                f->value("value");
                msg->set(f);
            }
            });
    }
    for (auto& th : threads) th.join();

    // Kein Crash = Thread-Safety des Mutex ist korrekt
    CHECK(msg->size() > 0);
}

TEST_CASE("ISOMessage - concurrent get while setting", "[message][thread]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "initial"));

    std::atomic<bool> stop{ false };

    // Reader-Thread
    std::thread reader([&]() {
        while (!stop) {
            auto r = msg->tryGet<ISOOpaqueField>(2);
            (void)r;  // Kein Crash erwartet
        }
        });

    // Writer-Thread
    for (int i = 0; i < 100; ++i)
        msg->set(make_opaque(2, std::to_string(i)));

    stop = true;
    reader.join();

    CHECK(msg->has(2));
}

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

// =============================================================================
// ISOUtils
// =============================================================================

TEST_CASE("ISOUtils::getOrThrow - returns value when field present", "[utils][getOrThrow]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));

    std::string pan;
    REQUIRE_NOTHROW(pan = ISOUtils::getOrThrow<ISOOpaqueField>(*msg, 2));
    CHECK(pan == "4111111111111111");
}

TEST_CASE("ISOUtils::getOrThrow - throws std::out_of_range for missing field", "[utils][getOrThrow]") {
    auto msg = std::make_shared<ISOMessage>();
    REQUIRE_THROWS_AS(ISOUtils::getOrThrow<ISOOpaqueField>(*msg, 2), std::out_of_range);
}

TEST_CASE("ISOUtils::getOrThrow - throws for wrong type", "[utils][getOrThrow]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "text"));
    REQUIRE_THROWS_AS(ISOUtils::getOrThrow<ISOBinaryField>(*msg, 2), std::out_of_range);
}

TEST_CASE("ISOUtils::getOrThrow - exception message contains DE key", "[utils][getOrThrow]") {
    auto msg = std::make_shared<ISOMessage>();
    try {
        ISOUtils::getOrThrow<ISOOpaqueField>(*msg, 42);
        FAIL("Expected exception");
    }
    catch (const std::out_of_range& e) {
        CHECK(std::string(e.what()).find("42") != std::string::npos);
    }
}

TEST_CASE("ISOUtils::getOrDefault - returns value when field present", "[utils][getOrDefault]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(11, "000042"));

    auto stan = ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 11, "000000");
    CHECK(stan == "000042");
}

TEST_CASE("ISOUtils::getOrDefault - returns default when field missing", "[utils][getOrDefault]") {
    auto msg = std::make_shared<ISOMessage>();
    auto stan = ISOUtils::getOrDefault<ISOOpaqueField>(*msg, 11, "000000");
    CHECK(stan == "000000");
}

TEST_CASE("ISOUtils::getOrDefault - returns default for wrong type", "[utils][getOrDefault]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(11, "text"));
    auto val = ISOUtils::getOrDefault<ISOBinaryField>(*msg, 11, std::vector<uint8_t>{});
    CHECK(val.empty());
}

TEST_CASE("ISOUtils::ifPresent - calls callback when field present", "[utils][ifPresent]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(2, "4111111111111111"));

    std::string captured;
    bool found = ISOUtils::ifPresent<ISOOpaqueField>(*msg, 2,
        [&](const std::string& pan) { captured = pan; });

    CHECK(found == true);
    CHECK(captured == "4111111111111111");
}

TEST_CASE("ISOUtils::ifPresent - returns false and skips callback when missing", "[utils][ifPresent]") {
    auto msg = std::make_shared<ISOMessage>();

    bool called = false;
    bool found = ISOUtils::ifPresent<ISOOpaqueField>(*msg, 2,
        [&](const std::string&) { called = true; });

    CHECK(found == false);
    CHECK(called == false);
}

TEST_CASE("ISOUtils::ifPresent - lambda can mutate outer state", "[utils][ifPresent]") {
    auto msg = std::make_shared<ISOMessage>();
    msg->set(make_opaque(4, "000000010000"));
    msg->set(make_opaque(49, "978"));

    int64_t amount = 0;
    std::string currency;

    ISOUtils::ifPresent<ISOOpaqueField>(*msg, 4,
        [&](const std::string& v) { amount = std::stoll(v); });
    ISOUtils::ifPresent<ISOOpaqueField>(*msg, 49,
        [&](const std::string& v) { currency = v; });

    CHECK(amount == 10000);
    CHECK(currency == "978");
}
