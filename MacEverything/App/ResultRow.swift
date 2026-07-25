import SwiftUI
import AppKit
import Quartz
import UniformTypeIdentifiers

/// Returns generic type icons synchronously and loads app-specific icons off the UI thread.
final class FileIconCache: ObservableObject {
    static let shared = FileIconCache()
    private let cache = NSCache<NSString, NSImage>()
    private let appIconQueue = DispatchQueue(label: "com.maceverything.app-icons", qos: .utility)
    private var loadingAppKeys: Set<String> = []
    private let loadingAppKeysLock = NSLock()
    @Published private(set) var revision: UInt64 = 0

    private init() {
        cache.countLimit = 500
    }

    func icon(for item: FileItem) -> NSImage {
        if item.isApplication {
            return appIcon(forPath: item.fullPath)
        }

        if item.isFolder {
            return genericIcon(key: "type:folder", contentType: .folder)
        }

        return extensionIcon((item.name as NSString).pathExtension.lowercased())
    }

    func icon(forPath path: String) -> NSImage {
        if path.lowercased().hasSuffix(".app") {
            return appIcon(forPath: path)
        }
        let ext = (path as NSString).pathExtension.lowercased()
        return extensionIcon(ext)
    }

    private func extensionIcon(_ ext: String) -> NSImage {
        let key = "ext:" + (ext.isEmpty ? "__no_ext__" : ext)
        let contentType = ext.isEmpty ? UTType.data : (UTType(filenameExtension: ext) ?? .data)
        return genericIcon(key: key, contentType: contentType)
    }

    private func genericIcon(key: String, contentType: UTType) -> NSImage {
        let key = key as NSString
        if let cached = cache.object(forKey: key) {
            return cached
        }
        let image = sizedCopy(of: NSWorkspace.shared.icon(for: contentType))
        cache.setObject(image, forKey: key)
        return image
    }

    private func appIcon(forPath path: String) -> NSImage {
        let key = "app:" + path
        if let cached = cache.object(forKey: key as NSString) {
            return cached
        }

        let placeholder = genericIcon(key: "type:application", contentType: .applicationBundle)
        loadingAppKeysLock.lock()
        let inserted = loadingAppKeys.insert(key).inserted
        loadingAppKeysLock.unlock()
        guard inserted else { return placeholder }

        appIconQueue.async { [weak self] in
            guard let self else { return }
            let image = self.sizedCopy(of: NSWorkspace.shared.icon(forFile: path))
            self.cache.setObject(image, forKey: key as NSString)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.loadingAppKeysLock.lock()
                self.loadingAppKeys.remove(key)
                self.loadingAppKeysLock.unlock()
                self.revision &+= 1
            }
        }
        return placeholder
    }

    private func sizedCopy(of source: NSImage) -> NSImage {
        let image = (source.copy() as? NSImage) ?? source
        image.size = NSSize(width: 24, height: 24)
        return image
    }
}

struct ResultRow: View {
    let item: FileItem
    let hints: [HighlightHint]
    let isSelected: Bool
    let requestedRename: Bool
    let selectedItems: [FileItem]
    let onSelect: (_ extending: Bool, _ toggling: Bool) -> Void
    var onRenameComplete: (() -> Void)?
    var onRenameSuccess: ((_ oldID: String, _ newName: String) -> Void)?
    var onDeleteItems: ((_ ids: [String]) -> Void)?
    @ObservedObject private var settings = AppSettings.shared
    @ObservedObject private var iconCache = FileIconCache.shared
    @ObservedObject private var thumbnailService = ThumbnailService.shared
    @EnvironmentObject private var columnLayout: ResultColumnLayout
    @EnvironmentObject private var theme: ThemeManager
    @State private var isHovered = false
    @State private var localRenaming = false
    @State private var editingName = ""
    @State private var renameError: String?
    @State private var showRenameError = false

    var body: some View {
        let rowHeight = ResultRowMetrics.effectiveHeight(
            configuredHeight: settings.resultRowHeight,
            fontLineHeight: theme.bodyNSFont.ascender - theme.bodyNSFont.descender + theme.bodyNSFont.leading
        )
        GeometryReader { proxy in
            rowContent(rowHeight: rowHeight, widths: columnLayout.resolvedWidths(
                showPath: settings.showPath,
                showExtension: settings.showExtension,
                showSize: settings.showSize,
                showModifiedDate: settings.showModifiedDate,
                availableWidth: proxy.size.width
            ))
        }
        .frame(height: rowHeight)
        .clipped()
        .accessibilityIdentifier("resultRow")
    }

    private func rowContent(rowHeight: CGFloat, widths: ResolvedResultColumnWidths) -> some View {
        let iconSize = min(rowHeight - 8, 32)
        let highlighted = highlightCrossMatches(
            path: item.path, name: item.name, hints: hints,
            nameFont: theme.bodyFont, nameColor: theme.resolvedTextColor,
            pathFont: theme.bodyFont, pathColor: .secondary)

        return HStack(spacing: 10) {
            HStack(spacing: 8) {
                fileVisual(for: item, pixelSize: iconSize)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: iconSize, height: iconSize)
                if isActivelyRenaming {
                    RenameTextField(text: $editingName,
                                    onCommit: { commitRename() },
                                    onCancel: { cancelRename() })
                } else {
                    highlighted.nameText.lineLimit(1)
                        .help(item.name)
                }
            }
            .frame(width: widths.name, alignment: .leading)

            if settings.showPath {
                highlighted.pathText
                    .lineLimit(1)
                    .help(item.path)
                    .frame(width: widths.path, alignment: .leading)
            }

            if settings.showExtension {
                Text(item.fileExtension)
                    .font(theme.bodyFont)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .help(item.fileExtension)
                    .frame(width: widths.ext, alignment: .leading)
            }

            if settings.showSize {
                Text(item.isRegularFile && item.size > 0 ? formatSize(item.size) : "")
                    .font(theme.bodyFont)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .frame(width: widths.size, alignment: .trailing)
            }

            if settings.showModifiedDate {
                Text(formatDate(item.modTime))
                    .font(theme.bodyFont)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .frame(width: widths.modified, alignment: .leading)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, max(2, (rowHeight - iconSize) / 2))
        .padding(.horizontal, 6)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(rowBackground)
        )
        .contentShape(Rectangle())
        .onHover { hovering in
            isHovered = hovering
        }
        .contextMenu {
            FileItemContextMenu(
                item: item,
                actionItems: contextActionItems,
                onRename: startRename,
                onDeleteItems: onDeleteItems
            )
        }
        .onDrag {
            let fullPath = item.path + "/" + item.name
            return NSItemProvider(object: NSURL(fileURLWithPath: fullPath))
        }
        .background(
            ResultClickMonitor(enabled: !isActivelyRenaming) { clickCount, modifiers in
                handleClick(clickCount: clickCount, modifiers: modifiers)
            }
        )
        .onChange(of: requestedRename) {
            if requestedRename {
                startRename()
            }
        }
        .alert(L10n.tr("Rename Failed"), isPresented: $showRenameError) {
            Button("OK") {}
        } message: {
            Text(renameError ?? "")
        }
    }

    private func fileVisual(for item: FileItem, pixelSize: CGFloat) -> Image {
        _ = iconCache.revision
        if settings.showThumbnails && !item.isFolder && !item.isApplication {
            _ = thumbnailService.revision
            if let thumb = thumbnailService.thumbnail(for: item.fullPath, modTime: item.modTime, pixelSize: pixelSize) {
                return Image(nsImage: thumb)
            }
        }
        return Image(nsImage: iconCache.icon(for: item))
    }

    private var rowBackground: Color {
        if isSelected {
            return Color.accentColor.opacity(0.24)
        }
        return isHovered ? Color.accentColor.opacity(0.12) : Color.clear
    }

    private var contextActionItems: [FileItem] {
        selectedItems.isEmpty || !isSelected
            ? [item]
            : selectedItems
    }

    private func formatSize(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
    }

    private func formatDate(_ modTime: time_t) -> String {
        let date = Date(timeIntervalSince1970: TimeInterval(modTime))
        return date.formatted(date: .abbreviated, time: .shortened)
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
                FileActions.open(item)
            case .reveal:
                FileActions.revealInFinder(item)
            }
        }
    }

    private var isActivelyRenaming: Bool {
        localRenaming || requestedRename
    }

    private func startRename() {
        editingName = item.name
        localRenaming = true
    }

    private func cancelRename() {
        localRenaming = false
        onRenameComplete?()
    }

    private func commitRename() {
        localRenaming = false
        onRenameComplete?()
        let newName = editingName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !newName.isEmpty, newName != item.name else { return }
        if newName.contains("/") || newName.contains("\0") {
            NSSound.beep()
            renameError = L10n.tr("Filename cannot contain / or null characters")
            showRenameError = true
            return
        }
        if newName.hasPrefix(".") {
            let oldHasPrefix = item.name.hasPrefix(".")
            if !oldHasPrefix {
                // Allow but don't block — user may intentionally hide a file
            }
        }
        let oldPath = item.path + "/" + item.name
        let newPath = item.path + "/" + newName
        do {
            try FileManager.default.moveItem(atPath: oldPath, toPath: newPath)
            onRenameSuccess?(item.id, newName)
        } catch {
            NSSound.beep()
            renameError = error.localizedDescription
            showRenameError = true
        }
    }

}

struct ResolvedResultColumnWidths {
    let name: CGFloat
    let path: CGFloat
    let ext: CGFloat
    let size: CGFloat
    let modified: CGFloat
}

final class ResultColumnLayout: ObservableObject {
    static let nameWidthRange: ClosedRange<CGFloat> = 160...520
    static let pathWidthRange: ClosedRange<CGFloat> = 180...800
    static let extWidthRange: ClosedRange<CGFloat> = 52...120
    static let sizeWidthRange: ClosedRange<CGFloat> = 72...150
    static let modifiedWidthRange: ClosedRange<CGFloat> = 120...240

    @Published var nameWidth: CGFloat = 260
    @Published var pathWidth: CGFloat = 260
    @Published var extWidth: CGFloat = 68
    @Published var sizeWidth: CGFloat = 92
    @Published var modifiedWidth: CGFloat = 150

    func resolvedWidths(showPath: Bool,
                        showExtension: Bool,
                        showSize: Bool,
                        showModifiedDate: Bool,
                        availableWidth: CGFloat) -> ResolvedResultColumnWidths {
        let visibleColumns = 1 + (showPath ? 1 : 0) + (showExtension ? 1 : 0) + (showSize ? 1 : 0) + (showModifiedDate ? 1 : 0)
        let interColumnSpace = CGFloat(max(0, visibleColumns - 1)) * 10
        let contentWidth = max(0, availableWidth - 12)
        let fixedTrailingWidth = (showExtension ? extWidth : 0) + (showSize ? sizeWidth : 0) + (showModifiedDate ? modifiedWidth : 0)

        if showPath {
            let remainingPathWidth = contentWidth - interColumnSpace - nameWidth - fixedTrailingWidth
            return ResolvedResultColumnWidths(
                name: nameWidth,
                path: max(Self.pathWidthRange.lowerBound, remainingPathWidth),
                ext: extWidth,
                size: sizeWidth,
                modified: modifiedWidth
            )
        }

        let remainingNameWidth = contentWidth - interColumnSpace - fixedTrailingWidth
        return ResolvedResultColumnWidths(
            name: max(Self.nameWidthRange.lowerBound, max(nameWidth, remainingNameWidth)),
            path: pathWidth,
            ext: extWidth,
            size: sizeWidth,
            modified: modifiedWidth
        )
    }
}

@MainActor
final class QuickLookPreviewController: NSObject, QLPreviewPanelDataSource, QLPreviewPanelDelegate {
    static let shared = QuickLookPreviewController()
    private var urls: [URL] = []

    func open(urls: [URL]) {
        guard !urls.isEmpty else { return }
        self.urls = urls
        let panel = QLPreviewPanel.shared()!
        panel.dataSource = self
        panel.delegate = self
        panel.currentPreviewItemIndex = 0
        panel.reloadData()
        panel.makeKeyAndOrderFront(nil)
    }

    func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int {
        urls.count
    }

    func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> (any QLPreviewItem)! {
        guard urls.indices.contains(index) else { return nil }
        return urls[index] as NSURL
    }
}

/// NSTextField wrapper that commits on focus loss and cancels on Escape.
/// Uses a local mouse-down event monitor to detect clicks outside the field.
private struct RenameTextField: NSViewRepresentable {
    @Binding var text: String
    var onCommit: () -> Void
    var onCancel: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSTextField {
        let field = NSTextField()
        field.isBordered = true
        field.bezelStyle = .roundedBezel
        field.font = .systemFont(ofSize: NSFont.systemFontSize(for: .small))
        field.focusRingType = .exterior
        field.delegate = context.coordinator
        field.stringValue = text
        context.coordinator.field = field
        DispatchQueue.main.async {
            field.window?.makeFirstResponder(field)
            field.currentEditor()?.selectAll(nil)
            context.coordinator.installClickMonitor()
        }
        return field
    }

    func updateNSView(_ field: NSTextField, context: Context) {
        context.coordinator.parent = self
    }

    static func dismantleNSView(_ field: NSTextField, coordinator: Coordinator) {
        coordinator.removeClickMonitor()
    }

    class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: RenameTextField
        weak var field: NSTextField?
        private var didEnd = false
        private var clickMonitor: Any?

        init(_ parent: RenameTextField) { self.parent = parent }

        func installClickMonitor() {
            guard clickMonitor == nil else { return }
            clickMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] event in
                guard let self, let field = self.field, !self.didEnd else { return event }
                let locationInField = field.convert(event.locationInWindow, from: nil)
                if !field.bounds.contains(locationInField) {
                    self.parent.text = field.stringValue
                    self.finish(commit: true)
                }
                return event
            }
        }

        func removeClickMonitor() {
            if let monitor = clickMonitor {
                NSEvent.removeMonitor(monitor)
                clickMonitor = nil
            }
        }

        func controlTextDidChange(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            parent.text = field.stringValue
        }

        func controlTextDidEndEditing(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            parent.text = field.stringValue
            let isCancel = (obj.userInfo?["NSTextMovement"] as? Int) == NSTextMovement.cancel.rawValue
            finish(commit: !isCancel)
        }

        private func finish(commit: Bool) {
            guard !didEnd else { return }
            didEnd = true
            removeClickMonitor()
            if commit {
                parent.onCommit()
            } else {
                parent.onCancel()
            }
        }

        deinit {
            removeClickMonitor()
        }
    }
}
