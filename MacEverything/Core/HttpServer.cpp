#include "HttpServer.h"
#include "SearchEngine.h"
#include "ContentIndex.h"
#include "Logger.h"
#include <cctype>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <thread>
#include <charconv>

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static std::string jsonEscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

struct ContentConfigUpdate {
    bool hasExtensions = false;
    bool hasMaxFileSize = false;
    std::vector<std::string> extensions;
    uint64_t maxFileSize = 0;
};

class ContentConfigJsonParser {
public:
    explicit ContentConfigJsonParser(const std::string& input) : input_(input) {}

    bool parse(ContentConfigUpdate& update) {
        skipWhitespace();
        if (!consume('{')) return false;
        skipWhitespace();
        if (consume('}')) return atEnd();
        while (true) {
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) return false;
            skipWhitespace();
            if (key == "extensions") {
                if (update.hasExtensions || !parseStringArray(update.extensions)) return false;
                update.hasExtensions = true;
            } else if (key == "maxFileSize") {
                if (update.hasMaxFileSize || !parseUInt(update.maxFileSize)) return false;
                update.hasMaxFileSize = true;
            } else {
                return false;
            }
            skipWhitespace();
            if (consume('}')) return atEnd();
            if (!consume(',')) return false;
            skipWhitespace();
        }
    }

private:
    const std::string& input_;
    size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                input_[pos_] == '\r' || input_[pos_] == '\n')) ++pos_;
    }
    bool consume(char c) {
        if (pos_ >= input_.size() || input_[pos_] != c) return false;
        ++pos_;
        return true;
    }
    bool atEnd() {
        skipWhitespace();
        return pos_ == input_.size();
    }
    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool parseHex4(uint32_t& value) {
        if (pos_ + 4 > input_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hexValue(input_[pos_++]);
            if (digit < 0) return false;
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return true;
    }
    static bool appendUtf8(uint32_t codePoint, std::string& out) {
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) return false;
        if (codePoint <= 0x7F) out.push_back(static_cast<char>(codePoint));
        else if (codePoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        return true;
    }
    bool parseString(std::string& out) {
        skipWhitespace();
        if (!consume('"')) return false;
        out.clear();
        while (pos_ < input_.size()) {
            unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos_ >= input_.size()) return false;
            char escape = input_[pos_++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t codePoint = 0;
                    if (!parseHex4(codePoint)) return false;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        if (pos_ + 2 > input_.size() || input_[pos_] != '\\' ||
                            input_[pos_ + 1] != 'u') return false;
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!parseHex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    }
                    if (!appendUtf8(codePoint, out)) return false;
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool parseStringArray(std::vector<std::string>& values) {
        if (!consume('[')) return false;
        skipWhitespace();
        if (consume(']')) return true;
        while (true) {
            std::string value;
            if (!parseString(value)) return false;
            values.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) return false;
            skipWhitespace();
        }
    }
    bool parseUInt(uint64_t& value) {
        size_t start = pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        if (start == pos_) return false;
        auto conversion = std::from_chars(input_.data() + start, input_.data() + pos_, value);
        return conversion.ec == std::errc{} && conversion.ptr == input_.data() + pos_;
    }
};

static bool urlDecode(const std::string& s, std::string& out) {
    out.clear();
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 2 >= s.size()) return false;
            int hi = 0, lo = 0;
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            hi = hexVal(s[i + 1]);
            lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
            return false;
        }
        if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return true;
}

static std::string trimOptionalWhitespace(std::string value) {
    auto isOWS = [](char c) { return c == ' ' || c == '\t'; };
    while (!value.empty() && isOWS(value.front())) value.erase(value.begin());
    while (!value.empty() && isOWS(value.back())) value.pop_back();
    return value;
}

struct ContentLengthHeader {
    bool present = false;
    bool valid = true;
    bool tooLarge = false;
    size_t value = 0;
};

static ContentLengthHeader parseContentLengthHeader(const std::string& raw,
                                                     size_t headerEnd) {
    ContentLengthHeader result;
    size_t lineStart = raw.find("\r\n");
    if (lineStart == std::string::npos || lineStart >= headerEnd) {
        result.valid = false;
        return result;
    }
    lineStart += 2;

    while (lineStart < headerEnd) {
        size_t lineEnd = raw.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > headerEnd) lineEnd = headerEnd;
        const std::string line = raw.substr(lineStart, lineEnd - lineStart);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            for (char& c : name) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (name == "content-length") {
                if (result.present) {
                    result.valid = false;
                    return result;
                }
                result.present = true;
                const std::string value = trimOptionalWhitespace(line.substr(colon + 1));
                uint64_t parsed = 0;
                const auto conversion = std::from_chars(
                    value.data(), value.data() + value.size(), parsed);
                if (value.empty() || conversion.ec != std::errc{} ||
                    conversion.ptr != value.data() + value.size()) {
                    result.valid = false;
                    return result;
                }
                if (parsed > 65'536) {
                    result.valid = false;
                    result.tooLarge = true;
                    return result;
                }
                result.value = static_cast<size_t>(parsed);
            }
        }
        lineStart = lineEnd + 2;
    }
    return result;
}

// ---------------------------------------------------------------------------
// HttpServer lifecycle
// ---------------------------------------------------------------------------

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(uint16_t port,
                       EngineGetter engineGetter,
                       ContentIndexGetter contentIndexGetter) {
    if (running_.load(std::memory_order_relaxed)) return true;

    getEngine_ = std::move(engineGetter);
    getContentIndex_ = std::move(contentIndexGetter);

    int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    serverFd_.store(serverFd, std::memory_order_release);
    if (serverFd < 0) {
        LOG_ERROR("HttpServer", "socket() failed: " << strerror(errno));
        return false;
    }

    int opt = 1;
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Retry bind on EADDRINUSE (previous instance may still be shutting down)
    constexpr int kMaxBindRetries = 5;
    bool bound = false;
    for (int attempt = 0; attempt < kMaxBindRetries; attempt++) {
        if (::bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE || attempt == kMaxBindRetries - 1) {
            LOG_ERROR("HttpServer", "bind() failed on port " << port
                << " after " << (attempt + 1) << " attempt(s): " << strerror(errno));
            ::close(serverFd);
            serverFd_.store(-1, std::memory_order_release);
            return false;
        }
        LOG_WARN("HttpServer", "bind() EADDRINUSE on port " << port
            << ", retry " << (attempt + 1) << "/" << kMaxBindRetries);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!bound) {
        ::close(serverFd);
        serverFd_.store(-1, std::memory_order_release);
        return false;
    }

    if (::listen(serverFd, 16) < 0) {
        LOG_ERROR("HttpServer", "listen() failed: " << strerror(errno));
        ::close(serverFd);
        serverFd_.store(-1, std::memory_order_release);
        return false;
    }

    port_ = port;
    running_.store(true, std::memory_order_release);
    workerThreads_.reserve(kWorkerCount);
    for (size_t i = 0; i < kWorkerCount; ++i) {
        workerThreads_.emplace_back(&HttpServer::workerLoop, this);
    }
    acceptThread_ = std::thread(&HttpServer::acceptLoop, this);

    LOG_INFO("HttpServer", "Listening on 127.0.0.1:" << port_);
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    int serverFd = serverFd_.exchange(-1, std::memory_order_acq_rel);
    if (serverFd >= 0) {
        ::shutdown(serverFd, SHUT_RDWR);
        ::close(serverFd);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(activeClientsMutex_);
        for (int fd : activeClients_) ::shutdown(fd, SHUT_RDWR);
    }
    connectionQueueCV_.notify_all();
    for (auto& worker : workerThreads_) {
        if (worker.joinable()) worker.join();
    }
    workerThreads_.clear();
    {
        std::lock_guard<std::mutex> lock(connectionQueueMutex_);
        while (!connectionQueue_.empty()) {
            ::close(connectionQueue_.front());
            connectionQueue_.pop();
        }
    }

    LOG_INFO("HttpServer", "Stopped");
}

bool HttpServer::isRunning() const {
    return running_.load(std::memory_order_relaxed);
}

uint16_t HttpServer::port() const {
    return port_;
}

void HttpServer::setAdminCallbacks(AdminCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(adminCallbacksMutex_);
    adminCallbacks_ = std::move(callbacks);
}

void HttpServer::setServerMetadata(ServerMetadata metadata) {
    std::lock_guard<std::mutex> lock(metadataMutex_);
    serverMetadata_ = std::move(metadata);
}

void HttpServer::setAuthToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(authTokenMutex_);
    authToken_ = token;
}

void HttpServer::setAuthenticationRequired(bool required) {
    std::lock_guard<std::mutex> lock(authTokenMutex_);
    authenticationRequired_ = required;
}

std::string HttpServer::authTokenSnapshot() {
    std::lock_guard<std::mutex> lock(authTokenMutex_);
    return authToken_;
}

HttpServer::AdminCallbacks HttpServer::adminCallbacksSnapshot() {
    std::lock_guard<std::mutex> lock(adminCallbacksMutex_);
    return adminCallbacks_;
}

// ---------------------------------------------------------------------------
// Accept loop & connection handling
// ---------------------------------------------------------------------------

void HttpServer::acceptLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        struct pollfd pfd{};
        int serverFd = serverFd_.load(std::memory_order_acquire);
        if (serverFd < 0) break;
        pfd.fd = serverFd;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // timeout, check running_

        int clientFd = ::accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        std::unique_lock<std::mutex> lock(connectionQueueMutex_);
        if (connectionQueue_.size() >= kMaxPendingConnections) {
            lock.unlock();
            std::string response = errorResponse(503, "Server busy");
            ::send(clientFd, response.data(), response.size(), 0);
            ::close(clientFd);
            continue;
        }
        connectionQueue_.push(clientFd);
        lock.unlock();
        connectionQueueCV_.notify_one();
    }
}

void HttpServer::workerLoop() {
    for (;;) {
        int clientFd = -1;
        {
            std::unique_lock<std::mutex> lock(connectionQueueMutex_);
            connectionQueueCV_.wait(lock, [this] {
                return !running_.load(std::memory_order_acquire) || !connectionQueue_.empty();
            });
            if (connectionQueue_.empty()) {
                if (!running_.load(std::memory_order_acquire)) return;
                continue;
            }
            clientFd = connectionQueue_.front();
            connectionQueue_.pop();
        }
        if (!running_.load(std::memory_order_acquire)) {
            ::close(clientFd);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(activeClientsMutex_);
            // stop() flips running_ before taking this lock. Rechecking here
            // closes the pop-to-register window where a slow client could
            // otherwise escape shutdown and delay worker joins until timeout.
            if (!running_.load(std::memory_order_acquire)) {
                ::close(clientFd);
                continue;
            }
            activeClients_.insert(clientFd);
        }
        handleConnection(clientFd);
        {
            std::lock_guard<std::mutex> lock(activeClientsMutex_);
            activeClients_.erase(clientFd);
        }
    }
}

void HttpServer::handleConnection(int clientFd) {
    // Set recv timeout to prevent a slow client from blocking the accept loop
    struct timeval tv{5, 0}; // 5 seconds
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Read request headers + partial body (up to 8KB initial read).
    char buf[8192];
    ssize_t n = ::recv(clientFd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        ::close(clientFd);
        return;
    }
    buf[n] = '\0';

    std::string raw(buf, static_cast<size_t>(n));

    constexpr size_t kMaxHeaderBytes = 65'536;
    size_t headerEnd = raw.find("\r\n\r\n");
    while (headerEnd == std::string::npos && raw.size() < kMaxHeaderBytes) {
        char extra[4096];
        ssize_t received = ::recv(clientFd, extra,
                                  std::min(sizeof(extra), kMaxHeaderBytes - raw.size()), 0);
        if (received <= 0) break;
        raw.append(extra, static_cast<size_t>(received));
        headerEnd = raw.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos) {
        const std::string response = errorResponse(400, "Incomplete or too large HTTP headers");
        ::send(clientFd, response.data(), response.size(), 0);
        ::close(clientFd);
        return;
    }

    const ContentLengthHeader contentLength = parseContentLengthHeader(raw, headerEnd);
    if (!contentLength.valid) {
        const int status = contentLength.tooLarge ? 413 : 400;
        const std::string response = errorResponse(
            status, contentLength.tooLarge ? "Request body too large" : "Invalid Content-Length");
        ::send(clientFd, response.data(), response.size(), 0);
        ::close(clientFd);
        return;
    }
    if (contentLength.present) {
        const size_t bodyStart = headerEnd + 4;
        size_t bodyHave = raw.size() - bodyStart;
        while (bodyHave < contentLength.value) {
            char extra[4096];
            const size_t needed = contentLength.value - bodyHave;
            ssize_t received = ::recv(clientFd, extra, std::min(sizeof(extra), needed), 0);
            if (received <= 0) break;
            raw.append(extra, static_cast<size_t>(received));
            bodyHave += static_cast<size_t>(received);
        }
        if (bodyHave < contentLength.value) {
            const std::string response = errorResponse(400, "Incomplete request body");
            ::send(clientFd, response.data(), response.size(), 0);
            ::close(clientFd);
            return;
        }
    }

    auto req = parseRequest(raw);
    std::string response = route(req);

    // Send response (may need multiple writes for large payloads)
    const char* data = response.data();
    size_t remaining = response.size();
    while (remaining > 0) {
        ssize_t sent = ::send(clientFd, data, remaining, 0);
        if (sent <= 0) break;
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    ::close(clientFd);
}

// ---------------------------------------------------------------------------
// HTTP parsing
// ---------------------------------------------------------------------------

HttpServer::HttpRequest HttpServer::parseRequest(const std::string& raw) {
    HttpRequest req;

    // Parse request line: "GET /path?query HTTP/1.1\r\n..."
    size_t methodEnd = raw.find(' ');
    if (methodEnd == std::string::npos) return req;
    req.method = raw.substr(0, methodEnd);

    size_t uriStart = methodEnd + 1;
    size_t uriEnd = raw.find(' ', uriStart);
    if (uriEnd == std::string::npos) uriEnd = raw.size();

    std::string uri = raw.substr(uriStart, uriEnd - uriStart);

    // Split path and query string
    size_t qPos = uri.find('?');
    if (qPos == std::string::npos) {
        req.path = uri;
    } else {
        req.path = uri.substr(0, qPos);
        std::string qs = uri.substr(qPos + 1);

        // Parse query parameters: key=value&key2=value2
        size_t pos = 0;
        while (pos < qs.size()) {
            size_t ampPos = qs.find('&', pos);
            std::string pair = (ampPos == std::string::npos)
                ? qs.substr(pos) : qs.substr(pos, ampPos - pos);

            size_t eqPos = pair.find('=');
            if (eqPos != std::string::npos) {
                std::string key, val;
                if (!urlDecode(pair.substr(0, eqPos), key) ||
                    !urlDecode(pair.substr(eqPos + 1), val)) {
                    req.valid = false;
                    return req;
                }
                req.query[key] = val;
            }
            pos = (ampPos == std::string::npos) ? qs.size() : ampPos + 1;
        }
    }

    // ── Parse Host, Origin, and Authorization headers ──
    size_t headerEnd = raw.find("\r\n\r\n");
    size_t headersStart = raw.find("\r\n");
    if (headersStart == std::string::npos) return req;
    headersStart += 2; // skip \r\n
    if (headerEnd == std::string::npos) headerEnd = raw.size();
    size_t hPos = headersStart;
    bool hasHost = false;
    bool hasOrigin = false;
    bool hasAuthorization = false;
    while (hPos < headerEnd) {
        size_t lineEnd = raw.find("\r\n", hPos);
        if (lineEnd == std::string::npos) lineEnd = headerEnd;
        std::string line = raw.substr(hPos, lineEnd - hPos);
        // Normalise to lower-case for comparison
        std::string lower = line;
        for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("host:", 0) == 0) {
            if (hasHost) {
                // Duplicate Host header — reject
                req.host = "__DUPLICATE__";
            } else {
                hasHost = true;
                const std::string value = trimOptionalWhitespace(line.substr(5));
                if (value.empty()) {
                    req.host = "";  // empty Host value
                } else {
                    req.host = value;
                    // Lowercase for comparison
                    for (auto& c : req.host) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
            }
        } else if (lower.rfind("origin:", 0) == 0) {
            req.originPresent = true;
            if (hasOrigin) {
                req.origin = "__DUPLICATE__";
            } else {
                hasOrigin = true;
                const std::string value = trimOptionalWhitespace(line.substr(7));
                if (value.empty()) {
                    req.origin = "";
                } else {
                    req.origin = value;
                    for (auto& c : req.origin) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
            }
        } else if (lower.rfind("authorization:", 0) == 0) {
            if (hasAuthorization) {
                req.authorization = "__DUPLICATE__";
            } else {
                hasAuthorization = true;
                req.authorization = trimOptionalWhitespace(line.substr(14));
            }
        }
        hPos = lineEnd + 2;
    }

    // Extract body (everything after the double-CRLF we already found)
    if (headerEnd != std::string::npos && headerEnd < raw.size()) {
        req.body = raw.substr(headerEnd + 4);
    }

    return req;
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

std::string HttpServer::route(const HttpRequest& req) {
    if (!req.valid) return errorResponse(400, "Invalid percent encoding");
    // ── Host: strict loopback + port validation (DNS rebinding mitigation) ──
    // Reject duplicate, empty, or missing Host.
    if (req.host == "__DUPLICATE__") {
        return errorResponse(400, "Bad Request: duplicate Host header");
    }
    if (req.host.empty()) {
        return errorResponse(400, "Bad Request: missing Host header");
    }
    {
        // Build the expected Host values for our bind address + port.
        const std::string expected1 = "127.0.0.1:" + std::to_string(port_);
        const std::string expected2 = "localhost:" + std::to_string(port_);
        // HTTP/1.1 allows omitting the port when it is the default for the
        // scheme, but our port is non-standard so the port MUST be present.
        // We still accept the bare loopback host for basic tooling, but only
        // exact loopback strings — no prefix matching that could be abused
        // via DNS rebinding (e.g. "127.attacker.example").
        const bool bareOk = (req.host == "127.0.0.1" || req.host == "localhost");
        if (req.host != expected1 && req.host != expected2 && !bareOk) {
            return errorResponse(400, "Bad Request: host must be loopback with correct port");
        }
    }

    // ── Origin: reject all browser origins by default (CSRF mitigation) ──
    // The server has no CORS headers and no browser-based client is
    // authorised.  Any Origin header present means a browser context is
    // attempting a cross-origin request, which we deny outright.
    if (req.origin == "__DUPLICATE__") {
        return errorResponse(400, "Bad Request: duplicate Origin header");
    }
    if (req.originPresent) {
        return errorResponse(403, "Forbidden: cross-origin requests are not allowed");
    }

    // ── Optional token authentication (health remains public when enabled) ──
    bool authenticationRequired;
    {
        std::lock_guard<std::mutex> lock(authTokenMutex_);
        authenticationRequired = authenticationRequired_;
    }
    if (authenticationRequired && req.path != "/api/health") {
        if (req.authorization == "__DUPLICATE__") {
            return errorResponse(400, "Bad Request: duplicate Authorization header");
        }
        std::string expectedToken = authTokenSnapshot();
        if (expectedToken.empty()) {
            return errorResponse(503, "Service unavailable: HTTP authentication is not configured");
        }
        // Must present "Bearer <token>"
        const std::string requiredPrefix = "Bearer ";
        const bool bearerScheme = req.authorization.size() >= requiredPrefix.size() &&
            std::equal(requiredPrefix.begin(), requiredPrefix.end(), req.authorization.begin(),
                       [](unsigned char lhs, unsigned char rhs) {
                           return std::tolower(lhs) == std::tolower(rhs);
                       });
        if (req.authorization.size() != requiredPrefix.size() + expectedToken.size() ||
            !bearerScheme) {
            return errorResponse(401, "Unauthorized: Bearer token required");
        }
        std::string presentedToken = req.authorization.substr(requiredPrefix.size());
        // Constant-time comparison for the fixed-size token.
        unsigned char mismatch = 0;
        for (size_t i = 0; i < expectedToken.size(); ++i) {
            mismatch |= static_cast<unsigned char>(presentedToken[i] ^ expectedToken[i]);
        }
        if (mismatch != 0) {
            return errorResponse(401, "Unauthorized: invalid token");
        }
    }

    if (req.method == "GET") {
        if (req.path == "/api/search") {
            return handleSearch(req.query);
        } else if (req.path == "/api/search/content") {
            return handleContentSearch(req.query);
        } else if (req.path == "/api/recent") {
            return handleRecent(req.query);
        } else if (req.path == "/api/status") {
            return handleStatus();
        } else if (req.path == "/api/memory") {
            return handleMemory();
        } else if (req.path == "/api/health") {
            return handleHealth();
        } else if (req.path == "/api/content/config") {
            return handleGetContentConfig();
        }
    } else if (req.method == "POST") {
        if (req.path == "/api/index/rebuild") {
            return handleRebuildIndex();
        } else if (req.path == "/api/content/rebuild") {
            return handleRebuildContentIndex();
        } else if (req.path == "/api/content/config") {
            return handleSetContentConfig(req.body);
        }
    } else {
        return errorResponse(405, "Method not allowed");
    }

    return errorResponse(404, "Not found");
}

// ---------------------------------------------------------------------------
// Endpoint handlers
// ---------------------------------------------------------------------------

std::string HttpServer::handleSearch(
        const std::unordered_map<std::string, std::string>& params) {
    auto qIt = params.find("q");
    if (qIt == params.end() || qIt->second.empty()) {
        return errorResponse(400, "Missing required parameter: q");
    }
    const std::string& keyword = qIt->second;

    uint32_t limit = 100;
    auto lIt = params.find("limit");
    if (lIt != params.end()) {
        char* endptr = nullptr;
        long v = std::strtol(lIt->second.c_str(), &endptr, 10);
        if (endptr != lIt->second.c_str() && v > 0) limit = static_cast<uint32_t>(std::min(v, 10000L));
    }

    bool useTrigram = true;
    auto tIt = params.find("trigram");
    if (tIt != params.end() && tIt->second == "0") {
        useTrigram = false;
    }

    auto engine = getEngine_();
    if (!engine) return errorResponse(503, "Engine not available");

    auto start = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint64_t generation = engine->compactionGeneration();
        QueryTimingInfo timing;
        auto indices = engine->query(keyword, limit, useTrigram, timing);
        std::ostringstream json;
        json << "{\"results\":[";
        bool first = true;
        size_t resultCount = 0;
        bool stable = engine->forEachRecordWithPathIfGeneration(indices, generation,
        [&](uint32_t /*idx*/, const FileRecord& r, const std::string& dirPath) {
            if (!first) json << ',';
            first = false;
            ++resultCount;
            std::string fullPath = SearchEngine::makeFullPath(dirPath, r.name);
            json << "{\"name\":\"" << jsonEscapeString(r.name) << "\""
                 << ",\"path\":\"" << jsonEscapeString(fullPath) << "\""
                 << ",\"type\":" << static_cast<int>(r.type)
                 << ",\"size\":" << r.size
                 << ",\"modTime\":" << r.modTime
                 << "}";
        });
        if (!stable) continue;

        auto elapsed = std::chrono::steady_clock::now() - start;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        json << std::fixed << std::setprecision(2);
        json << "],\"count\":" << resultCount
         << ",\"queryTimeMs\":" << ms
         << ",\"timing\":{"
         << "\"totalMs\":" << timing.totalMs
         << ",\"lockWaitMs\":" << timing.lockWaitMs
         << ",\"trigramMs\":" << timing.trigramMs
         << ",\"phase1Ms\":" << timing.phase1Ms
         << ",\"phase2Ms\":" << timing.phase2Ms
         << ",\"sortMs\":" << timing.sortMs
         << ",\"lockHeldMs\":" << timing.lockHeldMs
         << ",\"totalRecords\":" << timing.totalRecords
         << ",\"candidates\":" << timing.candidates
         << ",\"nameMatches\":" << timing.nameMatches
         << ",\"pathMatches\":" << timing.pathMatches
         << ",\"resultCount\":" << timing.resultCount
         << ",\"usedTrigram\":" << (timing.usedTrigram ? "true" : "false")
         << ",\"searchPath\":\"" << jsonEscapeString(timing.searchPath) << "\""
             << "}}";
        return jsonResponse(200, json.str());
    }
    return errorResponse(503, "Index compacted repeatedly; retry the query");
}

std::string HttpServer::handleContentSearch(
        const std::unordered_map<std::string, std::string>& params) {
    auto qIt = params.find("q");
    if (qIt == params.end() || qIt->second.empty()) {
        return errorResponse(400, "Missing required parameter: q");
    }
    const std::string& keyword = qIt->second;

    uint32_t limit = 100;
    auto lIt = params.find("limit");
    if (lIt != params.end()) {
        char* endptr = nullptr;
        long v = std::strtol(lIt->second.c_str(), &endptr, 10);
        if (endptr != lIt->second.c_str() && v > 0) limit = static_cast<uint32_t>(std::min(v, 200L));
    }

    auto contentIndex = getContentIndex_();
    if (!contentIndex) {
        return errorResponse(503, "Content index not available");
    }
    auto engine = getEngine_();
    if (!engine) return errorResponse(503, "Engine not available");

    for (int attempt = 0; attempt < 3; ++attempt) {
        uint64_t contentGeneration = contentIndex->mappingGeneration();
        if ((contentGeneration & 1U) != 0) continue;
        uint64_t generation = engine->compactionGeneration();
        auto matches = contentIndex->query(keyword, limit,
            [](uint32_t, std::string& fullPath) {
                return !fullPath.empty();
            });
        if (contentIndex->mappingGeneration() != contentGeneration) continue;

        std::unordered_map<uint32_t, const ContentMatch*> byIndex;
        std::vector<uint32_t> indices;
        for (const auto& match : matches) {
            indices.push_back(match.fileIndex);
            byIndex[match.fileIndex] = &match;
        }
        std::ostringstream json;
        json << "{\"results\":[";
        bool first = true;
        size_t resultCount = 0;
        bool stable = engine->forEachRecordWithPathIfGeneration(
            indices, generation, [&](uint32_t idx, const FileRecord& rec, const std::string& dirPath) {
                auto it = byIndex.find(idx);
                if (it == byIndex.end()) return;
                const auto& match = *it->second;
                if (!first) json << ',';
                first = false;
                ++resultCount;
                std::string fullPath = SearchEngine::makeFullPath(dirPath, rec.name);
                json << "{\"fileName\":\"" << jsonEscapeString(rec.name) << "\""
                     << ",\"filePath\":\"" << jsonEscapeString(fullPath) << "\""
                     << ",\"snippet\":\"" << jsonEscapeString(match.snippet) << "\""
                     << ",\"matchOffset\":" << match.matchOffset << "}";
            });
        if (!stable || contentIndex->mappingGeneration() != contentGeneration) continue;
        json << "],\"count\":" << resultCount << "}";
        return jsonResponse(200, json.str());
    }
    return errorResponse(503, "Index compacted repeatedly; retry the query");
}

std::string HttpServer::handleRecent(
        const std::unordered_map<std::string, std::string>& params) {
    uint32_t limit = 100;
    auto lIt = params.find("limit");
    if (lIt != params.end()) {
        char* endptr = nullptr;
        long v = std::strtol(lIt->second.c_str(), &endptr, 10);
        if (endptr != lIt->second.c_str() && v > 0) limit = static_cast<uint32_t>(std::min(v, 10000L));
    }

    auto engine = getEngine_();
    if (!engine) return errorResponse(503, "Engine not available");

    for (int attempt = 0; attempt < 3; ++attempt) {
        uint64_t generation = engine->compactionGeneration();
        auto indices = engine->recentIndices(limit);
        std::ostringstream json;
        json << "{\"results\":[";
        bool first = true;
        size_t resultCount = 0;
        bool stable = engine->forEachRecordWithPathIfGeneration(indices, generation,
        [&](uint32_t /*idx*/, const FileRecord& r, const std::string& dirPath) {
            if (!first) json << ',';
            first = false;
            ++resultCount;
            std::string fullPath = SearchEngine::makeFullPath(dirPath, r.name);
            json << "{\"name\":\"" << jsonEscapeString(r.name) << "\""
                 << ",\"path\":\"" << jsonEscapeString(fullPath) << "\""
                 << ",\"type\":" << static_cast<int>(r.type)
                 << ",\"size\":" << r.size
                 << ",\"modTime\":" << r.modTime
                 << "}";
        });
        if (!stable) continue;
        json << "],\"count\":" << resultCount << "}";
        return jsonResponse(200, json.str());
    }
    return errorResponse(503, "Index compacted repeatedly; retry the query");
}

std::string HttpServer::handleStatus() {
    auto engine = getEngine_();
    auto contentIndex = getContentIndex_();
    std::ostringstream json;
    json << "{\"recordCount\":" << (engine ? engine->recordCount() : 0)
         << ",\"liveRecordCount\":" << (engine ? engine->liveRecordCount() : 0)
         << ",\"phase2Pending\":" << (engine && engine->isPhase2Pending() ? "true" : "false")
         << ",\"contentIndexedFileCount\":"
         << (contentIndex ? contentIndex->indexedFileCount() : 0)
         << "}";
    return jsonResponse(200, json.str());
}

std::string HttpServer::handleMemory() {
    auto engine = getEngine_();
    if (!engine) return errorResponse(503, "Engine not available");

    auto m = engine->memoryBreakdown();
    auto opts = engine->options();
    auto field = [](std::ostringstream& json, const char* name, size_t bytes, bool comma = true) {
        if (comma) json << ',';
        json << "\"" << name << "\":{\"bytes\":" << bytes
             << ",\"mib\":" << std::fixed << std::setprecision(2)
             << (static_cast<double>(bytes) / 1048576.0) << "}";
    };

    std::ostringstream json;
    json << "{\"recordCount\":" << engine->recordCount()
         << ",\"liveRecordCount\":" << engine->liveRecordCount()
         << ",\"options\":{"
         << "\"enablePinyinInitials\":" << (opts.enablePinyinInitials ? "true" : "false")
         << ",\"enablePathTrigramIndex\":" << (opts.enablePathTrigramIndex ? "true" : "false")
         << ",\"enablePathIndex\":" << (opts.enablePathIndex ? "true" : "false")
         << "},\"entries\":{"
         << "\"pathIndex\":" << m.pathIndexEntries
         << ",\"pathLookup\":" << m.pathLookupEntries
         << ",\"lowerPathLookup\":" << m.lowerPathLookupEntries
         << ",\"nameTrigram\":" << m.nameTrigramEntries
         << ",\"pinyinTrigram\":" << m.pinyinTrigramEntries
         << ",\"pathTrigram\":" << m.pathTrigramEntries
         << ",\"extensionIndex\":" << m.extensionIndexEntries
         << "},\"memory\":{";

    field(json, "totalApprox", m.totalApproxBytes, false);
    field(json, "origNamePool", m.origNamePoolBytes);
    field(json, "namePool", m.namePoolBytes);
    field(json, "pinyinInitialsPool", m.pinyinInitialsPoolBytes);
    field(json, "pathPool", m.pathPoolBytes);
    field(json, "pathIndices", m.pathIndicesBytes);
    field(json, "types", m.typesBytes);
    field(json, "sizes", m.sizesBytes);
    field(json, "modTimes", m.modTimesBytes);
    field(json, "inodes", m.inodesBytes);
    field(json, "devIds", m.devIdsBytes);
    field(json, "pathIndex", m.pathIndexApproxBytes);
    field(json, "pathLookup", m.pathLookupApproxBytes);
    field(json, "lowerPathLookup", m.lowerPathLookupApproxBytes);
    field(json, "nameTrigram", m.nameTrigramApproxBytes);
    field(json, "nameTrigramPostings", m.nameTrigramPostingBytes);
    field(json, "pinyinTrigram", m.pinyinTrigramApproxBytes);
    field(json, "pinyinTrigramPostings", m.pinyinTrigramPostingBytes);
    field(json, "pathTrigram", m.pathTrigramApproxBytes);
    field(json, "pathTrigramPostings", m.pathTrigramPostingBytes);
    field(json, "pathIdxToRecords", m.pathIdxToRecordsBytes);
    field(json, "extensionIndex", m.extensionIndexApproxBytes);
    field(json, "extensionIndexPostings", m.extensionIndexPostingBytes);
    json << "}}";

    return jsonResponse(200, json.str());
}

std::string HttpServer::handleHealth() {
    auto engine = getEngine_();
    auto contentIndex = getContentIndex_();
    ServerMetadata meta;
    {
        std::lock_guard<std::mutex> lock(metadataMutex_);
        meta = serverMetadata_;
    }
    std::ostringstream json;
    const char* filenameState = !engine ? "starting" :
        (engine->isPhase2Pending() ? "readyPartial" : "ready");
    const char* contentState = contentIndex && contentIndex->indexedFileCount() > 0 ?
        "ready" : "notReady";
    json << "{\"status\":\"ok\""
         << ",\"pid\":" << getpid()
         << ",\"processType\":\"" << jsonEscapeString(meta.processType) << "\""
         << ",\"protocolVersion\":" << meta.protocolVersion
         << ",\"appVersion\":\"" << jsonEscapeString(meta.appVersion) << "\""
         << ",\"indexState\":{"
         << "\"filename\":\"" << filenameState << "\""
         << ",\"content\":\"" << contentState << "\""
         << "}}";
    return jsonResponse(200, json.str());
}

// ---------------------------------------------------------------------------
// Admin endpoint handlers
// ---------------------------------------------------------------------------

std::string HttpServer::handleRebuildIndex() {
    auto callbacks = adminCallbacksSnapshot();
    if (!callbacks.onRebuildIndex) {
        return errorResponse(503, "Admin callbacks not configured");
    }
    callbacks.onRebuildIndex();
    return jsonResponse(202, "{\"message\":\"Index rebuild started\"}");
}

std::string HttpServer::handleRebuildContentIndex() {
    auto callbacks = adminCallbacksSnapshot();
    if (!callbacks.onRebuildContentIndex) {
        return errorResponse(503, "Admin callbacks not configured");
    }
    callbacks.onRebuildContentIndex();
    return jsonResponse(202, "{\"message\":\"Content index rebuild started\"}");
}

std::string HttpServer::handleGetContentConfig() {
    auto callbacks = adminCallbacksSnapshot();
    if (!callbacks.onGetContentExtensions || !callbacks.onGetContentMaxFileSize) {
        return errorResponse(503, "Admin callbacks not configured");
    }

    auto exts = callbacks.onGetContentExtensions();
    uint64_t maxSize = callbacks.onGetContentMaxFileSize();

    std::ostringstream json;
    json << "{\"extensions\":[";
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i > 0) json << ',';
        json << "\"" << jsonEscapeString(exts[i]) << "\"";
    }
    json << "],\"maxFileSize\":" << maxSize << "}";
    return jsonResponse(200, json.str());
}

std::string HttpServer::handleSetContentConfig(const std::string& body) {
    auto callbacks = adminCallbacksSnapshot();
    if (!callbacks.onSetContentConfig ||
        !callbacks.onGetContentExtensions ||
        !callbacks.onGetContentMaxFileSize) {
        return errorResponse(503, "Admin callbacks not configured");
    }

    auto currentExts = callbacks.onGetContentExtensions();
    uint64_t currentMaxSize = callbacks.onGetContentMaxFileSize();
    ContentConfigUpdate update;
    if (!ContentConfigJsonParser(body).parse(update)) {
        return errorResponse(400, "Invalid content configuration JSON");
    }
    static constexpr uint64_t kMaxContentFileSize = 100ULL * 1024 * 1024;
    static constexpr size_t kMaxContentExtensions = 256;
    if (update.hasMaxFileSize &&
        (update.maxFileSize == 0 || update.maxFileSize > kMaxContentFileSize)) {
        return errorResponse(400, "maxFileSize must be between 1 and 104857600 bytes");
    }
    if (update.hasExtensions && update.extensions.size() > kMaxContentExtensions) {
        return errorResponse(400, "At most 256 content extensions are allowed");
    }
    for (const auto& ext : update.extensions) {
        if (ext.empty() || ext.size() > 64 || ext.front() == '.' ||
            std::any_of(ext.begin(), ext.end(), [](unsigned char c) {
                return c <= 0x20 || c == '/' || c == '\\';
            })) {
            return errorResponse(400, "Invalid file extension");
        }
    }

    const auto& finalExts = update.hasExtensions ? update.extensions : currentExts;
    uint64_t newMaxSize = update.hasMaxFileSize ? update.maxFileSize : currentMaxSize;
    callbacks.onSetContentConfig(finalExts, newMaxSize);

    // Build response
    std::ostringstream json;
    json << "{\"message\":\"Content config updated\",\"extensions\":[";
    for (size_t i = 0; i < finalExts.size(); ++i) {
        if (i > 0) json << ',';
        json << "\"" << jsonEscapeString(finalExts[i]) << "\"";
    }
    json << "],\"maxFileSize\":" << newMaxSize << "}";
    return jsonResponse(200, json.str());
}

// ---------------------------------------------------------------------------
// HTTP response builders
// ---------------------------------------------------------------------------

std::string HttpServer::jsonResponse(int status, const std::string& body) {
    const char* statusText = "OK";
    switch (status) {
        case 200: statusText = "OK"; break;
        case 202: statusText = "Accepted"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 405: statusText = "Method Not Allowed"; break;
        case 413: statusText = "Payload Too Large"; break;
        case 500: statusText = "Internal Server Error"; break;
        case 503: statusText = "Service Unavailable"; break;
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: application/json; charset=utf-8\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
    return resp.str();
}

std::string HttpServer::errorResponse(int status, const std::string& message) {
    std::string body = "{\"error\":\"" + jsonEscapeString(message) + "\"}";
    return jsonResponse(status, body);
}
