import Foundation

private var failures = 0

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() {
        fputs("FAIL: \(message)\n", stderr)
        failures += 1
    }
}

@main
struct CLIInstallTests {
    static func main() throws {
        let fileManager = FileManager.default
        let root = fileManager.temporaryDirectory
            .appendingPathComponent("mace-cli-install-\(UUID().uuidString)")
        let home = root.appendingPathComponent("home")
        let app = root.appendingPathComponent("MacEverything.app/Contents/MacOS")
        let binary = app.appendingPathComponent("mace")
        try fileManager.createDirectory(at: home, withIntermediateDirectories: true)
        try fileManager.createDirectory(at: app, withIntermediateDirectories: true)
        _ = fileManager.createFile(atPath: binary.path, contents: Data("mace".utf8),
                                   attributes: [.posixPermissions: 0o755])
        defer { try? fileManager.removeItem(at: root) }

        expect(CLIInstallManager.status(homeDirectory: home, binaryPath: binary.path) == .notInstalled,
               "initial status")
        try CLIInstallManager.install(homeDirectory: home, binaryPath: binary.path)
        expect(CLIInstallManager.status(homeDirectory: home, binaryPath: binary.path) == .installed,
               "installed status")
        let installPath = CLIInstallManager.installURL(homeDirectory: home).path
        let linkTarget = try fileManager.destinationOfSymbolicLink(atPath: installPath)
        expect(linkTarget == binary.path,
               "link points to bundled binary")

        let appAlias = root.appendingPathComponent("MacEverythingAlias.app")
        try fileManager.createSymbolicLink(at: appAlias,
                                           withDestinationURL: root.appendingPathComponent("MacEverything.app"))
        let aliasedBinary = appAlias.appendingPathComponent("Contents/MacOS/mace")
        expect(CLIInstallManager.status(homeDirectory: home,
                                        binaryPath: aliasedBinary.path) == .installed,
               "symlinked app ancestors resolve to the installed binary")

        try CLIInstallManager.uninstall(homeDirectory: home, binaryPath: binary.path)
        expect(CLIInstallManager.status(homeDirectory: home, binaryPath: binary.path) == .notInstalled,
               "uninstalled status")

        let renamedApp = root.appendingPathComponent("Renamed Search.app/Contents/MacOS")
        let renamedBinary = renamedApp.appendingPathComponent("mace")
        try fileManager.createDirectory(at: renamedApp, withIntermediateDirectories: true)
        _ = fileManager.createFile(atPath: renamedBinary.path, contents: Data("old mace".utf8),
                                   attributes: [.posixPermissions: 0o755])
        try fileManager.createDirectory(at: CLIInstallManager.installURL(homeDirectory: home)
            .deletingLastPathComponent(), withIntermediateDirectories: true)
        try fileManager.createSymbolicLink(atPath: CLIInstallManager.installURL(homeDirectory: home).path,
                                           withDestinationPath: renamedBinary.path)
        expect(CLIInstallManager.status(homeDirectory: home, binaryPath: binary.path) == .stale,
               "renamed app bundle CLI is recognized as stale")
        try CLIInstallManager.uninstall(homeDirectory: home, binaryPath: binary.path)

        try fileManager.createDirectory(at: CLIInstallManager.installURL(homeDirectory: home)
            .deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("user file".utf8).write(to: CLIInstallManager.installURL(homeDirectory: home))
        expect(CLIInstallManager.status(homeDirectory: home, binaryPath: binary.path) == .conflict,
               "regular file is a conflict")
        do {
            try CLIInstallManager.install(homeDirectory: home, binaryPath: binary.path)
            expect(false, "conflict must not be overwritten")
        } catch {}
        let conflictContents = try String(contentsOf: CLIInstallManager.installURL(homeDirectory: home),
                                          encoding: .utf8)
        expect(conflictContents == "user file",
               "conflicting file remains intact")

        expect(CLIInstallManager.pathIsConfigured(homeDirectory: home,
                                                   environmentPath: "/usr/bin:\(home.path)/.local/bin"),
               "PATH detection")
        expect(!CLIInstallManager.pathIsConfigured(homeDirectory: home,
                                                    environmentPath: "/usr/bin:/usr/local/bin"),
               "missing PATH detection")

        if failures > 0 { exit(1) }
        print("CLI install tests passed")
    }
}
