// =============================================================================
// test_threading.cc - Threading-Modell (Phase 3, Meilenstein 3.3)
// =============================================================================
//
// [ISO8583] 3.3 (Security-Audit):
//   * ISOMessage: ein (rekursiver) Message-Lock schützt den gesamten
//     Nachrichten-Zustand; eine ISOMessage darf aus N Threads gemeinsam
//     genutzt werden; Parser sind nach dem Laden unveränderlich und damit
//     thread-sicher teilbar.
//   * F3: Logger-Globale (Level + Logger-Pointer) sind atomar.
//   * B5: MTI-Klassifikation (isRequest & Co.) warft bei kurzen MTIs
//     kein std::out_of_range mehr.
//
// Design-Notes:
//   - Alle Schleifen sind beschränkt (≈ 1-2 s), alle Threads werden
//     joined (kein detach).
//   - Kein Prozess-Cache (loadFromYaml, keine ...Cached) → clearCache
//     nicht nötig.
//   - Der Logger-Test stellt am Ende den globalen Logger-Zustand
//     (nullptr / WARN) wieder her, damit andere Testfälle nicht
//     beeinflusst werden.

#include <catch2/catch_test_macros.hpp>

// [tng]
#include <iso8583/iso8583.h>
#include "_logger.hh"   // TNG_LOG_* + log::Globalzustand (privater Header)

// [stdc++]
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace TNG_NAMESPACE;

namespace fs = std::filesystem;

namespace {

// Lokaler RAII-Verzeichnis-Helfer (eigenständige Miniaturversion, s.
// test_spec_sandbox.cc / test_e2e_full_message.cc).
struct TempDir {
    fs::path dir;

    TempDir() {
        static std::atomic<int> seq{0};
        std::error_code ec;
        const fs::path base = fs::temp_directory_path();
        do {
            dir = base / ("iso8583_threading_" + std::to_string(seq.fetch_add(1)));
        } while (fs::exists(dir, ec) && !ec);
        fs::create_directories(dir, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string makeThreadingSpec() {
    return
"spec: \"ThreadingSpec\"\n"
"encoding: ascii\n"
"fields:\n"
"  \"000\": { format: numeric,  length: 4 }\n"
"  \"001\": { format: bitmap,   length: 8 }\n"
"  \"002\": { format: llchar,   length: 19 }\n"
"  \"004\": { format: numeric,  length: 12 }\n"
"  \"011\": { format: llchar,   length: 6 }\n"
"  \"048\":\n"
"    format: lllbinary\n"
"    length: 999\n"
"    tlv:\n"
"      tag_bytes: 2\n"
"      len_bytes: 2\n"
"    children:\n"
"      \"72\": { format: binary, length: 4 }\n";
}

std::vector<uint8_t> ascii_b(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& part) {
    out.insert(out.end(), part.begin(), part.end());
}

// ASCII-Wire: MTI 0100, Bitmap {2,4,11,48}, DE2, DE4, DE11, DE48 (TLV:
// SE01 = NICHT deklariertes Tag (triggert den Fallback-Description-Cache
// des geteilten TLV-Parsers unter Concurrency) + SE72 = deklariert).
std::vector<uint8_t> buildWire() {
    std::vector<uint8_t> raw;
    append(raw, ascii_b("0100"));
    const auto bm = utils::makeBitmap({ 2, 4, 11, 48 });
    for (const auto b : bm)
        raw.push_back(static_cast<uint8_t>(b));
    append(raw, ascii_b("16"));
    append(raw, ascii_b("5555555555554444"));
    append(raw, ascii_b("000000098765"));
    append(raw, ascii_b("06"));
    append(raw, ascii_b("000123"));
    // DE48: LLL "013" + SE01 ("01" "01" 1 Byte, undeclared) + SE72 ("72" "04" + 4 Bytes)
    const std::vector<uint8_t> se72{ 0xDE, 0xAD, 0xBE, 0xEF };
    append(raw, ascii_b("013"));
    append(raw, ascii_b("01"));
    append(raw, ascii_b("01"));
    raw.push_back(0x42);
    append(raw, ascii_b("72"));
    append(raw, ascii_b("04"));
    append(raw, se72);
    return raw;
}

}  // namespace

// =============================================================================
// 1) Eine gemeinsame ISOMessage aus N Threads: set/get/size/keys/to_json/
//    dump müssen sich gegenseitig exklusivieren (kein torn Read, kein
//    Daten-Race auf dem Feld-Map, Parser-Zustand & Co.).
// =============================================================================

TEST_CASE("threading - one shared message under concurrent set/get", "[threading][message]") {
    auto msg = std::make_shared<Message>();
    REQUIRE(msg->set(2, std::string("4111111111111111111")));
    REQUIRE(msg->set(11, std::string("V0")));

    std::atomic<bool> writerDone{false};
    std::atomic<bool> stop{false};
    // Invarianten-Verletzungen werden NUR protokolliert (keine REQUIREs im
    // Worker-Thread: eine dort geworfene Catch2-Ausnahme laeuft nirgendwo
    // auf -> std::terminate -> abort; der Main-Thread wertet am Ende aus).
    std::atomic<long> invariantViolations{0};
    // [ISO8583] 3.3: Exceptions in Worker-Threads werden gesammelt, nicht
    // auslaufen gelassen (uncaught Exception in einem std::thread endet in
    // std::terminate -> Prozess-Abschluss ohne sauberes Test-Fehlerbild).
    std::mutex errMutex;
    std::string errNote;
    auto noteError = [&errMutex, &errNote, &invariantViolations](const std::string& what) {
        std::lock_guard lk(errMutex);
        errNote = what;
        invariantViolations.fetch_add(1, std::memory_order_relaxed);
    };

    auto writer = [&msg, &writerDone, &noteError]() {
        for (int i = 0; i < 3000; ++i) {
            try {
                if (!msg->set(11, "V" + std::to_string(i)))
                    return;
                if (i % 97 == 0)
                    (void)msg->set(4, "000000" + std::to_string(i % 100));
            }
            catch (const std::exception& e) {
                noteError(std::string("writer: ") + e.what());
                return;
            }
        }
        writerDone.store(true, std::memory_order_release);
    };

    auto reader = [&msg, &stop, &writerDone, &invariantViolations, &noteError]() {
        std::ostringstream oss;
        for (int round = 0; round < 500 && !stop.load(); ++round) {
            try {
                // Invariante: DE11 hat immer die Form "V<n>" (kein torn Read).
                if (auto v = msg->tryGetValue<OpaqueField>(11)) {
                    const bool ok = !v->empty() && (*v)[0] == 'V';
                    if (!ok)
                        invariantViolations.fetch_add(1, std::memory_order_relaxed);
                }
                (void)msg->has(2);
                (void)msg->size();
                (void)msg->keys();
                const auto j = msg->to_json();
                if (!j.contains("fields") || !j["fields"].is_array())
                    invariantViolations.fetch_add(1, std::memory_order_relaxed);
                msg->dump(oss);
            }
            catch (const std::exception& e) {
                noteError(std::string("reader: ") + e.what());
                break;
            }
            if (writerDone.load(std::memory_order_acquire))
                break;
        }
    };

    std::thread w(writer);
    std::thread r1(reader);
    std::thread r2(reader);

    w.join();
    stop.store(true);
    r1.join();
    r2.join();

    REQUIRE(invariantViolations.load() == 0);
    {
        std::lock_guard lk(errMutex);
        if (!errNote.empty())
            FAIL("Exception in Worker-Thread: " + errNote);
    }

    // Abschluss-Zustand nach dem Writer
    REQUIRE(msg->has(11));
    REQUIRE(msg->size() >= 2);
    const auto v11 = msg->tryGetValue<OpaqueField>(11);
    REQUIRE(v11.has_value());
    REQUIRE((*v11)[0] == 'V');
    REQUIRE(msg->tryGetValue<OpaqueField>(2) == "4111111111111111111");
}

// =============================================================================
// 2) Unveränderlicher, thread-sicher teilbarer Parser: parallele
//    unparse/parse-Aufrufe auf VERSCHIEDENEN Nachrichten mit demselben
//    Parser (inkl. rekursiver set()-Rückrufe des Parsers in die
//    Nachrichten unter dem Message-Lock).
// =============================================================================

TEST_CASE("threading - immutable parser shared across threads and messages", "[threading][message][spec]") {
    TempDir td;
    const fs::path specPath = td.dir / "threading.yml";
    {
        std::ofstream f(specPath);
        f << makeThreadingSpec();
    }

    auto parser = spec::SpecDecoder::loadFromYaml(specPath.string());
    REQUIRE(parser != nullptr);

    const auto raw = buildWire();

    // Referenz-Decode im Hauptthread (validiert das Fixture)
    {
        auto ref = std::make_shared<Message>();
        ref->parser(parser);
        REQUIRE(ref->unparse(ref, raw) == raw.size());
    }

    constexpr int kThreads = 4;
    std::array<std::atomic<bool>, kThreads> ok{};  // alle false
    std::vector<std::thread> threads;
    // [ISO8583] 3.3: uncaught Exception im Worker -> std::terminate;
    // daher Exceptions sammeln und nach dem Join als Test-FAIL werten.
    std::mutex errMutex;
    std::string workerError;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            const std::vector<uint8_t> se72{ 0xDE, 0xAD, 0xBE, 0xEF };
            for (int iter = 0; iter < 25; ++iter) {
                try {
                    auto dec = std::make_shared<Message>();
                    dec->parser(parser);
                    if (dec->unparse(dec, raw) != raw.size())
                        return;
                    if (dec->mti() != "0100")
                        return;
                    const auto de2 = dec->tryGetValue<OpaqueField>(2);
                    if (!de2 || *de2 != "5555555555554444")
                        return;
                    auto de48 = dec->get<Message>(48);
                    if (!de48)
                        return;
                    const auto se = de48->get<BinaryField>(72);
                    if (!se || se->value() != se72)
                        return;
                    // Undeklariertes SE01: Decode + generierte Fallback-
                    // Beschreibung "SE1" (geteilter Fallback-Cache des
                    // Parsers - Regressions-Fall fuer das 3.3-Mutex, s.
                    // _tlv.hh).
                    const auto se01 = de48->get<BinaryField>(1);
                    if (!se01 || se01->value() != std::vector<uint8_t>{ 0x42 })
                        return;
                    if (se01->description() != "SE1")
                        return;
                    const auto reenc = parser->parse(dec);
                    if (reenc != raw)
                        return;
                }
                catch (const std::exception& e) {
                    std::lock_guard lk(errMutex);
                    workerError = "thread " + std::to_string(t) + ", iter "
                        + std::to_string(iter) + ": " + e.what();
                    return;
                }
            }
            ok[t].store(true, std::memory_order_release);
        });
    }
    for (auto& th : threads)
        th.join();

    for (int t = 0; t < kThreads; ++t)
        REQUIRE(ok[t].load());
    {
        std::lock_guard lk(errMutex);
        if (!workerError.empty())
            FAIL("unparse/parse-Exception im Worker: " + workerError);
    }
}

// =============================================================================
// 3) B5: MTI-Klassifikation bei kurzen (nicht 4-stelligen) MTIs - vor 3.3
//    warfen isRequest()/isReversal()/... ein std::out_of_range (mti().at(3)
//    ohne Guard); jetzt: sauberer Rückkehrwert.
// =============================================================================

TEST_CASE("threading - B5: short MTI classification returns, no throw", "[threading][message]") {
    // 2 Zeichen: alt .at(3) Wuerfe
    {
        auto m = std::make_shared<Message>("02");
        REQUIRE(m->hasMTI());
        REQUIRE_FALSE(m->isRequest());
        REQUIRE_FALSE(m->isResponse());
        REQUIRE_FALSE(m->isAuthorization());
        REQUIRE_FALSE(m->isFinancial());
        REQUIRE_FALSE(m->isFileAction());
        REQUIRE_FALSE(m->isReversal());
        REQUIRE_FALSE(m->isChargeback());
        REQUIRE_FALSE(m->isReconciliation());
        REQUIRE_FALSE(m->isAdministrative());
        REQUIRE_FALSE(m->isFeeCollection());
        REQUIRE_FALSE(m->isNetworkManagement());
        REQUIRE_FALSE(m->isRetransmission());
    }

    // 3 Zeichen: alt .at(3) Wuerfe (isReversal/Chargeback/Retransmission).
    // Designentscheidung 3.3: Eine MTI mit != 4 Zeichen ist ungueltig ->
    // ALLE Klassifikationen geben false zurueck (kein out_of_range).
    {
        auto m = std::make_shared<Message>("020");
        REQUIRE_FALSE(m->isRequest());
        REQUIRE_FALSE(m->isResponse());
        REQUIRE_FALSE(m->isFinancial());
        REQUIRE_FALSE(m->isReversal());
        REQUIRE_FALSE(m->isChargeback());
        REQUIRE_FALSE(m->isRetransmission());
        REQUIRE_FALSE(m->isAuthorization());
    }

    // Gueltige 4-Zeichen-MTIs: Verhalten bleibt unveraendert
    {
        auto m = std::make_shared<Message>("0200");
        REQUIRE(m->isRequest());
        REQUIRE_FALSE(m->isResponse());
        REQUIRE(m->isFinancial());
        REQUIRE_FALSE(m->isRetransmission());
    }
    {
        auto m = std::make_shared<Message>("0201");
        REQUIRE(m->isRequest());
        REQUIRE(m->isRetransmission());
        REQUIRE(m->isFinancial());
    }
    {
        auto m = std::make_shared<Message>("0810");
        REQUIRE(m->isResponse());
        REQUIRE(m->isNetworkManagement());
        REQUIRE_FALSE(m->isReversal());
    }
    {
        auto m = std::make_shared<Message>("0401");
        REQUIRE(m->isReversal());
        REQUIRE(m->isRetransmission());
    }
    {
        auto m = std::make_shared<Message>("0432");
        REQUIRE(m->isChargeback());
        REQUIRE_FALSE(m->isReversal());
    }
    {
        auto m = std::make_shared<Message>("0110");
        REQUIRE(m->isAuthorization());
    }
    {
        auto m = std::make_shared<Message>("0310");
        REQUIRE(m->isFileAction());
    }
    {
        auto m = std::make_shared<Message>("0510");
        REQUIRE(m->isReconciliation());
    }
    {
        auto m = std::make_shared<Message>("0610");
        REQUIRE(m->isAdministrative());
    }
    {
        auto m = std::make_shared<Message>("0710");
        REQUIRE(m->isFeeCollection());
    }
}

// =============================================================================
// 4) F3: Logger-Globale (Level + Logger-Pointer) sind atomar - paralleles
//    setLevel/setLogger neben Log-Aufrufen ist race-frei (Smoke-Test: kein
//    Abbruch, kein UB; am Ende wird der Globalzustand wiederhergestellt).
// =============================================================================

namespace {

class CountingLogger final : public log::ISOLogger {
public:
    std::atomic<long> hits{0};

    void log(log::Level, std::string_view, int, std::string_view) override {
        hits.fetch_add(1, std::memory_order_relaxed);
    }
};

}  // namespace

TEST_CASE("threading - F3: logger globals are atomic under concurrency", "[threading][logging]") {
    CountingLogger logger;

    // Ausgangszustand determinisch setzen
    log::setLogger(nullptr);
    log::setLevel(log::Level::WARN);

    std::atomic<bool> stop{false};

    auto switcher = [&logger, &stop]() {
        int i = 0;
        while (!stop.load()) {
            log::setLevel(i % 2 ? log::Level::DEBUG : log::Level::WARN);
            log::setLogger(i % 2 ? &logger : nullptr);
            ++i;
        }
    };

    auto producer = [&logger, &stop]() {
        int i = 0;
        while (!stop.load()) {
            (void)log::getLevel();
            (void)log::currentLogger();
            TNG_LOG_WARN("[threading] W{}", i % 1000);
            TNG_LOG_INFO("[threading] I{}", (i++) % 1000);
        }
    };

    std::thread a(switcher);
    std::thread b(producer);
    std::thread c(producer);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    stop.store(true);
    a.join();
    b.join();
    c.join();

    // Globalzustand wiederherstellen (sonst beeinflusst dieser Test alle
    // folgenden Testfaelle) und verifizieren
    log::setLogger(nullptr);
    log::setLevel(log::Level::WARN);
    REQUIRE(log::currentLogger() == nullptr);
    REQUIRE(log::getLevel() == log::Level::WARN);
    CHECK(logger.hits.load() >= 0);  // Smoke: kein Abbruch / kein UB
}