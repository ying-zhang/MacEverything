import Foundation

enum AppLogger {
    static func error(_ module: String, _ message: String) {
        _ = module
        _ = message
    }
}

private var failures = 0

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() {
        fputs("FAIL: \(message)\n", stderr)
        failures += 1
    }
}

@main
struct MCPConfigTests {
    static func main() throws {
        let home = FileManager.default.temporaryDirectory
            .appendingPathComponent("maceverything-mcp-config-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: home) }

        expect(MCPClient.allCases == [.codex, .claudeCode, .cursor, .claudeDesktop],
               "all supported clients are exposed")
        expect(MCPClient.codex.configFileURL(homeDirectory: home).path.hasSuffix("/.codex/config.toml"),
               "Codex config path")
        expect(MCPClient.claudeCode.configFileURL(homeDirectory: home).path.hasSuffix("/.claude.json"),
               "Claude Code uses current user-scoped config")
        expect(MCPClient.claudeCode.legacyConfigFileURL(homeDirectory: home)?.path
            .hasSuffix("/.claude/settings.json") == true,
               "Claude Code legacy config path is retained for migration")

        let binaryPath = "/Applications/MacEverything.app/Contents/MacOS/MacEverythingMCP"
        let cursorURL = MCPClient.cursor.configFileURL(homeDirectory: home)
        try MCPConfigManager.updateJSON(enabled: true,
                                        for: .cursor,
                                        at: cursorURL,
                                        binaryPath: binaryPath)
        let cursorRoot = try JSONSerialization.jsonObject(with: Data(contentsOf: cursorURL)) as! [String: Any]
        let cursorServers = cursorRoot["mcpServers"] as! [String: Any]
        let cursorEntry = cursorServers["maceverything"] as! [String: Any]
        expect(cursorEntry["type"] as? String == "stdio", "Cursor entry declares stdio transport")
        expect(cursorEntry["command"] as? String == binaryPath, "Cursor entry uses bundled binary")
        expect(MCPConfigManager.isEnabled(for: .cursor, homeDirectory: home), "Cursor enabled state")

        let desktopURL = MCPClient.claudeDesktop.configFileURL(homeDirectory: home)
        try MCPConfigManager.updateJSON(enabled: true,
                                        for: .claudeDesktop,
                                        at: desktopURL,
                                        binaryPath: binaryPath)
        let desktopRoot = try JSONSerialization.jsonObject(with: Data(contentsOf: desktopURL)) as! [String: Any]
        let desktopServers = desktopRoot["mcpServers"] as! [String: Any]
        let desktopEntry = desktopServers["maceverything"] as! [String: Any]
        expect(desktopEntry["type"] == nil, "Claude Desktop keeps command/args format")

        var invocations: [(String, [String])] = []
        let resolver: MCPConfigManager.ExecutableResolver = { name, _ in
            URL(fileURLWithPath: "/usr/bin/\(name)")
        }
        let runner: MCPConfigManager.CommandRunner = { executable, arguments in
            invocations.append((executable.lastPathComponent, arguments))
            return MCPCommandResult(exitCode: 0, output: "")
        }

        try MCPConfigManager.update(enabled: true,
                                    for: .codex,
                                    homeDirectory: home,
                                    binaryPath: binaryPath,
                                    executableResolver: resolver,
                                    commandRunner: runner)
        expect(invocations.last?.1 == ["mcp", "add", "maceverything", "--", binaryPath],
               "Codex uses official CLI add syntax")

        try MCPConfigManager.update(enabled: true,
                                    for: .claudeCode,
                                    homeDirectory: home,
                                    binaryPath: binaryPath,
                                    executableResolver: resolver,
                                    commandRunner: runner)
        expect(invocations.last?.1 == ["mcp", "add", "--transport", "stdio", "--scope", "user",
                                       "maceverything", "--", binaryPath],
               "Claude Code uses user-scoped stdio CLI syntax")

        let legacyClaudeURL = MCPClient.claudeCode.legacyConfigFileURL(homeDirectory: home)!
        try MCPConfigManager.updateJSON(enabled: true,
                                        for: .claudeCode,
                                        at: legacyClaudeURL,
                                        binaryPath: binaryPath)
        expect(MCPConfigManager.isEnabled(for: .claudeCode, homeDirectory: home),
               "Claude Code detects the legacy settings file")
        try MCPConfigManager.update(enabled: false,
                                    for: .claudeCode,
                                    homeDirectory: home,
                                    binaryPath: binaryPath,
                                    executableResolver: resolver,
                                    commandRunner: runner)
        expect(!MCPConfigManager.isEnabled(for: .claudeCode, homeDirectory: home),
               "Claude Code disable removes the legacy entry")

        expect(MCPConfigManager.codexServerIsEnabled(in: "[mcp_servers.maceverything]\ncommand = \"x\""),
               "Codex TOML section is detected")
        expect(MCPConfigManager.codexServerIsEnabled(in: "[mcp_servers.maceverything]\ncommand = \"x\"\n[features]\nfoo = true"),
               "Codex TOML section remains detected before later sections")
        expect(!MCPConfigManager.codexServerIsEnabled(in: "[mcp_servers.maceverything]\nenabled = false"),
               "disabled Codex TOML section is not enabled")
        expect(!MCPConfigManager.codexServerIsEnabled(
            in: "[mcp_servers.maceverything] # generated\nenabled\t=\tfalse # disabled"),
               "Codex TOML comments and tabs are handled")
        expect(!MCPConfigManager.codexServerIsEnabled(
            in: "[mcp_servers.maceverything]\r\nenabled = false\r\n"),
               "Codex TOML CRLF files are handled")

        try MCPConfigManager.updateJSON(enabled: false,
                                        for: .cursor,
                                        at: cursorURL,
                                        binaryPath: binaryPath)
        expect(!MCPConfigManager.isEnabled(for: .cursor, homeDirectory: home), "Cursor disable removes entry")

        let timeoutStart = Date()
        do {
            _ = try MCPConfigManager.runCommand(executable: URL(fileURLWithPath: "/bin/sleep"),
                                                arguments: ["5"],
                                                timeout: 0.05)
            expect(false, "hung MCP client command must time out")
        } catch MCPConfigManager.MCPError.commandTimedOut {
            expect(Date().timeIntervalSince(timeoutStart) < 2,
                   "hung MCP client command is terminated promptly")
        }

        if failures > 0 { exit(1) }
        print("MCP configuration tests passed")
    }
}
