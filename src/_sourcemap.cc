#include "_sourcemap.hh"

// [tng/internal]
#include "_logger.hh"
// [stdc++]
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <array>
#include <cstdint>
// [nlohmann/json]
#include <nlohmann/json.hpp>

namespace TNG_NAMESPACE::spec {

    // =============================================================================
    // SHA-256 – portable, keine externe Abhängigkeit
    // =============================================================================
    // Implementiert nach FIPS 180-4.

    namespace {

        static constexpr std::array<uint32_t, 64> K = { {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        } };

        inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        std::array<uint32_t, 8> sha256_block(
            std::array<uint32_t, 8> h,
            const uint8_t block[64])
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
            for (int i = 16; i < 64; ++i) {
                uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            auto [a, b, c, d, e, f, g, hh] = h;
            for (int i = 0; i < 64; ++i) {
                uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                uint32_t ch = (e & f) ^ (~e & g);
                uint32_t T1 = hh + S1 + ch + K[i] + w[i];
                uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t T2 = S0 + maj;
                hh = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
            }
            return { { h[0] + a, h[1] + b, h[2] + c, h[3] + d,
                      h[4] + e, h[5] + f, h[6] + g, h[7] + hh } };
        }

        std::string sha256_string(const std::string& data) {
            std::array<uint32_t, 8> h = { {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
            } };

            const uint8_t* in = reinterpret_cast<const uint8_t*>(data.data());
            uint64_t       len = data.size();
            uint64_t       bits = len * 8;

            std::vector<uint8_t> padded(in, in + len);
            padded.push_back(0x80);
            while (padded.size() % 64 != 56)
                padded.push_back(0x00);
            for (int i = 7; i >= 0; --i)
                padded.push_back(static_cast<uint8_t>(bits >> (i * 8)));

            for (std::size_t i = 0; i < padded.size(); i += 64)
                h = sha256_block(h, padded.data() + i);

            char hex[65]; hex[64] = '\0';
            for (int i = 0; i < 8; ++i)
                std::snprintf(hex + i * 8, 9, "%08x", h[i]);
            return std::string(hex);
        }

    } // anonymous namespace

    // =============================================================================
    // hash_files
    // =============================================================================

    std::string hash_files(const std::vector<std::string>& paths) {
        // Hash über Konkatenation aller Dateiinhalte + Dateinamen
        // (Dateiname einbeziehen damit eine leere Datei ≠ nicht vorhanden)
        std::string combined;
        for (const auto& p : paths) {
            combined += "file:" + p + "\n";
            std::ifstream f(p, std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                combined += ss.str();
            }
            combined += '\0';  // Trennzeichen zwischen Dateien
        }
        return "sha256:" + sha256_string(combined);
    }

    // =============================================================================
    // SourceMap
    // =============================================================================

    void SourceMap::record(int proc_line, const SourceLocation& loc) {
        entries_.emplace(proc_line, loc);
    }

    void SourceMap::finalise(const std::vector<std::string>& source_files) {
        hash_ = hash_files(source_files);
    }

    std::optional<SourceLocation> SourceMap::lookup(int proc_line) const {
        auto it = entries_.find(proc_line);
        if (it != entries_.end()) return it->second;
        return std::nullopt;
    }

    std::optional<SourceLocation> SourceMap::lookup_nearest(int proc_line) const {
        if (entries_.empty()) return std::nullopt;
        // upper_bound gibt den ersten Eintrag > proc_line
        auto it = entries_.upper_bound(proc_line);
        if (it == entries_.begin()) return it->second;
        --it;  // größter Eintrag <= proc_line
        return it->second;
    }

    // =============================================================================
    // Persistenz (JSON)
    // =============================================================================

    void SourceMap::save(const std::string& smap_path) const {
        nlohmann::json j;
        j["hash"] = hash_;
        j["entries"] = nlohmann::json::array();
        for (const auto& [proc_line, loc] : entries_) {
            j["entries"].push_back({
                {"proc_line", proc_line},
                {"file",      loc.file},
                {"line",      loc.line},
                {"col",       loc.col}
                });
        }
        std::ofstream out(smap_path);
        if (!out)
            TNG_LOG_WARN("[SourceMap] Konnte Sidecar nicht schreiben: {}", smap_path);
        else {
#ifdef NDEBUG
            out << j.dump();           // Release: minified
#else
            out << j.dump(2);          // Debug: pretty-printed
#endif
        }
    }

    std::optional<SourceMap> SourceMap::load(const std::string& smap_path,
        const std::string& current_hash)
    {
        std::ifstream in(smap_path);
        if (!in) return std::nullopt;  // Datei existiert nicht

        try {
            nlohmann::json j;
            in >> j;

            // Hash-Vergleich: Stimmt die Sidecar noch mit den Quelldateien überein?
            const std::string stored_hash = j.at("hash").get<std::string>();
            if (stored_hash != current_hash) {
                TNG_LOG_DEBUG("[SourceMap] Hash-Mismatch – Sidecar veraltet, wird neu erzeugt");
                return std::nullopt;
            }

            SourceMap sm;
            sm.hash_ = stored_hash;
            for (const auto& e : j.at("entries")) {
                SourceLocation loc;
                loc.file = e.at("file").get<std::string>();
                loc.line = e.at("line").get<int>();
                loc.col = e.at("col").get<int>();
                sm.entries_[e.at("proc_line").get<int>()] = loc;
            }
            TNG_LOG_DEBUG("[SourceMap] Geladen: {} Einträge aus '{}'",
                sm.entries_.size(), smap_path);
            return sm;
        }
        catch (const std::exception& e) {
            TNG_LOG_WARN("[SourceMap] Sidecar konnte nicht geladen werden ({}): {}",
                smap_path, e.what());
            return std::nullopt;
        }
    }

} // namespace TNG_NAMESPACE::spec
