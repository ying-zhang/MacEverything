#include "HttpServer.h"
#include "SearchEngine.h"
#include "ContentIndex.h"
#include "Logger.h"
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

static std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
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
        }
        if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
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

    // Read request headers + partial body (up to 8KB initial read)
    char buf[8192];
    ssize_t n = ::recv(clientFd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        ::close(clientFd);
        return;
    }
    buf[n] = '\0';

    std::string raw(buf, static_cast<size_t>(n));

    // If Content-Length header is present, ensure we have the full body
    auto clPos = raw.find("Content-Length:");
    if (clPos == std::string::npos) clPos = raw.find("content-length:");
    if (clPos != std::string::npos) {
        size_t valStart = clPos + 15; // strlen("Content-Length:")
        while (valStart < raw.size() && raw[valStart] == ' ') ++valStart;
        size_t valEnd = raw.find("\r\n", valStart);
        if (valEnd != std::string::npos) {
            std::string cl = raw.substr(valStart, valEnd - valStart);
            char* end = nullptr;
            errno = 0;
            unsigned long parsed = std::strtoul(cl.c_str(), &end, 10);
            if (errno != 0 || end == cl.c_str() || *end != '\0' || parsed > 65536UL) {
                std::string response = errorResponse(413, "Invalid or too large Content-Length");
                ::send(clientFd, response.data(), response.size(), 0);
                ::close(clientFd);
                return;
            }
            size_t contentLength = static_cast<size_t>(parsed);
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                size_t bodyStart = headerEnd + 4;
                size_t bodyHave = raw.size() - bodyStart;
                while (bodyHave < contentLength) {
                    char extra[4096];
                    ssize_t m = ::recv(clientFd, extra, sizeof(extra), 0);
                    if (m <= 0) break;
                    raw.append(extra, static_cast<size_t>(m));
                    bodyHave += static_cast<size_t>(m);
                }
            }
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
                std::string key = urlDecode(pair.substr(0, eqPos));
                std::string val = urlDecode(pair.substr(eqPos + 1));
                req.query[key] = val;
            }
            pos = (ampPos == std::string::npos) ? qs.size() : ampPos + 1;
        }
    }

    // Extract body (everything after \r\n\r\n)
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        req.body = raw.substr(headerEnd + 4);
    }

    return req;
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

std::string HttpServer::route(const HttpRequest& req) {
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
    QueryTimingInfo timing;
    auto indices = engine->query(keyword, limit, useTrigram, timing);

    std::ostringstream json;
    json << "{\"results\":[";

    bool first = true;
    engine->forEachRecordWithPath(indices,
        [&](uint32_t /*idx*/, const FileRecord& r, const std::string& dirPath) {
            if (!first) json << ',';
            first = false;
            std::string fullPath = SearchEngine::makeFullPath(dirPath, r.name);
            json << "{\"name\":\"" << jsonEscapeString(r.name) << "\""
                 << ",\"path\":\"" << jsonEscapeString(fullPath) << "\""
                 << ",\"type\":" << static_cast<int>(r.type)
                 << ",\"size\":" << r.size
                 << ",\"modTime\":" << r.modTime
                 << "}";
        });

    auto elapsed = std::chrono::steady_clock::now() - start;
    double ms = std::chrono::duration<double, std::milli>(elapsed).count();

    json << std::fixed << std::setprecision(2);
    json << "],\"count\":" << indices.size()
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
         << ",\"searchPath\":\"" << timing.searchPath << "\""
         << "}}";

    return jsonResponse(200, json.str());
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
        if (endptr != lIt->second.c_str() && v > 0) limit = static_cast<uint32_t>(std::min(v, 10000L));
    }

    auto contentIndex = getContentIndex_();
    if (!contentIndex) {
        return errorResponse(503, "Content index not available");
    }
    auto engine = getEngine_();
    if (!engine) return errorResponse(503, "Engine not available");

    auto matches = contentIndex->query(keyword, limit,
        [engine](uint32_t fileIndex, std::string& fullPath) {
            auto record = engine->getRecord(fileIndex);
            if (record.type != 1) return false;
            fullPath = SearchEngine::makeFullPath(record.path, record.name);
            return true;
        });

    std::ostringstream json;
    json << "{\"results\":[";

    bool first = true;
    for (const auto& match : matches) {
        FileRecord rec = engine->getRecord(match.fileIndex);
        if (rec.type == 0) continue; // tombstoned
        std::string dirPath = engine->resolveRecordPath(match.fileIndex);
        std::string fullPath = SearchEngine::makeFullPath(dirPath, rec.name);

        if (!first) json << ',';
        first = false;
        json << "{\"fileName\":\"" << jsonEscapeString(rec.name) << "\""
             << ",\"filePath\":\"" << jsonEscapeString(fullPath) << "\""
             << ",\"snippet\":\"" << jsonEscapeString(match.snippet) << "\""
             << ",\"matchOffset\":" << match.matchOffset
             << "}";
    }

    json << "],\"count\":" << matches.size() << "}";
    return jsonResponse(200, json.str());
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

    auto indices = engine->recentIndices(limit);

    std::ostringstream json;
    json << "{\"results\":[";

    bool first = true;
    engine->forEachRecordWithPath(indices,
        [&](uint32_t /*idx*/, const FileRecord& r, const std::string& dirPath) {
            if (!first) json << ',';
            first = false;
            std::string fullPath = SearchEngine::makeFullPath(dirPath, r.name);
            json << "{\"name\":\"" << jsonEscapeString(r.name) << "\""
                 << ",\"path\":\"" << jsonEscapeString(fullPath) << "\""
                 << ",\"type\":" << static_cast<int>(r.type)
                 << ",\"size\":" << r.size
                 << ",\"modTime\":" << r.modTime
                 << "}";
        });

    json << "],\"count\":" << indices.size() << "}";
    return jsonResponse(200, json.str());
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
    return jsonResponse(200, "{\"status\":\"ok\"}");
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

    // Simple JSON parsing for {"extensions":["a","b"],"maxFileSize":123}
    // Supports partial updates: only set fields present in the body.
    auto currentExts = callbacks.onGetContentExtensions();
    uint64_t currentMaxSize = callbacks.onGetContentMaxFileSize();

    bool hasExtensions = false;
    std::vector<std::string> newExts;

    // Parse "extensions":[...]
    auto extPos = body.find("\"extensions\"");
    if (extPos != std::string::npos) {
        auto arrStart = body.find('[', extPos);
        auto arrEnd = body.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            hasExtensions = true;
            std::string arrContent = body.substr(arrStart + 1, arrEnd - arrStart - 1);
            // Extract quoted strings
            size_t pos = 0;
            while (pos < arrContent.size()) {
                auto qStart = arrContent.find('"', pos);
                if (qStart == std::string::npos) break;
                auto qEnd = arrContent.find('"', qStart + 1);
                if (qEnd == std::string::npos) break;
                newExts.push_back(arrContent.substr(qStart + 1, qEnd - qStart - 1));
                pos = qEnd + 1;
            }
        }
    }

    // Parse "maxFileSize":number
    uint64_t newMaxSize = currentMaxSize;
    auto msPos = body.find("\"maxFileSize\"");
    if (msPos != std::string::npos) {
        auto colonPos = body.find(':', msPos + 13);
        if (colonPos != std::string::npos) {
            size_t numStart = colonPos + 1;
            while (numStart < body.size() && body[numStart] == ' ') ++numStart;
            size_t numEnd = numStart;
            while (numEnd < body.size() && body[numEnd] >= '0' && body[numEnd] <= '9') ++numEnd;
            if (numEnd > numStart) {
                uint64_t parsed = 0;
                const char* begin = body.data() + numStart;
                const char* end = body.data() + numEnd;
                auto conversion = std::from_chars(begin, end, parsed);
                if (conversion.ec != std::errc{} || conversion.ptr != end) {
                    return errorResponse(400, "Invalid maxFileSize");
                }
                newMaxSize = parsed;
            } else {
                return errorResponse(400, "Invalid maxFileSize");
            }
        }
    }

    const auto& finalExts = hasExtensions ? newExts : currentExts;
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
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 405: statusText = "Method Not Allowed"; break;
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
