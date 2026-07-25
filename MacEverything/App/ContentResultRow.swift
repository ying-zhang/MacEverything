import SwiftUI
import AppKit

struct ContentResultRow: View {
    let item: ContentFileItem
    let hints: [HighlightHint]
    var onDelete: (() -> Void)?
    @ObservedObject private var settings = AppSettings.shared
    @ObservedObject private var iconCache = FileIconCache.shared
    @EnvironmentObject private var theme: ThemeManager
    @State private var isHovered = false

    var body: some View {
        let rowHeight = ResultRowMetrics.effectiveHeight(
            configuredHeight: settings.resultRowHeight,
            fontLineHeight: theme.bodyNSFont.ascender - theme.bodyNSFont.descender + theme.bodyNSFont.leading
        )
        let iconSize = min(rowHeight - 8, 32)
        HStack(spacing: 8) {
            fileIcon(for: item.filePath)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: iconSize, height: iconSize)

            VStack(alignment: .leading, spacing: rowHeight < 40 ? 1 : 3) {
                HStack(spacing: 6) {
                    Text(item.fileName)
                        .font(theme.bodyFont)
                        .fontWeight(.medium)
                        .foregroundColor(theme.resolvedTextColor)
                        .lineLimit(1)
                        .help(item.fileName)

                    if settings.showPath {
                        Text(directoryPath(item.filePath))
                            .font(theme.bodyFont)
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                            .help(directoryPath(item.filePath))
                    }
                }

                if settings.showContentSnippets && rowHeight >= 40 {
                    highlightMatches(in: item.snippet, hints: hints, font: .caption, color: .secondary)
                        .lineLimit(rowHeight < 60 ? 1 : 2)
                }
            }

            Spacer()
        }
        .frame(height: rowHeight)
        .clipped()
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
            Button(L10n.tr("Open in Terminal")) { openInTerminal() }
            Button(role: .destructive) { trashFile() } label: {
                Text(L10n.tr("Move to Trash"))
            }
        }
        .onDrag {
            return NSItemProvider(object: NSURL(fileURLWithPath: item.filePath))
        }
        .background(
            ResultClickMonitor(enabled: true) { clickCount, modifiers in
                handleClick(clickCount: clickCount, modifiers: modifiers)
            }
        )
        .accessibilityIdentifier("contentResultRow")
    }

    private func directoryPath(_ fullPath: String) -> String {
        if let range = fullPath.range(of: "/", options: .backwards) {
            return String(fullPath[fullPath.startIndex..<range.lowerBound])
        }
        return fullPath
    }

    private func fileIcon(for path: String) -> Image {
        _ = iconCache.revision
        return Image(nsImage: iconCache.icon(forPath: path))
    }

    private func openFile() {
        FileActions.open(
            URL(fileURLWithPath: item.filePath),
            isApplication: item.filePath.lowercased().hasSuffix(".app")
        )
    }

    private func revealInFinder() {
        NSWorkspace.shared.selectFile(item.filePath, inFileViewerRootedAtPath: "")
    }

    private func openInFinder() {
        FileActions.open(URL(fileURLWithPath: directoryPath(item.filePath)))
    }

    private func handleClick(clickCount: Int, modifiers: NSEvent.ModifierFlags) {
        for action in ResultClickResolver.actions(
            clickCount: clickCount,
            modifiers: modifiers,
            supportsSelection: false
        ) {
            switch action {
            case .open:
                openFile()
            case .reveal:
                revealInFinder()
            case .selectExclusive, .selectRange, .toggleSelection:
                break
            }
        }
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
            onDelete?()
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
        QuickLookPreviewController.shared.open(urls: [URL(fileURLWithPath: item.filePath)])
    }
}
