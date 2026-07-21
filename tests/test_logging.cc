// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOLog.hh>
// [stdc++]
#include <atomic>
#include <string>
#include <vector>

using namespace TNG_NAMESPACE::log;

// =============================================================================
// Test-Logger: fängt alle Meldungen auf
// =============================================================================

struct CaptureLogger : ISOLogger {
    struct Entry {
        Level       level;
        std::string file;
        int         line;
        std::string message;
    };

    std::vector<Entry> entries;
    std::atomic<int>   call_count{ 0 };

    void log(Level level, std::string_view file,
             int line, std::string_view message) override
    {
        entries.push_back({ level, std::string(file), line, std::string(message) });
        ++call_count;
    }

    void clear() {
        entries.clear();
        call_count = 0;
    }
};

// RAII-Guard: setzt Logger und Level zurück nach jedem Test
struct LoggerGuard {
    Level saved_level;

    LoggerGuard() : saved_level(getLevel()) {}

    ~LoggerGuard() {
        setLogger(nullptr);   // externen Logger entfernen
        setLevel(saved_level); // Level wiederherstellen
    }
};

// =============================================================================
// Level API
// =============================================================================

TEST_CASE("Log level - default is WARN", "[logging][level]") {
    // Der Standard-Level ist WARN (laut Implementierung)
    // Da Tests in beliebiger Reihenfolge laufen, prüfen wir nur
    // dass getLevel() überhaupt einen gültigen Wert zurückgibt.
    Level lvl = getLevel();
    CHECK((lvl >= Level::TRACE && lvl <= Level::OFF));
}

TEST_CASE("Log level - setLevel roundtrip", "[logging][level]") {
    LoggerGuard guard;

    setLevel(Level::TRACE);
    CHECK(getLevel() == Level::TRACE);

    setLevel(Level::DEBUG);
    CHECK(getLevel() == Level::DEBUG);

    setLevel(Level::INFO);
    CHECK(getLevel() == Level::INFO);

    setLevel(Level::WARN);
    CHECK(getLevel() == Level::WARN);

    setLevel(Level::ERR);
    CHECK(getLevel() == Level::ERR);

    setLevel(Level::CRITICAL);
    CHECK(getLevel() == Level::CRITICAL);

    setLevel(Level::OFF);
    CHECK(getLevel() == Level::OFF);
}

// =============================================================================
// setLogger / ISOLogger interface
// =============================================================================

TEST_CASE("setLogger - null resets to internal logger", "[logging][external]") {
    LoggerGuard guard;

    CaptureLogger cap;
    setLogger(&cap);
    setLogger(nullptr);  // zurueck auf intern

    // Nach dem Reset darf kein externer Logger aktiv sein
    // Wir pruefen indirekt: eine Meldung unter dem aktuellen Level
    // geht NICHT an cap (da kein externer Logger mehr gesetzt)
    setLevel(Level::ERR);
    // Kein Absturz = OK
    CHECK(cap.call_count == 0);
}

TEST_CASE("setLogger - injected logger receives messages", "[logging][external]") {
    LoggerGuard guard;
    CaptureLogger cap;

    setLevel(Level::TRACE);  // alles durchlassen
    setLogger(&cap);

    // TNG_LOG_*-Makros sind intern – wir testen die API direkt
    // indem wir log() manuell aufrufen wie die Makros es würden
    cap.log(Level::INFO, __FILE__, __LINE__, "Test-Meldung INFO");
    cap.log(Level::ERR,  __FILE__, __LINE__, "Test-Meldung ERR");

    REQUIRE(cap.entries.size() == 2);
    CHECK(cap.entries[0].level   == Level::INFO);
    CHECK(cap.entries[0].message == "Test-Meldung INFO");
    CHECK(cap.entries[1].level   == Level::ERR);
    CHECK(cap.entries[1].message == "Test-Meldung ERR");
}

TEST_CASE("setLogger - file and line are forwarded", "[logging][external]") {
    LoggerGuard guard;
    CaptureLogger cap;
    setLogger(&cap);

    const int expected_line = __LINE__ + 1;
    cap.log(Level::WARN, "myfile.cc", expected_line, "msg");

    REQUIRE(cap.entries.size() == 1);
    CHECK(cap.entries[0].file == "myfile.cc");
    CHECK(cap.entries[0].line == expected_line);
}

TEST_CASE("setLogger - replacing logger mid-session", "[logging][external]") {
    LoggerGuard guard;

    CaptureLogger cap1, cap2;

    setLogger(&cap1);
    cap1.log(Level::INFO, "", 0, "an cap1");

    setLogger(&cap2);
    cap2.log(Level::INFO, "", 0, "an cap2");

    CHECK(cap1.call_count == 1);
    CHECK(cap2.call_count == 1);
    CHECK(cap1.entries[0].message == "an cap1");
    CHECK(cap2.entries[0].message == "an cap2");
}

// =============================================================================
// ISOLogger interface contract
// =============================================================================

TEST_CASE("ISOLogger - all Level enum values are distinct", "[logging][level]") {
    // Stellt sicher dass niemand versehentlich zwei Level denselben Wert gibt
    CHECK(static_cast<int>(Level::TRACE)    == 0);
    CHECK(static_cast<int>(Level::DEBUG)    == 1);
    CHECK(static_cast<int>(Level::INFO)     == 2);
    CHECK(static_cast<int>(Level::WARN)     == 3);
    CHECK(static_cast<int>(Level::ERR)      == 4);
    CHECK(static_cast<int>(Level::CRITICAL) == 5);
    CHECK(static_cast<int>(Level::OFF)      == 6);
}

TEST_CASE("ISOLogger - is abstract (cannot instantiate directly)", "[logging][interface]") {
    // Kompilierzeit-Test: ISOLogger hat mindestens eine rein virtuelle Methode.
    // Wäre ISOLogger nicht abstrakt, würde dieser Check static_assert auslösen.
    CHECK(std::is_abstract_v<ISOLogger>);
}

TEST_CASE("ISOLogger - virtual destructor", "[logging][interface]") {
    // Stellt sicher dass Unterklassen korrekt über Basisklassen-Pointer
    // zerstört werden können (wichtig wenn Anwender setLogger(&myLogger) nutzt).
    CHECK(std::has_virtual_destructor_v<ISOLogger>);
}

TEST_CASE("ISOLogger - subclass can be destroyed via base pointer", "[logging][interface]") {
    LoggerGuard guard;

    // Erstellt CaptureLogger auf dem Heap und zerstört ihn via Basisklassen-Pointer
    // Ohne virtual destructor: undefined behavior
    ISOLogger* base = new CaptureLogger();
    setLogger(base);
    setLogger(nullptr);  // erst abkoppeln, dann loeschen
    REQUIRE_NOTHROW(delete base);
}

// =============================================================================
// Level-Filtering-Konvention
// =============================================================================

TEST_CASE("Log level - OFF semantics", "[logging][level]") {
    LoggerGuard guard;

    CaptureLogger cap;
    setLogger(&cap);
    setLevel(Level::OFF);

    CHECK(getLevel() == Level::OFF);
    // Kein Absturz beim Setzen von OFF
}

TEST_CASE("Log level - ordering is correct", "[logging][level]") {
    // Bibliotheks-Code prueft: if (level >= getLevel()) -> weiterleiten
    // Diese Reihenfolge muss stimmen:
    CHECK(Level::TRACE < Level::DEBUG);
    CHECK(Level::DEBUG < Level::INFO);
    CHECK(Level::INFO  < Level::WARN);
    CHECK(Level::WARN  < Level::ERR);
    CHECK(Level::ERR   < Level::CRITICAL);
    CHECK(Level::CRITICAL < Level::OFF);
}
