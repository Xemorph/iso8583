// =============================================================================
// Fuzz-Ziel F4: Header-Byte-Images (WLP_FOHeader / BASE1Header)
// =============================================================================
//
// Invariante: beliebige Header-Bytes duerfen NIE out-of-bounds lesen (OOB/UB/
// Stack Overflow/ASan-Fund). Getter/Setter und die from-bytes-Konstruktoren /
// unpack() sind Fail-closed: zu kurze / groesse-fremde Rahmen werfen eine
// positionierte [ISO8583]-std::runtime_error statt zu lesen (s. Security-Plan,
// Phase 4, F4 — die P1/P2-Header-Guards aus Phase 1).
//
// Getrieben:
//   * BASE1Header (Visa, LENGTH=22)  — from-bytes + alle Getter/Setter
//   * WLP_FOHeader (LENGTH-4=89)     — from-bytes (gepackte EBCDIC-Portion)
//   * Fail-closed-Pfade: zu kurze BASE1-Rahmen / groesse-fremde WLP-Rahmen
//     MUESSEN werfen (sonst waere das eine OOB-Lesefalle).

#include "_fuzz_common.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace iso8583;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    if (!data || size > 1'000'000) {
        return 0;
    }
    const std::vector<uint8_t> b(data, data + size);
    try {
        // ── BASE1 (Visa), LENGTH = 22: volles Frame + Getter/Setter ─────────
        if (b.size() >= BASE1Header::LENGTH) {
            std::vector<uint8_t> h(b.begin(), b.begin() + BASE1Header::LENGTH);
            auto p = std::make_shared<BASE1Header>(std::move(h));
            (void)p->getHLen();
            (void)p->format();
            (void)p->source();
            (void)p->destination();
            (void)p->isRejected();
            (void)p->getRejectCode();
            p->setHFormat(1);
            p->setRtCtl(1);
            p->setFlags(1);
            p->setStatus(1);
            p->setBatchNumber(1);
            p->setLen(10);
            p->source("SRC");
            p->destination("DST");
            p->swapDirection();
        }

        // ── WLP_FO: from-bytes erwartet EXAKT LENGTH-4 (gepackt, EBCDIC) ─────
        const std::size_t wlpNeed = WLP_FOHeader::LENGTH - 4;
        if (b.size() >= wlpNeed) {
            std::vector<uint8_t> h(b.begin(), b.begin() + wlpNeed);
            auto p = std::make_shared<WLP_FOHeader>(std::move(h));
            (void)p->length();
            (void)p->version();
            p->length(10);
            p->version(1);
            p->sysId("SYS");
            p->record("REC");
            p->mti("0200");
            p->uuid("UUID");
            p->reference("REF");
            p->payment(5);
        }

        // ── Fail-closed 1: zu kurzer BASE1-Rahmen MUSS werfen ────────────────
        if (size >= 1) {
            const std::size_t n = std::min<std::size_t>(size, BASE1Header::LENGTH - 1);
            try {
                std::vector<uint8_t> h(b.begin(), b.begin() + n);  // < 22
                (void)std::make_shared<BASE1Header>(std::move(h));
            } catch (...) {  // erwartet: Fail-closed-Throw
            }
        }

        // ── Fail-closed 2: groesse-fremder WLP-Rahmen (88 statt 89) wirft ───
        {
            std::vector<uint8_t> h;
            const std::size_t m = WLP_FOHeader::LENGTH - 5;  // 88
            if (b.size() >= m) {
                h.assign(b.begin(), b.begin() + m);
            } else {
                h.assign(m, 0x40);
            }
            try {
                (void)std::make_shared<WLP_FOHeader>(h);  // 88 != 89 → Throw
            } catch (...) {  // erwartet: Fail-closed-Throw
            }
        }
    } catch (...) {
        // Invariante: kein Crash/UB. Abbrueche/Fehler sind erlaubte Ergebnisse.
    }
    return 0;
}

#if !defined(__clang__) && !defined(__APPLE__)
// Non-Clang (MSVC/GCC): Mini-Harness gegen feste Seeds + 256-Byte-Sweep.
int main() {
    fuzz::run_seeds();
    return 0;
}
#endif