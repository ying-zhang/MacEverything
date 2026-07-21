import SwiftUI
import AppKit
import Quartz
import UniformTypeIdentifiers

/// Finder-like icon-view cell for file search results. Shows a large thumbnail
/// (always on, regardless of the list-mode thumbnail toggle) above the filename.
/// Mirrors ResultRow's selection / click / context-menu / drag behavior, but
/// uses a stacked layout instead of the multi-column row.
struct ResultGridCell: View {
    let item: FileItem
    let hints: [HighlightHint]
    let isSelected: Bool
    let selectedItems: [FileItem]
    let onSelect: (_ extending: Bool, _ toggling: Bool) -> Void
    var onDeleteItems: ((_ ids: [String]) -> Void)?

    @ObservedObject private var settings = AppSettings.shared
    @ObservedObject private var iconCache = FileIconCache.shared
    @ObservedObject private var thumbnailService = ThumbnailService.shared
    @EnvironmentObject private var theme: ThemeManager
    @State private var isHovered = false

    var body: some View {
        let iconSize = settings.gridIconSize
        VStack(spacing: 6) {
            gridVisual(pixelSize: iconSize)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: iconSize, height: iconSize)

            highlightMatches(in: item.name, hints: hints,
                             font: theme.bodyFont, color: theme.resolvedTextColor)
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
                .help(item.name)
        }
        .frame(width: iconSize + 16, height: iconSize + 48)
        .padding(4)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(rowBackground)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .strokeBorder(Color.accentColor.opacity(isSelected ? 0.8 : 0), lineWidth: 2)
        )
        .contentShape(Rectangle())
        .onHover { hovering in
            isHovered = hovering
        }
        .contextMenu {
            let actionItems = contextActionItems
            Button(L10n.tr("Open")) { openFile(item) }
            Button(L10n.tr("Reveal in Finder")) { revealInFinder(item) }
            if item.type == 2 {
                Button(L10n.tr("Open Folder")) { openInFinder(item) }
            }
            Button(L10n.tr("Quick Look")) { quickLook(actionItems) }
            Divider()
            Button(L10n.tr("Copy Path")) { copyFiles(actionItems) }
            Button(L10n.tr("Copy Filename")) { copyFilenames(actionItems) }
            Divider()
            Button(L10n.tr("Move to Trash")) { trashFiles(actionItems) }
            Button(L10n.tr("Open in Terminal")) { openInTerminal(item) }
        }
        .onDrag {
            return NSItemProvider(object: NSURL(fileURLWithPath: item.fullPath))
        }
        .background(
            ResultClickMonitor(enabled: true) { clickCount, modifiers in
                handleClick(clickCount: clickCount, modifiers: modifiers)
            }
        )
        .accessibilityIdentifier("resultGridCell")
    }

    private func gridVisual(pixelSize: CGFloat) -> Image {
        // Subscribe to cache revisions so async thumbnails trigger a re-render.
        _ = iconCache.revision
        // Grid mode always shows thumbnails for browsable files; the list-mode
        // `showThumbnails` toggle does not apply here.
        if item.type != 2 && !item.name.hasSuffix(".app") {
            _ = thumbnailService.revision
            if let thumb = thumbnailService.thumbnail(for: item.fullPath,
                                                       modTime: item.modTime,
                                                       pixelSize: pixelSize) {
                return Image(nsImage: thumb)
            }
        }
        return Image(nsImage: iconCache.icon(for: item))
    }

    private var rowBackground: Color {
        if isSelected {
            return Color.accentColor.opacity(0.20)
        }
        return isHovered ? Color.accentColor.opacity(0.10) : Color.clear
    }

    private var contextActionItems: [FileItem] {
        selectedItems.isEmpty || !isSelected
            ? [item]
            : selectedItems
    }

    private func handleClick(clickCount: Int, modifiers: NSEvent.ModifierFlags) {
        let actions = ResultClickResolver.actions(
            clickCount: clickCount,
            modifiers: modifiers,
            supportsSelection: true
        )
        for action in actions {
            switch action {
            case .selectExclusive:
                onSelect(false, false)
            case .selectRange:
                onSelect(true, false)
            case .toggleSelection:
                onSelect(false, true)
            case .open:
                openFile(item)
            case .reveal:
                revealInFinder(item)
            }
        }
    }

    private func openFile(_ item: FileItem) {
        FileActions.open(
            URL(fileURLWithPath: item.fullPath),
            isApplication: item.type == 5 || item.name.lowercased().hasSuffix(".app")
        )
    }

    private func revealInFinder(_ item: FileItem) {
        NSWorkspace.shared.selectFile(item.fullPath, inFileViewerRootedAtPath: "")
    }

    private func openInFinder(_ item: FileItem) {
        FileActions.open(URL(fileURLWithPath: item.fullPath))
    }

    private func openInTerminal(_ item: FileItem) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-a", "Terminal", item.path]
        try? process.run()
    }

    private func copyFiles(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects(items.map { NSURL(fileURLWithPath: $0.fullPath) })
    }

    private func copyFilenames(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(items.map(\.name).joined(separator: "\n"), forType: .string)
    }

    private func trashFiles(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        var removed: [String] = []
        for item in items {
            do {
                try FileManager.default.trashItem(at: URL(fileURLWithPath: item.fullPath), resultingItemURL: nil)
                removed.append(item.id)
            } catch {
                NSSound.beep()
            }
        }
        if !removed.isEmpty {
            onDeleteItems?(removed)
        }
    }

    private func quickLook(_ items: [FileItem]) {
        QuickLookPreviewController.shared.open(urls: items.map { URL(fileURLWithPath: $0.fullPath) })
    }
}
