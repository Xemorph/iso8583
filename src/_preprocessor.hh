#pragma once

// [stdc++]
#include <string>
// [yaml-cpp]
#include <yaml-cpp/yaml.h>
// [tng]
#include <iso8583/config.h>

namespace TNG_NAMESPACE {

    namespace spec {

        class TNG_EXPORT SpecPreProcessor {
        public:
            static YAML::Node preprocessFile(const std::string& path);
        };

    }

}