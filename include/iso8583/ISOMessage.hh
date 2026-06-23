#pragma once

/**
 * @file ISOMessage.hh
 * @brief Öffentliche Datentypen für ISO 8583-Nachrichten.
 *
 * Dieser Header enthält alle Typen, mit denen ein normaler Anwender
 * direkt arbeitet:
 *
 *  - @ref tng::ISOMessage        – die ISO 8583-Nachricht selbst
 *  - @ref ISOOpaqueField         – alphanumerisches Datenfeld (std::string)
 *  - @ref ISOBinaryField         – binäres Datenfeld (std::vector<uint8_t>)
 *  - @ref ISOBitmap              – Bitmap-Feld
 *  - @ref ISOCodeField           – numerisches Code-Feld (int32_t)
 *  - @ref tng::BaseHeader        – einfacher Rohdaten-Header
 *  - @ref tng::BASE1Header       – Visa/BASE1-Header
 *  - @ref tng::WLP_FOHeader      – WLP FO-Header
 *  - @ref tng::ISOUtils          – Hilfsfunktionen (z. B. flatten)
 *
 * Typischer Anwendungsfall:
 * @code
 *   #include <iso8583/ISOMessage.hh>
 *   #include <iso8583/ISOSpec.hh>
 *
 *   // Spezifikation laden
 *   auto parser = tng::spec::SpecDecoder::loadFromYaml("visa.yaml");
 *
 *   // Nachricht entpacken (unparse = Bytes → Felder)
 *   auto msg = std::make_shared<tng::ISOMessage>();
 *   msg->parser(parser);
 *   msg->unparse(msg, rawBytes);
 *
 *   // Felder lesen
 *   if (auto pan = msg->tryGet<ISOOpaqueField>(2))
 *       std::cout << "PAN: " << (*pan)->readable_value() << "\n";
 *
 *   // Nachricht als JSON ausgeben
 *   std::cout << msg->to_json().dump(2) << "\n";
 * @endcode
 */

#include "detail/_components.hh"
