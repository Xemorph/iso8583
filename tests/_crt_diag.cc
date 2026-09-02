// =============================================================================
// _crt_diag.cc - Test-only CRT-Diagnostik (NUR Debug + Windows)
// =============================================================================
//
// [ISO8583] 3.3 (Security-Audit, Concurrency):
//  - CRT-Asserts (z.B. STL-Iterator-Validierung "vector iterators in
//    range are from different containers") werden auf stderr geroutet
//    STATT Dialogfenster, damit automatische Testlaeufe die genaue
//    Assert-Meldung + Datei/Zeile mitloggen koennen.
//  - std::terminate (z.B. uncaught Exception in einem Worker-Thread)
//    loggt die Exception-Meldung, bevor der Prozess abgebrochen wird.
//
// WICHTIG (UCRT): _CrtSetReportMode/_CrtSetReportFile akzeptieren NUR
// EINE Report-Art pro Aufruf - OR-Masken (z.B. _CRT_ASSERT | _CRT_ERROR)
// sind ungueltig und loesen SELBST einen CRT-Assert aus
// ("nRptType >= 0 && nRptType < _CRT_ERRCNT"). Deshalb hier nur
// _CRT_ASSERT - der Report-Typ, den die STL-Debug-Checks nutzen.
//
// Diese Datei ist KEIN TEST (keine TEST_CASEs) und gehoert NIE in die
// Library - nur in die Catch2-Test-Exe (tests/).

#if defined(_DEBUG) && defined(_WIN32)

#include <cstdio>
#include <crtdbg.h>
#include <exception>

namespace {

struct CrtDiagInit {
    CrtDiagInit() {
        // NUR _CRT_ASSERT, einzeln (keine OR-Maske, kein WNDW - WNDW
        // wuerde in automatischen Laufen ein Dialogfenster aufpoppen
        // und blockieren).
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

        // Uncaught Exception (z.B. in einem Worker-Thread) endet in
        // std::terminate. Vor dem Abort die Exception-Meldung loggen,
        // damit automatische Laeufe eine Spur hinterlassen.
        std::set_terminate([]() {
            std::fprintf(stderr, "[crt-diag] std::terminate aufgerufen "
                         "(uncaught Exception oder Fatal Error)\n");
            if (auto ex = std::current_exception()) {
                try {
                    std::rethrow_exception(ex);
                }
                catch (const std::exception& e) {
                    std::fprintf(stderr, "[crt-diag]   was: %s\n", e.what());
                }
                catch (...) {
                    std::fprintf(stderr, "[crt-diag]   was: <nicht std::exception>\n");
                }
            }
            else {
                std::fprintf(stderr, "[crt-diag]   (keine Exception - z.B. "
                          "double-free, failed allocation)\n");
            }
            std::fflush(stderr);
            std::abort();
        });
    }
};

inline const CrtDiagInit kCrtDiagInit{};

} // namespace

#endif // _DEBUG && _WIN32