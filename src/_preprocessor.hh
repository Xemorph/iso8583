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
        static PreprocessResult preprocessWithSourceMap(const std::string& path);
    };

}
