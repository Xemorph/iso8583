// =============================================================================
// test_strict_length.cc - FR-5: Opt-in 'strict_length' (Unterlaengen-Pruefung)
// =============================================================================
//
// Default (ohne Key): Legacy-Padding, unveraendert (NUMERIC links mit '0',
// CHAR rechts mit Leerzeichen). Mit 'strict_length: true' (Feld-Key oder
// Root-Default) wird ein zu kurzer Wert bei fester Laenge beim Serialisieren
// im strict-Modus abgelehnt; nicht-strikt: Warnung + Padding.
// L-praefixierte Felder und 'remaining' sind nie betroffen.
//
// Testnamen bewusst ASCII-only (Windows-ctest, s. AGENTS.md).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>
#include <atomic>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace TNG_NAMESPACE;
using Catch::Matchers::ContainsSubstring;

namespace {

struct TempYaml {
    std::filesystem::path path;
    explicit TempYaml(const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto pid = static_cast<unsigned long long>(::getpid());
        path = std::filesystem::temp_directory_path()
            / ("libiso8583_test_sl_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".yml");
        std::ofstream f(path);
        f << content;
    }
    ~TempYaml() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
};

std::string specYaml(const std::string& root, const std::string& fieldExtra) {
    return "spec: \"Strict Length\"\nencoding: ascii\n" + root + R"(
fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "018": { format: numeric, length: 4)" + fieldExtra + R"( }
  "037": { format: char,    length: 6 }
  "048": { format: llchar,  length: 20 }
)";
}

std::vector<uint8_t> build(const std::shared_ptr<ISOParserPtrBase>& parser,
                           const std::string& de18, bool strict = true) {
    parser->strict(strict);
    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(18), de18));
    return msg->parse(msg);
}

} // namespace

TEST_CASE("strict_length - default keeps legacy left zero padding", "[strictlength][field]") {
    TempYaml y(specYaml("", ""));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto bytes = build(parser, "541");
    const std::string tail(bytes.end() - 4, bytes.end());
    CHECK(tail == "0541");
}

TEST_CASE("strict_length - field key rejects undersized value in strict mode", "[strictlength][field][error]") {
    TempYaml y(specYaml("", ", strict_length: true"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    CHECK_THROWS_WITH(build(parser, "541"), ContainsSubstring("zu kurz"));
    // exakte Laenge bleibt gueltig
    const auto bytes = build(parser, "0541");
    CHECK(std::string(bytes.end() - 4, bytes.end()) == "0541");
}

TEST_CASE("strict_length - non-strict mode warns and still pads", "[strictlength][field]") {
    TempYaml y(specYaml("", ", strict_length: true"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto bytes = build(parser, "541", /*strict=*/false);
    CHECK(std::string(bytes.end() - 4, bytes.end()) == "0541");
}

TEST_CASE("strict_length - root default applies to fixed fields", "[strictlength][spec]") {
    TempYaml y(specYaml("strict_length: true\n", ""));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    CHECK_THROWS_WITH(build(parser, "541"), ContainsSubstring("zu kurz"));
}

TEST_CASE("strict_length - field key overrides root default", "[strictlength][spec]") {
    TempYaml y(specYaml("strict_length: true\n", ", strict_length: false"));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    const auto bytes = build(parser, "541");
    CHECK(std::string(bytes.end() - 4, bytes.end()) == "0541");
}

TEST_CASE("strict_length - char fixed field and prefixed field", "[strictlength][field][error]") {
    TempYaml y(specYaml("strict_length: true\n", ""));
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    parser->strict(true);

    SECTION("undersized CHAR is rejected") {
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
        REQUIRE(msg->set(TNG_KEY_TYPE(37), std::string("ABC")));
        CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("zu kurz"));
    }
    SECTION("shorter value in LL field stays valid") {
        auto msg = std::make_shared<Message>();
        msg->parser(parser);
        REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
        REQUIRE(msg->set(TNG_KEY_TYPE(48), std::string("ABC")));
        CHECK_NOTHROW(msg->parse(msg));
    }
}

TEST_CASE("strict_length - amount plain field and remaining are handled", "[strictlength][amount]") {
    TempYaml y(R"(
spec: "Strict Length Amount"
encoding: ascii
strict_length: true

fields:
  "000": { format: numeric, length: 4 }
  "001": { format: bitmap,  length: 8 }
  "004": { format: amount, length: 12, scale: 2 }
  "061":
    type: nested
    format: binary
    length: 26
    children:
      - { format: numeric, length: 1 }
      - { format: remaining, length: 10 }
)");
    auto parser = spec::SpecDecoder::loadFromYaml(y.str());
    parser->strict(true);

    auto msg = std::make_shared<Message>();
    msg->parser(parser);
    REQUIRE(msg->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(msg->set(TNG_KEY_TYPE(4), std::string("19990")));   // 5 statt 12 Ziffern
    CHECK_THROWS_WITH(msg->parse(msg), ContainsSubstring("zu kurz"));

    // remaining (Maximum) ist nie betroffen: nur DE4 exakt setzen
    auto ok = std::make_shared<Message>();
    ok->parser(parser);
    REQUIRE(ok->set(TNG_KEY_TYPE(0), std::string("0200")));
    REQUIRE(ok->set(TNG_KEY_TYPE(4), std::string("000000019990")));
    CHECK_NOTHROW(ok->parse(ok));
}
