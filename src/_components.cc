#include <iso8583/detail/_components.hh>
// [stdc++]
#include <iostream>
// [tng/parser]
#include "_parser.hh"
#include "_utils.hh"
// [tng/logger]
#include "_logger.hh"
// [fmt]
#define FMT_UNICODE 1
#include <fmt/format.h>
#include <fmt/ranges.h>
// [tng/date]
#include "_date.hh"

template < typename IntegerType, typename T >
TNG_NAMESPACE::ISOComponent<IntegerType, T>::ISOComponent(const IntegerType& key)
    : k_(key)
{
    d_ = T();
}

template < typename IntegerType, typename T >
TNG_NAMESPACE::ISOComponent<IntegerType, T>::ISOComponent(const IntegerType& key, const T& data)
    : k_(key), d_(data)
{}

template < typename IntegerType, typename T >
void TNG_NAMESPACE::ISOComponent<IntegerType, T>::key(IntegerType k) {
    k_ = k;
}
template < typename IntegerType, typename T >
TNG_KEY_TYPE TNG_NAMESPACE::ISOComponent<IntegerType, T>::key() const {
    return k_;
}

template < typename IntegerType, typename T >
void TNG_NAMESPACE::ISOComponent<IntegerType, T>::value(const T& d) {
    d_ = d;
}
template < typename IntegerType, typename T >
const T& TNG_NAMESPACE::ISOComponent<IntegerType, T>::value() const {
    return d_;
}
template < typename IntegerType, typename T >
T& TNG_NAMESPACE::ISOComponent<IntegerType, T>::value() {
    return d_;
}

template < typename IntegerType, typename T >
std::string TNG_NAMESPACE::ISOComponent<IntegerType, T>::readable_value() const {
    if constexpr (std::is_same_v<T, dynamic_bitset<>>)
        return d_.to_string('0', '1');
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
        std::string hex;
        hex.reserve(d_.size() * 2);

        for (auto b : d_)
            hex += fmt::format("{:02X}", b);

        return hex;
    }
    else if constexpr (std::is_same_v<T, std::string>)
        return d_;
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(d_);
    else
        return "[unsupported type]";
}

template < typename IntegerType, typename T >
void TNG_NAMESPACE::ISOComponent<IntegerType, T>::description(const nonstd::string_view& desc) {
    desc_ = desc;
}

template < typename IntegerType, typename T >
const nonstd::string_view TNG_NAMESPACE::ISOComponent<IntegerType, T>::description() const {
    return desc_;
}
template < typename IntegerType, typename T >
void TNG_NAMESPACE::ISOComponent<IntegerType, T>::explanation(const nonstd::string_view& expl) {
    expl_ = expl;
}

template < typename IntegerType, typename T >
const nonstd::string_view TNG_NAMESPACE::ISOComponent<IntegerType, T>::explanation() const {
    return expl_;
}


template < typename IntegerType, typename T >
bool TNG_NAMESPACE::ISOComponent<IntegerType, T>::is_composite() const {
    return false;
}

template < typename IntegerType, typename T >
void TNG_NAMESPACE::ISOComponent<IntegerType, T>::print(bool nested) const {
    if constexpr (std::is_same_v< T, dynamic_bitset<> >)
        fmt::print("{}BMP## {:.<48} [{}] =vorhandene Datenfelder\n",
            nested ? "\u2500\u2574" : "\u251C\u2574", desc_, tng::utils::getSetBMPFieldsCSV((static_cast<dynamic_bitset<>>(d_)))); //(static_cast<dynamic_bitset<>>(d_)).to_string('0', '1'));//
    else if constexpr (std::is_same_v< T, std::vector<uint8_t> >)
        fmt::print("{}DE{:03} {:.<48} [0x{:X}]\n",
            nested ? "\u2500\u2574" : "\u251C\u2574", k_, desc_, fmt::join(d_, ""));
    else if constexpr (std::is_same_v< T, ISO_MAP >)
        fmt::print("{}DE{:03}: \"Currently unsupported\"\n",
            nested ? "\u2500\u2574" : "\u251C\u2574", k_);
    else if constexpr (std::is_same_v< T, std::string >) {
        if (!expl_.empty())
            /*
            if (k_ == 2 && !nested)
                fmt::print("{}DE{:03} {:.<48} [{}] ={}\n",
                    nested ? "\u2500\u2574" : "\u251C\u2574", k_, desc_, tng::utils::protect<'#'>(d_), expl_);
            else*/
            fmt::print("{}DE{:03} {:.<48} [{}] ={}\n",
                nested ? "\u2500\u2574" : "\u251C\u2574", k_, desc_, d_, expl_);
        else
            /*
            if (k_ == 2 && !nested)
                fmt::print("{}DE{:03} {:.<48} [{}]\n",
                    nested ? "\u2500\u2574" : "\u251C\u2574", k_, desc_, tng::utils::protect<'#'>(d_));
            else*/
            fmt::print("{}DE{:03} {:.<48} [{}]\n",
                nested ? "\u2500\u2574" : "\u251C\u2574", k_, desc_, d_);
    }
    else return;
}

template < typename IntegerType, typename T >
json TNG_NAMESPACE::ISOComponent<IntegerType, T>::to_json() const {
    json j;
    j["key"] = k_;

    if constexpr (std::is_same_v<T, dynamic_bitset<>>) {
        // Spezielle Verarbeitung für dynamic_bitset
        j["value"] = d_.to_string('0', '1');
    }
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
        // Spezielle Verarbeitung für std::vector<uint8_t>
        std::string hex_str;
        for (auto byte : d_) {
            hex_str += fmt::format("{:02X}", byte);
        }
        j["value"] = hex_str;
    }
    else if constexpr (std::is_same_v< T, std::string >) {
        // Standardverhalten für andere Typen
        j["value"] =  d_;
    }
    else /*if constexpr (std::is_same_v<T, ISO_MAP>)*/ {
        // ISO_MAP ist ein komplexerer Typ, der ggf. nicht direkt darstellbar ist
        j["value"] = "Unsupported";  // Optional: Eine Nachricht, dass der Typ nicht unterstützt wird
    }

    return j;
}

// Explicit instantiations for the concrete ISOComponent types used by the library.
// TNG_EXPORT is intentionally omitted here: on GCC/Clang the visibility attribute
// on an explicit instantiation is ignored (and warns) when the class template was
// already declared with visibility in the header. On MSVC the dllexport is carried
// through from the class definition in the header.
template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::string >;
template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, std::vector<uint8_t> >;
template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, dynamic_bitset<> >;
template class TNG_NAMESPACE::ISOComponent< TNG_KEY_TYPE, ISO_MAP >;


TNG_NAMESPACE::ISOMessage::ISOMessage()
    : ISOComponent(-1), hf_(-1), recalc_(true)
{
    description("N/A"_sv);
}
TNG_NAMESPACE::ISOMessage::ISOMessage(const ISO_MAP::key_type& key)
    : ISOComponent(key), hf_(-1), recalc_(true)
{
    description("N/A"_sv);
}
TNG_NAMESPACE::ISOMessage::ISOMessage(nonstd::string_view mti)
    : ISOComponent(-1), hf_(-1), recalc_(true)
{
    description("N/A"_sv);
    if (isInner()) {
        TNG_LOG_ERROR("[ISOMessage::()] Cannot set MTI on inner message");
        return;
    }
    set(std::make_shared<ISOOpaqueField>(0, nonstd::to_string(mti)));
}

// Composites are just container and do not contain any usefull values.
// They have just children
std::string TNG_NAMESPACE::ISOMessage::readable_value() const {
    return {};
}

bool TNG_NAMESPACE::ISOMessage::is_composite() const {
    return true;
}

void TNG_NAMESPACE::ISOMessage::print(bool nested) const {
    if (is_composite()) {
        if (key() == -1) {
            if (!expl_.empty())
                fmt::print("\u252C\x1b[4m{}\x1b[0m: [ISOMessage]\n", expl_);
            else
                fmt::print("\u252CISOMessage:\n");
        }
        else {
            fmt::print("{}DE{:03} {}:\n", "\u253C", k_, desc_);
        }

        ISO_MAP_CONST_ITERATOR citr = d_.cbegin();
        while (citr != d_.cend()) {
            // bmp
            const ISO_MAP::key_type de = citr->first;
            // print
            if (key() > -1)
                if (std::distance(citr, d_.cend()) == 1)
                    fmt::print("{}{}", "\u2514", "\u2574");
                else
                    fmt::print("{}{}", "\u251C", "\u2574");
            citr->second->print((key() > -1 ? true : false));
            // increment
            citr++;
        }
    }
}

bool TNG_NAMESPACE::ISOMessage::has(const ISO_MAP::key_type& key) {
    // Try to find element
    ISO_MAP_ITERATOR itr = d_.find(key);
    if (itr == d_.end())
        return false;
    return true;
}

bool TNG_NAMESPACE::ISOMessage::set(ISO_MAP::mapped_type component) {
    if (!component) {
        TNG_LOG_ERROR("[ISOMessage::set] Null-Komponente übergeben – wird ignoriert");
        return false;
    }
    const ISO_MAP::key_type key = component->key();
    try {
        std::unique_lock<std::shared_mutex> w_lock(d_lock_);
        d_.insert_or_assign(key, std::move(component));
        if (key > hf_) hf_ = key;
        recalc_ = true;
        return true;
    }
    catch (const std::bad_alloc& e) {
        TNG_LOG_ERROR("[ISOMessage::set] Speicher konnte nicht alloziert werden (DE{}): {}", key, e.what());
        return false;
    }
    catch (const std::exception& e) {
        TNG_LOG_ERROR("[ISOMessage::set] Unerwarteter Fehler beim Einfügen von DE{}: {}", key, e.what());
        return false;
    }
}

// ── Interne Hilfsfunktion: Erstellt ein ISOComponent aus String-Wert ──────────
// Kapselt die Parser-Typ-Prüfung für set(key, data) und set(dot, data).
static ISO_MAP::mapped_type make_component_from_string(TNG_KEY_TYPE key, std::string data,
    const TNG_NAMESPACE::ISOFieldParserPtrBase* fieldParser)   // nullptr = kein Parser
{
    using namespace TNG_NAMESPACE;

    if (fieldParser) {
        switch (fieldParser->type()) {
        case ISOFieldParserType::OPAQUE:
        case ISOFieldParserType::EXCEPTIONAL:
        case ISOFieldParserType::REMAINING:
        case ISOFieldParserType::UNUSED: {
            auto f = std::make_shared<ISOOpaqueField>(key);
            f->value(std::move(data));
            f->description(fieldParser->description());
            return f;
        }
        case ISOFieldParserType::BINARY: {
            auto f = std::make_shared<ISOBinaryField>(key);
            std::vector<uint8_t> bytes;
            bytes.reserve(data.size() / 2);
            for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
                try {
                    bytes.push_back(static_cast<uint8_t>(
                        std::stoul(data.substr(i, 2), nullptr, 16)));
                }
                catch (...) {
                    TNG_LOG_WARN("[ISOMessage::set] Ungültiges Hex-Byte "
                        "an Position {} in DE{}", i, key);
                    return nullptr;
                }
            }
            f->value(std::move(bytes));
            f->description(fieldParser->description());
            return f;
        }
        case ISOFieldParserType::BITMAP:
            TNG_LOG_WARN("[ISOMessage::set] DE{}: BITMAP wird automatisch "
                "berechnet – manuelles set() nicht erlaubt", key);
            return nullptr;
        case ISOFieldParserType::NESTED:
            TNG_LOG_WARN("[ISOMessage::set] DE{}: NESTED-Felder haben "
                "keinen eigenen Wert – Subfelder per Dot-Notation setzen", key);
            return nullptr;
        default:
            break;
        }
    }

    // Kein Parser oder unbekannter Typ → immer OpaqueField
    auto f = std::make_shared<ISOOpaqueField>(key);
    f->value(std::move(data));
    return f;
}

bool TNG_NAMESPACE::ISOMessage::set(const ISO_MAP::key_type& key, std::string data) {
    ISO_MAP::mapped_type component;
    {
        std::shared_lock<std::shared_mutex> r_lock(d_lock_);
        const ISOFieldParserPtrBase* fieldParser = nullptr;
        std::shared_ptr<const ISOFieldParserPtrBase> fp_holder;

        if (p_) {
            if (auto base = std::dynamic_pointer_cast<ISOBaseParser>(p_)) {
                fp_holder = base->field_parser(key);
                fieldParser = fp_holder.get();
            }
        }
        component = make_component_from_string(key, std::move(data), fieldParser);
    }
    if (!component) return false;
    return set(std::move(component));
}

bool TNG_NAMESPACE::ISOMessage::set(const ISO_MAP::key_type& key, std::string_view data) {
    return set(key, std::string(data));
}

bool TNG_NAMESPACE::ISOMessage::set_recursive(
    const TNG_KEY_TYPE* keys,
    std::size_t         depth,
    std::string         data)
{
    // keys[0]       = Key auf dieser Ebene
    // keys[1..n-1]  = verbleibende Segmente
    // depth == 1    = Blatt – normaler set(key, data)
    if (depth == 1)
        return set(keys[0], std::move(data));

    const TNG_KEY_TYPE parentKey = keys[0];

    // Parser-Prüfung für diesen Key
    std::shared_ptr<ISOBaseParser>               base;
    std::shared_ptr<const ISOFieldParserPtrBase> parentFP;
    std::shared_ptr<ISOParserPtrBase>            subParserPtr;

    {
        std::shared_lock<std::shared_mutex> r_lock(d_lock_);

        if (p_) {
            base = std::dynamic_pointer_cast<ISOBaseParser>(p_);
            if (base) {
                parentFP = base->field_parser(parentKey);
                if (parentFP) {
                    if (parentFP->type() != ISOFieldParserType::NESTED) {
                        TNG_LOG_ERROR("[ISOMessage::set] DE{} ist nicht NESTED",
                            parentKey);
                        return false;
                    }
                    subParserPtr = parentFP->subParser();
                }
            }
        }
    }

    // Parent-ISOMessage holen oder neu anlegen
    std::shared_ptr<ISOMessage> parentMsg;
    {
        std::unique_lock<std::shared_mutex> w_lock(d_lock_);

        auto it = d_.find(parentKey);
        if (it != d_.end()) {
            parentMsg = std::dynamic_pointer_cast<ISOMessage>(it->second);
            if (!parentMsg) {
                TNG_LOG_ERROR("[ISOMessage::set] DE{} existiert aber ist kein Composite",
                    parentKey);
                return false;
            }
        }
        else {
            parentMsg = std::make_shared<ISOMessage>(parentKey);
            if (parentFP)
                parentMsg->description(parentFP->description());
            // Sub-Parser setzen damit die nächste Ebene Typ-Prüfung machen kann
            if (subParserPtr)
                parentMsg->parser(subParserPtr);
            d_.insert_or_assign(parentKey, parentMsg);
            if (parentKey > hf_) hf_ = parentKey;
            recalc_ = true;
        }
    }

    // Nächste Ebene rekursiv im Child-Context auflösen
    return parentMsg->set_recursive(keys + 1, depth - 1, std::move(data));
}

bool TNG_NAMESPACE::ISOMessage::set(const std::string& dot_key, std::string data) {
    // Dot-Notation in Segmente aufteilen
    std::vector<TNG_KEY_TYPE> keys;
    std::istringstream ss(dot_key);
    std::string token;
    while (std::getline(ss, token, '.')) {
        try {
            keys.push_back(static_cast<TNG_KEY_TYPE>(std::stoi(token)));
        }
        catch (...) {
            TNG_LOG_ERROR("[ISOMessage::set] Ungültiger Key '{}' in '{}'",
                token, dot_key);
            return false;
        }
    }

    if (keys.empty()) {
        TNG_LOG_ERROR("[ISOMessage::set] Leere Dot-Notation");
        return false;
    }

    // Ein Segment = normaler key-basierter Aufruf
    if (keys.size() == 1)
        return set(keys[0], std::move(data));

    // Mehrere Segmente = rekursiv in Subfelder
    return set_recursive(keys.data(), keys.size(), std::move(data));
}

bool TNG_NAMESPACE::ISOMessage::set(const std::string& dot_key, std::string_view data) {
    return set(dot_key, std::string(data));
}

bool TNG_NAMESPACE::ISOMessage::unset(const ISO_MAP::key_type& key) {
    // Guarded by std::mutex -> unique
    std::unique_lock<std::shared_mutex> w_lock(d_lock_);
    // Try to find element
    ISO_MAP_ITERATOR itr = d_.find(key);
    if (itr == d_.end())
        return false;
    (void)d_.erase(itr);
    return true;
}

void TNG_NAMESPACE::ISOMessage::reset(void) {
    // Guarded by std::mutex -> unique
    std::unique_lock<std::shared_mutex> w_lock(d_lock_);
    // Clear whole RB-Tree
    d_.clear();
}

const std::size_t TNG_NAMESPACE::ISOMessage::size() const noexcept {
    return d_.size();
}

void TNG_NAMESPACE::ISOMessage::parser(const ::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr& p) {
    p_ = p;
    try {
        description(dynamic_cast<::TNG_NAMESPACE::ISOBaseParser*>(p_.get())->description());
    } catch (const std::exception) {

    }
}
::TNG_NAMESPACE::ISOParserPtrBase::ISOParserPtrBaseSmartPtr TNG_NAMESPACE::ISOMessage::parser() const {
    return (p_ ? p_ : nullptr);
}

std::size_t TNG_NAMESPACE::ISOMessage::unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c, const std::vector<uint8_t>& b) {
    return (p_ ? p_->unparse(c, b) : SIZE_MAX);
}

void TNG_NAMESPACE::ISOMessage::header(const std::vector<uint8_t>& b) {
    hdr_ = std::make_shared<::TNG_NAMESPACE::BaseHeader>(b);
}
void TNG_NAMESPACE::ISOMessage::header(::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr& hdr) {
    hdr_ = std::move(hdr);
}

::TNG_NAMESPACE::ISOHeader::ISOHeaderSmartPtr TNG_NAMESPACE::ISOMessage::header() {
    return hdr_;
}

std::vector<uint8_t> TNG_NAMESPACE::ISOMessage::header() const {
    return (hdr_ ? hdr_->pack() : std::vector<uint8_t>{});
}

void TNG_NAMESPACE::ISOMessage::direction(Direction dir) {
    dir_ = dir;
}

TNG_NAMESPACE::ISOMessage::Direction TNG_NAMESPACE::ISOMessage::direction() {
    return (dir_);
}


bool TNG_NAMESPACE::ISOMessage::isInner() const {
    return (k_ > -1);
}

bool TNG_NAMESPACE::ISOMessage::hasMTI() {
    if (isInner())
        return false; // No need for exception
    return (has(0));
}

nonstd::string_view TNG_NAMESPACE::ISOMessage::mti() {
    if (isInner())
        throw std::logic_error("MTI cannot be in inner data elements");
    if (!has(0))
        throw std::logic_error("No MTI data element found");
    return (get<ISOOpaqueField>(0)->value());
}

bool TNG_NAMESPACE::ISOMessage::isRequest() {
    return (((int)mti().at(2) - '0') % 2 == 0);
}

bool TNG_NAMESPACE::ISOMessage::isResponse() {
    return (!isRequest());
}

bool TNG_NAMESPACE::ISOMessage::isAuthorization() {
    return (has(0) && (mti().at(1) == '1'));
}

bool TNG_NAMESPACE::ISOMessage::isFinancial() {
    return (has(0) && (mti().at(1) == '2'));
}

bool TNG_NAMESPACE::ISOMessage::isFileAction() {
    return (has(0) && (mti().at(1) == '3'));
}

bool TNG_NAMESPACE::ISOMessage::isReversal() {
    return (has(0) && (mti().at(1) == '4') && ((mti().at(3) == '0') || (mti().at(3) == '1')));
}

bool TNG_NAMESPACE::ISOMessage::isChargeback() {
    return (has(0) && (mti().at(1) == '4') && ((mti().at(3) == '2') || (mti().at(3) == '3')));
}

bool TNG_NAMESPACE::ISOMessage::isReconciliation() {
    return (has(0) && (mti().at(1) == '5'));
}

bool TNG_NAMESPACE::ISOMessage::isAdministrative() {
    return (has(0) && (mti().at(1) == '6'));
}

bool TNG_NAMESPACE::ISOMessage::isFeeCollection() {
    return (has(0) && (mti().at(1) == '7'));
}

bool TNG_NAMESPACE::ISOMessage::isNetworkManagement() {
    return (has(0) && (mti().at(1) == '8'));
}

bool TNG_NAMESPACE::ISOMessage::isRetransmission() {
    return (has(0) && (mti().at(3) == '1'));
}

// ============================================================================

static std::string toEbcdic(const std::string& ascii) {
    // Create conversion instance without initialization
    iconv_wrapper::iconv enc;
    // Initialize conversion instance: from Local Encoding to EBCDIC (IBM-1047)
    enc.open("", "IBM-1047");
    // Convert and output test string
    std::string ebcdic(enc.convert(ascii));
    // Close conversion instance
    enc.close();

    return ebcdic;
}

static std::string fromEbcdic(const std::string& ebcdic) {
    // Create conversion instance without initialization
    iconv_wrapper::iconv enc;
    // Initialize conversion instance: from EBCDIC (IBM-1047) to Local Encoding
    enc.open("IBM-1047", "");
    // Convert and output test string
    std::string ascii(enc.convert(ebcdic));
    // Close conversion instance
    enc.close();

    return ascii;
}

static std::string getFormattedTimestamp() {
    auto tp = std::chrono::system_clock::now();
    auto dp = date::floor<date::days>(tp);
    date::year_month_day ymd{ dp };
    auto time = date::make_time(tp - dp);

    std::stringstream ss;
    ss << std::setfill('0')
        << std::setw(4) << int(ymd.year()) << '.'
        << std::setw(2) << unsigned(ymd.month()) << '.'
        << std::setw(2) << unsigned(ymd.day()) << '.'
        << std::setw(2) << time.hours().count() << '.'
        << std::setw(2) << time.minutes().count() << '.'
        << std::setw(2) << time.seconds().count() << '.'
        << std::setw(4) << time.subseconds().count();
    return ss.str();
}

static std::array<uint8_t, 3> str2bcd(std::string_view s) {
    if (s.size() > 6)
        throw std::invalid_argument("Maximal 6 Ziffern erlaubt!");
    if (s.size() % 2 != 0)
        s = std::string("0") + std::string(s);
    std::array<uint8_t, 3> bcd{ 0, 0, 0 };

    size_t o = 3 - (s.size() / 2);
    size_t ri = 0;
    for (size_t i = 0; i < s.size(); i += 2) {
        char hi = s[i];
        char lo = s[i + 1];

        if (!std::isdigit(static_cast<unsigned char>(hi)) ||
            !std::isdigit(static_cast<unsigned char>(lo))) {
            throw std::invalid_argument("Ungültige Eingabe (keine Ziffer)");
        }

        bcd[o + ri] = static_cast<uint8_t>(((hi - '0') << 4) | (lo - '0'));
        ++ri;
    }

    return bcd;
}
static std::string bcd2str(const std::vector<uint8_t>& b, size_t offset, size_t len) {
    // TODO: Implementiere BCD-Konvertierung hier
    return "";
}

TNG_NAMESPACE::BASE1Header::BASE1Header() : BASE1Header("000000"_sv, "000000"_sv) {
}
TNG_NAMESPACE::BASE1Header::BASE1Header(nonstd::string_view _source, nonstd::string_view _destination)
    : BaseHeader(std::vector<uint8_t>(LENGTH, 0))
{
    header[0] = LENGTH; // hlen
    setHFormat(1);
    format(2);
    source(_source);
    destination(_destination);
}
TNG_NAMESPACE::BASE1Header::BASE1Header(nonstd::string_view _source, nonstd::string_view _destination, int _format)
    : BaseHeader(std::vector<uint8_t>(LENGTH, 0))
{
    header[0] = LENGTH; // hlen
    setHFormat(1);
    format(_format);
    source(_source);
    destination(_destination);
}
TNG_NAMESPACE::BASE1Header::BASE1Header(const std::vector<uint8_t>& header) : BaseHeader(header) {}

// Getter/Setter für die Header-Felder
int TNG_NAMESPACE::BASE1Header::getHLen() const { return (header[0] & 0xFF); }
void TNG_NAMESPACE::BASE1Header::setHFormat(int hformat) { header[1] = static_cast<uint8_t>(hformat); }
int TNG_NAMESPACE::BASE1Header::format() const { return (header[2] & 0xFF); }
void TNG_NAMESPACE::BASE1Header::format(int format) { header[2] = static_cast<uint8_t>(format); }
void TNG_NAMESPACE::BASE1Header::setRtCtl(int i) { header[11] = static_cast<uint8_t>(i); }
void TNG_NAMESPACE::BASE1Header::setFlags(int i) {
    header[12] = static_cast<uint8_t>(i >> 8);
    header[13] = static_cast<uint8_t>(i);
}
void TNG_NAMESPACE::BASE1Header::setStatus(int i) {
    header[14] = static_cast<uint8_t>(i >> 16);
    header[15] = static_cast<uint8_t>(i >> 8);
    header[16] = static_cast<uint8_t>(i);
}
void TNG_NAMESPACE::BASE1Header::setBatchNumber(int i) { header[17] = static_cast<uint8_t>(i); }
void TNG_NAMESPACE::BASE1Header::setLen(std::size_t len) {
    len += header.size();
    header[3] = static_cast<uint8_t>(len >> 8);
    header[4] = static_cast<uint8_t>(len);
}

void TNG_NAMESPACE::BASE1Header::destination(nonstd::string_view dest) {
    auto bcd = str2bcd(dest);
    std::copy(bcd.begin(), bcd.end(), header.begin() + 5);
}
void TNG_NAMESPACE::BASE1Header::source(nonstd::string_view src) {
    auto bcd = str2bcd(src);
    std::copy(bcd.begin(), bcd.end(), header.begin() + 8);
}
std::optional<nonstd::string_view> TNG_NAMESPACE::BASE1Header::source() const {
    return bcd2str(header, 8, 6);
}
std::optional<nonstd::string_view> TNG_NAMESPACE::BASE1Header::destination() const {
    return bcd2str(header, 5, 6);
}
void TNG_NAMESPACE::BASE1Header::swapDirection() {
    if (header.size() >= LENGTH) {
        std::array<uint8_t, 3> source;
        std::copy(header.begin() + 8, header.begin() + 11, source.begin());
        std::copy(header.begin() + 5, header.begin() + 8, header.begin() + 8);
        std::copy(source.begin(), source.end(), header.begin() + 5);
    }
}
bool TNG_NAMESPACE::BASE1Header::isRejected() const {
    return header.size() >= 26 && (header[22] & 0x80) == 0x80;
}
std::string TNG_NAMESPACE::BASE1Header::getRejectCode() const {
    if (isRejected()) {
        return bcd2str(header, 24, 4);
    }
    return "";
}

std::ostream& operator<<(std::ostream& os, const ::TNG_NAMESPACE::BASE1Header& hdr) {
    os << "[H 01] Header Length        " << hdr.getHLen() << '\n'
        << "[H 02] Header Format        " << 1 << '\n'
        << "[H 03] Text Format          " << hdr.format() << '\n'
        << "[H 04] Total Length         " << hdr.format() << '\n'
        << "[H 05] Destination ID       " << hdr.destination().value() << '\n'
        << "[H 06] Source ID            " << hdr.source().value() << '\n'
        << "[H 07] Round-Trip CTRL Info " << hdr.format() << '\n'
        << "[H 08] BASE I Flags         " << hdr.format() << '\n'
        << "[H 09] Message Status Flags " << hdr.format() << '\n'
        << "[H 10] Batch Number         " << hdr.format() << '\n'
        << "[H 11] Reserved             " << hdr.format() << '\n'
        << "[H 12] User-Info            " << hdr.format() << '\n'
    ;
    return os;
}

// === WLPFOHeader ============================================================

TNG_NAMESPACE::WLP_FOHeader::WLP_FOHeader()
    : WLP_FOHeader("", "", 2, "", "", "00")
{}

TNG_NAMESPACE::WLP_FOHeader::WLP_FOHeader(
    nonstd::string_view _record,
    nonstd::string_view _mti,
    int _version,
    nonstd::string_view _uuid,
    nonstd::string_view _reference,
    int _payment
) : BaseHeader(std::vector<uint8_t>(LENGTH, 0))
{
    length(LENGTH);
    sysId("WLP-FO"_sv);
    record(_record);
    mti(_mti);
    creationTs();
    version(_version);
    uuid(_uuid);
    reference(_reference);
    payment(_payment);
}
TNG_NAMESPACE::WLP_FOHeader::WLP_FOHeader(
    nonstd::string_view _record,
    nonstd::string_view _mti,
    int _version,
    nonstd::string_view _uuid,
    nonstd::string_view _reference,
    nonstd::string_view _payment
) : BaseHeader(std::vector<uint8_t>(LENGTH, 0))
{
    length(LENGTH);
    sysId("WLP-FO"_sv);
    record(_record);
    mti(_mti);
    creationTs();
    version(_version);
    uuid(_uuid);
    reference(_reference);
    payment(_payment);
}

TNG_NAMESPACE::WLP_FOHeader::WLP_FOHeader(const std::vector<uint8_t>& header)
    : BaseHeader(header)
{}


std::vector<uint8_t> TNG_NAMESPACE::WLP_FOHeader::pack() const {
    std::string asciiHeader(header.begin() + 4, header.end());
    std::string ebcdicHeader = toEbcdic(asciiHeader);
    return std::vector<uint8_t>(ebcdicHeader.begin(), ebcdicHeader.end());
}
std::size_t TNG_NAMESPACE::WLP_FOHeader::unpack(const std::vector<uint8_t>& b) {
    std::string ebcdicHeader(b.begin(), b.end());
    std::string asciiHeader = fromEbcdic(ebcdicHeader);
    header = std::vector<uint8_t>(asciiHeader.begin(), asciiHeader.end());
    return header.size();
}

void TNG_NAMESPACE::WLP_FOHeader::length(int len) {
    header[0] = static_cast<uint8_t>(len >> 24);
    header[1] = static_cast<uint8_t>(len >> 16);
    header[2] = static_cast<uint8_t>(len >> 8);
    header[3] = static_cast<uint8_t>(len);
}

int TNG_NAMESPACE::WLP_FOHeader::length() const {
    return (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
}

void TNG_NAMESPACE::WLP_FOHeader::sysId(nonstd::string_view s) {
    if (s.size() > 10) throw std::invalid_argument("sysId too long");
    std::string str(s);
    std::fill(header.begin() + 4, header.begin() + 14, ' ');
    std::copy(str.begin(), str.end(), header.begin() + 4);
}

void TNG_NAMESPACE::WLP_FOHeader::record(nonstd::string_view s) {
    if (s.size() > 10) throw std::invalid_argument("record too long");
    std::string str(s);
    std::fill(header.begin() + 14, header.begin() + 24, ' ');
    std::copy(str.begin(), str.end(), header.begin() + 14);
}

void TNG_NAMESPACE::WLP_FOHeader::mti(nonstd::string_view s) {
    if (s.size() != 4) throw std::invalid_argument("mti must be 4 chars");
    std::copy(s.begin(), s.end(), header.begin() + 24);
}
void TNG_NAMESPACE::WLP_FOHeader::creationTs() {
    std::string ts = getFormattedTimestamp();
    if (ts.size() > 26) ts.erase(26);
    if (ts.size() != 26) throw std::runtime_error("Timestamp format error");
    std::copy(ts.begin(), ts.end(), header.begin() + 28);
}
void TNG_NAMESPACE::WLP_FOHeader::creationTs(nonstd::string_view s) {
    if (s.size() != 26) throw std::invalid_argument("creationTs must be 26 chars");
    std::copy(s.begin(), s.end(), header.begin() + 28);
}

void TNG_NAMESPACE::WLP_FOHeader::version(int v) {
    if (v < 1 || v > 2) throw std::invalid_argument("version must be 1 or 2");
    header[54] = static_cast<uint8_t>(v);
}

int TNG_NAMESPACE::WLP_FOHeader::version() const {
    return header[54];
}

void TNG_NAMESPACE::WLP_FOHeader::uuid(nonstd::string_view s) {
    if (s.size() > 20) throw std::invalid_argument("uuid too long");
    std::string str(s);
    std::fill(header.begin() + 55, header.begin() + 75, '0');
    std::copy(str.begin(), str.end(), header.begin() + 55);
}

void TNG_NAMESPACE::WLP_FOHeader::reference(nonstd::string_view s) {
    if (s.size() > 16) throw std::invalid_argument("reference too long");
    std::string str(s);
    std::fill(header.begin() + 75, header.begin() + 91, '0');
    std::copy(str.begin(), str.end(), header.begin() + 75);
}

void TNG_NAMESPACE::WLP_FOHeader::payment(size_t payment) {
    std::string p = std::to_string(payment);
    if (2 > p.size()) p.insert(0, 2 - p.size(), '0');
    if (p.size() != 2) throw std::invalid_argument("payment must be 2 chars");
    std::copy(p.begin(), p.end(), header.begin() + 91);
}
void TNG_NAMESPACE::WLP_FOHeader::payment(nonstd::string_view s) {
    std::string p(s);
    if (2 > p.size()) p.insert(0, 2 - p.size(), '0');
    if (p.size() != 2) throw std::invalid_argument("payment must be 2 chars");
    std::copy(p.begin(), p.end(), header.begin() + 91);
}

std::unique_ptr<::TNG_NAMESPACE::ISOHeader> TNG_NAMESPACE::WLP_FOHeader::clone() const {
    auto h = std::make_unique<WLP_FOHeader>();
    h->header = this->header;
    h->asciiEncoding = this->asciiEncoding;
    return h;
}

// === ISOUtils ====

inline const ISO_MAP* as_map(const TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr& node)
{
    auto msg = dynamic_cast<const TNG_NAMESPACE::ISOMessage*>(node.get());
    if (!msg)
        return nullptr;

    return &msg->value();
}

void flatten_iso(
    const TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr& node,
    const std::string& path,
    std::map<std::string, std::string>& out)
{
    if (!node)
        return;

    if (!node->is_composite()) {
        out[path] = node->readable_value();
        return;
    }

    auto map_ptr = as_map(node);
    if (!map_ptr)
        return;

    for (const auto& [key, child] : *map_ptr) {

        std::string next =
            path.empty()
            ? std::to_string(key)
            : path + "." + std::to_string(key);

        flatten_iso(child, next, out);
    }
}

std::map<std::string, std::string> TNG_NAMESPACE::ISOUtils::flatten(const ISOMessage& msg) {
    std::map<std::string, std::string> out;

    const ISO_MAP map_ptr = msg.value(); // ISO_MAP

    for (const auto& [key, child] : map_ptr) {
        flatten_iso(child, std::to_string(key), out);
    }

    return out;
}