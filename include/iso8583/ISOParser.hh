#pragma once

/**
 * @file ISOParser.hh
 * @brief Erweiterter Parser-Zugang für erfahrene Anwender.
 *
 * @warning Dieser Header ist für **erfahrene Anwender** gedacht, die einen
 *          eigenen Parser implementieren möchten. Für den Standardfall
 *          reicht @ref ISOSpec.hh vollständig aus.
 *
 * Durch Einbinden dieses Headers erhält man Zugriff auf:
 *  - `ISOParserPtrBase`      – abstrakte Basisklasse für jeden Parser
 *  - `ISOFieldParserPtrBase` – abstrakte Basisklasse für Feld-Parser
 *  - `ISOFieldParserType`    – Enum der Feld-Typen (BITMAP, BINARY, OPAQUE, …)
 *
 * Die konkreten Implementierungen (`ISOBaseParser`, `ISOFieldParser<>`,
 * alle `IFE_*`/`IFA_*`-Aliase aus `fmt_types.hh`) sind **bewusst nicht**
 * Teil der öffentlichen API. Sie befinden sich im `src/`-Verzeichnis
 * der Bibliothek und sind ausschließlich für die interne Nutzung oder
 * für Anwender gedacht, die die Bibliotheksquellen direkt einbinden.
 *
 * Minimales Beispiel für einen eigenen Parser:
 * @code
 *   #include <iso8583/ISOParser.hh>
 *   #include <iso8583/ISOMessage.hh>
 *   #include <iso8583/_codec.hh>
 *
 *   class MyCustomParser : public tng::ISOParserPtrBase {
 *   public:
 *       bool emit_bitmap() noexcept override { return true; }
 *
 *       std::size_t unparse(
 *           tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
 *           const std::vector<uint8_t>& b) override
 *       {
 *           // ... eigene Implementierung ...
 *           return b.size();
 *       }
 *
 *       std::vector<uint8_t> parse(
 *           tng::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) override
 *       {
 *           // ... eigene Implementierung ...
 *           return {};
 *       }
 *   };
 * @endcode
 */

#include "config.h"
#include "detail/_interfaces.hh"
