#include "_tlv.hh"
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

    void log_error_se_overflow(std::size_t se_num, std::size_t se_len,
                               std::size_t pos,    std::size_t buf_sz)
    {
        TNG_LOG_ERROR("[ISOTLVParser] SE{} Länge {} überschreitet Payload "
                      "(pos={} buf={})", se_num, se_len, pos, buf_sz);
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
        const std::shared_ptr<ISOMessage>& msg,
        std::size_t  se_num,
        const std::vector<uint8_t>& buf,
        std::size_t  data_offset,
        std::size_t  data_len,
        std::size_t  wire_offset,
        std::size_t  wire_len)
    {
        auto se = std::make_shared<ISOBinaryField>(
            static_cast<TNG_KEY_TYPE>(se_num));
        se->value(std::vector<uint8_t>(
            buf.begin() + static_cast<std::ptrdiff_t>(data_offset),
            buf.begin() + static_cast<std::ptrdiff_t>(data_offset + data_len)));
        se->description("SE" + std::to_string(se_num));
        se->wire_offset(wire_offset);
        se->wire_length(wire_len);
        msg->set(se);
    }

} // namespace TNG_NAMESPACE::tlv_detail
