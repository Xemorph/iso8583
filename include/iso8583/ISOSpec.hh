#pragma once

/**
 * @file ISOSpec.hh
 * @brief YAML-basierter Spezifikations-Loader für ISO 8583-Parser.
 *
 * Dies ist der **empfohlene Weg** für Standardanwender, um einen Parser
 * zu konfigurieren. Die interne Parser-Klassenstruktur (`ISOBaseParser`,
 * `ISOFieldParser<>` usw.) bleibt vollständig verborgen.
 *
 * Beispiel:
 * @code
 *   #include <iso8583/ISOSpec.hh>
 *   #include <iso8583/ISOMessage.hh>
 *
 *   // Parser aus YAML-Datei erzeugen
 *   auto parser = tng::spec::SpecDecoder::loadFromYaml("visa_spec.yaml");
 *
 *   // Parser einer Nachricht zuweisen
 *   auto msg = std::make_shared<tng::ISOMessage>();
 *   msg->parser(parser);
 *
 *   // Bytes entpacken
 *   std::vector<uint8_t> rawBytes = ...;
 *   msg->unparse(msg, rawBytes);
 * @endcode
 *
 * @note Der zurückgegebene `ISOParserPtrBaseSmartPtr` kann direkt an
 *       `ISOMessage::parser()` übergeben werden. Eine Kenntnis der
 *       konkreten Parser-Implementierung ist nicht erforderlich.
 */
// [tng]
#include "config.h"
#include "ISOParser.hh"
// [stdc++]
#include <memory>
#include <string>

namespace TNG_NAMESPACE {
    namespace spec {

        /**
         * @brief Lädt eine ISO 8583-Parser-Konfiguration aus einer YAML-Datei.
         *
         * Die YAML-Datei beschreibt die Spezifikation der Datenelemente (DEs),
         * Encoding, Längenfelder und Bitmaps.
         *
         * @return Opaker Smart-Pointer auf den konfigurierten Parser.
         *         Kann direkt an `ISOMessage::parser()` übergeben werden.
         * @throws std::runtime_error bei ungültiger oder nicht lesbarer YAML-Datei.
         */
        class TNG_EXPORT SpecDecoder {
        public:
            static ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr
                loadFromYaml(const std::string& /*path*/ );
        };

    }
}
