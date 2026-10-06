#include "_parser.hh"
// [tng]
#include "_logger.hh"

namespace {

    // Fallback-Bitmap-Berechnung für ISOBaseParser::parse(): wird nur
    // erreicht, wenn KEIN "format: bitmap"-Feldparser an Position 1 der Spec
    // deklariert ist (ein Custom-Protokoll ohne regulären Bitmap-Typ) - bei
    // jeder normalen ISO-8583-Spec läuft stattdessen der Zweig, der die
    // bereits korrekte Bitmap-Komponente (m->tryGet<Bitmap>(-1)) über den
    // Bitmap-Feldparser encodiert (siehe ISOBaseParser::parse() unten).
    //
    // Bit-Platzierung folgt derselben Standard-Konvention wie überall sonst
    // in der Bibliothek (bmp[N], 1-indiziert, = DE N - siehe byte2bitset()
    // weiter unten in dieser Datei und ISOBitmapFieldParser::parse() in
    // _parser.hh): DE1/65/129 sind reservierte Extension-Indikator-
    // Positionen und werden deshalb übersprungen statt als reguläre Felder
    // behandelt; die Secondary-/Tertiary-Präsenzbits werden automatisch aus
    // der höchsten tatsächlich gesetzten DE abgeleitet.
    std::vector<uint8_t> buildFallbackBitmap(
        const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m,
        std::size_t field_count)
    {
        const TNG_KEY_TYPE max_de = std::min<TNG_KEY_TYPE>(
            static_cast<TNG_KEY_TYPE>(field_count) - 1, 192);

        std::vector<uint8_t> bmp(24, 0x00);
        std::size_t bmp_sz = 8;

        for (TNG_KEY_TYPE i = 2; i <= max_de; ++i) {
            if (i == 65 || i == 129) continue; // reservierte Indikator-Slots
            if (!m->has(i)) continue;

            if (i > 128)      bmp_sz = 24;
            else if (i > 64)  bmp_sz = std::max(bmp_sz, std::size_t{ 16 });

            const std::size_t p = static_cast<std::size_t>(i) - 1;
            const std::size_t byte_idx = p / 8;
            const std::size_t bit_idx = 7 - (p % 8);
            bmp[byte_idx] |= static_cast<uint8_t>(1u << bit_idx);
        }

        if (bmp_sz >= 16) bmp[0] |= 0x80u; // Secondary Bitmap Present
        if (bmp_sz >= 24) bmp[8] |= 0x80u; // Tertiary Bitmap Present

        bmp.resize(bmp_sz);
        return bmp;
    }

} // namespace

// ─── ISOBaseParser::parse ─────────────────────────────────────────────────────
// Serialisiert eine ISOMessage in einen Wire-Buffer.
// Inverse Logik zu unparse():
//   1. Header-Bytes (falls vorhanden, als Nullen – Anwender füllt sie selbst)
//   2. MTI (Slot 0)
//   3. Bitmap aus den gesetzten Feldern berechnen und encodieren
//   4. Datenfelder in Reihenfolge der Parser-Liste
std::vector<uint8_t> TNG_NAMESPACE::ISOBaseParser::parse(
    ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c) const
{
    if (!c || !c->is_composite()) {
        // [ISO8583] B4: struktureller Fehler -> Fail-closed, statt stummer
        // Leer-Serialisierung ("parse erfolgreich mit 0 Bytes").
        throw std::runtime_error("[ISO8583] Parser: Komponente ist null oder kein Composite");
    }

    if (l_.empty()) {
        throw std::runtime_error("[ISO8583] Parser nicht konfiguriert (keine Felder definiert)");
    }

    auto m = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOMessage>(c);
    if (!m) {
        throw std::runtime_error("[ISO8583] Parser: Komponente ist kein ISOMessage");
    }

    // FR-12/FR-13 (0.9.0): Container-Sondermodi (eigene Encode-Pfade, kein
    // MTI/Header/Top-Level-Bitmap).
    if (container_ && bitmap_container_ > 0)
        return parseBitmapContainer(m);
    if (container_ && nibble_pack_)
        return parseNibblePack(m);

    std::vector<uint8_t> out;

    // ── 1. Header ────────────────────────────────────────────────────────────
    // Header-Bytes als Nullen reservieren; Anwender befüllt via ISOMessage::header()
    if (hdr_sz_ > 0) {
        auto hdr = m->header();
        if (hdr && hdr->size() >= hdr_sz_) {
            std::vector<uint8_t> hdr_bytes = hdr->pack();
            // [ISO8583] A2: Inkonsistente Header-Serialisierung (gepackte
            // Bytes < hdr_sz_) wäre früher ein OOB-Read (begin()+hdr_sz_)
            // und/oder ein verkleinertes Wire-Image (verlorener Präfix).
            if (hdr_bytes.size() < hdr_sz_)
                throw std::runtime_error("[ISO8583] Header-Serialisierung inkonsistent: gepackte Bytes " +
                    std::to_string(hdr_bytes.size()) + " < erwartet " + std::to_string(hdr_sz_));
            out.insert(out.end(), hdr_bytes.begin(), hdr_bytes.begin() + std::ptrdiff_t(hdr_sz_));
        }
        else
            out.insert(out.end(), hdr_sz_, 0x00);
    }

    // ── 2. MTI ───────────────────────────────────────────────────────────────
    {
        // FR-3 (0.6.0): Container-Sub-Parser kennen kein MTI – Slot 0
        // ist das erste Kind-Feld. Ohne Guard würde bei genau einem
        // Kind der MTI-Block UND der Daten-Loop dasselbe Feld
        // serialisieren (Doppel-Serialisierung, verkerrtes Längen-Prefix).
        auto p = container_ ? nullptr : l_.at(::TNG_NAMESPACE::ISOMessage::MTI_KEY);
        if (p &&
            p->type() != ISOFieldParserType::BITMAP &&
            p->type() != ISOFieldParserType::UNUSED)
        {
            auto mti_comp = m->get<ISOComponentPtrBase>(::TNG_NAMESPACE::Message::MTI_KEY);
            if (mti_comp) {
                auto mti_bytes = p->parse(mti_comp);
                out.insert(out.end(), mti_bytes.begin(), mti_bytes.end());
                TNG_LOG_TRACE("[ISOBaseParser::parse] MTI: {} bytes", mti_bytes.size());
            }
            else {
                TNG_LOG_WARN("[ISOBaseParser::parse] Kein MTI-Feld (DE0) gesetzt");
            }
        }
    }

    // ── 3. Bitmap berechnen ───────────────────────────────────────────────────
    const TNG_KEY_TYPE ff = first_field();

    if (emit_bitmap() && l_.size() > 1) {
        auto p = l_.at(1);
        if (p && p->type() == ISOFieldParserType::BITMAP) {
            // Regulärer Pfad (praktisch immer aktiv, sobald eine Spec ein
            // "format: bitmap"-Feld deklariert): die Bitmap-Komponente wurde
            // bereits korrekt befüllt - entweder 1:1 aus der Original-Wire
            // beim Dekodieren (siehe unparse(); erhält dabei auch Bits für
            // undeklarierte/private Felder) oder frisch via
            // ISOMessage::recalcBitmap() beim Aufbau einer neuen Nachricht.
            // Ein direkter Parser-Aufruf (parser->parse(msg)) an einer
            // nie dekodierten/ausgebauten Nachricht überspringt diese
            // Materialisierung - fail-closed statt
            // std::bad_optional_access an ferner Stelle (FR-3, 0.6.0).
            // Hier wird sie nur noch encodiert - kein erneutes Bit-Setzen.
            auto bmp_comp = m->tryGet< ::TNG_NAMESPACE::Bitmap >(::TNG_NAMESPACE::Message::BITMAP_KEY);
            if (!bmp_comp)
                throw std::runtime_error("[ISO8583] Parser: Bitmap-Komponente fehlt (direkter Parser-Aufruf) - ISOMessage::parse() verwenden oder die Nachricht dekodieren");
            auto encoded = p->parse(*bmp_comp);
            out.insert(out.end(), encoded.begin(), encoded.end());
            TNG_LOG_TRACE("[ISOBaseParser::parse] Bitmap: {} bytes", encoded.size());
        }
        else {
            // Fallback: kein deklarierter BITMAP-Feldparser an Position 1
            // (Custom-Protokoll ohne "format: bitmap"-Feld) - siehe
            // buildFallbackBitmap() oben für Details zur Bit-Konvention.
            const auto bmp_buf = buildFallbackBitmap(m, l_.size());
            out.insert(out.end(), bmp_buf.begin(), bmp_buf.end());
        }
    }

    // ── 4. Datenfelder serialisieren ─────────────────────────────────────────
    // Hinweis: l_.at(i) gibt shared_ptr<const ISOFieldParserPtrBase> zurück.
    // parse() ist nicht const → const_pointer_cast nötig.
    CONST_ITERATOR _begin = l_.cbegin() + ff;
    CONST_ITERATOR _end = l_.cend();

    TNG_KEY_TYPE i = ff;
    std::for_each(_begin, _end,
        [&i, &m, &out, this]
        (const std::shared_ptr<const ::TNG_NAMESPACE::ISOFieldParserPtrBase>& ptr) -> void
        {
            // Bitmap-Extension-Slots überspringen (DE65, DE129 als Bitmap-Indikator)
            if (emit_bitmap() && (i == 65 || i == 129) &&
                ptr && ptr->type() == ISOFieldParserType::BITMAP) {
                ++i; return;
            }

            if (!ptr || ptr->type() == ISOFieldParserType::UNUSED) { ++i; return; }
            if (!m->has(i)) { ++i; return; }

            auto de_comp = m->get<ISOComponentPtrBase>(i);
            if (!de_comp) { ++i; return; }

            auto de_bytes = ptr->parse(de_comp);
            if (de_bytes.empty() && ptr->type() != ISOFieldParserType::REMAINING)
                TNG_LOG_WARN("[ISOBaseParser::parse] DE{:03d} '{}' lieferte leere Bytes",
                    i, nonstd::to_string(ptr->description()));

            TNG_LOG_DEBUG("[ISOBaseParser::parse] DE{:03d} '{}' → {} bytes",
                i, nonstd::to_string(ptr->description()), de_bytes.size());

            out.insert(out.end(), de_bytes.begin(), de_bytes.end());
            ++i;
        });

    TNG_LOG_INFO("[ISOBaseParser::parse] Fertig: {} bytes total", out.size());
    return out;
}

// --- Top-Level unparse (kein base_offset) ------------------------------------
std::size_t TNG_NAMESPACE::ISOBaseParser::unparse(
    ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
    const std::vector<uint8_t>& b)
{
    return unparse(c, b, 0u);
}

// --- unparse mit base_offset -------------------------------------------------
// base_offset: Byte-Position des Buffers b innerhalb der Original-Nachricht.
// Für Top-Level-Aufrufe ist das 0. Für nested-Felder ist es der Offset des
// Elternfeldes (nach seinem eigenen Längen-Prefix), sodass Kind-Offsets
// korrekt relativ zur Original-Nachricht angegeben werden.
std::size_t TNG_NAMESPACE::ISOBaseParser::unparse(
    ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
    const std::vector<uint8_t>& b,
    std::size_t base_offset)
{
    if (b.empty()) {
        // [ISO8583] P2: ein leeres Frame kann keine gültige ISO-8583-
        // Nachricht sein. strict: verwerfen; nicht-strikt: Legacy-Warnung.
        if (strict_)
            throw std::runtime_error("[ISO8583] Byte-Image ist leer - keine gültige ISO-8583-Nachricht");
        TNG_LOG_WARN("[ISOBaseParser] Empty byte image received");
        return 0u;
    }
    if (l_.empty()) {
        // [ISO8583] B4: struktureller Fehler -> Fail-closed.
        throw std::runtime_error("[ISO8583] Parser nicht konfiguriert (keine Felder definiert)");
    }
    if (c == nullptr || !c->is_composite()) {
        // [ISO8583] B4: struktureller Fehler -> Fail-closed.
        throw std::runtime_error("[ISO8583] Parser: Komponente ist null oder kein Composite");
    }

    auto m = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOMessage>(c);
    if (m == nullptr) {
        // [ISO8583] B4: struktureller Fehler -> Fail-closed.
        throw std::runtime_error("[ISO8583] Parser: Komponente ist kein ISOMessage");
    }

    // FR-12/FR-13 (0.9.0): Container-Sondermodi (eigene Decode-Pfade).
    if (container_ && bitmap_container_ > 0)
        return unparseBitmapContainer(m, b, base_offset);
    if (container_ && nibble_pack_)
        return unparseNibblePack(m, b, base_offset);

    std::size_t consumed = 0u;

    // Header
    if (hdr_sz_ != 0) {
        if (b.size() < hdr_sz_) {
            throw std::runtime_error(
                "[ISO8583] Byte-Image zu kurz für Header: benötigt " +
                std::to_string(hdr_sz_) + " Bytes, vorhanden " +
                std::to_string(b.size()) + " Bytes");
        }
        std::vector<uint8_t> h(hdr_sz_, 0x00);
        std::memcpy(h.data(), b.data(), hdr_sz_);
        consumed += hdr_sz_;
    }

    // -- MTI ------------------------------------------------------------------
    // FR-3 (0.6.0): Container-Modus – kein MTI, Slot 0 = erstes Kind-Feld.
    std::shared_ptr<const ::TNG_NAMESPACE::ISOFieldParserPtrBase> p =
        container_ ? nullptr : l_.at(::TNG_NAMESPACE::Message::MTI_KEY);
    if (p && (
        p->type() != ::TNG_NAMESPACE::ISOFieldParserType::BITMAP &&
        p->type() != ::TNG_NAMESPACE::ISOFieldParserType::UNUSED))
    {
        TNG_LOG_TRACE("[ISOBaseParser] [  MTI] '{}' offset={} use_count={}",
            p->description(), base_offset + consumed, p.use_count());

        auto mti = p->create_component(::TNG_NAMESPACE::Message::MTI_KEY);
        mti->description(p->description());
        // [ISO8583] 3.4 (PCI): Sensitive-Marker vom Parser auf das Component
        // uebertragen (Spec: 'sensitive: true').
        mti->set_sensitive(p->sensitive());
        mti->wire_offset(base_offset + consumed);
        std::size_t mti_bytes;
        try {
            mti_bytes = p->unparse(mti, b, consumed);
        } catch (const std::exception& e) {
            // Feld-Fehler (z.B. EILSEQ aus iconv bei binären Bytes in EBCDIC-Feldern)
            // in die Positions-Exception-Konvention der Bibliothek übersetzen.
            throw std::runtime_error("[ISO8583] MTI @ Offset " +
                std::to_string(base_offset + consumed) + ": " + e.what());
        }
        mti->wire_length(mti_bytes);
        consumed += mti_bytes;
        // [ISO8583] B3: set() kann fehlschlagen (z.B. OOM) - dann wäre die
        // Nachricht nur teilweise dekodiert (stille Datenkorruption).
        if (!m->set(mti))
            throw std::runtime_error("[ISO8583] MTI: ISOMessage::set fehlgeschlagen (Speicherfehler?)");
    }

    // -- Bitmap ---------------------------------------------------------------
    dynamic_bitset<> bmp;
    std::size_t bmp_sz = 0;
    TNG_KEY_TYPE hf = (TNG_KEY_TYPE)(l_.size() - 1);

    if (emit_bitmap()) {
        TNG_LOG_TRACE("[ISOBaseParser] BMAP1 '{}' offset={}",
            l_.at(1)->description(), base_offset + consumed);

        auto bitmap = std::make_shared< ::TNG_NAMESPACE::Bitmap >(::TNG_NAMESPACE::Message::BITMAP_KEY);
        bitmap->description(l_.at(1)->description());
        // [ISO8583] 3.4: Bitmap ist strukturell (Setzliste von DE-Nummern) -
        // der Marker wird uebernommen, wirkt sich aber auf dump() nicht aus.
        bitmap->set_sensitive(l_.at(1)->sensitive());
        bitmap->wire_offset(base_offset + consumed);
        std::size_t bmp_bytes;
        try {
            bmp_bytes = l_.at(1)->unparse(bitmap, b, consumed);
        } catch (const std::exception& e) {
            throw std::runtime_error("[ISO8583] Bitmap @ Offset " +
                std::to_string(base_offset + consumed) + ": " + e.what());
        }
        bitmap->wire_length(bmp_bytes);
        consumed += bmp_bytes;
        // FR-9b (0.8.0): Bit 1 zeigt eine Sekundär-Bitmap an, der Bitmap-
        // Feldparser hat aber nur die Primär-Bitmap gelesen (Spec: 'length'
        // < 16). Die Folgefelder wären stillschweigend um 8 Byte verschoben.
        // strict: positionierter Fehler; nicht-strikt: Warnung (Legacy).
        {
            const auto& peek = bitmap->value();
            if (peek.size() > 1 && peek[1] && bmp_bytes < 16) {
                const std::string msg =
                    "Bitmap @ Offset " + std::to_string(base_offset + consumed - bmp_bytes) +
                    ": Bit 1 (Sekundär-Bitmap) ist gesetzt, die Spec deklariert für das "
                    "Bitmap-Feld aber nur " + std::to_string(bmp_bytes) +
                    " Byte ('length: " + std::to_string(bmp_bytes) +
                    "') - 'length: 16' setzen (FR-9), sonst wären alle Folgefelder verschoben";
                if (strict_)
                    throw std::runtime_error("[ISO8583] " + msg);
                TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Legacy-Dekodierung)", msg);
            }
        }
        bmp = bitmap->value();
        bmp.shrink_to_fit();
        bmp_sz = ((bmp.size() - 1 + 63) >> 6) << 3;
        // [ISO8583] B3: siehe MTI oben.
        if (!m->set(bitmap))
            throw std::runtime_error("[ISO8583] Bitmap: ISOMessage::set fehlgeschlagen (Speicherfehler?)");

        // [ISO8583] P4: find_first() liefert npos, wenn die Bitmap gar
        // keine Bits gesetzt hat (z.B. all-zero-Bitmap). Der alte Code
        // konvertierte npos nach TNG_KEY_TYPE (mit int16_t-Keys: 0xFFFF ->
        // -1) und klemmte hf auf -1, wodurch der Feld-Loop-Range unten
        // invertiert wurde (_begin > _end). std::for_each lief dann ueber
        // das Ende des Parser-Deques hinaus: "cannot seek deque iterator
        // out of range" (Debug-Assert) bzw. undefiniertes Verhalten
        // (Release-Build). Ohne gesetzte Bits bleibt der volle Feld-Range
        // erhalten; der Loop ueberspringt alle abwesenden Felder, da
        // bmp[i] == false ist.
        auto last_idx = bmp.find_first();
        if (last_idx != dynamic_bitset<>::npos) {
            for (auto idx = last_idx; idx != dynamic_bitset<>::npos; ) {
                last_idx = idx;
                idx = bmp.find_next(idx);
            }
            hf = std::min(hf, (TNG_KEY_TYPE)last_idx);
        }
    }

    // -- Datenfelder ----------------------------------------------------------
    CONST_ITERATOR _begin = l_.cbegin() + first_field();
    CONST_ITERATOR _end   = (hf + 1 < (TNG_KEY_TYPE)l_.size())
                          ? (l_.cbegin() + hf + 1)
                          : l_.cend();
    if (_end < _begin) {
        // [ISO8583] P4: Defensive in der Tiefe - eine invertierte Range
        // wuerde std::for_each ueber das Ende der Parser-Liste (deque)
        // laufen lassen.
        _end = _begin;
    }

    TNG_KEY_TYPE i = first_field();
    std::for_each(_begin, _end,
        [&i, &hf, &bmp, &bmp_sz, &consumed, &b, &m, &base_offset, this]
        (const std::shared_ptr<const ::TNG_NAMESPACE::ISOFieldParserPtrBase>& ptr) -> void
    {
        if (consumed == b.size()) {
            TNG_LOG_INFO("[ISOBaseParser] All bytes consumed after {} bytes", consumed);
            return;
        }

        if (hf > 128 && i == 65)
            return ((void)(++i));

        if (bmp.size() == 0 || bmp[i])
            if (ptr->type() != ::TNG_NAMESPACE::ISOFieldParserType::UNUSED) {
                TNG_LOG_DEBUG("[ISOBaseParser] DE{:03d} '{}' offset={} use_count={} buf_size={}",
                    i, nonstd::to_string(ptr->description()),
                    base_offset + consumed, ptr.use_count(), b.size());

                auto de = ptr->create_component(i);
                de->description(ptr->description());
                // [ISO8583] 3.4 (PCI): Sensitive-Marker vom Parser auf das
                // Component uebertragen (Spec: 'sensitive: true').
                de->set_sensitive(ptr->sensitive());
                de->wire_offset(base_offset + consumed);
                std::size_t de_bytes;
                try {
                    de_bytes = ptr->unparse(de, b, consumed);
                } catch (const std::exception& e) {
                    // Feld-Fehler (z.B. EILSEQ aus iconv bei binären Bytes in
                    // EBCDIC-Feldern) in die Positions-Exception-Konvention der
                    // Bibliothek übersetzen: sauberes, positioniertes
                    // std::runtime_error statt Crash/nacktem std::system_error.
                    // Bereits positionierte Fehler (z.B. aus Nested-Sub-Parsern)
                    // werden nur um den äußeren Feld-Kontext ergänzt.
                    char de_key[16];
                    std::snprintf(de_key, sizeof(de_key), "DE%03d", static_cast<int>(i));
                    throw std::runtime_error(
                        std::string("[ISO8583] ") + de_key + " '" +
                        std::string(ptr->description()) + "' @ Offset " +
                        std::to_string(base_offset + consumed) + ": " + e.what());
                }
                de->wire_length(de_bytes);
                consumed += de_bytes;
                // [ISO8583] B3: set() kann fehlschlagen (z.B. OOM) - dann
                // wäre die Nachricht nur teilweise dekodiert.
                if (!m->set(de))
                    throw std::runtime_error("[ISO8583] DE" + std::to_string(i) +
                        ": ISOMessage::set fehlgeschlagen (Speicherfehler?)");
            }

        ++i;
    });

    if (consumed != b.size()) {
        // [ISO8583] P2: unkonvertierte Rest-Bytes. strict: verwerfen;
        // nicht-strikt: Legacy-Logmeldung (Datenverlust bleibt möglich).
        if (strict_)
            throw std::runtime_error("[ISO8583] Unverbrauchte Bytes am Pufferende: " +
                std::to_string(b.size() - consumed) + " von " + std::to_string(b.size()) +
                " Bytes nicht konvertiert (Nachrichtenstruktur deckt das Byte-Image nicht vollständig ab)");
        TNG_LOG_ERROR("[ISOBaseParser] Byte consumption mismatch: expected={} actual={}",
            b.size(), consumed);
    }

    return consumed;
}


// ─── FR-12 (0.9.0): Bitmap-Container ─────────────────────────────────────────
// Sub-Payload = Bitmap (bitmap_container_ Byte) + die Kinder, deren Bit
// gesetzt ist. Bit n (1-indiziert, MSB des ersten Bytes = Bit 1) gehoert zu
// Slot n; Bit 1 ist ein NORMALES Kind (die Sekundaer-Bitmap-Semantik des
// Top-Level-Parsers gilt hier nicht). Die Bitmap-Komponente liegt wie bei
// Top-Level-Nachrichten unter Message::BITMAP_KEY (-1) - Kind-Key 1 bleibt frei.
std::size_t TNG_NAMESPACE::ISOBaseParser::unparseBitmapContainer(
    const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m,
    const std::vector<uint8_t>& b,
    std::size_t base_offset)
{
    const std::size_t n_bytes = bitmap_container_;
    if (b.size() < n_bytes) {
        const std::string msg = "Bitmap-Container @ Offset " + std::to_string(base_offset) +
            ": Bitmap am Pufferende abgeschnitten: benoetigt " + std::to_string(n_bytes) +
            " Byte, vorhanden " + std::to_string(b.size()) + " Byte";
        if (strict_)
            throw std::runtime_error("[ISO8583] " + msg);
        TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Container bleibt leer)", msg);
        return 0u;
    }

    dynamic_bitset<> bmp(n_bytes * 8 + 1); // Index 0 ungenutzt (wie ueberall)
    for (std::size_t i = 0; i < n_bytes * 8; ++i)
        if (b[i >> 3] & (0x80u >> (i & 7u)))
            bmp.set(i + 1);

    auto bitmap = std::make_shared< ::TNG_NAMESPACE::Bitmap >(::TNG_NAMESPACE::Message::BITMAP_KEY);
    bitmap->description("Bitmap");
    bitmap->wire_offset(base_offset);
    bitmap->wire_length(n_bytes);
    bitmap->value(bmp);
    if (!m->set(bitmap))
        throw std::runtime_error("[ISO8583] Bitmap-Container: Bitmap ISOMessage::set fehlgeschlagen (Speicherfehler?)");

    std::size_t consumed = n_bytes;
    for (std::size_t bit = 1; bit <= n_bytes * 8; ++bit) {
        if (!bmp[bit])
            continue;
        const auto ptr = (bit < l_.size()) ? l_[bit] : nullptr;
        if (!ptr || ptr->type() == ::TNG_NAMESPACE::ISOFieldParserType::UNUSED) {
            // Ohne Kind-Definition ist die Laenge der Folgebytes unbekannt -
            // ein stilles Weiterlesen waere eine Fehlinterpretation.
            const std::string msg = "Bitmap-Container @ Offset " + std::to_string(base_offset) +
                ": Bit " + std::to_string(bit) + " ist gesetzt, die Spec deklariert dafuer "
                "kein Kind - Folgebytes sind nicht dekodierbar";
            if (strict_)
                throw std::runtime_error("[ISO8583] " + msg);
            TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Dekodierung wird abgebrochen)", msg);
            break;
        }

        TNG_LOG_DEBUG("[ISOBaseParser] Bit{:03d} '{}' offset={} buf_size={}",
            bit, nonstd::to_string(ptr->description()), base_offset + consumed, b.size());

        const auto key = static_cast<TNG_KEY_TYPE>(bit);
        auto de = ptr->create_component(key);
        de->description(ptr->description());
        de->set_sensitive(ptr->sensitive());
        de->wire_offset(base_offset + consumed);
        std::size_t de_bytes;
        try {
            de_bytes = ptr->unparse(de, b, consumed);
        } catch (const std::exception& e) {
            char de_key[16];
            std::snprintf(de_key, sizeof(de_key), "DE%03d", static_cast<int>(bit));
            throw std::runtime_error(
                std::string("[ISO8583] ") + de_key + " '" +
                std::string(ptr->description()) + "' @ Offset " +
                std::to_string(base_offset + consumed) + ": " + e.what());
        }
        de->wire_length(de_bytes);
        consumed += de_bytes;
        if (!m->set(de))
            throw std::runtime_error("[ISO8583] DE" + std::to_string(bit) +
                ": ISOMessage::set fehlgeschlagen (Speicherfehler?)");
    }

    if (consumed != b.size()) {
        if (strict_)
            throw std::runtime_error("[ISO8583] Unverbrauchte Bytes am Pufferende: " +
                std::to_string(b.size() - consumed) + " von " + std::to_string(b.size()) +
                " Bytes nicht konvertiert (Nachrichtenstruktur deckt das Byte-Image nicht vollstaendig ab)");
        TNG_LOG_ERROR("[ISOBaseParser] Byte consumption mismatch: expected={} actual={}",
            b.size(), consumed);
    }
    return consumed;
}

std::vector<uint8_t> TNG_NAMESPACE::ISOBaseParser::parseBitmapContainer(
    const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m) const
{
    const std::size_t n_bytes = bitmap_container_;

    // Kinder ohne deklarierten Slot waeren auf dem Wire nicht adressierbar
    // (kein Bit): strict -> positionierter Fehler, sonst Warnung + auslassen.
    for (const auto k : m->keys()) {
        const bool declared = k >= 1 && static_cast<std::size_t>(k) < l_.size() &&
            static_cast<std::size_t>(k) <= n_bytes * 8 && l_[static_cast<std::size_t>(k)] &&
            l_[static_cast<std::size_t>(k)]->type() != ::TNG_NAMESPACE::ISOFieldParserType::UNUSED;
        if (declared)
            continue;
        const std::string msg = "Bitmap-Container: Kind " + std::to_string(k) +
            " ist in der Spec nicht deklariert (kein Bit in der " +
            std::to_string(n_bytes) + "-Byte-Bitmap)";
        if (strict_)
            throw std::runtime_error("[ISO8583] " + msg);
        TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Kind wird ausgelassen)", msg);
    }

    std::vector<uint8_t> out(n_bytes, 0x00);
    const std::size_t last = std::min(l_.size(), n_bytes * 8 + 1);
    for (std::size_t slot = 1; slot < last; ++slot) {
        const auto& ptr = l_[slot];
        if (!ptr || ptr->type() == ::TNG_NAMESPACE::ISOFieldParserType::UNUSED)
            continue;
        const auto key = static_cast<TNG_KEY_TYPE>(slot);
        if (!m->has(key))
            continue;
        auto comp = m->get<ISOComponentPtrBase>(key);
        if (!comp)
            continue;
        const auto bytes = ptr->parse(comp);
        out[(slot - 1) >> 3] |= static_cast<uint8_t>(0x80u >> ((slot - 1) & 7u));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    TNG_LOG_INFO("[ISOBaseParser::parseBitmapContainer] Fertig: {} bytes", out.size());
    return out;
}

// ─── FR-13 (0.9.0): Nibble-Packing ───────────────────────────────────────────
// Die BCD-Kinder (je pack_digits_[slot] Ziffern) bilden einen Ziffern-Strom
// ohne Byte-Grenzen dazwischen: Container = ceil(Summe/2) Byte, Padding bei
// ungerader Gesamtziffernzahl nach 'bcd_pad'. Ein kuerzerer Container (Kinder
// am Ende fehlen) bleibt erlaubt; ein Kind darf nicht mittendrin abgeschnitten sein.
std::size_t TNG_NAMESPACE::ISOBaseParser::unparseNibblePack(
    const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m,
    const std::vector<uint8_t>& b,
    std::size_t base_offset)
{
    using ::TNG_NAMESPACE::codec::Encoder;
    std::size_t total = 0;
    for (const auto d : pack_digits_)
        total += d;
    const std::size_t full_bytes = (total + 1) / 2;

    // Mehr Bytes als der volle Container braucht: Rest-Bytes (strict: Fehler).
    const std::size_t usable = std::min(b.size(), full_bytes);
    const std::size_t digits_avail = std::min(total, usable * 2);

    if (pack_pad_explicit_ && digits_avail == total && (total & 1u) != 0u &&
        usable == full_bytes &&
        !::TNG_NAMESPACE::codec::detail::bcd_pad_nibble_ok(b, 0, digits_avail, pack_pad_)) {
        const std::string msg = "Nibble-Container @ Offset " + std::to_string(base_offset) +
            ": BCD-Padding-Nibble weicht von 'bcd_pad' ab (" + std::to_string(digits_avail) +
            " Ziffern)";
        if (strict_)
            throw std::runtime_error("[ISO8583] " + msg);
        TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt)", msg);
    }

    const std::string digits = ::TNG_NAMESPACE::codec::as<std::string, Encoder::BCD>(
        b, 0, digits_avail, strict_, pack_pad_);

    std::size_t off = 0;
    for (std::size_t slot = 0; slot < pack_digits_.size(); ++slot) {
        const std::size_t dig = pack_digits_[slot];
        if (dig == 0)
            continue; // nop-Platzhalter
        if (off >= digits.size())
            break;    // verkuerzter Container: Rest-Kinder fehlen
        if (off + dig > digits.size()) {
            const std::string msg = "Nibble-Container @ Offset " + std::to_string(base_offset) +
                ": Kind " + std::to_string(slot) + " ist abgeschnitten (benoetigt " +
                std::to_string(dig) + " Ziffern, vorhanden " +
                std::to_string(digits.size() - off) + ")";
            if (strict_)
                throw std::runtime_error("[ISO8583] " + msg);
            TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Kind wird ausgelassen)", msg);
            break;
        }
        const auto& ptr = l_[slot];
        auto de = ptr->create_component(static_cast<TNG_KEY_TYPE>(slot));
        auto opq = std::dynamic_pointer_cast< ::TNG_NAMESPACE::OpaqueField >(de);
        if (!opq)
            throw std::runtime_error("[ISO8583] Nibble-Container: Kind " + std::to_string(slot) +
                " ist kein Textfeld (Loader-Invariante verletzt)");
        opq->value(digits.substr(off, dig));
        opq->description(ptr->description());
        opq->set_sensitive(ptr->sensitive());
        // Byte-Naeherung (E4): Offset = Startbyte, Laenge = beruehrte Bytes.
        opq->wire_offset(base_offset + off / 2);
        opq->wire_length((off + dig + 1) / 2 - off / 2);
        if (!m->set(opq))
            throw std::runtime_error("[ISO8583] DE" + std::to_string(slot) +
                ": ISOMessage::set fehlgeschlagen (Speicherfehler?)");
        off += dig;
    }

    if (b.size() > full_bytes) {
        if (strict_)
            throw std::runtime_error("[ISO8583] Unverbrauchte Bytes am Pufferende: " +
                std::to_string(b.size() - full_bytes) + " von " + std::to_string(b.size()) +
                " Bytes nicht konvertiert (Nibble-Container deckt hoechstens " +
                std::to_string(full_bytes) + " Byte ab)");
        TNG_LOG_ERROR("[ISOBaseParser] Byte consumption mismatch: expected={} actual={}",
            b.size(), full_bytes);
    }
    return usable;
}

std::vector<uint8_t> TNG_NAMESPACE::ISOBaseParser::parseNibblePack(
    const std::shared_ptr<::TNG_NAMESPACE::ISOMessage>& m) const
{
    using ::TNG_NAMESPACE::codec::Encoder;
    std::size_t total = 0;
    for (const auto d : pack_digits_)
        total += d;

    // Nicht deklarierte Kinder (kein Ziffern-Slot) sind nicht abbildbar.
    for (const auto k : m->keys()) {
        const bool declared = k >= 0 && static_cast<std::size_t>(k) < pack_digits_.size() &&
            pack_digits_[static_cast<std::size_t>(k)] > 0;
        if (declared)
            continue;
        const std::string msg = "Nibble-Container: Kind " + std::to_string(k) +
            " ist in der Spec nicht deklariert";
        if (strict_)
            throw std::runtime_error("[ISO8583] " + msg);
        TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: Kind wird ausgelassen)", msg);
    }

    // Letztes gesetztes Kind bestimmen; davor duerfen keine Luecken liegen.
    std::size_t last_set = 0;
    bool any = false;
    for (std::size_t slot = 0; slot < pack_digits_.size(); ++slot)
        if (pack_digits_[slot] > 0 && m->has(static_cast<TNG_KEY_TYPE>(slot))) {
            last_set = slot;
            any = true;
        }

    std::string digits;
    if (any) {
        for (std::size_t slot = 0; slot <= last_set; ++slot) {
            const std::size_t dig = pack_digits_[slot];
            if (dig == 0)
                continue;
            std::string v;
            const auto key = static_cast<TNG_KEY_TYPE>(slot);
            if (m->has(key)) {
                auto opq = m->get< ::TNG_NAMESPACE::OpaqueField >(key);
                if (!opq)
                    throw std::runtime_error("[ISO8583] Nibble-Container: Kind " +
                        std::to_string(slot) + " ist kein OpaqueField (Parser erwartet Ziffern)");
                v = opq->value();
            }
            else {
                const std::string msg = "Nibble-Container: Kind " + std::to_string(slot) +
                    " fehlt, obwohl ein spaeteres Kind gesetzt ist (Luecke im Ziffern-Strom)";
                if (strict_)
                    throw std::runtime_error("[ISO8583] " + msg);
                TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: mit Nullen aufgefuellt)", msg);
            }
            if (v.size() != dig) {
                const std::string msg = "Nibble-Container: Kind " + std::to_string(slot) +
                    " hat " + std::to_string(v.size()) + " Ziffern, erwartet " + std::to_string(dig);
                if (strict_ && !v.empty())
                    throw std::runtime_error("[ISO8583] " + msg);
                if (!v.empty())
                    TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt: links mit Nullen aufgefuellt/gekuerzt)", msg);
                if (v.size() > dig)
                    v.resize(dig);
                else
                    v.insert(0, dig - v.size(), '0');
            }
            if (std::any_of(v.begin(), v.end(), [](char c) { return c < '0' || c > '9'; })) {
                const std::string msg = "Nibble-Container: Kind " + std::to_string(slot) +
                    " enthaelt Nicht-Ziffern (BCD)";
                if (strict_)
                    throw std::runtime_error("[ISO8583] " + msg);
                TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt)", msg);
            }
            digits += v;
        }
    }

    // Ungerade Ziffernzahl eines VERKUERZTEN Containers: das Padding-Nibble
    // wuerde beim Decode wie eine echte Ziffer des Folgekinds aussehen.
    if ((digits.size() & 1u) != 0u && digits.size() != total) {
        const std::string msg = "Nibble-Container: verkuerzter Container mit ungerader Ziffernzahl (" +
            std::to_string(digits.size()) + ") endet mitten im Byte - das Padding-Nibble waere beim "
            "Decode nicht von einer Ziffer unterscheidbar";
        if (strict_)
            throw std::runtime_error("[ISO8583] " + msg);
        TNG_LOG_WARN("[ISOBaseParser] {} (nicht-strikt)", msg);
    }

    std::vector<uint8_t> out(::TNG_NAMESPACE::codec::required_sz_for_as<Encoder::BCD>(digits.size()), 0);
    if (!digits.empty())
        ::TNG_NAMESPACE::codec::to<Encoder::BCD>(digits, out, 0, strict_, pack_pad_);
    TNG_LOG_INFO("[ISOBaseParser::parseNibblePack] Fertig: {} bytes", out.size());
    return out;
}
