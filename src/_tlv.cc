#include "_tlv.hh"
// [stdc++]
#include <limits>
// [tng]
#include "_logger.hh"

// =============================================================================
// tlv_detail – Runtime-Hilfsfunktionen
// =============================================================================
// Alle TNG_LOG_*-Aufrufe leben hier in der .cc-Datei.
// Das Template in _tlv_parser.hh ruft diese Funktionen auf statt selbst zu
// loggen – dadurch gibt es keine "unresolved symbol"-Fehler in Test-TUs die
// _logger.hh nicht sehen, und der Log-Code wird nicht in jede
// Template-Instanziierung hineinkopiert.

namespace TNG_NAMESPACE::tlv_detail {

    void log_error_not_composite() {
        TNG_LOG_ERROR("[ISOTLVParser] Komponente ist kein ISOMessage");
    }

    void log_warn_tcc_missing() {
        TNG_LOG_WARN("[ISOTLVParser] TCC fehlt im Payload");
    }

    void log_warn_tcc_not_set() {
        TNG_LOG_WARN("[ISOTLVParser] TCC-Feld nicht gesetzt – schreibe Leerzeichen");
    }

    void log_warn_se_missing(std::size_t se_num) {
        TNG_LOG_WARN("[ISOTLVParser] SE{} nicht gesetzt – wird nicht serialisiert", se_num);
    }

    void log_error_se_overflow(std::size_t se_num, std::size_t se_len,
        std::size_t pos, std::size_t buf_sz)
    {
        TNG_LOG_ERROR("[ISOTLVParser] SE{} Länge {} überschreitet Payload "
                      "(pos={} buf={})", se_num, se_len, pos, buf_sz);
    }

    void log_error_se_length_missing(std::size_t se_num, std::size_t pos, std::size_t buf_sz) {
        TNG_LOG_ERROR("[ISOTLVParser] SE{} - kein Platz mehr für Length-Feld "
                      "(pos={} buf={})", se_num, pos, buf_sz);
    }

    void log_error_ber_tag_overflow(std::size_t offset, std::size_t buf_sz) {
        TNG_LOG_ERROR("[BerTag] Tag-Feld überschreitet Payload (offset={} buf={})",
            offset, buf_sz);
    }

    void log_error_ber_length_overflow(std::size_t offset, std::size_t buf_sz) {
        TNG_LOG_ERROR("[BerLength] Length-Feld überschreitet Payload (offset={} buf={})",
            offset, buf_sz);
    }

    void log_error_ber_length_indefinite() {
        TNG_LOG_ERROR("[BerLength] Indefinite-Form (0x80) wird nicht unterstützt - "
                      "TLV-Kontext benötigt eine feste SE-Länge");
    }

    // [ISO8583] Phase 4 (F5, P3): SE-Tag paßt nicht in TNG_KEY_TYPE.
    void log_warn_se_key_too_wide(std::size_t se_num) {
        TNG_LOG_WARN("[ISOTLVParser] SE-Tag 0x{:X} ({}) passt nicht in TNG_KEY_TYPE "
                     "(max. {}) - SE wird NICHT gespeichert (kein Fehlrouting). "
                     "Für EMV-Tags >= 0x8000 ISO8583_BERTLV=ON (int32_t-Keys) nutzen.",
            se_num, se_num,
            static_cast<std::size_t>(std::numeric_limits<TNG_KEY_TYPE>::max()));
    }

    void log_debug_tcc(const std::string& tcc) {
        TNG_LOG_DEBUG("[ISOTLVParser] TCC='{}'", tcc);
    }

    void log_debug_se_read(std::size_t se_num, std::size_t se_len) {
        TNG_LOG_DEBUG("[ISOTLVParser] SE{:02d} gelesen, len={}", se_num, se_len);
    }

    void log_debug_se_write(std::size_t se_num, std::size_t data_sz) {
        TNG_LOG_DEBUG("[ISOTLVParser] SE{:02d} → {} bytes", se_num, data_sz);
    }

    std::vector<TNG_KEY_TYPE> sorted_se_keys(const ISO_MAP& fields) {
        std::vector<TNG_KEY_TYPE> keys;
        keys.reserve(fields.size());
        for (const auto& [key, _] : fields)
            if (key >= 0)
                keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    void store_se(
        const std::shared_ptr< ::TNG_NAMESPACE::Message >& msg,
        std::size_t se_num, const std::vector<uint8_t>& buf,
        std::size_t data_offset, std::size_t data_len,
        std::size_t wire_offset, std::size_t wire_len,
        const nonstd::string_view& description,
        bool sensitive,
        const TlvChildInfo* child,
        bool strict)
    {
        // [ISO8583] Phase 4 (F5, P3): kein stiller static_cast-Verlust. Ein
        // BER-Tag, das in TNG_KEY_TYPE nicht passt (z.B. 0x9F26 = 40742 >
        // int16_max in der Default-Build), würde via static_cast<int16_t>(40742)
        // == -24794 als verfälschter negativer Key gespeichert - beim
        // Reserialize (sorted_se_keys, key >= 0-Filter) ginge er verloren bzw.
        // es träfe ein falsches SE (Fehlrouting). Stattdessen: warnen + SE
        // überspringen. In der ISO8583_BERTLV-Build (int32_t) passen alle
        // 2-Byte-EMV-Tags; dieser Pfad greift dort nicht.
        if (se_num > static_cast<std::size_t>(std::numeric_limits<TNG_KEY_TYPE>::max())) {
            log_warn_se_key_too_wide(se_num);
            return; // SE wird bewusst NICHT gespeichert (kein Fehlrouting)
        }
        // 0.6.4: Container-Kind (constructed): kein Skalar-Feld. Der
        // Kind-Wert ist selbst eine Folge von TLVs und wird über den
        // Sub-Parser in eine Sub-Message dekodiert (Muster: NESTED-Pfad,
        // src/_parser.hh) — die inneren TLVs sind danach per Punkt-Notation
        // erreichbar (z.B. "57.69.63"). Der Sub-Parser wird an die
        // Sub-Nachricht angehängen, damit sie sich selbst re-serialisieren
        // kann (parse() im ISOTLVParser-Encode-Pfad). Fehlt der Sub-Parser
        // (defensiv; der Loader setzt container immer zusammen mit
        // subParser), gilt der Skalar-Pfad (rohe Bytes wie undeklariert).
        if (child && child->container && child->subParser) {
            auto subMsg = std::make_shared< ::TNG_NAMESPACE::ISOMessage >(
                static_cast<TNG_KEY_TYPE>(se_num));
            // ZUERST den Sub-Parser anhängen: ISOMessage::parser() übernimmt
            // die Beschreibung des Basis-Parsers ("<tlv>"), erst danach die
            // deklarierte (Kind-)Beschreibung setzen, damit sie bleibt
            // (s. ISOMessage::parser, _components.cc).
            subMsg->parser(child->subParser);
            // `description` zeigt in Parser-langlebigen Speicher (s.
            // ISOTLVParser::description_for_wire) — sichere, nicht
            // kopierende Sicht.
            subMsg->description(description);
            // [ISO8583] 3.4 (PCI): Sensitivität des Container-Kinds aus der
            // Spec (pro-Tag oder Container-Ebene); der Sub-Baum erbt sie über
            // den Sub-Parser (sensitive-All-Family bzw. Kind-Erbe beim Build).
            subMsg->set_sensitive(sensitive);
            subMsg->wire_offset(wire_offset);
            subMsg->wire_length(wire_len);
            // Wert-Bytes = äußeres TLV-Frame abzüglich Tag+Length: das
            // Length-Feld wurde bereits von LenPolicy konsumiert, ein
            // zusätzliches L-Präfix existiert NICHT.
            const std::vector<uint8_t> valueBytes(
                buf.begin() + static_cast<std::ptrdiff_t>(data_offset),
                buf.begin() + static_cast<std::ptrdiff_t>(data_offset + data_len));
            // Absolutes Wire-Offset des Wert-Anfangs = Frame-Anfang +
            // (Tag+Länge)-Größe; der Sub-Parser nutzt es als base_offset für
            // die wire_offset seiner eigenen Kinder.
            const std::size_t value_wire = wire_offset + (wire_len - data_len);
            (void)child->subParser->unparse(subMsg, valueBytes, value_wire);
            msg->set(subMsg);
            return;
        }
        // FR-1 (0.5.0): deklarierter Text-Kind (char/numeric/nopad_char) →
        // OpaqueField per Codec (strict: nicht-mappbare Bytes werfen ein
        // std::runtime_error; nicht-strikt: Legacy-Sentinel-Mapping '.'/'?').
        // Binäre Kinder und undeklarierte Tags → BinaryField (rohe Bytes, Unchanged).
        std::shared_ptr< ::TNG_NAMESPACE::ISOComponentPtrBase > se;
        if (child && child->text) {
            std::shared_ptr< ::TNG_NAMESPACE::OpaqueField > of;
            if (child->amount) {
                // 0.6.0: Betrag-Kind → AmountField (jPOS-Form oder Plain-Form je 'scale:')
                of = child->scale.has_value()
                    ? std::make_shared< ::TNG_NAMESPACE::AmountField >(
                        static_cast<TNG_KEY_TYPE>(se_num), ::TNG_NAMESPACE::AmountForm::plain, *child->scale, child->sign)
                    : std::make_shared< ::TNG_NAMESPACE::AmountField >(
                        static_cast<TNG_KEY_TYPE>(se_num));
            }
            else
                of = std::make_shared< ::TNG_NAMESPACE::OpaqueField >(
                    static_cast<TNG_KEY_TYPE>(se_num));
            (void)of->value(child_as_string(child->enc, buf, data_offset, data_len, strict));
            se = of;
        }
        else {
            auto bf = std::make_shared< ::TNG_NAMESPACE::BinaryField >(
                static_cast<TNG_KEY_TYPE>(se_num));
            (void)bf->value(std::vector<uint8_t>(
                buf.begin() + static_cast<std::ptrdiff_t>(data_offset),
                buf.begin() + static_cast<std::ptrdiff_t>(data_offset + data_len)));
            se = bf;
        }
        // `description` zeigt bereits in einen langlebigen, vom Parser
        // besessenen Speicher (siehe ISOTLVParser::description_for_wire) -
        // keine Kopie/Temporäre hier, sonst dangelnde Sicht (siehe dortiger
        // Kommentar).
        se->description(description);
        // [ISO8583] 3.4 (PCI): Sensitivität aus der Spec (pro-Tag oder
        // Container-Ebene) → dump() maskiert den SE-Wert mit "***".
        se->set_sensitive(sensitive);
        se->wire_offset(wire_offset);
        se->wire_length(wire_len);
        msg->set(se);
    }

} // namespace TNG_NAMESPACE::tlv_detail
