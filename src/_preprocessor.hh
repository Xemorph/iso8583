#pragma once

// [stdc++]
#include <string>
// [yaml-cpp]
#include <yaml-cpp/yaml.h>
// [tng]
#include <iso8583/config.h>
// [tng/internal]
#include "_sourcemap.hh"

namespace TNG_NAMESPACE::spec {

    struct PreprocessResult {
        YAML::Node node;
        SourceMap  source_map;
    };

    class TNG_EXPORT SpecPreProcessor {
    public:
        /// Verarbeitet eine YAML-Spec-Datei und gibt das prozessierte YAML zurück.
        /// Rückwärtskompatibel – ohne SourceMap.
        static YAML::Node preprocessFile(const std::string& path);

        /// Verarbeitet eine YAML-Spec-Datei und baut gleichzeitig eine SourceMap auf.
        /// Die SourceMap wird automatisch als Sidecar (path + ".smap") gespeichert
        /// wenn sie noch nicht existiert oder veraltet ist.
        ///
        /// @param trackSourceMap  Bei `false` wird KEINE Positionsverfolgung pro
        ///        YAML-Knoten durchgeführt (das ist der dominante Kostenfaktor
        ///        beim Laden - siehe Kommentar in _preprocessor.cc). Fehler-
        ///        meldungen fallen dann auf die Position im bereits prozessierten
        ///        (also `!use`/`!template`/`!merge`-aufgelösten) Dokument zurück,
        ///        statt auf die ursprüngliche Datei/Zeile zu verweisen - für eine
        ///        Spec ohne diese Direktiven (der Normalfall) ist das identisch
        ///        genau, für Specs MIT ihnen weniger präzise. Existiert bereits
        ///        eine gültige `.smap`-Sidecar-Datei (z.B. aus einem früheren
        ///        Lauf mit `trackSourceMap=true`), wird die trotzdem genutzt -
        ///        volle Präzision "geschenkt", ganz ohne die teure Neuverfolgung.
        static PreprocessResult preprocessWithSourceMap(
            const std::string& path, bool trackSourceMap = true);
    };

}
