/// MacEverything MCP Server — stdio-based Model Context Protocol server.
/// Acts as a lightweight proxy: translates MCP tool calls into HTTP requests
/// to the running MacEverything app (localhost:19860).
///
/// Protocol: JSON-RPC 2.0 over stdio, newline-delimited.
/// Spec version: 2025-03-26

#import <Foundation/Foundation.h>

#include "MaceClient.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <exception>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════
//  Configuration
// ═══════════════════════════════════════════════════════

static constexpr const char* kServerName = "MacEverything";
static constexpr const char* kServerVersion = "1.0.0";
static constexpr const char* kProtocolVersion = "2025-03-26";
static constexpr const char* kHttpHost = "127.0.0.1";
static constexpr uint16_t kDefaultHttpPort = 19860;

static uint16_t g_httpPort = kDefaultHttpPort;

static uint16_t configuredHttpPort() {
    constexpr int kMinimumPort = 1024;
    constexpr int kMaximumPort = 65535;
    CFStringRef domain = CFSTR("com.maceverything.app");
    CFPreferencesAppSynchronize(domain);
    CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("settings.httpPort"), domain);
    if (!value) return kDefaultHttpPort;

    int port = 0;
    bool valid = CFGetTypeID(value) == CFNumberGetTypeID() &&
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &port) &&
        port >= kMinimumPort && port <= kMaximumPort;
    CFRelease(value);
    return valid ? static_cast<uint16_t>(port) : kDefaultHttpPort;
}

// ═══════════════════════════════════════════════════════
//  JSON helpers
// ═══════════════════════════════════════════════════════

static std::string jsonEscape(const std::string& s) {
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

static std::string cppString(NSString* value) {
    if (![value isKindOfClass:[NSString class]]) return "";
    const char* utf8 = value.UTF8String;
    return utf8 ? std::string(utf8) : std::string();
}

static id parseJSON(const std::string& json, NSError** error) {
    NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
    return [NSJSONSerialization JSONObjectWithData:data
                                           options:NSJSONReadingFragmentsAllowed
                                             error:error];
}

static std::string serializeJSONValue(id value) {
    if (!value) return "null";
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:value
                                                   options:NSJSONWritingFragmentsAllowed
                                                     error:&error];
    if (!data || error) return "null";
    return std::string(static_cast<const char*>(data.bytes), data.length);
}

// ═══════════════════════════════════════════════════════
//  HTTP client
// ═══════════════════════════════════════════════════════

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool ok = false;
};

static HttpResponse httpGet(const std::string& path) {
    HttpResponse resp;
    static constexpr size_t kMaxHttpResponseBytes = 16 * 1024 * 1024;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        resp.body = "Failed to create socket";
        return resp;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_httpPort);
    inet_pton(AF_INET, kHttpHost, &addr.sin_addr);

    // Set connect timeout (3 seconds)
    struct timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        resp.body = "Cannot connect to MacEverything (localhost:" +
                    std::to_string(g_httpPort) + "). Is the app running?";
        return resp;
    }

    // Send request
    std::string req = "GET " + path + " HTTP/1.1\r\n"
                      "Host: 127.0.0.1:" + std::to_string(g_httpPort) + "\r\n"
                      "Accept: application/json\r\n";
    // Attach the local HTTP token (non-/api/health only).
    std::string token = mace::readAuthToken();
    if (!token.empty() && path != "/api/health") {
        req += "Authorization: Bearer " + token + "\r\n";
    }
    req += "Connection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t count = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            close(fd);
            resp.body = "Failed to send request to MacEverything";
            return resp;
        }
        sent += static_cast<size_t>(count);
    }

    // Read response
    std::string raw;
    char buf[4096];
    while (true) {
        ssize_t count = recv(fd, buf, sizeof(buf), 0);
        if (count > 0) {
            if (static_cast<size_t>(count) > kMaxHttpResponseBytes - raw.size()) {
                close(fd);
                resp.body = "MacEverything response exceeds the 16 MB limit";
                return resp;
            }
            raw.append(buf, static_cast<size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            resp.body = (errno == EAGAIN || errno == EWOULDBLOCK)
                ? "Timed out waiting for MacEverything response"
                : "Failed to receive response from MacEverything";
            close(fd);
            return resp;
        }
    }
    close(fd);

    if (raw.empty()) {
        resp.body = "Empty response from MacEverything";
        return resp;
    }

    // Parse status line
    auto statusEnd = raw.find("\r\n");
    if (statusEnd != std::string::npos) {
        auto sp1 = raw.find(' ');
        auto sp2 = raw.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos) {
            std::string code = raw.substr(sp1 + 1, sp2 - sp1 - 1);
            char* end = nullptr;
            errno = 0;
            long parsed = std::strtol(code.c_str(), &end, 10);
            if (errno == 0 && end != code.c_str() && *end == '\0' &&
                parsed >= 100 && parsed <= 999) {
                resp.statusCode = static_cast<int>(parsed);
            }
        }
    }

    // Extract body (after \r\n\r\n)
    auto bodyStart = raw.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        resp.body = raw.substr(bodyStart + 4);
    }

    resp.ok = (resp.statusCode >= 200 && resp.statusCode < 300);
    return resp;
}

// ═══════════════════════════════════════════════════════
//  MCP response builders
// ═══════════════════════════════════════════════════════

static std::vector<std::string>*& activeResponseCollector() {
    static std::vector<std::string>* collector = nullptr;
    return collector;
}

class ResponseCollectorScope {
public:
    explicit ResponseCollectorScope(std::vector<std::string>& responses) {
        activeResponseCollector() = &responses;
    }
    ~ResponseCollectorScope() { activeResponseCollector() = nullptr; }
};

static void sendResponse(const std::string& json) {
    if (activeResponseCollector()) {
        activeResponseCollector()->push_back(json);
        return;
    }
    std::cout << json << "\n";
    std::cout.flush();
}

static void sendResult(const std::string& id, const std::string& resultJson) {
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"id\":" << id
        << ",\"result\":" << resultJson << "}";
    sendResponse(out.str());
}

static void sendError(const std::string& id, int code, const std::string& message) {
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"id\":" << id
        << ",\"error\":{\"code\":" << code
        << ",\"message\":\"" << jsonEscape(message) << "\"}}";
    sendResponse(out.str());
}

static void sendToolResult(const std::string& id, const std::string& text, bool isError = false) {
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"id\":" << id
        << ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""
        << jsonEscape(text) << "\"}],\"isError\":"
        << (isError ? "true" : "false") << "}}";
    sendResponse(out.str());
}

// ═══════════════════════════════════════════════════════
//  Tool definitions
// ═══════════════════════════════════════════════════════

static std::string toolDefinitions() {
    return R"JSON({"tools":[)JSON"
        R"JSON({"name":"search_files","description":"Search for files and directories by name. Supports substring matching with trigram acceleration for fast results across millions of files.","inputSchema":{"type":"object","properties":{"query":{"type":"string","description":"Search keyword (substring match, case-insensitive)"},"limit":{"type":"integer","description":"Maximum number of results (default 100, max 10000)","default":100}},"required":["query"]},"annotations":{"readOnlyHint":true}},)JSON"
        R"JSON({"name":"search_content","description":"Full-text content search across indexed files. Returns matching file paths with context snippets.","inputSchema":{"type":"object","properties":{"query":{"type":"string","description":"Content search keyword"},"limit":{"type":"integer","description":"Maximum number of results (default 100, max 200)","default":100,"maximum":200}},"required":["query"]},"annotations":{"readOnlyHint":true}},)JSON"
        R"JSON({"name":"recent_files","description":"List recently modified files.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","description":"Maximum number of results (default 100, max 10000)","default":100}},"required":[]},"annotations":{"readOnlyHint":true}},)JSON"
        R"JSON({"name":"index_status","description":"Get the current index status including record count and content index stats.","inputSchema":{"type":"object","properties":{},"required":[]},"annotations":{"readOnlyHint":true}})JSON"
        R"JSON(]})JSON";
}

// ═══════════════════════════════════════════════════════
//  Tool handlers
// ═══════════════════════════════════════════════════════

static long argumentLimit(NSDictionary* args, long maximum = 10000) {
    id value = args[@"limit"];
    if (![value isKindOfClass:[NSNumber class]]) return -1;
    long limit = [static_cast<NSNumber*>(value) longValue];
    if (limit <= 0) return -1;
    return std::min(limit, maximum);
}

struct ToolHandlerResult {
    std::string text;
    bool isError = false;
};

static ToolHandlerResult handleSearchFiles(NSDictionary* args) {
    std::string query = cppString(args[@"query"]);
    if (query.empty()) return {"Error: missing required parameter 'query'", true};

    long limit = argumentLimit(args);
    std::string path = "/api/search?q=" + mace::urlEncode(query);
    if (limit > 0) path += "&limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    return {resp.body, !resp.ok};
}

static ToolHandlerResult handleSearchContent(NSDictionary* args) {
    std::string query = cppString(args[@"query"]);
    if (query.empty()) return {"Error: missing required parameter 'query'", true};

    long limit = argumentLimit(args, 200);
    std::string path = "/api/search/content?q=" + mace::urlEncode(query);
    if (limit > 0) path += "&limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    return {resp.body, !resp.ok};
}

static ToolHandlerResult handleRecentFiles(NSDictionary* args) {
    long limit = argumentLimit(args);
    std::string path = "/api/recent";
    if (limit > 0) path += "?limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    return {resp.body, !resp.ok};
}

static ToolHandlerResult handleIndexStatus() {
    auto resp = httpGet("/api/status");
    return {resp.body, !resp.ok};
}

// ═══════════════════════════════════════════════════════
//  MCP method dispatch
// ═══════════════════════════════════════════════════════

static void handleRequest(NSDictionary* request) {
    id rawID = request[@"id"];
    NSString* methodValue = request[@"method"];
    NSString* jsonRPCVersion = request[@"jsonrpc"];
    std::string method = cppString(methodValue);
    std::string requestID = serializeJSONValue(rawID);
    bool isNotification = rawID == nil;

    if (![jsonRPCVersion isKindOfClass:[NSString class]] ||
        ![jsonRPCVersion isEqualToString:@"2.0"] ||
        ![methodValue isKindOfClass:[NSString class]]) {
        sendError(requestID, -32600, "Invalid Request: method must be a string");
        return;
    }

    // Valid notifications never receive a response.
    if (isNotification) {
        if (method == "notifications/initialized") {
            fprintf(stderr, "[MCP] Client initialized\n");
        }
        return;
    }

    // Requests — must send a response
    if (method == "initialize") {
        std::ostringstream result;
        result << "{\"protocolVersion\":\"" << kProtocolVersion << "\""
               << ",\"capabilities\":{\"tools\":{}}"
               << ",\"serverInfo\":{\"name\":\"" << kServerName
               << "\",\"version\":\"" << kServerVersion << "\"}"
               << ",\"instructions\":\"MacEverything provides fast file search across your entire Mac. "
               << "Use search_files for filename search, search_content for full-text search.\""
               << "}";
        sendResult(requestID, result.str());
        return;
    }

    if (method == "ping") {
        sendResult(requestID, "{}");
        return;
    }

    if (method == "tools/list") {
        sendResult(requestID, toolDefinitions());
        return;
    }

    if (method == "tools/call") {
        id paramsValue = request[@"params"];
        if (![paramsValue isKindOfClass:[NSDictionary class]]) {
            sendError(requestID, -32602, "Invalid params: missing tool name");
            return;
        }
        NSDictionary* params = static_cast<NSDictionary*>(paramsValue);
        NSString* toolNameValue = params[@"name"];
        if (![toolNameValue isKindOfClass:[NSString class]]) {
            sendError(requestID, -32602, "Invalid params: missing tool name");
            return;
        }
        std::string toolName = cppString(toolNameValue);

        id argumentsValue = params[@"arguments"];
        if (argumentsValue && argumentsValue != [NSNull null] &&
            ![argumentsValue isKindOfClass:[NSDictionary class]]) {
            sendError(requestID, -32602, "Invalid params: arguments must be an object");
            return;
        }
        NSDictionary* args = [argumentsValue isKindOfClass:[NSDictionary class]]
            ? static_cast<NSDictionary*>(argumentsValue) : @{};

        ToolHandlerResult toolResult;

        if (toolName == "search_files") {
            toolResult = handleSearchFiles(args);
        } else if (toolName == "search_content") {
            toolResult = handleSearchContent(args);
        } else if (toolName == "recent_files") {
            toolResult = handleRecentFiles(args);
        } else if (toolName == "index_status") {
            toolResult = handleIndexStatus();
        } else {
            toolResult = {"Unknown tool: " + toolName, true};
        }
        sendToolResult(requestID, toolResult.text, toolResult.isError);
        return;
    }

    // Unknown method
    sendError(requestID, -32601, "Method not found: " + method);
}

// ═══════════════════════════════════════════════════════
//  Argument parsing
// ═══════════════════════════════════════════════════════

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "MCP (Model Context Protocol) server for MacEverything.\n"
        "Reads JSON-RPC from stdin, writes to stdout.\n\n"
        "Options:\n"
        "  --port PORT  Override the port from MacEverything settings"
        " (fallback %u)\n"
        "  --help       Show this help\n",
        prog, kDefaultHttpPort);
}

static bool parsePort(const char* text, uint16_t& port) {
    if (!text || *text == '\0') return false;
    char* end = nullptr;
    errno = 0;
    unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535) {
        return false;
    }
    port = static_cast<uint16_t>(value);
    return true;
}

// ═══════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    g_httpPort = configuredHttpPort();

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--port") == 0) {
            if (++i >= argc || !parsePort(argv[i], g_httpPort)) {
                fprintf(stderr, "Invalid port; expected an integer from 1 to 65535\n");
                printUsage(argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    fprintf(stderr, "[MCP] MacEverything MCP server starting (HTTP proxy -> localhost:%u)\n",
            g_httpPort);

    // Main loop: read newline-delimited JSON-RPC from stdin
    static constexpr size_t kMaxInputLineBytes = 1024 * 1024;
    std::string line;
    bool inputComplete = false;
    while (!inputComplete) {
        line.clear();
        bool oversized = false;
        char ch = 0;
        while (std::cin.get(ch)) {
            if (ch == '\n') break;
            if (line.size() < kMaxInputLineBytes) line.push_back(ch);
            else oversized = true;
        }
        inputComplete = !std::cin && ch != '\n';
        if (line.empty() && !oversized && inputComplete) break;
        if (oversized) {
            sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                         "\"error\":{\"code\":-32700,\"message\":\"Input exceeds 1 MB limit\"}}");
            continue;
        }
        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        // Trim trailing whitespace/CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }

        @autoreleasepool {
            NSError* parseError = nil;
            id message = parseJSON(line, &parseError);
            if (!message || parseError) {
                fprintf(stderr, "[MCP] Invalid JSON-RPC message: %s\n",
                        parseError.localizedDescription.UTF8String ?: "parse error");
                sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                             "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
                continue;
            }

            if ([message isKindOfClass:[NSArray class]]) {
                NSArray* batch = static_cast<NSArray*>(message);
                if (batch.count == 0) {
                    sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                                 "\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
                    continue;
                }

                std::vector<std::string> responses;
                try {
                    ResponseCollectorScope collectorScope(responses);
                    for (id item in batch) {
                        if ([item isKindOfClass:[NSDictionary class]]) {
                            handleRequest(static_cast<NSDictionary*>(item));
                        } else {
                            sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                                         "\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
                        }
                    }
                } catch (const std::exception& error) {
                    fprintf(stderr, "[MCP] Failed to build batch response: %s\n", error.what());
                    sendResponse("[{\"jsonrpc\":\"2.0\",\"id\":null,"
                                 "\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}]");
                    continue;
                }
                if (!responses.empty()) {
                    std::ostringstream batchResponse;
                    batchResponse << '[';
                    for (size_t i = 0; i < responses.size(); ++i) {
                        if (i > 0) batchResponse << ',';
                        batchResponse << responses[i];
                    }
                    batchResponse << ']';
                    sendResponse(batchResponse.str());
                }
                continue;
            }

            if (![message isKindOfClass:[NSDictionary class]]) {
                sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                            "\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
                continue;
            }
            handleRequest(static_cast<NSDictionary*>(message));
        }
    }

    fprintf(stderr, "[MCP] stdin closed, shutting down\n");
    return 0;
}
