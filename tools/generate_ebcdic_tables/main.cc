// generate_ebcdic_tables — ICU-78.3-Orakel-Generator/-Verifizierer fuer die
// gecheckten EBCDIC-(IBM-1047)-Konvertierungstabellen in
// include/iso8583/_codec.hh (kEbcdicToAscii / kAsciiToEbcdic / kEbcdicValid).
//
// Das Tool ist reine Build-/CI-Infrastruktur: ICU wird NIE an Runtime-Targets
// der Bibliothek gelinkt (s. tools/generate_ebcdic_tables/CMakeLists.txt).
// Die Laufzeit-Umwandlung nutzt ausschliesslich die gecheckten Tabellen.
//
// Verwendung:
//   iso8583_ebcdic_table_gen --out <dir>     regneriert die ICU-Verdict-JSONs
//   iso8583_ebcdic_table_gen --expect <dir>  prueft, ob die in <dir> gecheckten
//                                             JSONs byte-identisch mit der
//                                             aktuellen ICU-78.3-Ausgabe sind
//
// Exit-Codes: 0 = OK/Stimmig, 1 = Mismatch, 2 = Umgebungsfehler
// (ICU major != 78, Converter nicht verfuegbar, Argumentfehler).
//
// JSON-Format (stabil, nicht aenderbar ohne Test-Update):
//   {"converter": "<name>", "bytes": [{"byte": N, "ok": true, "out": M},
//                                     {"byte": N, "ok": false, "status": S,
//                                      "in_consumed": C[, "detail": "..."]}]}

#include <unicode/ucnv.h>
#include <unicode/utypes.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Verdict {
    bool    ok = false;      // conversion fully succeeded
    int32_t status = 0;      // UErrorCode on failure (0 = success path)
    int32_t stage = 0;       // 1 = converter open, 2 = conversion
    char    ascii = 0;       // produced char on success
    size_t  in_consumed = 0; // input bytes consumed before a hard failure
    std::string detail;
};

UConverter* open_conv(const char* name, std::string& report)
{
    UErrorCode err = U_ZERO_ERROR;
    UConverter* c = ucnv_open(name, &err);
    if (U_FAILURE(err)) {
        report += "  FATAL: ucnv_open(\"" + std::string(name) +
                  "\") failed, status=" + std::to_string(static_cast<int32_t>(err)) + "\n";
    }
    return c;
}

// Convert the whole `in` buffer (charset of `src_conv`) into the charset of
// `dst_conv`, replicating the library wrapper's grow dance: on
// U_BUFFER_OVERFLOW_ERROR the output buffer doubles and the call is retried
// (the I/O pointers preserve consumed/produced positions).
struct ConvResult {
    Verdict             verdict;
    std::vector<char>   out;
};

ConvResult convert_full(UConverter* src_conv, UConverter* dst_conv,
                        const uint8_t* in, size_t in_len)
{
    ConvResult r;
    const char* source       = reinterpret_cast<const char*>(in);
    const char* source_limit = source + in_len;
    const char* source_pos   = source;

    std::vector<char> out(256);
    char*       target       = out.data();
    const char* target_limit = out.data() + out.size();

    UErrorCode err = U_ZERO_ERROR;
    int32_t guard = 0;
    for (;;) {
        // NOTE: target converter is the FIRST argument (ICU convention).
        ucnv_convertEx(dst_conv, src_conv,
                       &target, target_limit,
                       &source_pos, source_limit,
                       nullptr, nullptr, nullptr, nullptr,
                       /*reset=*/true, /*flush=*/true, &err);

        if (U_SUCCESS(err)) {
            if (source_pos == source_limit) {
                r.verdict.ok = true;
                r.out.assign(out.data(), target);
                if (r.out.size() == 1) r.verdict.ascii = r.out[0];
                r.verdict.in_consumed = static_cast<size_t>(source_pos - source);
                return r;
            }
            if (target == target_limit) {
                // output full, input left -> grow and retry
                if (++guard > 12) {
                    r.verdict.stage = 2;
                    r.verdict.status = U_BUFFER_OVERFLOW_ERROR;
                    r.verdict.detail = "grow-guard-exceeded";
                    r.verdict.in_consumed = static_cast<size_t>(source_pos - source);
                    return r;
                }
                const size_t used = static_cast<size_t>(target - out.data());
                out.resize(out.size() * 2);
                target       = out.data() + used;
                target_limit = out.data() + out.size();
                continue;
            }
            r.verdict.stage  = 2;
            r.verdict.status = 0;
            r.verdict.detail = std::string("stalled: inleft=") +
                               std::to_string(static_cast<size_t>(source_limit - source_pos));
            r.verdict.in_consumed = static_cast<size_t>(source_pos - source);
            return r;
        }
        if (err == U_BUFFER_OVERFLOW_ERROR) {
            if (++guard > 12) {
                r.verdict.stage = 2;
                r.verdict.status = U_BUFFER_OVERFLOW_ERROR;
                r.verdict.detail = "grow-guard-exceeded";
                r.verdict.in_consumed = static_cast<size_t>(source_pos - source);
                return r;
            }
            const size_t used = static_cast<size_t>(target - out.data());
            out.resize(out.size() * 2);
            target       = out.data() + used;
            target_limit = out.data() + out.size();
            continue;
        }
        // hard (non-retriable) failure — source_pos points at the bad input
        r.verdict.stage  = 2;
        r.verdict.status = static_cast<int32_t>(err);
        r.verdict.in_consumed = static_cast<size_t>(source_pos - source);
        r.verdict.detail = "hard-fail after " + std::to_string(r.verdict.in_consumed) +
                           " B in";
        return r;
    }
}

// BYTE-STABLE writer — das Format ist Teil des Orakel-Pins; Aenderungen
// erfordern neue pinned-Dateien + Test-Update.
std::string to_json(const char* name, const std::vector<Verdict>& v)
{
    std::ostringstream f;
    f << "{\n  \"converter\": \"" << name << "\",\n  \"bytes\": [\n";
    for (size_t i = 0; i < v.size(); ++i) {
        const Verdict& x = v[i];
        if (x.ok) {
            f << "    {\"byte\": " << i << ", \"ok\": true, \"out\": "
              << static_cast<int>(static_cast<uint8_t>(x.ascii)) << "}";
        }
        else {
            f << "    {\"byte\": " << i << ", \"ok\": false, \"status\": "
              << x.status << ", \"in_consumed\": " << x.in_consumed;
            if (!x.detail.empty())
                f << ", \"detail\": \"" << x.detail << "\"";
            f << "}";
        }
        if (i + 1 < v.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    return f.str();
}

void print_table(const char* title, const std::vector<Verdict>& v)
{
    std::printf("\n=== %s ===\n", title);
    std::printf("    ");
    for (int c = 0; c < 16; ++c) std::printf("%02X ", c);
    std::printf("\n");
    for (int r = 0; r < 16; ++r) {
        std::printf("%02X| ", r);
        for (int c = 0; c < 16; ++c) {
            const Verdict& x = v[r * 16 + c];
            if (x.ok) {
                const char ch = x.ascii;
                if (ch >= ' ' && ch <= '~') std::printf("%c   ", ch);
                else std::printf(" .%02X ", static_cast<unsigned>(static_cast<uint8_t>(ch)));
            }
            else {
                std::printf("!!%3d ", x.status);
            }
        }
        std::printf("\n");
    }
}

bool file_bytes(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    // -- ICU-Version-Pin -------------------------------------------------------
    // Der Orakel-Pin gilt NUR fuer ICU 78.x; andere Majors koennen andere
    // IBM-1047-Zuordnungen liefern (C1-Steuerbyte-Gebiet) und wuerden die
    // gecheckten Tabellen verfalschen.
    if (U_ICU_VERSION_MAJOR_NUM != 78) {
        std::fprintf(stderr,
            "FATAL: erwartet ICU major 78 (Orakel-Pin), gefunden major %d. "
            "Der Pin in vcpkg.json muss \"78.3\" sein.\n",
            U_ICU_VERSION_MAJOR_NUM);
        return 2;
    }
    std::printf("ICU version: major=%d (data: icudt%s%s%s)\n",
                U_ICU_VERSION_MAJOR_NUM,
#ifdef U_ICU_VERSION_SHORT
                U_ICU_VERSION_SHORT,
#else
                "",
#endif
#ifdef U_ICUDATA_TYPE_LETTER
                U_ICUDATA_TYPE_LETTER,
#else
                "",
#endif
                "");

    // -- Argumente --------------------------------------------------------------
    enum class Mode { Out, Expect };
    Mode mode = Mode::Out;
    bool have_mode = false;
    std::string dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" || a == "--expect") {
            if (have_mode) { std::fprintf(stderr, "FATAL: nur ein Modus (--out|--expect) erlaubt\n"); return 2; }
            mode = (a == "--out") ? Mode::Out : Mode::Expect;
            have_mode = true;
            if (i + 1 >= argc) { std::fprintf(stderr, "FATAL: %s braucht ein Verzeichnis-Argument\n", a.c_str()); return 2; }
            dir = argv[++i];
        }
        else { std::fprintf(stderr, "FATAL: unbekanntes Argument \"%s\"\n", a.c_str()); return 2; }
    }
    if (!have_mode) {
        std::fprintf(stderr, "Usage: %s --out <dir> | --expect <dir>\n", argv[0]);
        return 2;
    }

    // -- Converter ---------------------------------------------------------------
    std::string report;
    UConverter* c1047  = open_conv("IBM-1047", report);
    UConverter* cascii = open_conv("US-ASCII", report);
    if (!c1047 || !cascii) {
        std::printf("%s", report.c_str());
        std::printf("Abbruch: Converter(s) nicht verfuegbar.\n");
        return 2;
    }

    // -- Per-Byte-Sweeps -----------------------------------------------------------
    std::vector<Verdict> e2a(256), a2e(256);
    for (int b = 0; b < 256; ++b) {
        const uint8_t in = static_cast<uint8_t>(b);
        e2a[b] = convert_full(c1047, cascii, &in, 1).verdict;
        a2e[b] = convert_full(cascii, c1047, &in, 1).verdict;
    }
    print_table("E2A: IBM-1047 -> US-ASCII  (char shown if printable, !!N = reject, status N)", e2a);
    print_table("A2E: US-ASCII -> IBM-1047", a2e);

    size_t e2a_reject = 0, a2e_reject = 0;
    for (int b = 0; b < 256; ++b) {
        if (!e2a[b].ok) ++e2a_reject;
        if (!a2e[b].ok) ++a2e_reject;
    }
    std::printf("\nE2A rejected: %zu/256, A2E rejected: %zu/256\n", e2a_reject, a2e_reject);

    const std::string e2a_json = to_json("IBM-1047 -> US-ASCII", e2a);
    const std::string a2e_json = to_json("US-ASCII -> IBM-1047", a2e);

    if (mode == Mode::Out) {
        auto w = [](const std::string& path, const std::string& content) {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { std::fprintf(stderr, "FATAL: nicht schreibbar: %s\n", path.c_str()); return false; }
            f << content;
            return static_cast<bool>(f);
        };
        const std::string p1 = dir + "/icu_verdicts_e2a.json";
        const std::string p2 = dir + "/icu_verdicts_a2e.json";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec); // Zieldir anlegen, falls nicht vorhanden
        if (!w(p1, e2a_json) || !w(p2, a2e_json)) return 2;
        std::printf("Artefakte geschrieben: %s, %s\n", p1.c_str(), p2.c_str());
    }
    else { // Mode::Expect
        auto check = [](const std::string& path, const std::string& expected) {
            std::string actual;
            if (!file_bytes(path, actual)) {
                std::fprintf(stderr, "MISMATCH: fehlt oder nicht lesbar: %s\n", path.c_str());
                return false;
            }
            if (actual != expected) {
                std::fprintf(stderr, "MISMATCH: %s weicht von der ICU-78.3-Referenzausgabe ab\n", path.c_str());
                return false;
            }
            std::printf("OK: %s stimmt mit dem ICU-78.3-Orakel ueberein\n", path.c_str());
            return true;
        };
        const bool ok1 = check(dir + "/icu_verdicts_e2a.json", e2a_json);
        const bool ok2 = check(dir + "/icu_verdicts_a2e.json", a2e_json);
        if (c1047) ucnv_close(c1047);
        if (cascii) ucnv_close(cascii);
        if (!ok1 || !ok2) {
            std::fprintf(stderr, "VERIFY FAILED: regenerate mit: cmake --build <builddir> --target update-ebcdic-tables\n");
            return 1;
        }
        std::printf("VERIFY PASSED: beide Verdict-Dateien sind byte-stabil (ICU 78.3)\n");
        return 0;
    }

    if (c1047) ucnv_close(c1047);
    if (cascii) ucnv_close(cascii);
    return 0;
}