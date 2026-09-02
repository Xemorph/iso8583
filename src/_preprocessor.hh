#pragma once

// [stdc++]
#include <string>
#include <vector>
// [ryml]
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
// [tng]
#include <iso8583/config.h>
#include <iso8583/ISOSpec.hh>
// [tng/internal]
#include "_sourcemap.hh"

namespace TNG_NAMESPACE::spec {

    // PreprocessResult::tree - die Wurzel enthält DIREKT die Top-Level-Keys
    // der Spec (spec/encoding/header/fields/...), kein Wrapper-Knoten. Der
    // Baum ist vollständig eigenständig (keine Abhängigkeit von irgendeiner
    // Datei-Arena oder einem anderen Tree - alle Strings leben in seiner
    // eigenen Arena).
    //
    // source_map: Positions-Herkunft, aber mit einer WICHTIGEN Änderung
    // gegenüber der ursprünglichen yaml-cpp-Version - der Tracking-Key ist
    // NICHT mehr eine Zeilennummer im prozessierten Dokument, sondern die
    // ryml::id_type (Knoten-ID) des jeweiligen Knotens INNERHALB von `tree`.
    // Grund: ryml erlaubt Positions-Abfragen nur für den zuletzt geparsten
    // Baum eines Parser-Objekts (siehe _preprocessor.cc) - es gibt daher
    // keine "prozessierte Zeile" mehr, die nach dem Preprocessing noch
    // sinnvoll wäre. Die Knoten-ID ist dagegen stabil für die gesamte
    // Lebensdauer von `tree` und damit ein zuverlässigerer Schlüssel.
    struct PreprocessResult {
        ryml::Tree tree;
        SourceMap  source_map;
        // [ISO8583] 3.2 (Sicherheits-Audit): alle beim Load gelesenen
        // Quelldateien (Top-Level zuerst, absolut, Dedupliziert) - Grundlage
        // fuer den Content-Snapshot-Hash des Spec-Caches (publish-then-verify).
        std::vector<std::string> sourceFiles;
    };

    class TNG_EXPORT SpecPreProcessor {
    public:
        /// Verarbeitet eine YAML-Spec-Datei und baut gleichzeitig eine SourceMap auf.
        ///
        /// Seit 0.3.0 steuert `opts` (SpecLoadOptions) zusätzlich:
        ///   - Include-Sandbox: `!include_files`-Pfade, die außerhalb der
        ///     erlaubten Wurzeln (Default: Verzeichnis der Top-Level-Spec) auflösen,
        ///     werfen (fail-closed) - schützt vor `../`-Traversals, absoluten
        ///     Pfaden und Symlink-Escapes (s. SpecLoadOptions::sandbox).
        ///   - Ressourcenlimits: maxSpecBytes (je Datei, wird beim Einlesen
        ///     erzwungen), maxIncludeFiles (Gesamtzahl), maxSmapBytes (Sidecar-Lesen).
        ///   - Sidecar-Schreiben: nur wenn `allowSmapWrite` UND innerhalb der
        ///     Sandbox-Wurzeln - der Load selbst ist davon unberührt.
        static PreprocessResult preprocessWithSourceMap(
            const std::string& path, const SpecLoadOptions& opts);
    };

}
