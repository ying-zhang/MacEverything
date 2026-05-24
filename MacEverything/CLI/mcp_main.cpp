/// MacEverything MCP Server — stdio-based Model Context Protocol server.
/// Acts as a lightweight proxy: translates MCP tool calls into HTTP requests
/// to the running MacEverything app (localhost:19860).
///
/// Protocol: JSON-RPC 2.0 over stdio, newline-delimited.
/// Spec version: 2025-03-26

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <climits>
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

static std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

/// Extract a string value for a given key from a JSON object string.
/// Returns empty string if not found.
static std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";

    // Find the colon after the key
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return "";

    if (json[pos] == '"') {
        // String value — find the closing quote (handle escapes)
        size_t start = pos + 1;
        size_t end = start;
        while (end < json.size()) {
            if (json[end] == '\\') { end += 2; continue; }
            if (json[end] == '"') break;
            end++;
        }
        return json.substr(start, end - start);
    }
    return "";
}

static bool jsonTryGetString(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;

    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return false;

    size_t start = pos + 1;
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\\') { end += 2; continue; }
        if (json[end] == '"') break;
        end++;
    }
    if (end >= json.size()) return false;
    out = json.substr(start, end - start);
    return true;
}

/// Extract an integer or number value for a given key.
/// Returns -1 if not found.
static long jsonGetNumber(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return -1;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return -1;

    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return -1;

    // Read digits (possibly with leading minus)
    std::string numStr;
    if (json[pos] == '-') { numStr += '-'; pos++; }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        numStr += json[pos++];
    }
    if (numStr.empty() || numStr == "-") return -1;
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(numStr.c_str(), &end, 10);
    if (errno != 0 || end == numStr.c_str() || *end != '\0') return -1;
    return value;
}

/// Extract the raw JSON value for a given key (object, array, string, number, bool, null).
static std::string jsonGetRaw(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";

    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return "";

    char ch = json[pos];

    if (ch == '"') {
        // String
        size_t end = pos + 1;
        while (end < json.size()) {
            if (json[end] == '\\') { end += 2; continue; }
            if (json[end] == '"') break;
            end++;
        }
        return json.substr(pos, end - pos + 1);
    }

    if (ch == '{' || ch == '[') {
        // Object or array — match braces
        char open = ch, close = (ch == '{') ? '}' : ']';
        int depth = 1;
        size_t end = pos + 1;
        bool inStr = false;
        while (end < json.size() && depth > 0) {
            if (json[end] == '\\' && inStr) { end += 2; continue; }
            if (json[end] == '"') inStr = !inStr;
            if (!inStr) {
                if (json[end] == open) depth++;
                else if (json[end] == close) depth--;
            }
            if (depth > 0) end++;
        }
        return json.substr(pos, end - pos + 1);
    }

    // Number, bool, null
    size_t end = json.find_first_of(",}] \t\r\n", pos);
    if (end == std::string::npos) end = json.size();
    return json.substr(pos, end - pos);
}

/// Check if a JSON-RPC message has an "id" field (request vs notification).
static bool jsonHasId(const std::string& json) {
    // Look for "id" key at the top level
    // Simple heuristic: find "id": outside of nested objects
    auto pos = json.find("\"id\"");
    if (pos == std::string::npos) return false;

    // Make sure it's at top level (not nested deep)
    int depth = 0;
    for (size_t i = 0; i < pos; i++) {
        if (json[i] == '{' || json[i] == '[') depth++;
        else if (json[i] == '}' || json[i] == ']') depth--;
    }
    return depth <= 1;
}

/// Extract the "id" field as a raw JSON value (could be number or string).
static std::string jsonGetId(const std::string& json) {
    return jsonGetRaw(json, "id");
}

static std::vector<std::string> splitTopLevelArray(const std::string& json) {
    std::vector<std::string> items;
    size_t first = json.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || json[first] != '[') return items;

    int depth = 0;
    bool inStr = false;
    size_t itemStart = std::string::npos;
    for (size_t i = first; i < json.size(); ++i) {
        char ch = json[i];
        if (inStr) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '"') {
                inStr = false;
            }
            continue;
        }
        if (ch == '"') {
            inStr = true;
        } else if (ch == '{' || ch == '[') {
            if (depth == 1 && itemStart == std::string::npos) itemStart = i;
            depth++;
        } else if (ch == '}' || ch == ']') {
            depth--;
            if (depth == 1 && itemStart != std::string::npos) {
                items.push_back(json.substr(itemStart, i - itemStart + 1));
                itemStart = std::string::npos;
            } else if (depth <= 0) {
                break;
            }
        }
    }
    return items;
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
                      "Connection: close\r\n"
                      "\r\n";
    send(fd, req.c_str(), req.size(), 0);

    // Read response
    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, n);
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

static void sendResponse(const std::string& json) {
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
        R"JSON({"name":"search_content","description":"Full-text content search across indexed files. Returns matching file paths with context snippets.","inputSchema":{"type":"object","properties":{"query":{"type":"string","description":"Content search keyword"},"limit":{"type":"integer","description":"Maximum number of results (default 100, max 10000)","default":100}},"required":["query"]},"annotations":{"readOnlyHint":true}},)JSON"
        R"JSON({"name":"recent_files","description":"List recently modified files.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","description":"Maximum number of results (default 100, max 10000)","default":100}},"required":[]},"annotations":{"readOnlyHint":true}},)JSON"
        R"JSON({"name":"index_status","description":"Get the current index status including record count and content index stats.","inputSchema":{"type":"object","properties":{},"required":[]},"annotations":{"readOnlyHint":true}})JSON"
        R"JSON(]})JSON";
}

// ═══════════════════════════════════════════════════════
//  Tool handlers
// ═══════════════════════════════════════════════════════

static std::string handleSearchFiles(const std::string& args) {
    std::string query = jsonGetString(args, "query");
    if (query.empty()) return "Error: missing required parameter 'query'";

    long limit = jsonGetNumber(args, "limit");
    std::string path = "/api/search?q=" + urlEncode(query);
    if (limit > 0) path += "&limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    if (!resp.ok) return resp.body;
    return resp.body;
}

static std::string handleSearchContent(const std::string& args) {
    std::string query = jsonGetString(args, "query");
    if (query.empty()) return "Error: missing required parameter 'query'";

    long limit = jsonGetNumber(args, "limit");
    std::string path = "/api/search/content?q=" + urlEncode(query);
    if (limit > 0) path += "&limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    if (!resp.ok) return resp.body;
    return resp.body;
}

static std::string handleRecentFiles(const std::string& args) {
    long limit = jsonGetNumber(args, "limit");
    std::string path = "/api/recent";
    if (limit > 0) path += "?limit=" + std::to_string(limit);

    auto resp = httpGet(path);
    if (!resp.ok) return resp.body;
    return resp.body;
}

static std::string handleIndexStatus() {
    auto resp = httpGet("/api/status");
    if (!resp.ok) return resp.body;
    return resp.body;
}

// ═══════════════════════════════════════════════════════
//  MCP method dispatch
// ═══════════════════════════════════════════════════════

static void handleRequest(const std::string& json) {
    std::string method = jsonGetString(json, "method");
    std::string id = jsonGetId(json);
    bool isNotification = !jsonHasId(json);

    // Notifications — no response needed
    if (isNotification) {
        if (method == "notifications/initialized") {
            fprintf(stderr, "[MCP] Client initialized\n");
        }
        // Silently ignore other notifications
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
        sendResult(id, result.str());
        return;
    }

    if (method == "ping") {
        sendResult(id, "{}");
        return;
    }

    if (method == "tools/list") {
        sendResult(id, toolDefinitions());
        return;
    }

    if (method == "tools/call") {
        std::string params = jsonGetRaw(json, "params");
        std::string toolName;
        if (params.empty() || !jsonTryGetString(params, "name", toolName)) {
            sendError(id, -32602, "Invalid params: missing tool name");
            return;
        }

        // Extract "arguments" from within "params"
        std::string args = params.empty() ? "{}" : jsonGetRaw(params, "arguments");
        if (args.empty()) args = "{}";

        std::string resultText;
        bool isError = false;

        if (toolName == "search_files") {
            resultText = handleSearchFiles(args);
        } else if (toolName == "search_content") {
            resultText = handleSearchContent(args);
        } else if (toolName == "recent_files") {
            resultText = handleRecentFiles(args);
        } else if (toolName == "index_status") {
            resultText = handleIndexStatus();
        } else {
            resultText = "Unknown tool: " + toolName;
            isError = true;
        }

        // Check if result looks like an HTTP error
        if (resultText.find("Cannot connect") != std::string::npos ||
            resultText.find("Failed to create socket") != std::string::npos ||
            resultText.find("Empty response") != std::string::npos) {
            isError = true;
        }

        sendToolResult(id, resultText, isError);
        return;
    }

    // Unknown method
    sendError(id, -32601, "Method not found: " + method);
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
        "  --port PORT  MacEverything HTTP port (default: %u)\n"
        "  --help       Show this help\n",
        prog, kDefaultHttpPort);
}

// ═══════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_httpPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    fprintf(stderr, "[MCP] MacEverything MCP server starting (HTTP proxy -> localhost:%u)\n",
            g_httpPort);

    // Main loop: read newline-delimited JSON-RPC from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        // Trim trailing whitespace/CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }

        // Validate it starts with '{' (single request) or '[' (batch)
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;

        if (line[first] == '[') {
            auto batch = splitTopLevelArray(line);
            if (batch.empty()) {
                sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                            "\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
                continue;
            }
            for (const auto& item : batch) {
                handleRequest(item);
            }
            continue;
        }

        if (line[first] != '{') {
            fprintf(stderr, "[MCP] Invalid JSON-RPC message (not an object)\n");
            // Send parse error if there might be an id
            sendResponse("{\"jsonrpc\":\"2.0\",\"id\":null,"
                        "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
            continue;
        }

        handleRequest(line);
    }

    fprintf(stderr, "[MCP] stdin closed, shutting down\n");
    return 0;
}
