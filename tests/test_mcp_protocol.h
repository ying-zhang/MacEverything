#pragma once
// Part 49: MCP Protocol Tests
// Tests the MCP server binary via subprocess stdio communication.

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

struct McpExecResult {
    std::string binary;
    std::string output;
    int waitStatus = -1;
};

static std::string findMcpBinary() {
    const char* configured = std::getenv("MACE_MCP_BINARY");
    std::string binary = configured ? configured : "";
    if (!binary.empty()) return access(binary.c_str(), X_OK) == 0 ? binary : "";
    if (binary.empty()) {
        const char* candidates[] = {
            "./artifacts/MacEverything.app/Contents/MacOS/MacEverythingMCP",
            "./build/mcp/Release/MacEverythingMCP",
            "./build/arm64/Release/MacEverythingMCP",
            "./build/Release/MacEverythingMCP"
        };
        for (const char* candidate : candidates) {
            if (access(candidate, X_OK) == 0) {
                binary = candidate;
                break;
            }
        }
    }
    return binary;
}

/// Send JSON-RPC messages to the MCP binary via pipe and capture its status and output.
static McpExecResult mcpExecWithStatus(const std::string& input) {
    McpExecResult result;
    result.binary = findMcpBinary();
    if (result.binary.empty()) return result;
    int stdinPipe[2];
    int stdoutPipe[2];
    if (pipe(stdinPipe) != 0) return result;
    if (pipe(stdoutPipe) != 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        return result;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return result;
    }
    if (pid == 0) {
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, STDERR_FILENO);
            if (devNull != STDERR_FILENO) close(devNull);
        }
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        execl(result.binary.c_str(), result.binary.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    size_t written = 0;
    while (written < input.size()) {
        ssize_t count = write(stdinPipe[1], input.data() + written, input.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += static_cast<size_t>(count);
    }
    close(stdinPipe[1]);

    std::string output;
    char buf[4096];
    while (true) {
        ssize_t count = read(stdoutPipe[0], buf, sizeof(buf));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        result.output.append(buf, static_cast<size_t>(count));
    }
    close(stdoutPipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.waitStatus = status;
    return result;
}

static std::string mcpExec(const std::string& input) {
    return mcpExecWithStatus(input).output;
}

/// Split newline-delimited responses into individual JSON strings.
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing \r
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

/// Check if a JSON string contains a given substring.
static bool jsonContains(const std::string& json, const std::string& needle) {
    return json.find(needle) != std::string::npos;
}

static void runMcpProtocolTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 49: MCP Protocol Tests\n";
    std::cout << "========================================\n\n";

    auto probe = mcpExecWithStatus(R"({"jsonrpc":"2.0","id":0,"method":"ping"})");
    if (probe.binary.empty()) {
        check(false, "MCP binary must exist and be executable for protocol tests");
        return;
    }
    if (probe.waitStatus == -1) {
        check(false, "MCP protocol probe could not start or wait for the process");
        return;
    }
    if (WIFSIGNALED(probe.waitStatus)) {
        std::string message = "MCP binary terminated by signal " +
                              std::to_string(WTERMSIG(probe.waitStatus));
        check(false, message.c_str());
        return;
    }
    if (!WIFEXITED(probe.waitStatus) || WEXITSTATUS(probe.waitStatus) != 0) {
        std::string message = "MCP binary exited with status " +
                              std::to_string(WIFEXITED(probe.waitStatus)
                                                 ? WEXITSTATUS(probe.waitStatus) : -1);
        check(false, message.c_str());
        return;
    }
    if (probe.output.empty()) {
        check(false, "MCP binary exited successfully without a protocol response");
        return;
    }

    // -- Test 1: Initialize --
    std::cout << "  --- Test 1: Initialize ---\n";
    {
        std::string input = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 1, "initialize: exactly 1 response line");
        check(jsonContains(lines[0], "\"protocolVersion\":\"2025-03-26\""), "initialize: has protocol version");
        check(jsonContains(lines[0], "\"name\":\"MacEverything\""), "initialize: has server name");
        check(jsonContains(lines[0], "\"tools\":{}"), "initialize: has tools capability");
        check(jsonContains(lines[0], "\"id\":1"), "initialize: id matches");
    }

    // -- Test 2: tools/list --
    std::cout << "\n  --- Test 2: tools/list ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
            "\n"
            R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "tools/list: 2 response lines (init + list)");
        auto& toolsResp = lines[1];
        check(jsonContains(toolsResp, "\"id\":2"), "tools/list: id matches");
        check(jsonContains(toolsResp, "search_files"), "tools/list: has search_files");
        check(jsonContains(toolsResp, "search_content"), "tools/list: has search_content");
        check(jsonContains(toolsResp, "recent_files"), "tools/list: has recent_files");
        check(jsonContains(toolsResp, "index_status"), "tools/list: has index_status");
        check(jsonContains(toolsResp, "inputSchema"), "tools/list: tools have inputSchema");
    }

    // -- Test 3: ping --
    std::cout << "\n  --- Test 3: ping ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":99,"method":"ping"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "ping: 2 response lines");
        check(jsonContains(lines[1], "\"id\":99"), "ping: id matches");
        check(jsonContains(lines[1], "\"result\":{}"), "ping: result is empty object");
    }

    // -- Test 4: Unknown method returns error --
    std::cout << "\n  --- Test 4: Unknown method ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":5,"method":"nonexistent/method"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "unknown method: 2 response lines");
        check(jsonContains(lines[1], "\"error\""), "unknown method: has error field");
        check(jsonContains(lines[1], "-32601"), "unknown method: error code is -32601");
    }

    // -- Test 5: notifications/initialized is silent --
    std::cout << "\n  --- Test 5: Notification is silent ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        // Notification has no "id" so should produce no response
        check(lines.size() == 1, "notification: only 1 response (initialize), notification is silent");
    }

    // -- Test 6: tools/call with unknown tool --
    std::cout << "\n  --- Test 6: Unknown tool call ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"nonexistent_tool","arguments":{}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "unknown tool: 2 response lines");
        check(jsonContains(lines[1], "\"isError\":true"), "unknown tool: isError is true");
        check(jsonContains(lines[1], "Unknown tool"), "unknown tool: error message mentions unknown tool");
    }

    // -- Test 7: tools/call search_files without query --
    std::cout << "\n  --- Test 7: search_files missing query ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"search_files","arguments":{}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "missing query: 2 response lines");
        check(jsonContains(lines[1], "missing required parameter"), "missing query: error message mentions missing parameter");
    }

    // -- Test 8: Empty and whitespace lines are skipped --
    std::cout << "\n  --- Test 8: Empty lines skipped ---\n";
    {
        std::string input =
            "\n\n"
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n\n\n"
            R"({"jsonrpc":"2.0","id":2,"method":"ping"})"
            "\n\n";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "empty lines: still 2 responses");
        check(jsonContains(lines[0], "\"id\":1"), "empty lines: first response has id 1");
        check(jsonContains(lines[1], "\"id\":2"), "empty lines: second response has id 2");
    }

    // -- Test 9: Invalid JSON returns parse error --
    std::cout << "\n  --- Test 9: Invalid JSON ---\n";
    {
        std::string input = "this is not json at all";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 1, "invalid JSON: 1 response");
        check(jsonContains(lines[0], "-32700"), "invalid JSON: parse error code -32700");
    }

    // -- Test 10: JSON-looking text inside a value does not affect dispatch --
    std::cout << "\n  --- Test 10: Structured dispatch ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":"safe","method":"ping","params":{"note":"method: tools/list, id: 99"}})";
        auto lines = splitLines(mcpExec(input));
        check(lines.size() == 1, "structured dispatch: one response");
        check(jsonContains(lines[0], "\"id\":\"safe\""), "structured dispatch: string id preserved");
        check(jsonContains(lines[0], "\"result\":{}"), "structured dispatch: ping selected");
        check(!jsonContains(lines[0], "search_files"), "structured dispatch: value text ignored");
    }

    // -- Test 11: Invalid structured field types are rejected --
    std::cout << "\n  --- Test 11: Structured validation ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":21,"method":7})"
            "\n"
            R"({"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"search_files","arguments":[]}})";
        auto lines = splitLines(mcpExec(input));
        check(lines.size() == 2, "structured validation: two responses");
        check(jsonContains(lines[0], "-32600"), "non-string method rejected");
        check(jsonContains(lines[1], "-32602"), "non-object arguments rejected");
    }

    // -- Test 12: Mixed batch parses objects structurally --
    std::cout << "\n  --- Test 12: Mixed batch ---\n";
    {
        std::string input =
            R"([{"jsonrpc":"2.0","id":31,"method":"ping"},42,{"jsonrpc":"2.0","id":32,"method":"tools/list"}])";
        auto lines = splitLines(mcpExec(input));
        check(lines.size() == 1, "mixed batch: one JSON array response");
        check(!lines[0].empty() && lines[0].front() == '[' && lines[0].back() == ']',
              "mixed batch: response is an array");
        check(jsonContains(lines[0], "\"id\":31"), "mixed batch: first request");
        check(jsonContains(lines[0], "-32600"), "mixed batch: primitive rejected");
        check(jsonContains(lines[0], "\"id\":32"), "mixed batch: final request");
    }

    // -- Test 13: Explicit null arguments are treated as an empty object --
    std::cout << "\n  --- Test 13: Null arguments ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":41,"method":"tools/call","params":{"name":"index_status","arguments":null}})";
        auto lines = splitLines(mcpExec(input));
        check(lines.size() == 1, "null arguments: one response");
        check(!jsonContains(lines[0], "-32602"), "null arguments: accepted as empty object");
        check(jsonContains(lines[0], "\"id\":41"), "null arguments: id preserved");
    }

    // -- Test 14: A notification-only batch produces no response --
    std::cout << "\n  --- Test 14: Notification-only batch ---\n";
    {
        std::string input =
            R"([{"jsonrpc":"2.0","method":"notifications/initialized"},{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}])";
        auto lines = splitLines(mcpExec(input));
        check(lines.empty(), "notification-only batch: no response");
    }

    std::cout << "\n  Part 49 complete.\n\n";
}
