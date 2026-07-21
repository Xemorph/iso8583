#include "_parser.hh"
// [tng]
#include "_logger.hh"

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
        TNG_LOG_ERROR("[ISOBaseParser::parse] Null-Komponente oder kein Composite");
        return {};
    }

    if (l_.empty()) {
        TNG_LOG_ERROR("[ISOBaseParser::parse] Parser-Liste ist leer – nicht konfiguriert?");
        return {};
    }

    auto m = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOMessage>(c);
    if (!m) {
        TNG_LOG_ERROR("[ISOBaseParser::parse] Komponente ist kein ISOMessage");
        return {};
    }

    std::vector<uint8_t> out;

    // ── 1. Header ────────────────────────────────────────────────────────────
    // Header-Bytes als Nullen reservieren; Anwender befüllt via ISOMessage::header()
    if (hdr_sz_ > 0) {
        auto hdr = m->header();
        if (hdr && hdr->size() >= hdr_sz_) {
            std::vector<uint8_t> hdr_bytes = hdr->pack();
            out.insert(out.end(), hdr_bytes.begin(), hdr_bytes.begin() + hdr_sz_);
        }
        else
            out.insert(out.end(), hdr_sz_, 0x00);
    }

    // ── 2. MTI ───────────────────────────────────────────────────────────────
    {
        auto p = l_.at(0);
        if (p &&
            p->type() != ISOFieldParserType::BITMAP &&
            p->type() != ISOFieldParserType::UNUSED)
        {
            auto mti_comp = m->get<ISOComponentPtrBase>(0);
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
    const TNG_KEY_TYPE max_de = static_cast<TNG_KEY_TYPE>(l_.size()) - 1;

    // Bitmap-Puffer (Byte-Array, Big-Endian bit ordering)
    std::vector<uint8_t> bmp_buf;

    if (emit_bitmap()) {
        // Single-Pass: Bits setzen und bmp_sz gleichzeitig bestimmen.
        // Worst-case 24 Bytes vorab allozieren, am Ende kürzen.
        bmp_buf.assign(24, 0x00);
        std::size_t bmp_sz = 8;

        for (TNG_KEY_TYPE i = ff; i <= max_de; ++i) {
            if (!m->has(i)) continue;
            const std::size_t bit_pos = static_cast<std::size_t>(i - ff);
            if (bit_pos >= 128) bmp_sz = 24;
            else if (bit_pos >= 64)  bmp_sz = std::max(bmp_sz, std::size_t{ 16 });
            const std::size_t byte_idx = bit_pos / 8;
            const std::size_t bit_idx = 7 - (bit_pos % 8);
            bmp_buf[byte_idx] |= static_cast<uint8_t>(1u << bit_idx);
        }
        bmp_buf.resize(bmp_sz);

        // Extension-Bits: Bit 0 (MSB) von Block 2/3 zeigt Secondary/Tertiary an
        if (bmp_sz >= 16) bmp_buf[8] |= 0x80u;
        if (bmp_sz >= 24) bmp_buf[16] |= 0x80u;

        // Bitmap encodieren via Bitmap-FieldParser (trägt das richtige Encoding)
        if (l_.size() > 1) {
            auto p = l_.at(1);
            if (p && p->type() == ISOFieldParserType::BITMAP) {
                auto bmp_comp = m->tryGet< ::TNG_NAMESPACE::Bitmap >(-1);
                auto encoded = p->parse(bmp_comp.value());
                out.insert(out.end(), encoded.begin(), encoded.end());
                TNG_LOG_TRACE("[ISOBaseParser::parse] Bitmap: {} bytes", encoded.size());
            }
            else {
                out.insert(out.end(), bmp_buf.begin(), bmp_buf.end());
            }
        }
    }

    // ── 4. Datenfelder serialisieren ─────────────────────────────────────────
    // Hinweis: l_.at(i) gibt shared_ptr<const ISOFieldParserPtrBase> zurück.
    // parse() ist nicht const → const_pointer_cast nötig.
    CONST_ITERATOR _begin = l_.cbegin() + ff;
    CONST_ITERATOR _end = l_.cend();

    TNG_KEY_TYPE i = ff;
    std::for_each(_begin, _end,
        [&i, &m, &out, &bmp_buf, this]
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
        TNG_LOG_WARN("[ISOBaseParser] Empty byte image received");
        return 0u;
    }
    if (l_.empty()) {
        TNG_LOG_ERROR("[ISOBaseParser] Field parser list is empty - parser not configured?");
        return 0u;
    }
    if (c == nullptr || !c->is_composite()) {
        fmt::println("WARNING: Invalid component or null-pointer.");
        return 0u;
    }

    auto m = std::dynamic_pointer_cast<::TNG_NAMESPACE::ISOMessage>(c);
    if (m == nullptr)
        return 0u;

    std::size_t consumed = 0u;

    // Header
    if (hdr_sz_ != 0) {
        std::vector<uint8_t> h(hdr_sz_, 0x00);
        std::memcpy(h.data(), b.data(), hdr_sz_);
        consumed += hdr_sz_;
    }

    // -- MTI ------------------------------------------------------------------
    std::shared_ptr<const ::TNG_NAMESPACE::ISOFieldParserPtrBase> p = l_.at(0);
    if (p && (
        p->type() != ::TNG_NAMESPACE::ISOFieldParserType::BITMAP &&
        p->type() != ::TNG_NAMESPACE::ISOFieldParserType::UNUSED))
    {
        TNG_LOG_TRACE("[ISOBaseParser] [  MTI] '{}' offset={} use_count={}",
            p->description(), base_offset + consumed, p.use_count());

        auto mti = p->create_component(0);
        mti->description(p->description());
        mti->wire_offset(base_offset + consumed);
        const std::size_t mti_bytes = p->unparse(mti, b, consumed);
        mti->wire_length(mti_bytes);
        consumed += mti_bytes;
        m->set(mti);
    }

    // -- Bitmap ---------------------------------------------------------------
    dynamic_bitset<> bmp;
    std::size_t bmp_sz = 0;
    TNG_KEY_TYPE hf = (TNG_KEY_TYPE)(l_.size() - 1);

    if (emit_bitmap()) {
        TNG_LOG_TRACE("[ISOBaseParser] BMAP1 '{}' offset={}",
            l_.at(1)->description(), base_offset + consumed);

        auto bitmap = std::make_shared< ::TNG_NAMESPACE::Bitmap >(-1);
        bitmap->description(l_.at(1)->description());
        bitmap->wire_offset(base_offset + consumed);
        const std::size_t bmp_bytes = l_.at(1)->unparse(bitmap, b, consumed);
        bitmap->wire_length(bmp_bytes);
        consumed += bmp_bytes;
        bmp = bitmap->value();
        bmp.shrink_to_fit();
        bmp_sz = ((bmp.size() - 1 + 63) >> 6) << 3;
        m->set(bitmap);

        auto last_idx = bmp.find_first();
        for (auto idx = last_idx; idx != dynamic_bitset<>::npos; ) {
            last_idx = idx;
            idx = bmp.find_next(idx);
        }
        hf = std::min(hf, (TNG_KEY_TYPE)last_idx);
    }

    // -- Datenfelder ----------------------------------------------------------
    CONST_ITERATOR _begin = l_.cbegin() + first_field();
    CONST_ITERATOR _end   = (hf + 1 < (TNG_KEY_TYPE)l_.size())
                          ? (l_.cbegin() + hf + 1)
                          : l_.cend();

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
                de->wire_offset(base_offset + consumed);
                const std::size_t de_bytes = ptr->unparse(de, b, consumed);
                de->wire_length(de_bytes);
                consumed += de_bytes;
                m->set(de);
            }

        ++i;
    });

    if (consumed != b.size())
        TNG_LOG_ERROR("[ISOBaseParser] Byte consumption mismatch: expected={} actual={}",
            b.size(), consumed);

    return consumed;
}