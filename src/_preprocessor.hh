#pragma once

// [stdc++]
#include <string>
// [ryml]
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
// [tng]
#include <iso8583/config.h>
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
    };

    class TNG_EXPORT SpecPreProcessor {
    public:
        /// Verarbeitet eine YAML-Spec-Datei und baut gleichzeitig eine SourceMap auf.
        /// Die SourceMap wird automatisch als Sidecar (path + ".smap") gespeichert
        /// wenn sie noch nicht existiert oder veraltet ist.
        ///
        /// @param trackSourceMap  Bei `false` wird KEINE Positionsverfolgung pro
        ///        YAML-Knoten durchgeführt. Fehlermeldungen fallen dann auf eine
        ///        generische, weniger präzise Angabe zurück - für eine Spec
        ///        ohne !use/!template/!merge/!include_files (der Normalfall)
        ///        bleibt die Präzision unverändert hoch. Existiert bereits eine
        ///        gültige `.smap`-Sidecar-Datei, wird die trotzdem genutzt.
        static PreprocessResult preprocessWithSourceMap(
            const std::string& path, bool trackSourceMap = true);
    };

}
