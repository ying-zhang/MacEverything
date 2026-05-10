import SwiftUI

struct PermissionView: View {
    @State private var hasFullDiskAccess: Bool = true

    var body: some View {
        Group {}.onAppear { checkAccess() }
            .task {
                while !hasFullDiskAccess {
                    try? await Task.sleep(for: .seconds(3))
                    checkAccess()
                }
            }

        if !hasFullDiskAccess {
            HStack {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.yellow)
                Text(L10n.tr("Full Disk Access is required to scan all files."))
                    .font(.caption)
                Spacer()
                Button(L10n.tr("Open Settings")) {
                    openPrivacySettings()
                }
                .font(.caption)
                .buttonStyle(.bordered)
            }
            .padding(8)
            .background(Color.yellow.opacity(0.15))
        }
    }

    private func checkAccess() {
        // Test read access to a TCC-protected directory
        let testPath = NSHomeDirectory() + "/Library/Safari"
        hasFullDiskAccess = FileManager.default.isReadableFile(atPath: testPath)
    }

    private func openPrivacySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles") {
            NSWorkspace.shared.open(url)
        }
    }
}
