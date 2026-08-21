import Foundation
import Combine
import Darwin

enum MCPClient: String, CaseIterable, Sendable {
    case codex
    case claudeCode
    case cursor
    case claudeDesktop

    var displayName: String {
        switch self {
        case .codex: return "Codex"
        case .claudeCode: return "Claude Code"
        case .cursor: return "Cursor"
        case .claudeDesktop: return "Claude Desktop"
        }
    }

    var configFileURL: URL {
        configFileURL(homeDirectory: FileManager.default.homeDirectoryForCurrentUser)
    }

    func configFileURL(homeDirectory: URL) -> URL {
        switch self {
        case .codex:
            return homeDirectory.appendingPathComponent(".codex/config.toml")
        case .claudeCode:
            return homeDirectory.appendingPathComponent(".claude.json")
        case .cursor:
            return homeDirectory.appendingPathComponent(".cursor/mcp.json")
        case .claudeDesktop:
            return homeDirectory
                .appendingPathComponent("Library/Application Support/Claude/claude_desktop_config.json")
        }
    }

    func legacyConfigFileURL(homeDirectory: URL) -> URL? {
        guard self == .claudeCode else { return nil }
        return homeDirectory.appendingPathComponent(".claude/settings.json")
    }

    var cliName: String? {
        switch self {
        case .codex: return "codex"
        case .claudeCode: return "claude"
        case .cursor, .claudeDesktop: return nil
        }
    }

    func enableArguments(binaryPath: String) -> [String]? {
        switch self {
        case .codex:
            return ["mcp", "add", "maceverything", "--", binaryPath]
        case .claudeCode:
            return ["mcp", "add", "--transport", "stdio", "--scope", "user",
                    "maceverything", "--", binaryPath]
        case .cursor, .claudeDesktop:
            return nil
        }
    }

    var disableArguments: [String]? {
        switch self {
        case .codex:
            return ["mcp", "remove", "maceverything"]
        case .claudeCode:
            return ["mcp", "remove", "--scope", "user", "maceverything"]
        case .cursor, .claudeDesktop:
            return nil
        }
    }
}

struct MCPCommandResult {
    let exitCode: Int32
    let output: String
}

struct MCPConfigManager {
    typealias ExecutableResolver = (_ name: String, _ homeDirectory: URL) -> URL?
    typealias CommandRunner = (_ executable: URL, _ arguments: [String]) throws -> MCPCommandResult

    static var mcpBinaryPath: String? {
        guard let execURL = Bundle.main.executableURL else { return nil }
        return execURL.deletingLastPathComponent()
            .appendingPathComponent("MacEverythingMCP").path
    }

    static func isEnabled(for client: MCPClient) -> Bool {
        isEnabled(for: client, homeDirectory: FileManager.default.homeDirectoryForCurrentUser)
    }

    static func isEnabled(for client: MCPClient, homeDirectory: URL) -> Bool {
        let url = client.configFileURL(homeDirectory: homeDirectory)
        switch client {
        case .codex:
            guard let text = try? String(contentsOf: url, encoding: .utf8) else { return false }
            return codexServerIsEnabled(in: text)
        case .claudeCode, .cursor, .claudeDesktop:
            let urls = [url, client.legacyConfigFileURL(homeDirectory: homeDirectory)].compactMap { $0 }
            return urls.contains { configURL in
                guard let root = try? readJSON(at: configURL),
                      let servers = root["mcpServers"] as? [String: Any] else { return false }
                return servers["maceverything"] != nil
            }
        }
    }

    static func setEnabled(_ enabled: Bool, for client: MCPClient) throws {
        guard let binaryPath = mcpBinaryPath else {
            throw MCPError.binaryNotFound
        }
        try update(enabled: enabled,
                   for: client,
                   homeDirectory: FileManager.default.homeDirectoryForCurrentUser,
                   binaryPath: binaryPath,
                   executableResolver: resolveExecutable,
                   commandRunner: { executable, arguments in
                       try runCommand(executable: executable, arguments: arguments)
                   })
    }

    static func update(enabled: Bool,
                       for client: MCPClient,
                       homeDirectory: URL,
                       binaryPath: String,
                       executableResolver: ExecutableResolver,
                       commandRunner: CommandRunner) throws {
        if let cliName = client.cliName {
            guard let executable = executableResolver(cliName, homeDirectory) else {
                throw MCPError.clientCLINotFound(cliName)
            }
            let arguments = enabled ? client.enableArguments(binaryPath: binaryPath) : client.disableArguments
            guard let arguments else { throw MCPError.invalidClientConfiguration }
            let legacyURL = client.legacyConfigFileURL(homeDirectory: homeDirectory)
            let legacyWasEnabled = legacyURL.map { serverIsEnabled(inJSONAt: $0) } ?? false
            let currentConfigIsEnabled = serverIsEnabled(
                inJSONAt: client.configFileURL(homeDirectory: homeDirectory))
            let result = try commandRunner(executable, arguments)
            guard result.exitCode == 0 || (!enabled && legacyWasEnabled && !currentConfigIsEnabled) else {
                throw MCPError.commandFailed(result.output)
            }
            if !enabled, let legacyURL, legacyWasEnabled {
                try updateJSON(enabled: false, for: client, at: legacyURL, binaryPath: binaryPath)
            }
            return
        }

        try updateJSON(enabled: enabled,
                       for: client,
                       at: client.configFileURL(homeDirectory: homeDirectory),
                       binaryPath: binaryPath)
    }

    static func updateJSON(enabled: Bool,
                           for client: MCPClient,
                           at url: URL,
                           binaryPath: String) throws {
        let fileManager = FileManager.default
        var root = fileManager.fileExists(atPath: url.path) ? try readJSON(at: url) : [:]
        var servers = root["mcpServers"] as? [String: Any] ?? [:]

        if enabled {
            var entry: [String: Any] = [
                "command": binaryPath,
                "args": [String]()
            ]
            if client == .cursor {
                entry["type"] = "stdio"
            }
            servers["maceverything"] = entry
        } else {
            servers.removeValue(forKey: "maceverything")
        }

        if servers.isEmpty {
            root.removeValue(forKey: "mcpServers")
        } else {
            root["mcpServers"] = servers
        }
        try writeJSON(root, to: url)
    }

    static func codexServerIsEnabled(in text: String) -> Bool {
        let lines = text.split(separator: "\n", omittingEmptySubsequences: false)
        var inServerSection = false
        var foundServerSection = false

        for rawLine in lines {
            let line = rawLine.split(separator: "#", maxSplits: 1,
                                     omittingEmptySubsequences: false)[0]
                .trimmingCharacters(in: .whitespacesAndNewlines)
            if line.hasPrefix("[") {
                inServerSection = line == "[mcp_servers.maceverything]" ||
                    line == "[mcp_servers.\"maceverything\"]"
                foundServerSection = foundServerSection || inServerSection
                continue
            }
            if inServerSection,
               line.filter({ !$0.isWhitespace }) == "enabled=false" {
                return false
            }
        }
        return foundServerSection
    }

    private static func resolveExecutable(named name: String, homeDirectory: URL) -> URL? {
        let fileManager = FileManager.default
        var candidates: [URL] = []

        if let path = ProcessInfo.processInfo.environment["PATH"] {
            candidates.append(contentsOf: path.split(separator: ":").map {
                URL(fileURLWithPath: String($0)).appendingPathComponent(name)
            })
        }

        candidates.append(homeDirectory.appendingPathComponent(".local/bin/\(name)"))
        candidates.append(URL(fileURLWithPath: "/usr/local/bin/\(name)"))
        candidates.append(URL(fileURLWithPath: "/opt/homebrew/bin/\(name)"))

        if name == "codex" {
            candidates.append(URL(fileURLWithPath: "/Applications/ChatGPT.app/Contents/Resources/codex"))
            candidates.append(URL(fileURLWithPath: "/Applications/Codex.app/Contents/Resources/codex"))
        }

        let nodeVersions = homeDirectory.appendingPathComponent(".nvm/versions/node")
        if let versions = try? fileManager.contentsOfDirectory(at: nodeVersions,
                                                                includingPropertiesForKeys: nil) {
            candidates.append(contentsOf: versions.sorted {
                $0.lastPathComponent.compare($1.lastPathComponent, options: .numeric) == .orderedDescending
            }.map {
                $0.appendingPathComponent("bin/\(name)")
            })
        }

        return candidates.first { fileManager.isExecutableFile(atPath: $0.path) }
    }

    static func runCommand(executable: URL,
                           arguments: [String],
                           timeout: TimeInterval = 30) throws -> MCPCommandResult {
        let process = Process()
        let outputURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("mace-mcp-command-\(UUID().uuidString).log")
        FileManager.default.createFile(atPath: outputURL.path, contents: nil)
        defer { try? FileManager.default.removeItem(at: outputURL) }
        let outputHandle = try FileHandle(forWritingTo: outputURL)
        defer { try? outputHandle.close() }
        process.executableURL = executable
        process.arguments = arguments
        process.standardInput = FileHandle.nullDevice
        process.standardOutput = outputHandle
        process.standardError = outputHandle
        try process.run()

        let deadline = Date().addingTimeInterval(timeout)
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.02)
        }
        if process.isRunning {
            process.terminate()
            let terminationDeadline = Date().addingTimeInterval(1)
            while process.isRunning && Date() < terminationDeadline {
                Thread.sleep(forTimeInterval: 0.02)
            }
            if process.isRunning {
                Darwin.kill(process.processIdentifier, SIGKILL)
            }
            process.waitUntilExit()
            throw MCPError.commandTimedOut
        }
        process.waitUntilExit()
        try outputHandle.synchronize()
        let data = try Data(contentsOf: outputURL)
        let output = String(data: data, encoding: .utf8) ?? ""
        return MCPCommandResult(exitCode: process.terminationStatus, output: output)
    }

    private static func readJSON(at url: URL) throws -> [String: Any] {
        let data = try Data(contentsOf: url)
        guard let dict = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw MCPError.invalidJSON
        }
        return dict
    }

    private static func serverIsEnabled(inJSONAt url: URL) -> Bool {
        guard let root = try? readJSON(at: url),
              let servers = root["mcpServers"] as? [String: Any] else { return false }
        return servers["maceverything"] != nil
    }

    private static func writeJSON(_ dict: [String: Any], to url: URL) throws {
        let dir = url.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let existingMode = (try? FileManager.default.attributesOfItem(atPath: url.path)[.posixPermissions])
            as? NSNumber
        let data = try JSONSerialization.data(
            withJSONObject: dict,
            options: [.prettyPrinted, .sortedKeys]
        )
        try data.write(to: url, options: .atomic)
        try FileManager.default.setAttributes(
            [.posixPermissions: existingMode ?? NSNumber(value: 0o600)],
            ofItemAtPath: url.path
        )
    }

    enum MCPError: LocalizedError {
        case binaryNotFound
        case clientCLINotFound(String)
        case commandFailed(String)
        case commandTimedOut
        case invalidClientConfiguration
        case invalidJSON

        var errorDescription: String? {
            switch self {
            case .binaryNotFound:
                return NSLocalizedString("MCP binary not found in app bundle", comment: "")
            case .clientCLINotFound(let name):
                return String(format: NSLocalizedString("%@ CLI not found", comment: ""), name)
            case .commandFailed(let output):
                return output.isEmpty
                    ? NSLocalizedString("MCP client command failed", comment: "")
                    : output
            case .commandTimedOut:
                return NSLocalizedString("MCP client command timed out", comment: "")
            case .invalidClientConfiguration:
                return NSLocalizedString("Invalid MCP client configuration", comment: "")
            case .invalidJSON:
                return NSLocalizedString("Config file contains invalid JSON", comment: "")
            }
        }
    }
}

@MainActor
final class MCPIntegrationModel: ObservableObject {
    static let shared = MCPIntegrationModel()

    @Published private(set) var enabled: [MCPClient: Bool] = [:]
    @Published private(set) var updating: Set<MCPClient> = []
    @Published private(set) var errors: [MCPClient: String] = [:]

    private init() {
        refresh()
    }

    func refresh() {
        for client in MCPClient.allCases where !updating.contains(client) {
            enabled[client] = MCPConfigManager.isEnabled(for: client)
        }
    }

    func isEnabled(_ client: MCPClient) -> Bool {
        enabled[client] ?? false
    }

    func setEnabled(_ desired: Bool, for client: MCPClient) {
        guard !updating.contains(client) else { return }
        let previous = isEnabled(client)
        enabled[client] = desired
        updating.insert(client)
        errors[client] = nil

        Task { @MainActor in
            let errorMessage = await Task.detached(priority: .userInitiated) {
                do {
                    try MCPConfigManager.setEnabled(desired, for: client)
                    return nil as String?
                } catch {
                    return error.localizedDescription
                }
            }.value

            updating.remove(client)
            if let errorMessage {
                enabled[client] = previous
                errors[client] = errorMessage
                AppLogger.error("MCP", "Failed to update \(client.displayName) config: \(errorMessage)")
            } else {
                enabled[client] = MCPConfigManager.isEnabled(for: client)
            }
        }
    }
}
