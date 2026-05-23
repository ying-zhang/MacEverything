import SwiftUI
import AppKit

struct ContentResultRow: View {
    let item: ContentFileItem
    let hints: [HighlightHint]
    @ObservedObject private var settings = AppSettings.shared
    @State private var isHovered = false

    var body: some View {
        let dense = settings.resultDensity == .compact
        HStack(spacing: 8) {
            fileIcon(for: item.filePath)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: dense ? 20 : 24, height: dense ? 20 : 24)

            VStack(alignment: .leading, spacing: dense ? 1 : 3) {
                HStack(spacing: 6) {
                    Text(item.fileName)
                        .font(.title3)
                        .fontWeight(.medium)
                        .foregroundColor(.primary)
                        .lineLimit(1)

                    if settings.showPath {
                        Text(directoryPath(item.filePath))
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                }

                if settings.showContentSnippets {
                    highlightMatches(in: item.snippet, hints: hints, font: .caption, color: .secondary)
                        .lineLimit(dense ? 1 : 2)
                }
            }

            Spacer()
        }
        .padding(.vertical, dense ? 2 : 5)
        .padding(.horizontal, 6)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isHovered ? Color.accentColor.opacity(0.12) : Color.clear)
        )
        .contentShape(Rectangle())
        .onHover { hovering in
            isHovered = hovering
        }
        .contextMenu {
            Button(L10n.tr("Open")) { openFile() }
            Button(L10n.tr("Reveal in Finder")) { revealInFinder() }
            Button(L10n.tr("Quick Look")) { quickLook() }
            Divider()
            Button(L10n.tr("Copy File")) { copyFile() }
            Button(L10n.tr("Copy Filename")) { copyFilename() }
            Divider()
            Button(L10n.tr("Move to Trash")) { trashFile() }
            Button(L10n.tr("Open in Terminal")) { openInTerminal() }
        }
        .onDrag {
            return NSItemProvider(object: NSURL(fileURLWithPath: item.filePath))
        }
        .onTapGesture(count: 2) {
            if NSEvent.modifierFlags.contains(.command) {
                revealInFinder()
            } else {
                openFile()
            }
        }
        .onTapGesture(count: 1) {
            if NSEvent.modifierFlags.contains(.command) {
                revealInFinder()
            }
        }
        .accessibilityIdentifier("contentResultRow")
    }

    private func directoryPath(_ fullPath: String) -> String {
        if let range = fullPath.range(of: "/", options: .backwards) {
            return String(fullPath[fullPath.startIndex..<range.lowerBound])
        }
        return fullPath
    }

    private func fileIcon(for path: String) -> Image {
        Image(nsImage: FileIconCache.shared.icon(forPath: path))
    }

    private func openFile() {
        if !NSWorkspace.shared.open(URL(fileURLWithPath: item.filePath)) {
            NSSound.beep()
        }
    }

    private func revealInFinder() {
        NSWorkspace.shared.selectFile(item.filePath, inFileViewerRootedAtPath: "")
    }

    private func copyPath() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(item.filePath, forType: .string)
    }

    private func copyFile() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects([NSURL(fileURLWithPath: item.filePath)])
    }

    private func copyFilename() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(item.fileName, forType: .string)
    }

    private func trashFile() {
        do {
            try FileManager.default.trashItem(at: URL(fileURLWithPath: item.filePath), resultingItemURL: nil)
        } catch {
            NSSound.beep()
        }
    }

    private func openInTerminal() {
        let dirPath = (item.filePath as NSString).deletingLastPathComponent
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-a", "Terminal", dirPath]
        try? process.run()
    }

    private func quickLook() {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/qlmanage")
        process.arguments = ["-p", item.filePath]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try? process.run()
    }
}
