#pragma once

/**
 * @file ISOMessage.hh
 * @brief Öffentliche Datentypen für ISO 8583-Nachrichten.
 *
 * Dieser Header enthält alle Typen, mit denen ein normaler Anwender
 * direkt arbeitet:
 *
 *  - @ref iso8583::Message      – die ISO 8583-Nachricht selbst
 *  - @ref iso8583::OpaqueField  – alphanumerisches Datenfeld (std::string)
 *  - @ref iso8583::BinaryField  – binäres Datenfeld (std::vector<uint8_t>)
 *  - @ref iso8583::Bitmap       – Bitmap-Feld
 *  - @ref iso8583::CodeField    – numerisches Code-Feld (int32_t)
 *  - @ref iso8583::BaseHeader   – einfacher Rohdaten-Header
 *  - @ref iso8583::BASE1Header  – Visa/BASE1-Header
 *  - @ref iso8583::WLP_FOHeader – WLP FO-Header
 *  - @ref iso8583::utils        – Hilfsfunktionen (z. B. flatten)
 *
 * Typischer Anwendungsfall:
 * @code
 *   #include <iso8583/ISOMessage.hh>
 *   #include <iso8583/ISOSpec.hh>
 *
 *   // Spezifikation laden
 *   auto parser = iso8583::spec::SpecDecoder::loadFromYaml("visa.yaml");
 *
 *   // Nachricht entpacken (unparse = Bytes → Felder)
 *   auto msg = std::make_shared<iso8583::Message>();
 *   msg->parser(parser);
 *   msg->unparse(msg, rawBytes);
 *
 *   // Felder lesen
 *   if (auto pan = msg->tryGet<OpaqueField>(2))
 *       std::cout << "PAN: " << (*pan)->readable_value() << "\n";
 *
 *   // Nachricht als JSON ausgeben
 *   std::cout << msg->to_json().dump(2) << "\n";
 * @endcode
 */

#include "detail/_components.hh"
