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
 *
 * @par Thread-Sicherheit (ab 0.3.0)
 * Eine @ref iso8583::Message kann aus **N Threads gleichzeitig** genutzt
 * werden: Alle öffentlichen Einstiegspunkte (set/unset/has/get/tryGet* /
 * reset/keys/size/to_json/dump/parser/parse/unparse/header/direction/
 * hasMTI/mti/isRequest …) nehmen denselben (rekursiven) Message-Lock genau
 * EINMAL; interne Aufrufketten (z. B. parse → recalcBitmap → set) laufen
 * unter dem gehaltenen Lock. Writer und Reader sind sich gegenseitig
 * exklusiv (ein Lock, kein paralleler Read-Modus).
 *
 * Parser sind nach dem Laden **unveränderlich** und dürfen deshalb
 * thread-übergreifend und über mehrere Nachrichten hinweg geteilt werden
 * (parallele parse()/unparse()-Aufrufe auf *verschiedenen* Nachrichten mit
 * demselben Parser sind sicher).
 *
 * **Restrisiko (dokumentiert):** `mti()` liefert ein `string_view` **in
 * den mutierbaren Feld-Speicher** der Nachricht – vor thread-übergreifender
 * Nutzung kopieren: `std::string m = msg->mti();`. Gleiches gilt für
 * `tryGetValueRef` (Zero-Copy-Referenz).
 */

#include "detail/_components.hh"
