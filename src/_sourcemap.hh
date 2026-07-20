#pragma once

// =============================================================================
// SourceMap – bildet verarbeitete YAML-Nodes auf ihre Original-Quellen ab
// =============================================================================
//
// Problem:
//   Der Preprocessor expandiert !use, !template und !include_files zu einem
//   flachen, prozessierten YAML-Dokument. Zeilen-/Spaltenangaben aus YAML::Mark
//   beziehen sich danach auf das prozessierte Dokument – nicht auf die originale
//   Datei die der User bearbeitet hat.
//
// Lösung:
//   Während der Preprocessor läuft, baut er eine SourceMap auf:
//     prozessierte Zeile  →  (original-Datei, original-Zeile, original-Spalte)
//
//   Die Map wird als Sidecar-Datei neben der Spec gespeichert:
//     mastercard.yml  →  mastercard.yml.smap
//
//   Beim nächsten Laden wird die Sidecar automatisch gefunden und validiert.
//   Als Integritätsprüfung dient ein SHA-256-Hash über alle Quelldateien.
//   Stimmt der Hash nicht überein, wird die Sidecar verworfen und neu erzeugt.
//
// Format der .smap-Datei (JSON):
//   {
//     "hash":    "sha256:abc123...",
//     "entries": [
//       { "proc_line": 15, "file": "schemes/gmc.yml", "line": 42, "col": 3 }
//     ]
//   }

// [tng/config]
#include <iso8583/config.h>
// [stdc++]
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace TNG_NAMESPACE::spec {

    // ── SourceLocation ────────────────────────────────────────────────────────

    /// Herkunfts-Information eines YAML-Nodes im Original-Dokument.
    struct TNG_EXPORT SourceLocation {
        std::string file;   ///< Absoluter Pfad zur Original-Datei
        int         line;   ///< 1-basierte Zeilennummer
        int         col;    ///< 1-basierte Spaltennummer

        std::string to_string() const {
            return file + ":" + std::to_string(line) + ":" + std::to_string(col);
        }
    };

    // ── SourceMap ─────────────────────────────────────────────────────────────

    class TNG_EXPORT SourceMap {
    public:
        // ── Aufbau (während Preprocessing) ───────────────────────────────────

        /// Erfasst einen Node: prozessierte Zeile → Original-Location.
        void record(int proc_line, const SourceLocation& loc);

        /// Gibt den Hash zurück (leer solange finalise() nicht aufgerufen).
        const std::string& hash() const noexcept { return hash_; }

        /// Berechnet und speichert den SHA-256-Hash aller Quelldateien.
        /// Muss aufgerufen werden nachdem alle Dateien verarbeitet wurden.
        void finalise(const std::vector<std::string>& source_files);

        // ── Lookup (während Validierung / Fehlerbehandlung) ──────────────────

        /// Gibt die Original-Location für eine prozessierte Zeile zurück.
        /// Gibt nullopt zurück wenn keine Eintrag vorhanden (z.B. generierter Node).
        std::optional<SourceLocation> lookup(int proc_line) const;

        /// Gibt die nächste bekannte Location für eine prozessierte Zeile zurück.
        /// Sucht rückwärts wenn keine exakte Übereinstimmung – immer ein Ergebnis.
        std::optional<SourceLocation> lookup_nearest(int proc_line) const;

        // ── Persistenz ───────────────────────────────────────────────────────

        /// Speichert die SourceMap als JSON-Sidecar neben der Spec.
        /// smap_path = spec_path + ".smap"
        void save(const std::string& smap_path) const;

        /// Lädt eine SourceMap aus einer Sidecar-Datei.
        /// Gibt nullopt zurück wenn:
        ///   - Datei nicht vorhanden
        ///   - Hash stimmt nicht überein (Dateien geändert)
        ///   - JSON korrupt
        static std::optional<SourceMap> load(const std::string& smap_path,
                                             const std::string& current_hash);

        bool empty() const noexcept { return entries_.empty(); }

    private:
        std::string                  hash_;
        std::map<int, SourceLocation> entries_; // proc_line → origin
    };

    // ── SHA-256 Hilfsfunktion ─────────────────────────────────────────────────

    /// Berechnet einen SHA-256-Hash über den Inhalt aller angegebenen Dateien.
    /// Format des Rückgabewerts: "sha256:<hex>"
    TNG_EXPORT std::string hash_files(const std::vector<std::string>& paths);

} // namespace TNG_NAMESPACE::spec
