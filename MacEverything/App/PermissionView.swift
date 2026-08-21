import SwiftUI

struct PermissionView: View {
    @AppStorage("settings.indexingPermissionMode") private var indexingMode = ""
    @State private var hasFullDiskAccess = false
    @State private var isCheckingAccess = false

    var body: some View {
        if shouldShowBanner {
            Group {
            if indexingMode.isEmpty {
                HStack(spacing: 10) {
                    Image(systemName: "folder.badge.questionmark")
                        .foregroundColor(.accentColor)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(L10n.tr("Choose how MacEverything should start indexing."))
                            .font(.callout.weight(.medium))
                        Text(L10n.tr("Quick Start indexes your selected folders without requiring Full Disk Access."))
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Button(L10n.tr("Full Disk Search")) {
                        indexingMode = "full"
                        openPrivacySettings()
                    }
                    .buttonStyle(.bordered)
                    Button(L10n.tr("Quick Start")) {
                        indexingMode = "quick"
                    }
                    .buttonStyle(.borderedProminent)
                }
            } else if indexingMode.hasPrefix("full") && !hasFullDiskAccess {
                HStack(spacing: 10) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.yellow)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(L10n.tr("Full Disk Search requires Full Disk Access."))
                            .font(.callout.weight(.medium))
                        Text(L10n.tr("Grant access in System Settings, reopen MacEverything if needed, then rebuild the index."))
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Button(L10n.tr("Use Quick Start")) { indexingMode = "quick" }
                        .buttonStyle(.bordered)
                    Button(L10n.tr("Open Settings")) { openPrivacySettings() }
                        .buttonStyle(.borderedProminent)
                }
            } else if indexingMode == "full" && hasFullDiskAccess {
                HStack(spacing: 10) {
                    Image(systemName: "checkmark.shield.fill")
                        .foregroundColor(.green)
                    Text(L10n.tr("Full Disk Access detected. Rebuild once to index the whole disk."))
                        .font(.callout)
                    Spacer()
                    Button(L10n.tr("Rebuild Full Disk Index")) { enableFullDiskIndex() }
                        .buttonStyle(.borderedProminent)
                }
            }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, indexingMode.isEmpty ? 10 : 8)
            .background(Color(nsColor: .controlBackgroundColor))
            .onAppear { checkAccess() }
            .task(id: indexingMode) {
                while indexingMode.hasPrefix("full") && !hasFullDiskAccess {
                    try? await Task.sleep(for: .seconds(3))
                    checkAccess()
                }
            }
        }
    }

    private var shouldShowBanner: Bool {
        indexingMode.isEmpty ||
        (indexingMode.hasPrefix("full") && !hasFullDiskAccess) ||
        (indexingMode == "full" && hasFullDiskAccess)
    }

    private func checkAccess() {
        guard !isCheckingAccess else { return }
        isCheckingAccess = true
        Task {
            hasFullDiskAccess = await Task.detached(priority: .utility) {
                Self.detectFullDiskAccess()
            }.value
            isCheckingAccess = false
        }
    }

    nonisolated private static func detectFullDiskAccess() -> Bool {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let protectedFiles = ["Library/Safari/History.db", "Library/Messages/chat.db"]
            .map { home.appendingPathComponent($0) }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
        if !protectedFiles.isEmpty {
            return protectedFiles.contains { url in
                guard let handle = try? FileHandle(forReadingFrom: url) else { return false }
                try? handle.close()
                return true
            }
        }

        let protectedDirectories = ["Library/Safari", "Library/Mail", "Library/Messages"]
            .map { FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent($0) }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
        return protectedDirectories.contains { url in
            (try? FileManager.default.contentsOfDirectory(at: url,
                                                          includingPropertiesForKeys: nil)) != nil
        }
    }

    private func enableFullDiskIndex() {
        let settings = AppSettings.shared
        settings.indexRoots = ["/"]
        settings.contentSearchUsesIndexRoots = true
        SearchServiceModel.shared.applyRuntimeConfiguration()
        indexingMode = "fullConfigured"
        NotificationCenter.default.post(name: .rebuildIndex, object: nil)
    }

    private func openPrivacySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles") {
            NSWorkspace.shared.open(url)
        }
    }
}
