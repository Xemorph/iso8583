#pragma once

// [stdc++]
#include <memory>
#include <string>
// [tng/public]
#include <iso8583/config.h>
#include <iso8583/detail/_interfaces.hh>
// [tng/internal]
#include "_parser.hh"

namespace TNG_NAMESPACE {
    namespace spec {

    // Interne Implementierungsklasse.
    // Nach außen ist nur ISOSpec.hh / SpecDecoder mit dem Rückgabetyp
    // ISOParserPtrBaseSmartPtr sichtbar.
    class TNG_EXPORT SpecDecoder {
    public:
        // Gibt ISOParserPtrBaseSmartPtr zurück (nicht ISOBaseParser),
        // damit die interne Parser-Hierarchie nach außen verborgen bleibt.
        static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
            loadFromYaml(const std::string& path);
    };

    }
}
