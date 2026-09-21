#pragma once

// [stdc++]
#include <memory>
#include <string>
// [tng/public]
#include <iso8583/config.h>
#include <iso8583/ISOSpec.hh>
#include <iso8583/detail/_interfaces.hh>
// [tng/internal]
#include "_parser.hh"

// Hinweis: SpecDecoder ist in ISOSpec.hh deklariert.
// _spec.hh existiert nur noch um die internen Parser-Header einzubinden
// die _spec.cc für die Implementierung braucht.

namespace TNG_NAMESPACE::spec {
    // (0.6.0, FE-1) Testnaht (nur für Tests, keine Produktions-API):
    // Lädt das Field-only-YAML unter `path` über denselben Preprocessor-
    // und SourceMap-Pfad wie der Loader (loadAndParse) und wirft
    // validateFieldSpecYaml darüber. Existiert, weil die Field-only-
    // Loader-Eintritte (loadField*) erst mit WP3/0.6.0 vorhanden sind –
    // WP1 validiert so bereits die Validierung (TDD-RED).
    TNG_EXPORT void validateFieldSpecYamlFile(const std::string& path);
}
