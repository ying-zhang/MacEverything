import Foundation

enum CLIInstallStatus: Equatable {
    case notInstalled
    case installed
    case stale
    case conflict
}

struct CLIInstallManager {
    static var bundledBinaryPath: String? {
        guard let executableURL = Bundle.main.executableURL else { return nil }
        return executableURL.deletingLastPathComponent().appendingPathComponent("mace").path
    }

    static func installURL(homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser) -> URL {
        homeDirectory.appendingPathComponent(".local/bin/mace")
    }

    static func status(homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser,
                       binaryPath: String? = bundledBinaryPath,
                       fileManager: FileManager = .default) -> CLIInstallStatus {
        let destination = installURL(homeDirectory: homeDirectory)
        guard let linkTarget = try? fileManager.destinationOfSymbolicLink(atPath: destination.path) else {
            return fileManager.fileExists(atPath: destination.path) ? .conflict : .notInstalled
        }
        let resolvedTarget = resolve(linkTarget: linkTarget, relativeTo: destination)
        if let binaryPath,
           URL(fileURLWithPath: resolvedTarget).resolvingSymlinksInPath().standardizedFileURL.path ==
            URL(fileURLWithPath: binaryPath).resolvingSymlinksInPath().standardizedFileURL.path {
            return .installed
        }
        return isMacEverythingCLIPath(resolvedTarget) ? .stale : .conflict
    }

    static func install(homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser,
                        binaryPath: String? = bundledBinaryPath,
                        fileManager: FileManager = .default) throws {
        guard let binaryPath, fileManager.isExecutableFile(atPath: binaryPath) else {
            throw CLIInstallError.binaryNotFound
        }
        let destination = installURL(homeDirectory: homeDirectory)
        let currentStatus = status(homeDirectory: homeDirectory,
                                   binaryPath: binaryPath,
                                   fileManager: fileManager)
        guard currentStatus != .conflict else { throw CLIInstallError.pathConflict(destination.path) }
        if currentStatus == .installed { return }

        try fileManager.createDirectory(at: destination.deletingLastPathComponent(),
                                        withIntermediateDirectories: true)
        if currentStatus == .stale {
            try fileManager.removeItem(at: destination)
        }
        try fileManager.createSymbolicLink(atPath: destination.path,
                                           withDestinationPath: binaryPath)
    }

    static func uninstall(homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser,
                          binaryPath: String? = bundledBinaryPath,
                          fileManager: FileManager = .default) throws {
        let currentStatus = status(homeDirectory: homeDirectory,
                                   binaryPath: binaryPath,
                                   fileManager: fileManager)
        let destination = installURL(homeDirectory: homeDirectory)
        switch currentStatus {
        case .installed, .stale:
            try fileManager.removeItem(at: destination)
        case .notInstalled:
            return
        case .conflict:
            throw CLIInstallError.pathConflict(destination.path)
        }
    }

    static func pathIsConfigured(homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser,
                                 environmentPath: String? = ProcessInfo.processInfo.environment["PATH"]) -> Bool {
        let binPath = homeDirectory.appendingPathComponent(".local/bin").standardizedFileURL.path
        return environmentPath?.split(separator: ":").contains {
            URL(fileURLWithPath: String($0)).standardizedFileURL.path == binPath
        } ?? false
    }

    private static func resolve(linkTarget: String, relativeTo linkURL: URL) -> String {
        if linkTarget.hasPrefix("/") { return linkTarget }
        return linkURL.deletingLastPathComponent().appendingPathComponent(linkTarget).standardizedFileURL.path
    }

    private static func isMacEverythingCLIPath(_ path: String) -> Bool {
        let binary = URL(fileURLWithPath: path).standardizedFileURL
        let macOSDirectory = binary.deletingLastPathComponent()
        let contentsDirectory = macOSDirectory.deletingLastPathComponent()
        let appBundle = contentsDirectory.deletingLastPathComponent()
        return binary.lastPathComponent == "mace" &&
            macOSDirectory.lastPathComponent == "MacOS" &&
            contentsDirectory.lastPathComponent == "Contents" &&
            appBundle.pathExtension.lowercased() == "app"
    }

    enum CLIInstallError: LocalizedError {
        case binaryNotFound
        case pathConflict(String)

        var errorDescription: String? {
            switch self {
            case .binaryNotFound:
                return NSLocalizedString("Bundled mace executable was not found", comment: "")
            case .pathConflict(let path):
                return String(format: NSLocalizedString("Another file already exists at %@", comment: ""), path)
            }
        }
    }
}
