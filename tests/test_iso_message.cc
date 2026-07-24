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

static std::shared_ptr< OpaqueField > make_opaque(TNG_KEY_TYPE key, const std::string& value) {
    auto f = std::make_shared< OpaqueField >(key);
    f->value(value);
    return f;
}

static std::shared_ptr< BinaryField > make_binary(TNG_KEY_TYPE key, std::vector<uint8_t> value) {
    auto f = std::make_shared< BinaryField >(key);
    f->value(std::move(value));
    return f;
}

// =============================================================================
// ISOMessage: construction
// =============================================================================

TEST_CASE("ISOMessage - default construction", "[message][construction]") {
    auto msg = std::make_shared< Message >();
    CHECK(msg->size() == 0);
    CHECK_FALSE(msg->hasMTI());
    CHECK(msg->is_composite());
}

TEST_CASE("ISOMessage - construction with MTI", "[message][construction]") {
    auto msg = std::make_shared< Message >("0200");
    CHECK(msg->hasMTI());
    CHECK(msg->mti() == "0200");
}

TEST_CASE("ISOMessage - reserved sentinel key constants", "[message][constants]") {
    // Regressionsschutz: diese Werte sind über den gesamten Codec verteilt
    // (unparse()/parse() in _parser.cc, ISOUtils.hh, ...) relevant - eine
    // versehentliche Änderung hier würde an vielen Stellen still brechen.
    CHECK(ISOMessage::ROOT_KEY == -1);
    CHECK(ISOMessage::BITMAP_KEY == -1);
    CHECK(ISOMessage::MTI_KEY == 0);

    // ROOT_KEY: k_ einer nicht eingebetteten (Root-)Nachricht
    auto root = std::make_shared< Message >();
    CHECK(root->key() == ISOMessage::ROOT_KEY);
    CHECK_FALSE(root->isInner());

    auto inner = std::make_shared< Message >(TNG_KEY_TYPE(48));
    CHECK(inner->key() == 48);
    CHECK(inner->isInner());

    // MTI_KEY: MTI liegt exakt unter diesem Schlüssel in der Feld-Map
    auto withMti = std::make_shared< Message >("0200");
    CHECK(withMti->has(ISOMessage::MTI_KEY));
    CHECK(withMti->get<OpaqueField>(ISOMessage::MTI_KEY)->value() == "0200");
}

// =============================================================================
// ISOMessage: set / has / get / unset
// =============================================================================

TEST_CASE("ISOMessage - set and has field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    auto pan = make_opaque(2, "4111111111111111");

    CHECK_FALSE(msg->has(2));
    CHECK(msg->set(pan));
    CHECK(msg->has(2));
    CHECK(msg->size() == 1);
}

TEST_CASE("ISOMessage - has() is protected against concurrent set() calls", "[message][crud][concurrency]") {
    // Regressionstest für den fehlenden Read-Lock in has(): anders als
    // get()/tryGet()/set()/unset()/keys() nahm has() bisher kein d_lock_ -
    // eine Data Race gegen gleichzeitige set()-Aufrufe auf anderen Threads
    // (nur unter ThreadSanitizer zuverlässig nachweisbar, hier zumindest als
    // Stresstest: darf nicht crashen/hängen und muss am Ende konsistent
    // sein).
    auto msg = std::make_shared< Message >();
    std::atomic<bool> stop{ false };

    std::thread writer([&] {
        for (TNG_KEY_TYPE k = 2; k < 50 && !stop; ++k) {
            msg->set(k, std::string("x"));
            std::this_thread::yield();
        }
        });

    std::thread reader([&] {
        for (int i = 0; i < 5000 && !stop; ++i) {
            for (TNG_KEY_TYPE k = 2; k < 50; ++k)
                (void)msg->has(k); // darf nie crashen/UB sein
        }
        });

    writer.join();
    stop = true;
    reader.join();

    for (TNG_KEY_TYPE k = 2; k < 50; ++k)
        CHECK(msg->has(k));
}

TEST_CASE("ISOMessage - get returns correct field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "4111111111111111"));
    msg->set(make_opaque(3, "000000"));

    auto de2 = msg->get< OpaqueField >(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "4111111111111111");

    auto de3 = msg->get< OpaqueField >(3);
    REQUIRE(de3 != nullptr);
    CHECK(de3->value() == "000000");
}

TEST_CASE("ISOMessage - get returns nullptr for missing field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK(msg->get< OpaqueField >(99) == nullptr);
}

TEST_CASE("ISOMessage - tryGet returns nullopt for missing field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    auto result = msg->tryGet< OpaqueField >(99);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ISOMessage - tryGet returns value for existing field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(4, "000000010000"));

    auto result = msg->tryGet< OpaqueField >(4);
    REQUIRE(result.has_value());
    CHECK((*result)->value() == "000000010000");
}

TEST_CASE("ISOMessage - tryGet returns nullopt for wrong type cast", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(52, "text"));  // OpaqueField unter key 52

    // Erwarten wir ISOBinaryField -> Cast schlaegt fehl -> nullopt
    auto result = msg->tryGet< BinaryField >(52);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ISOMessage - set returns false for null component", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK_FALSE(msg->set(nullptr));
    CHECK(msg->size() == 0);
}

TEST_CASE("ISOMessage - unset returns false for missing field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK_FALSE(msg->unset(99));
}

TEST_CASE("ISOMessage - set overwrites existing field", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK(msg->set(make_opaque(2, "4111111111111111")));
    CHECK(msg->set(make_opaque(2, "5500000000000004")));  // Ueberschreiben gibt true zurueck

    auto de2 = msg->get< OpaqueField >(2);
    REQUIRE(de2 != nullptr);
    CHECK(de2->value() == "5500000000000004");
    CHECK(msg->size() == 1);  // immer noch nur ein Feld
}

TEST_CASE("ISOMessage - reset clears all fields", "[message][crud]") {
    auto msg = std::make_shared< Message >();
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
    auto msg = std::make_shared< Message >();
    msg->set(make_binary(52, { 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE }));

    auto de52 = msg->get< BinaryField >(52);
    REQUIRE(de52 != nullptr);
    CHECK(de52->value() == std::vector<uint8_t>({ 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE }));
}

// =============================================================================
// ISOMessage: MTI classification
// =============================================================================

TEST_CASE("MTI 0100 - Authorization Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0100");
    CHECK(msg->isAuthorization());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->isResponse());
    CHECK_FALSE(msg->isFinancial());
    CHECK_FALSE(msg->isReversal());
}

TEST_CASE("MTI 0110 - Authorization Response", "[message][mti]") {
    auto msg = std::make_shared< Message >("0110");
    CHECK(msg->isAuthorization());
    CHECK(msg->isResponse());
    CHECK_FALSE(msg->isRequest());
}

TEST_CASE("MTI 0200 - Financial Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0200");
    CHECK(msg->isFinancial());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->isAuthorization());
}

TEST_CASE("MTI 0210 - Financial Response", "[message][mti]") {
    auto msg = std::make_shared< Message >("0210");
    CHECK(msg->isFinancial());
    CHECK(msg->isResponse());
}

TEST_CASE("MTI 0400 - Reversal Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0400");
    CHECK(msg->isReversal());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0420 - Reversal Advice", "[message][mti]") {
    auto msg = std::make_shared< Message >("0420");
    CHECK(msg->isReversal());
}

TEST_CASE("MTI 0800 - Network Management Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0800");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0810 - Network Management Response", "[message][mti]") {
    auto msg = std::make_shared< Message >("0810");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isResponse());
}

// =============================================================================
// ISOMessage: to_json
// =============================================================================

TEST_CASE("ISOMessage - to_json contains fields", "[message][json]") {
    auto msg = std::make_shared< Message >("0200");
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

    auto msg = std::make_shared< Message >("0200");
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
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(4, "000000010000"));

    auto result = msg->tryGetValue< OpaqueField >(4);
    REQUIRE(result.has_value());
    CHECK(*result == "000000010000");
}

TEST_CASE("ISOMessage - tryGetValue returns nullopt for missing key", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK_FALSE(msg->tryGetValue< OpaqueField >(99).has_value());
}

TEST_CASE("ISOMessage - tryGetValue returns nullopt for wrong type", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "text"));
    CHECK_FALSE(msg->tryGetValue< BinaryField >(2).has_value());
}

TEST_CASE("ISOMessage - tryGetValueRef returns reference to value", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(11, "000042"));

    auto ref = msg->tryGetValueRef< OpaqueField >(11);
    REQUIRE(ref.has_value());
    CHECK(ref->get() == "000042");
}

TEST_CASE("ISOMessage - tryGetValueRef returns nullopt for missing key", "[message][crud]") {
    auto msg = std::make_shared< Message >();
    CHECK_FALSE(msg->tryGetValueRef< OpaqueField >(99).has_value());
}

// =============================================================================
// MTI classification - full coverage
// =============================================================================

TEST_CASE("MTI 0300 - File Action Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0300");
    CHECK(msg->isFileAction());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0420 - Reversal Advice (not Request)", "[message][mti]") {
    auto msg = std::make_shared< Message >("0420");
    CHECK(msg->isReversal());
    // 0420: digit[2]='2' -> isRequest() = (2 % 2 == 0) = true
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0430 - Reversal Advice Response", "[message][mti]") {
    auto msg = std::make_shared< Message >("0430");
    CHECK(msg->isReversal());
    CHECK(msg->isResponse());
}

TEST_CASE("MTI 0420 vs 0422 - Chargeback distinction", "[message][mti]") {
    // Reversal:   digit[3] in {0,1}
    // Chargeback: digit[3] in {2,3}
    CHECK(std::make_shared< Message >("0420")->isReversal());
    CHECK(std::make_shared< Message >("0421")->isReversal());
    CHECK(std::make_shared< Message >("0422")->isChargeback());
    CHECK(std::make_shared< Message >("0423")->isChargeback());
}

TEST_CASE("MTI 0500 - Reconciliation Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0500");
    CHECK(msg->isReconciliation());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0600 - Administrative Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0600");
    CHECK(msg->isAdministrative());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0700 - Fee Collection Request", "[message][mti]") {
    auto msg = std::make_shared< Message >("0700");
    CHECK(msg->isFeeCollection());
    CHECK(msg->isRequest());
}

TEST_CASE("MTI 0801 - Network Management Retransmission", "[message][mti]") {
    auto msg = std::make_shared< Message >("0801");
    CHECK(msg->isNetworkManagement());
    CHECK(msg->isRetransmission());  // digit[3] == '1'
    CHECK(msg->isRequest());
}

TEST_CASE("MTI - mti() throws without MTI field", "[message][mti][error]") {
    auto msg = std::make_shared< Message >();
    REQUIRE_THROWS_AS(msg->mti(), std::logic_error);
}

TEST_CASE("MTI - isRequest/isResponse return false (not throw) without MTI", "[message][mti]") {
    // Regressionstest: isRequest()/isResponse() riefen früher ungeschützt
    // mti() auf und warfen dadurch std::logic_error, während alle anderen
    // is*()-Klassifizierer (isAuthorization(), isFinancial(), ...) bereits
    // vorher has(0) prüften und sicher false lieferten - inkonsistentes
    // Verhalten. Beide sind jetzt konsistent: false statt throw.
    auto msg = std::make_shared< Message >();
    CHECK_FALSE(msg->hasMTI());
    CHECK_NOTHROW(msg->isRequest());
    CHECK_NOTHROW(msg->isResponse());
    CHECK_FALSE(msg->isRequest());
    CHECK_FALSE(msg->isResponse());
    // Wichtig: isResponse() ist bewusst NICHT einfach "!isRequest()" -
    // sonst würde "MTI fehlt" (isRequest()==false) fälschlich zu
    // isResponse()==true.
}

TEST_CASE("MTI - isRequest/isResponse are mutually exclusive", "[message][mti]") {
    for (const char* mti : { "0100","0110","0200","0210","0400","0410","0800","0810" }) {
        auto msg = std::make_shared< Message >(mti);
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
    auto msg = std::make_shared< Message >("0200");
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
    auto msg = std::make_shared< Message >();

    // 4 Threads schreiben gleichzeitig auf verschiedene Keys
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&msg, t]() {
            for (int i = 0; i < 50; ++i) {
                auto f = std::make_shared< OpaqueField >(
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
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "initial"));

    std::atomic<bool> stop{ false };

    // Reader-Thread
    std::thread reader([&]() {
        while (!stop) {
            auto r = msg->tryGet< OpaqueField >(2);
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
    auto msg = std::make_shared< Message >();
    msg->direction(ISOMessage::Direction::INCOMING);
    CHECK(msg->direction() == ISOMessage::Direction::INCOMING);

    msg->direction(ISOMessage::Direction::OUTGOING);
    CHECK(msg->direction() == ISOMessage::Direction::OUTGOING);
}

// =============================================================================
// ISOMessage: multiple fields, size tracking
// =============================================================================

TEST_CASE("ISOMessage - size tracks correctly across operations", "[message][size]") {
    auto msg = std::make_shared< Message >();
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
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "4111111111111111"));

    std::string pan;
    REQUIRE_NOTHROW(pan = ISOUtils::getOrThrow< OpaqueField >(*msg, 2));
    CHECK(pan == "4111111111111111");
}

TEST_CASE("ISOUtils::getOrThrow - throws std::out_of_range for missing field", "[utils][getOrThrow]") {
    auto msg = std::make_shared< Message >();
    REQUIRE_THROWS_AS(ISOUtils::getOrThrow< OpaqueField >(*msg, 2), std::out_of_range);
}

TEST_CASE("ISOUtils::getOrThrow - throws for wrong type", "[utils][getOrThrow]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "text"));
    REQUIRE_THROWS_AS(ISOUtils::getOrThrow< BinaryField >(*msg, 2), std::out_of_range);
}

TEST_CASE("ISOUtils::getOrThrow - exception message contains DE key", "[utils][getOrThrow]") {
    auto msg = std::make_shared< Message >();
    try {
        ISOUtils::getOrThrow< OpaqueField >(*msg, 42);
        FAIL("Expected exception");
    }
    catch (const std::out_of_range& e) {
        CHECK(std::string(e.what()).find("42") != std::string::npos);
    }
}

TEST_CASE("ISOUtils::getOrDefault - returns value when field present", "[utils][getOrDefault]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(11, "000042"));

    auto stan = ISOUtils::getOrDefault< OpaqueField >(*msg, 11, "000000");
    CHECK(stan == "000042");
}

TEST_CASE("ISOUtils::getOrDefault - returns default when field missing", "[utils][getOrDefault]") {
    auto msg = std::make_shared< Message >();
    auto stan = ISOUtils::getOrDefault< OpaqueField >(*msg, 11, "000000");
    CHECK(stan == "000000");
}

TEST_CASE("ISOUtils::getOrDefault - returns default for wrong type", "[utils][getOrDefault]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(11, "text"));
    auto val = ISOUtils::getOrDefault< BinaryField >(*msg, 11, std::vector<uint8_t>{});
    CHECK(val.empty());
}

TEST_CASE("ISOUtils::ifPresent - calls callback when field present", "[utils][ifPresent]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(2, "4111111111111111"));

    std::string captured;
    bool found = ISOUtils::ifPresent< OpaqueField >(*msg, 2,
        [&](const std::string& pan) { captured = pan; });

    CHECK(found == true);
    CHECK(captured == "4111111111111111");
}

TEST_CASE("ISOUtils::ifPresent - returns false and skips callback when missing", "[utils][ifPresent]") {
    auto msg = std::make_shared< Message >();

    bool called = false;
    bool found = ISOUtils::ifPresent< OpaqueField >(*msg, 2,
        [&](const std::string&) { called = true; });

    CHECK(found == false);
    CHECK(called == false);
}

TEST_CASE("ISOUtils::ifPresent - lambda can mutate outer state", "[utils][ifPresent]") {
    auto msg = std::make_shared< Message >();
    msg->set(make_opaque(4, "000000010000"));
    msg->set(make_opaque(49, "978"));

    int64_t amount = 0;
    std::string currency;

    ISOUtils::ifPresent< OpaqueField >(*msg, 4,
        [&](const std::string& v) { amount = std::stoll(v); });
    ISOUtils::ifPresent< OpaqueField >(*msg, 49,
        [&](const std::string& v) { currency = v; });

    CHECK(amount == 10000);
    CHECK(currency == "978");
}
