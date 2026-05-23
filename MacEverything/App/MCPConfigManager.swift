import Foundation

enum MCPClient: String, CaseIterable {
    case claudeCode
    case cursor
    case claudeDesktop

    var displayName: String {
        switch self {
        case .claudeCode: return "Claude Code"
        case .cursor: return "Cursor"
        case .claudeDesktop: return "Claude Desktop"
        }
    }

    var configFileURL: URL {
        let home = FileManager.default.homeDirectoryForCurrentUser
        switch self {
        case .claudeCode:
            return home.appendingPathComponent(".claude/settings.json")
        case .cursor:
            return home.appendingPathComponent(".cursor/mcp.json")
        case .claudeDesktop:
            return home
                .appendingPathComponent("Library/Application Support/Claude/claude_desktop_config.json")
        }
    }
}

struct MCPConfigManager {
    static var mcpBinaryPath: String? {
        guard let execURL = Bundle.main.executableURL else { return nil }
        return execURL.deletingLastPathComponent()
            .appendingPathComponent("MacEverythingMCP").path
    }

    static func isEnabled(for client: MCPClient) -> Bool {
        guard let root = try? readJSON(at: client.configFileURL) else { return false }
        guard let servers = root["mcpServers"] as? [String: Any] else { return false }
        return servers["maceverything"] != nil
    }

    static func setEnabled(_ enabled: Bool, for client: MCPClient) {
        do {
            if enabled {
                try enable(for: client)
            } else {
                try disable(for: client)
            }
        } catch {
            AppLogger.error("MCP", "Failed to update \(client.displayName) config: \(error)")
        }
    }

    // MARK: - Private

    private static func enable(for client: MCPClient) throws {
        guard let binaryPath = mcpBinaryPath else {
            throw MCPError.binaryNotFound
        }
        let url = client.configFileURL
        var root = (try? readJSON(at: url)) ?? [:]
        var servers = root["mcpServers"] as? [String: Any] ?? [:]
        servers["maceverything"] = [
            "command": binaryPath,
            "args": [String]()
        ] as [String: Any]
        root["mcpServers"] = servers
        try writeJSON(root, to: url)
    }

    private static func disable(for client: MCPClient) throws {
        let url = client.configFileURL
        var root = try readJSON(at: url)
        guard var servers = root["mcpServers"] as? [String: Any] else { return }
        servers.removeValue(forKey: "maceverything")
        if servers.isEmpty {
            root.removeValue(forKey: "mcpServers")
        } else {
            root["mcpServers"] = servers
        }
        try writeJSON(root, to: url)
    }

    private static func readJSON(at url: URL) throws -> [String: Any] {
        let data = try Data(contentsOf: url)
        guard let dict = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw MCPError.invalidJSON
        }
        return dict
    }

    private static func writeJSON(_ dict: [String: Any], to url: URL) throws {
        let dir = url.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let data = try JSONSerialization.data(
            withJSONObject: dict,
            options: [.prettyPrinted, .sortedKeys]
        )
        try data.write(to: url, options: .atomic)
    }

    private enum MCPError: LocalizedError {
        case binaryNotFound
        case invalidJSON

        var errorDescription: String? {
            switch self {
            case .binaryNotFound: return "MCP binary not found in app bundle"
            case .invalidJSON: return "Config file contains invalid JSON"
            }
        }
    }
}
