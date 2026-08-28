// =============================================================================
// tcp_gateway - Minimal ISO 8583 TCP gateway
//
// Laueert auf einem TCP-Port, nimmt pro Verbindung die kompletten rohen
// Wire-Bytes einer kodierten ISO-8583-Nachricht entgegen, dekodiert sie
// mit libiso8583 (msg->unparse) und gibt SUCCESS/FAIL plus die geparsten
// Felder aus.
//
// Verwendung:
//   iso8583_tcp_gateway [Port] [Spec-Datei.yml]
//   (Defaults: Port 9000, ./ascii_spec.yml)
//
// Frame-Vertrag: Der Peer liefert die Nachricht "so wie sie kommt" - ein
// Frame ohne eigene Laenge/Endemarke. Das Gateway liest, bis der Socket
// keine frischen Bytes mehr bringt (SO_RCVTIMEO, s. RX_TIMEOUT_MS) oder
// der Peer die Verbindung schliesst.
//
// Testen (aus einem anderen Terminal):
//   python send_test.py                       # ASCII-Frame an 127.0.0.1:9000
//   python send_test.py 127.0.0.1 9000 ebcdic # EBCDIC-Frame (mit spec_ebcdic.yml)
// =============================================================================

#include <iso8583/ISOLog.hh>
#include <iso8583/ISOMessage.hh>
#include <iso8583/ISOSpec.hh>

#include <cctype>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#    define NOMINMAX  // windows.h darf die max/min-Makros nicht definieren
#    include <winsock2.h>
#    include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32")
using socket_t = ::SOCKET;
using socklen_t = int;
#    define CLOSE_SOCKET(s) ::closesocket(s)
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/time.h>
#    include <unistd.h>
using socket_t = int;              // ::socklen_t kommt aus <sys/socket.h>
#    define CLOSE_SOCKET(s) ::close(s)
#endif

namespace {

    using namespace ::iso8583;

    constexpr int DEFAULT_PORT = 9000;
    constexpr const char* DEFAULT_SPEC = "ascii_spec.yml";
    constexpr std::size_t MAX_FRAME_BYTES = 1024 * 1024;  // Sicherheitslimit
    constexpr int RX_TIMEOUT_MS = 5000;                    // "Frame fertig" nach Stille

    volatile std::sig_atomic_t g_stop = 0;
    socket_t g_listen_sock = -1;

    // Pro Gateway-Lauf geladener Parser; ISOParserPtrBase ist read-only nach
    // dem Build -> darf in handle_client thread-parallel gelesen werden.
    ISOParserPtrBase::ISOParserPtrBaseSmartPtr g_parser;

    void on_signal(int) { g_stop = 1; }

    // Log-Zeile ohne Format-Geheimnisse (va_args-basiert).
    void out(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);
        std::fputs(buf, stdout);
        std::fflush(stdout);
    }

    // ── Frame empfangen: recv-Schleife bis Timeout/EOF ───────────────────────
    bool receive_frame(socket_t sock, std::vector<uint8_t>& out) {
        out.clear();
        char buf[4096];
        while (out.size() < MAX_FRAME_BYTES) {
            const int n = static_cast<int>(::recv(sock, buf, sizeof buf, 0));
            if (n > 0) {
                out.insert(out.end(), buf, buf + n);
                continue;
            }
            if (n == 0)
                return !out.empty();  // ordentliches EOF (Peer schloss)
            // n < 0: blocking-Socket mit SO_RCVTIMEO -> Timeout = Frame fertig;
            // jeder andere Fehler bricht die Verbindung ab.
#    if defined(_WIN32)
            const bool timeout = (WSAGetLastError() == WSAETIMEDOUT);
#    else
            const bool timeout = (errno == EAGAIN || errno == EWOULDBLOCK);
#    endif
            if (timeout)
                return !out.empty();
            return false;
        }
        return out.size() >= MAX_FRAME_BYTES;  // Limit erreicht -> Frame unvollstaendig, aber weiter
    }

    // ── Eine Verbindung: empfangen, dekodieren, berichten ────────────────────
    void handle_client(socket_t client) {
        // recv-Timeout: Der Peer sendet einen Frame und bleibt oft einfach
        // verbunden -> ohne Timeout wuerde receive_frame fuer immer haengen.
#    if defined(_WIN32)
        const DWORD timeout = RX_TIMEOUT_MS;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
#    else
        const struct timeval tv{ RX_TIMEOUT_MS / 1000, (RX_TIMEOUT_MS % 1000) * 1000 };
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#    endif

        std::vector<uint8_t> raw;
        if (!receive_frame(client, raw) || raw.empty()) {
            out("RESULT: FAIL - connection closed before any data arrived\n\n");
            return;
        }

        out("RECV: %zu bytes\n", raw.size());

        try {
            auto msg = std::make_shared<Message>();
            msg->parser(g_parser);

            const std::size_t consumed = msg->unparse(msg, raw);

            if (!msg->hasMTI()) {
                out("RESULT: FAIL - no MTI decoded (spec cannot parse these bytes)\n\n");
                return;
            }

            const std::string mti{std::string(msg->mti())};
            out("MTI   : %s  (%s, %s%s)\n",
                mti.c_str(),
                msg->isFinancial() ? "financial" : "non-financial",
                msg->isRequest() ? "request" : (msg->isResponse() ? "response" : "other"),
                msg->isAuthorization() ? " / authorization" : "");
            out("FIELDS: %zu (incl. MTI + bitmap)\n", msg->size());
            out("JSON  :\n%s\n", msg->to_json().dump(2).c_str());

            if (consumed == std::numeric_limits<std::size_t>::max()) {
                out("RESULT: FAIL - no parser attached to message\n\n");
                return;
            }
            if (consumed < raw.size())
                out("NOTE  : only %zu of %zu bytes consumed by spec (trailing %zu ignored)\n",
                    consumed, raw.size(), raw.size() - consumed);

            out("RESULT: SUCCESS - message %s decoded, %zu field(s)\n\n", mti.c_str(), msg->size());
        } catch (const std::exception& e) {
            out("RESULT: FAIL - decode error: %s\n\n", e.what());
        }
    }

    int run(int port, const std::string& spec_path) {
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
#    if defined(_WIN32)
        WSADATA wsa;
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            out("FATAL: WSAStartup failed\n");
            return 1;
        }
#    endif

        // 1) Spec einmal am Start laden.
        try {
            g_parser = spec::SpecDecoder::loadFromYaml(spec_path);
        } catch (const std::exception& e) {
            out("FATAL: cannot load spec '%s': %s\n", spec_path.c_str(), e.what());
            return 1;
        }
        if (!g_parser) {
            out("FATAL: spec '%s' loaded but yielded no parser\n", spec_path.c_str());
            return 1;
        }
        out("SPEC  : %s (loaded)\n", spec_path.c_str());

        // 2) TCP-Server-Socket.
        g_listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (g_listen_sock < 0) {
            out("FATAL: socket() failed\n");
            return 1;
        }
        const int yes = 1;
        ::setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<unsigned short>(port));
        if (::bind(g_listen_sock, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0
            || ::listen(g_listen_sock, 8) != 0) {
            out("FATAL: bind/listen on port %d failed\n", port);
            CLOSE_SOCKET(g_listen_sock);
            return 1;
        }
        out("LISTEN: 0.0.0.0:%d (Ctrl+C to stop)\n\n", port);

        // 3) Akzeptier-Loop mit 1s-Timeout, damit der Stop-Flag geprüft wird.
        while (!g_stop) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(g_listen_sock, &rfds);
            timeval tv{ 1, 0 };
            const int maxfd = static_cast<int>(g_listen_sock) + 1;
#    if defined(_WIN32)
            if (::select(0, &rfds, nullptr, nullptr, &tv) <= 0)
                continue;
#    else
            if (::select(maxfd, &rfds, nullptr, nullptr, &tv) <= 0)
                continue;
#    endif

            sockaddr_in peer{};
            socklen_t plen = sizeof peer;
            const socket_t client = ::accept(g_listen_sock, reinterpret_cast<sockaddr*>(&peer), &plen);
            if (client < 0)
                continue;
            const int nodelay = 1;
            ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);

            out("CONN  : %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
            handle_client(client);
            CLOSE_SOCKET(client);
        }

        CLOSE_SOCKET(g_listen_sock);
#    if defined(_WIN32)
        ::WSACleanup();
#    endif
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    int port = DEFAULT_PORT;
    std::string spec_path = DEFAULT_SPEC;
    if (argc > 1 && argv[1][0] != '\0' && std::isdigit(static_cast<unsigned char>(argv[1][0])))
        port = std::atoi(argv[1]);
    if (argc > 2)
        spec_path = argv[2];

    if (argc > 3) {
        std::fprintf(stderr, "usage: %s [port] [spec.yml]\n", argv[0]);
        return 2;
    }

    // Logging: der Default-WARN-Logger reicht; fuer eigene Logger
    // iso8583::log::setLogger(...) mit einem ISOLogger-Subclass einsetzen.
    return run(port, spec_path);
}