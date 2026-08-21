#pragma once
// Part 81: HTTP security regression tests — token, Host, Origin, and
// duplicate/malformed header validation.

#include "../MacEverything/Core/HttpServer.h"
#include "../MacEverything/Core/HttpToken.h"
#include "../MacEverything/Core/PathUtils.h"
#include "../MacEverything/Core/SearchEngine.h"
#include "../MacEverything/Core/ContentIndex.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

// Helper: send a raw HTTP request with full control over headers.
static std::string httpSendRaw(uint16_t port, const std::string& request) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }
    ::send(fd, request.data(), request.size(), MSG_NOSIGNAL);
    std::string response;
    char buf[8192];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    ::close(fd);
    return response;
}

// Extract HTTP status code from response.
static int httpStatus(const std::string& response) {
    auto sp1 = response.find(' ');
    if (sp1 == std::string::npos) return 0;
    auto sp2 = response.find(' ', sp1 + 1);
    std::string code = response.substr(sp1 + 1, sp2 - sp1 - 1);
    return std::atoi(code.c_str());
}

// Build an HTTP GET request with custom headers.
static std::string mkGet(uint16_t port, const std::string& path,
                         const std::string& host,
                         const std::string& auth = "",
                         const std::string& origin = "") {
    (void)port;
    std::string req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    if (!auth.empty()) req += "Authorization: " + auth + "\r\n";
    if (!origin.empty()) req += "Origin: " + origin + "\r\n";
    req += "Connection: close\r\n\r\n";
    return req;
}

static void runHttpSecurityTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 81 — HTTP Security Regression\n";
    std::cout << "========================================\n\n";

    // Token storage creates missing parent directories, enforces 0600, and
    // returns the same token to independent clients.
    {
        const auto tokenHome = fs::temp_directory_path() / "mace_http_token_test";
        fs::remove_all(tokenHome);
        const auto tokenParent = tokenHome / "Library/Application Support/com.maceverything.app";
        const auto tokenPath = tokenParent / ".http_token";

        const std::string created = HttpToken::ensureTokenFile(tokenPath.string(), tokenParent.string());
        check(created.size() == 64, "HTTP token is 256-bit lowercase hex");
        check(HttpToken::readToken(tokenPath.string()) == created, "HTTP client reads generated token");
        struct stat tokenStat{};
        check(stat(tokenPath.c_str(), &tokenStat) == 0 &&
              (tokenStat.st_mode & 0777) == 0600,
              "HTTP token file mode is 0600");
        const std::string custom(64, 'a');
        check(HttpToken::writeToken(custom, tokenPath.string(), tokenParent.string()),
              "Custom HTTP token is saved");
        check(HttpToken::readToken(tokenPath.string()) == custom, "Custom HTTP token is readable");
        check(!HttpToken::writeToken("too-short", tokenPath.string(), tokenParent.string()),
              "Malformed custom token is rejected");
        {
            std::ofstream oversized(tokenPath, std::ios::trunc);
            oversized << std::string(4'096, 'a');
        }
        const std::string repaired = HttpToken::ensureTokenFile(tokenPath.string(), tokenParent.string());
        check(HttpToken::isValidToken(repaired), "Oversized token file is read with a bound and repaired");

        fs::remove(tokenPath);
        const auto victimPath = tokenParent / "token-victim";
        {
            std::ofstream victim(victimPath, std::ios::trunc);
            victim << "preserve-me";
        }
        check(::link(victimPath.c_str(), tokenPath.c_str()) == 0,
              "Hard-linked token test fixture is created");
        check(!HttpToken::writeToken(custom, tokenPath.string(), tokenParent.string()),
              "Hard-linked token file is rejected");
        std::ifstream victim(victimPath);
        std::string victimContent((std::istreambuf_iterator<char>(victim)),
                                  std::istreambuf_iterator<char>());
        check(victimContent == "preserve-me",
              "Rejected token file does not truncate hard-link target");

        fs::remove_all(tokenHome);
    }

    // ── Set up a real HttpServer with a token ──
    // We don't need a real SearchEngine/ContentIndex for header validation
    // tests — the route() method runs header checks before touching engines.
    // But we DO need getter functions that return non-null for /api/health.
    // Use a tiny in-memory SearchEngine so the server doesn't crash.

    auto dummyEngine = std::make_shared<SearchEngine>(SearchEngineOptions{});
    auto dummyContent = std::make_shared<ContentIndex>();

    HttpServer server;
    server.setServerMetadata({"test", 1, "1.0.0-test"});
    server.setAuthToken("abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234");
    auto configuredExtensions = std::make_shared<std::vector<std::string>>(std::vector<std::string>{"txt"});
    auto configuredMaxSize = std::make_shared<uint64_t>(1024 * 1024);
    HttpServer::AdminCallbacks adminCallbacks;
    adminCallbacks.onGetContentExtensions = [configuredExtensions] { return *configuredExtensions; };
    adminCallbacks.onGetContentMaxFileSize = [configuredMaxSize] { return *configuredMaxSize; };
    adminCallbacks.onSetContentConfig = [configuredExtensions, configuredMaxSize](
        const std::vector<std::string>& exts, uint64_t size) {
        *configuredExtensions = exts;
        *configuredMaxSize = size;
    };
    server.setAdminCallbacks(std::move(adminCallbacks));

    uint16_t port = 19861; // non-default to avoid conflicts
    bool started = server.start(port,
        [&dummyEngine]() -> std::shared_ptr<SearchEngine> { return dummyEngine; },
        [&dummyContent]() -> std::shared_ptr<ContentIndex> { return dummyContent; });
    check(started, "HttpServer starts on test port");
    // Give the accept thread a moment to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string goodToken = "Bearer abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234";
    const std::string badToken  = "Bearer 0000000000000000000000000000000000000000000000000000000000000000";
    const std::string hostPort = std::string("127.0.0.1:") + std::to_string(port);
    const std::string hostLocal = std::string("localhost:") + std::to_string(port);

    // Authentication is opt-in. Local clients need no token by default.
    {
        std::string req = mkGet(port, "/api/status", hostPort);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200,
              "Authentication disabled by default → no token required");
    }
    server.setAuthenticationRequired(true);

    // ═══════════════════════════════════════════════════════
    //  Token authentication
    // ═══════════════════════════════════════════════════════

    // /api/health — no token required
    {
        std::string req = mkGet(port, "/api/health", hostPort);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "/api/health without token → 200");
    }
    {
        std::string req = mkGet(port, "/api/health", hostPort, goodToken);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "/api/health with valid token → 200");
    }

    // /api/status — token required
    {
        std::string req = mkGet(port, "/api/status", hostPort);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 401, "/api/status without token → 401");
        check(resp.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0,
              "401 response uses the Unauthorized reason phrase");
    }
    {
        std::string req = mkGet(port, "/api/status", hostPort, badToken);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 401, "/api/status with wrong token → 401");
    }
    {
        std::string req = mkGet(port, "/api/status", hostPort, goodToken);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "/api/status with correct token → 200");
    }
    {
        std::string lowerScheme = goodToken;
        lowerScheme.replace(0, 6, "bearer");
        auto resp = httpSendRaw(port, mkGet(port, "/api/status", hostPort, lowerScheme));
        check(httpStatus(resp) == 200, "Bearer authentication scheme is case-insensitive");
    }
    {
        std::string req = "GET /api/status HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Duplicate Authorization header → 400");
    }

    // Admin endpoints (POST) — token required
    {
        std::string body = "{}";
        std::string req = "POST /api/index/rebuild HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 401, "POST /api/index/rebuild without token → 401");
    }
    {
        std::string body = R"({"maxFileSize":18446744073709551615})";
        std::string req = "POST /api/content/config HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Oversized content maxFileSize → 400");
    }
    {
        std::string body = R"({"extensions":["a\"b","json"],"maxFileSize":2097152})";
        std::string req = "POST /api/content/config HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200 && configuredExtensions->size() == 2 &&
              configuredExtensions->front() == "a\"b" && *configuredMaxSize == 2097152,
              "Strict content config JSON decodes escaped strings");
    }
    {
        std::string body = R"({"extensions":["txt",],"maxFileSize":1})";
        std::string req = "POST /api/content/config HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;
        check(httpStatus(httpSendRaw(port, req)) == 400,
              "Malformed content config JSON is rejected");
    }
    {
        std::string body = "{\"extensions\":[";
        for (int i = 0; i < 257; ++i) {
            if (i > 0) body += ',';
            body += "\"x" + std::to_string(i) + "\"";
        }
        body += "]}";
        std::string req = "POST /api/content/config HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Authorization: " + goodToken + "\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;
        check(httpStatus(httpSendRaw(port, req)) == 400,
              "Excessive content extension count is rejected");
    }
    {
        auto resp = httpSendRaw(port, mkGet(port, "/api/health?q=%GG", hostPort));
        check(httpStatus(resp) == 400, "Invalid percent encoding is rejected");
    }

    // Malformed Authorization header
    {
        std::string req = mkGet(port, "/api/status", hostPort, "Basic dXNlcjpwYXNz");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 401, "Basic auth → 401 (Bearer required)");
    }
    {
        std::string req = mkGet(port, "/api/status", hostPort, "Bearer short");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 401, "Short Bearer token → 401");
    }

    // ═══════════════════════════════════════════════════════
    //  Host header validation
    // ═══════════════════════════════════════════════════════

    // Valid hosts
    {
        std::string req = mkGet(port, "/api/health", hostPort);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "Host: 127.0.0.1:<port> → accepted");
    }
    {
        std::string req = mkGet(port, "/api/health", hostLocal);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "Host: localhost:<port> → accepted");
    }
    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host:\t" + hostPort + "\t\r\n"
                          "CONTENT-LENGTH:\t0\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200,
              "Header names are case-insensitive and tab OWS is accepted");
    }
    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Content-Length: 0\r\n"
                          "content-length: 0\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Duplicate Content-Length header → 400");
    }
    {
        std::string req = "POST /api/index/rebuild HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Content-Length: 65537\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(resp.rfind("HTTP/1.1 413 Payload Too Large\r\n", 0) == 0,
              "Oversized request uses the Payload Too Large reason phrase");
    }
    {
        std::string req = mkGet(port, "/api/health", std::string("[::1]:") + std::to_string(port));
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "IPv6 Host is rejected while server binds IPv4 only");
    }

    // Invalid hosts — DNS rebinding attacks
    {
        std::string req = mkGet(port, "/api/health", std::string("127.attacker.example:") + std::to_string(port));
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Host: 127.attacker.example → 400 (not loopback)");
    }
    {
        std::string req = mkGet(port, "/api/health", std::string("example.com:") + std::to_string(port));
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Host: example.com → 400");
    }
    {
        std::string req = mkGet(port, "/api/health", "127.0.0.2:" + std::to_string(port));
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Host: 127.0.0.2 → 400 (wrong loopback addr)");
    }

    // Duplicate Host header — must be rejected
    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Duplicate Host header → 400");
    }

    // Empty Host value — must be rejected
    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host:\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Empty Host header value → 400");
    }

    // ═══════════════════════════════════════════════════════
    //  Origin header — must reject all browser origins
    // ═══════════════════════════════════════════════════════

    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Origin:\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 403, "Empty Origin header → 403");
        check(resp.rfind("HTTP/1.1 403 Forbidden\r\n", 0) == 0,
              "403 response uses the Forbidden reason phrase");
    }
    {
        std::string req = mkGet(port, "/api/health", hostPort, "", "http://localhost");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 403, "Origin: http://localhost → 403 (browser rejected)");
    }
    {
        std::string req = mkGet(port, "/api/health", hostPort, "", "http://127.0.0.1");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 403, "Origin: http://127.0.0.1 → 403");
    }
    {
        std::string req = mkGet(port, "/api/health", hostPort, "", "null");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 403, "Origin: null → 403 (file:// rejection)");
    }
    {
        std::string req = mkGet(port, "/api/health", hostPort, "", "http://evil.example.com");
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 403, "Origin: evil.example.com → 403");
    }

    // No Origin header — must succeed
    {
        std::string req = mkGet(port, "/api/health", hostPort);
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 200, "No Origin header → accepted");
    }

    // Duplicate Origin header — rejected
    {
        std::string req = "GET /api/health HTTP/1.1\r\n"
                          "Host: " + hostPort + "\r\n"
                          "Origin: http://localhost\r\n"
                          "Origin: http://127.0.0.1\r\n"
                          "Connection: close\r\n\r\n";
        auto resp = httpSendRaw(port, req);
        check(httpStatus(resp) == 400, "Duplicate Origin header → 400");
    }

    // ═══════════════════════════════════════════════════════
    //  /api/health content check
    // ═══════════════════════════════════════════════════════

    {
        std::string req = mkGet(port, "/api/health", hostPort);
        auto resp = httpSendRaw(port, req);
        check(resp.find("\"processType\":\"test\"") != std::string::npos,
              "/api/health contains processType");
        check(resp.find("\"appVersion\":\"1.0.0-test\"") != std::string::npos,
              "/api/health contains appVersion");
        check(resp.find("\"pid\"") != std::string::npos,
              "/api/health contains pid");
        check(resp.find("\"indexState\"") != std::string::npos,
              "/api/health contains indexState");
        // Must NOT leak tokens or paths
        check(resp.find("abcd1234") == std::string::npos,
              "/api/health does NOT leak auth token");
        check(resp.find("Bearer") == std::string::npos,
              "/api/health does NOT leak auth keywords");
    }

    server.stop();
    std::cout << "\n";
}
