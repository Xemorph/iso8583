#include "_parser.hh"
// [tng]
#include "_logger.hh"

// ─── Top-Level unparse (kein base_offset) ────────────────────────────────────
std::size_t TNG_NAMESPACE::ISOBaseParser::unparse(
    ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
    const std::vector<uint8_t>& b)
{
    return unparse(c, b, 0u);
}

// ─── unparse mit base_offset ─────────────────────────────────────────────────
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

    // ── MTI ──────────────────────────────────────────────────────────────────
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

    // ── Bitmap ───────────────────────────────────────────────────────────────
    dynamic_bitset<> bmp;
    std::size_t bmp_sz = 0;
    TNG_KEY_TYPE hf = (TNG_KEY_TYPE)(l_.size() - 1);

    if (emit_bitmap()) {
        TNG_LOG_TRACE("[ISOBaseParser] BMAP1 '{}' offset={}",
            l_.at(1)->description(), base_offset + consumed);

        auto bitmap = std::make_shared<ISOBitmap>(-1);
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

    // ── Datenfelder ──────────────────────────────────────────────────────────
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