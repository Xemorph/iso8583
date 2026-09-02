// [ISO8583] 3.2 (Sicherheits-Audit): Spec-Cache-Haertung - TOCTOU
// (Hot-Swap: publizierter Parser entspricht immer exakt einer kompletten
// Inhaltversion), LRU-Cap (max. 64 Eintraege) und De-Duplizierung paralleler
// Loads desselben Pfaeds (stabiles shared_ptr).
//
// [catch2]
#include <catch2/catch_test_macros.hpp>
// [tng]
#include <iso8583/ISOSpec.hh>
// [stdc++]
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace TNG_NAMESPACE;
using namespace TNG_NAMESPACE::spec;
namespace fs = std::filesystem;

// =============================================================================
// RAII-Helfer fuer temporaere Dateien (gleiches Muster wie in
// test_spec_sandbox.cc / test_preprocessor.cc)
// =============================================================================

namespace {

struct TempDir {
    fs::path path;

    TempDir() {
        path = fs::temp_directory_path()
             / ("iso8583_cch_" + std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    // Schreibt (oder ueberschreibt) eine Datei. Gibt den absoluten Pfad zurueck.
    // [ISO8583] 3.3: ATOMARER Swap (schreiben nach <name>.tmp + rename) -
    // Ein parallel ladender Reader sieht damit NIE eine teilgeschriebene
    // oder leere Datei. Vor 3.3 wurde in-place getrunctet; das liess im
    // TOCTOU-Test seltene Load-Fehler waehrend des Swaps (laufender
    // Reader sah eine transient leere Datei -> "Leere YAML-Datei").
    // Fehlschlaegt der rename (Reader haelt die Datei noch oeffnet),
    // wird ohne Atomaritaet zurueckgefallen - genuegend fuer ONE-SHOT
    // Writes ohne parallele Reader (Fixtures, LRU-Dateien).
    std::string write(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        auto tmp = p;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << content;
        }
        std::error_code ec;
        fs::rename(tmp, p, ec);
        if (ec) {
            std::ofstream f2(p, std::ios::binary | std::ios::trunc);
            f2 << content;
            std::error_code ec2;
            fs::remove(tmp, ec2);
        }
        return p.string();
    }

    // [ISO8583] 3.3: ATOMARER Swap mit Retry - fuer den TOCTOU-Writer,
    // der die Spec unter einem aktiv ladenden Reader tauscht. Wie write()
    // (tmp + rename), ABER ohne in-place-Fallback: solange der Reader die
    // Zielfeile noch oeffnet haelt (Windows-Handles ohne FILE_SHARE_DELETE),
    // schlaegt fs::rename (MoveFileEx REPLACE_EXISTING) fehl - dann wird
    // kurz gewartet und der Swap WIEDERHOLT. Ein in-place-Fallback wuerde
    // die Zielfeile transient leeren -> "Leere YAML-Datei" in laufenden
    // Reads.
    // BEKANNTES WINDOWS-FENSTER: rename(MoveFileEx REPLACE_EXISTING) bei
    // vorhandener Zielfeile ist dort NICHT atomar (Delete + Move als zwei
    // Kernel-Operationen); laeuft der AV-Filter auf der Source-Datei, kann
    // die Zielfeile zwischen beiden Schritten transient fehlen. Der
    // TOCTOU-Reader toleriert diese OS-Level-Races (Diskriminator:
    // Datei-Existenz im Fehlermoment) - sie sind kein Cache-Defekt. Der
    // naechste Swap (bzw. der finale Retry-Swirl) stellt die Zielfeile
    // wieder her (rename bei fehlender Zielfeile = reines Create).
    // Liefert false, wenn nach 100 Versuchen (~0.2 s) kein Swap klappte.
    bool swap(const std::string& name, const std::string& content) {
        auto p = path / name;
        fs::create_directories(p.parent_path());
        auto tmp = p;
        tmp += ".tmp";
        for (int attempt = 0; attempt < 100; ++attempt) {
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f)
                    return false;
                f << content;
            }
            std::error_code ec;
            fs::rename(tmp, p, ec);
            if (!ec)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
};
} // namespace

// =============================================================================
// Spec-Varianten fuer den Hot-Swap-Test: V1 und V2 unterscheiden sich sowohl
// im Namen als auch in der Feldstruktur (DE3 nur in V1, DE2-Laenge 19 vs. 16).
// Ein "gemischter" Parser/Spec-Kombination wuerde damit sofort auffallen:
// name == "V1"  <=>  has(3)  <=>  field(2).max_length == 19
// =============================================================================

static const std::string kSpecV1 =
    "spec: \"V1\"\n"
    "encoding: ascii\n"
    "fields:\n"
    "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
    "  \"001\": { type: scalar, format: bitmap,  length: 8 }\n"
    "  \"002\": { type: scalar, format: llchar,  length: 19 }\n"
    "  \"003\": { type: scalar, format: numeric, length: 2 }\n";

static const std::string kSpecV2 =
    "spec: \"V2\"\n"
    "encoding: ascii\n"
    "fields:\n"
    "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
    "  \"001\": { type: scalar, format: bitmap,  length: 8 }\n"
    "  \"002\": { type: scalar, format: llchar,  length: 16 }\n";

// Prueft die Konsistenzinvariante eines (parser, spec)-Paares: name,
// Feldstruktur und DE2-Laenge muessen zueinander passen (exakt V1 oder
// exakt V2 - nie eine Mischform).
static bool isConsistentPair(const ISOSpec::SmartPtr& spec) {
    if (!spec)
        return false;
    const bool isV1 = (spec->name() == "V1");
    if (spec->has(3) != isV1)
        return false;
    auto f2 = spec->field(2);
    if (!f2.has_value())
        return false;
    return (f2->format.max_length == (isV1 ? 19 : 16));
}

// =============================================================================
// TOCTOU: Hot-Swap waehrend des Ladens - eine zur Laufzeit ausgetauschte
// Spec-Datei darf keinen "gemischten" Parser/Spec-Stand liefern; nach Ende
// des Swaps muss der Cache gegen die Endversion konvergieren.
// =============================================================================

TEST_CASE("Cache TOCTOU - hot-swapped spec yields only consistent versions",
    "[spec][cache][threading]")
{
    TempDir dir;
    const std::string file = dir.write("hot.yml", kSpecV1);

    SpecDecoder::clearCache();
    SpecDecoder::invalidateCache(file);

    std::atomic<bool> writerDone{ false };
    std::atomic<bool> stopWriter{ false };

    // Writer: tauscht die Datei ~1 s lang alle 5 ms zwischen V1/V2 aus,
    // endet dann in V2 und stoppt. try/catch, damit selbst ein seltener
    // OS-I/Fehler (z.B. AV-Scan-Stall auf den Temp-Dateien) den Prozess
    // nicht beendet (der Writer kann unten detached werden).
    std::thread writer([&] {
        try {
            bool v1 = true;
            using namespace std::chrono_literals;
            const auto deadline = std::chrono::steady_clock::now() + 1000ms;
            while (!stopWriter.load() && std::chrono::steady_clock::now() < deadline) {
                // [ISO8583] 3.3: atomarer Swap (tmp+rename mit Retry) -
                // ein Reader sieht waehrend des Swaps nie eine leere oder
                // teilgeschriebene Datei (siehe TempDir::swap).
                dir.swap("hot.yml", v1 ? kSpecV1 : kSpecV2);
                v1 = !v1;
                std::this_thread::sleep_for(5ms);
            }
            // Endzustand: der finale Swap MUSS ankommen (der Reader hat zu
            // diesem Zeitpunkt seine Schleife beendet bzw. wird sie beenden;
            // Retry bis die Datei wirklich V2 ist).
            int finalTries = 0;
            while (!dir.swap("hot.yml", kSpecV2) && ++finalTries < 5000)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        catch (...) { /* I/O-Fehler: writerDone wird trotzdem gesetzt */ }
        writerDone.store(true);
    });

    // [ISO8583] 3.3: RAII-Guard - der Writer-Thread darf beim Verlassen
    // des Test-Scope NIEMALS joinable sein: zerstoert man ein joinables
    // std::thread, ruft das std::terminate() auf (Prozess-Abschluss ohne
    // sauberes Test-Fehlerbild). Ein REQUIRE-Failure/Exception in der
    // Reader-Schleife WAEHREND des Writer-Laufs wuerde den Scope andernfalls
    // entwinden, waehrend der Writer noch laeuft. Normalpfad: der explizite
    // join/detach unten macht den Thread nicht-joinable, der Guard wird
    // zur No-Op. Ausnahmepfad: der Guard wartet kurz und detacht.
    struct WriterGuard {
        std::thread&       th;
        std::atomic<bool>& done;
        WriterGuard(std::thread& t, std::atomic<bool>& d) : th(t), done(d) {}
        ~WriterGuard() {
            if (!th.joinable())
                return;
            for (int i = 0; i < 200 && !done.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            th.detach();
        }
    } writerGuard(writer, writerDone);

    // Reader: laedt im Schleife mit CheckEveryCall und prueft bei JEDEM
    // ERFOLGREICHEN Ergebnis die Konsistenzinvariante (exakt eine komplette
    // Version).
    //
    // [ISO8583] 3.3: Transiente OS-Level-Dateiswap-Races werden TOLERIERT:
    // Unter Windows ist fs::rename (MoveFileEx + REPLACE_EXISTING) bei
    // vorhandener Zielfeile NICHT atomar (Ziel-Delete und Source-Move sind
    // zwei getrennte Kernel-Operationen). Laeuft gerade der AV-Filter auf
    // der Source-Temp-Datei, kann die Zielfeile zwischen beiden Schritten
    // transient FEHLEN (Writer-Swap). Ein Load, das exakt in dieses Fenster
    // laeuft, wirft ("Datei nicht lesbar") - ein Artefakt des Test-Setups
    // unter Windows, KEIN Cache-Inkonsistenz-Fall: jedes Load, das ein
    // Ergebnis liefert, MUSS versionskonsistent sein (unterhalb gecheckt).
    // Diskriminator: existiert die Datei im Fehlermoment, handelt es sich
    // um einen echten Load-/Cache-Fehler -> Re-Throw. Unter Linux (rename
    // = atomar) tritt dieser Fall nie auf.
    int transientFileRaces = 0;
    bool sawV1 = false, sawV2 = false;
    const auto t0 = std::chrono::steady_clock::now();
    while (!writerDone.load()
        && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4))
    {
        ISOSpec::SmartPtr spec;
        try
        {
            const auto [parser, sp] = SpecDecoder::loadBothFromYamlCached(
                file, SpecLoadOptions{}, /*validation=*/CacheValidation::CheckEveryCall);
            (void)parser;
            spec = sp;
        }
        catch (const std::exception&)
        {
            std::error_code ec;
            if (fs::exists(file, ec) && !ec)
                throw; // Datei vorhanden -> kein Swap-Race, echtes Fehler
            ++transientFileRaces;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        REQUIRE(isConsistentPair(spec));
        if (spec->name() == "V1") sawV1 = true;
        if (spec->name() == "V2") sawV2 = true;
    }
    // Der Race-Zaehler ist ein Umgebungs-Signal (AV-Filter-Aktivitaet), kein
    // Testkriterium - nur als Plausibilitaetsgrenze gegen degenerierte Laeufe.
    CHECK(transientFileRaces < 1000);

    // Beschaenktes Writer-Warten: Bei einem seltenen OS-Level-I/O-Stall
    // (z.B. AV-Scan auf frisch angelegten Temp-Dateien) darf das join
    // den gesamten Testlauf nicht haengen lassen - dann wird der Writer
    // detached und nur die strengen Versions-Assertions entfallen (die
    // Konsistenzinvariante wurde bei jedem Read ohnehin gecheckt).
    const auto joinDeadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(15);
    while (!writerDone.load() && std::chrono::steady_clock::now() < joinDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    bool writerOk = writerDone.load();
    if (writerOk)
        writer.join();
    else {
        stopWriter.store(true);
        writer.detach();
    }

    if (writerOk) {
        // Beide Versionen wurden beobachtet (der Writer lief laenger als
        // der Reader-Start; die mtime-/Hash-Invalidierung hat greifen
        // muessen):
        REQUIRE(sawV1);
        REQUIRE(sawV2);
    }

    // Konvergenz: Der Cache muss gegen die Endversion (V2) laufen -
    // naechste N Aufrufe liefern durchweg V2 (kein veralteter Eintrag).
    // Transiente Swap-Races werden wie in der Reader-Schleife toleriert
    // (relevant, falls der Writer am Join-Deadline-Sprung noch laeuft und
    // die Datei weiter tauscht); die Versions-Assertion entfaellt dann
    // sowieso (writerOk == false).
    for (int i = 0; i < 20; ++i) {
        ISOSpec::SmartPtr spec;
        try
        {
            const auto [parser, sp] = SpecDecoder::loadBothFromYamlCached(
                file, SpecLoadOptions{}, /*validation=*/CacheValidation::CheckEveryCall);
            (void)parser;
            spec = sp;
        }
        catch (const std::exception&)
        {
            std::error_code ec;
            if (fs::exists(file, ec) && !ec)
                throw;
            ++transientFileRaces;
            continue;
        }
        REQUIRE(isConsistentPair(spec));
        if (writerOk && i >= 5)
            REQUIRE(spec->name() == "V2");
    }
}

// =============================================================================
// LRU-Cap: Nach 64 distinct Pfaden ist der Cache voll; der 65. Load
// eviziert den aeltesten Eintrag; neuere Eintraege bleiben im Cache
// (Pointer-Identitaet des publizierten Parsers, beobachtbar mit
// TrustUntilInvalidated).
// =============================================================================

TEST_CASE("Cache LRU - 65th distinct path evicts the oldest entry",
    "[spec][cache]")
{
    TempDir dir;
    SpecDecoder::clearCache();

    static const std::string base =
        "spec: \"L\"\n"
        "encoding: ascii\n"
        "fields:\n"
        "  \"000\": { type: scalar, format: numeric, length: 4 }\n"
        "  \"001\": { type: scalar, format: bitmap,  length: 8 }\n"
        "  \"002\": { type: scalar, format: llchar,  length: 19 }\n";

    const int N = 64;
    std::vector<std::string> paths;
    std::vector<ISOParserPtrBase::ISOParserPtrBaseSmartPtr> firstPtr;
    paths.reserve(N + 1);
    firstPtr.reserve(N + 1);

    for (int i = 0; i < N; ++i) {
        paths.push_back(dir.write(("lru_" + std::to_string(i) + ".yml"), base));
        firstPtr.push_back(SpecDecoder::loadFromYamlCached(
            paths.back(), SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated));
    }
    // Nach 64 distinct Pfaden ist der Cache (Cap 64) genau voll.

    // 65. distinct Pfad: Cache ueberlaeuft -> aeltester Eintrag (i=0) wird
    // eviziert.
    paths.push_back(dir.write("lru_64.yml", base));
    firstPtr.push_back(SpecDecoder::loadFromYamlCached(
        paths.back(), SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated));

    // i=1 und i=64 sind nach dem Ueberlauf noch im Cache: identische
    // Parser-Pointer (Cache-Treffer, stoeren das LRU-Setzen nicht).
    // (Vor dem i=0-Reload pruefen - dessen Reload wuerde sonst selbst
    // eviktieren.)
    auto re1 = SpecDecoder::loadFromYamlCached(
        paths[1], SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated);
    REQUIRE(re1.get() == firstPtr[1].get());

    // i=64 (der 65.) ist natuerlich auch noch da:
    auto re64 = SpecDecoder::loadFromYamlCached(
        paths[64], SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated);
    REQUIRE(re64.get() == firstPtr[64].get());

    // i=0 ist eviziert: Reload erzeugt einen NEUEN Parser (Pointer-Wechsel;
    // dabei wird der aelteste verbliebene Eintrag i=1 eviziert).
    auto re0 = SpecDecoder::loadFromYamlCached(
        paths[0], SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated);
    REQUIRE(re0.get() != firstPtr[0].get());

    SpecDecoder::clearCache();
}

// =============================================================================
// Parallele Loads desselben Pfaeds: De-Duplizierung - nach dem Setzeln des
// Caches liefern alle Threads (und naechste Aufrufe) denselben, stabilen
// Parser-Pointer.
// =============================================================================

TEST_CASE("Cache concurrency - parallel loads dedup to one stable parser",
    "[spec][cache][threading]")
{
    TempDir dir;
    const std::string file = dir.write("par.yml", kSpecV1);

    SpecDecoder::clearCache();

    constexpr int kIters = 50;
    std::vector<ISOParserPtrBase::ISOParserPtrBaseSmartPtr> lastPtr(2);
    std::vector<std::shared_ptr<std::atomic<bool>>> stable(2);
    // [ISO8583] 3.3: Exceptions in den Worker-Threads muessen abgefangen
    // werden - eine uncaught Exception in einem std::thread endet in
    // std::terminate (Prozess-Abschuss, kein sauberes Test-Fehlerbild).
    // Stattdessen wird der Fehler hier gesammelt und nach dem Join als
    // normales Test-FAIL gewertet.
    std::array<std::string, 2> workerError;
    std::mutex errMutex;

    auto worker = [&](int tid) {
        stable[tid] = std::make_shared<std::atomic<bool>>(true);
        for (int i = 0; i < kIters; ++i) {
            auto loadOnce = [&]() -> ISOParserPtrBase::ISOParserPtrBaseSmartPtr {
                try {
                    return SpecDecoder::loadFromYamlCached(
                        file, SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated);
                }
                catch (const std::exception& e) {
                    std::lock_guard lock(errMutex);
                    workerError[tid] = std::string("iteration ") + std::to_string(i) + ": " + e.what();
                    throw;
                }
            };
            bool threw = false;
            ISOParserPtrBase::ISOParserPtrBaseSmartPtr p;
            try {
                p = loadOnce();
            }
            catch (...) {
                threw = true;
            }
            if (threw) {
                stable[tid]->store(false);
                break;  // weitere Iterationen wuerden identisch scheitern
            }
            if (i > 0 && lastPtr[tid])
                stable[tid]->store(p.get() == lastPtr[tid].get());
            lastPtr[tid] = p;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    t0.join();
    t1.join();

    // Worker-Fehler als sauberes Test-FAIL (statt std::terminate):
    std::string err;
    { std::lock_guard lock(errMutex);
      err = workerError[0].empty() ? workerError[1] : workerError[0]; }
    REQUIRE(err.empty());
    if (!err.empty())
        FAIL("Worker-Load fehlgeschlagen: " + err);

    // Ab dem 2. Aufruf ist der Cache gesetzt: jeder Thread sah durchweg
    // denselben Parser-Pointer.
    REQUIRE(stable[0]->load());
    REQUIRE(stable[1]->load());

    // Beide Threads enden beim selben (stabilen, gecachten) Parser.
    REQUIRE(lastPtr[0].get() == lastPtr[1].get());

    // Und ein frischer Aufruf liefert denselben Pointer (kein erneuter Load).
    auto fresh = SpecDecoder::loadFromYamlCached(
        file, SpecLoadOptions{}, CacheValidation::TrustUntilInvalidated);
    REQUIRE(fresh.get() == lastPtr[0].get());

    SpecDecoder::clearCache();
}