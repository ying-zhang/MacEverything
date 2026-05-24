import SwiftUI
import AppKit
import Quartz

/// Shared icon cache to avoid repeated NSWorkspace.shared.icon(forFile:) calls.
/// - App bundles (type=5): cached by full path (each app has a unique icon)
/// - Regular files (type=1): cached by file extension (same ext → same icon)
/// - Directories/symlinks/other: cached by type (one icon per type)
final class FileIconCache {
    static let shared = FileIconCache()
    private let cache = NSCache<NSString, NSImage>()

    private init() {
        cache.countLimit = 500
    }

    func icon(for item: FileItem) -> NSImage {
        // Detect app bundles by name suffix (handles both type=5 and legacy type=2)
        let isApp = item.name.hasSuffix(".app")

        let key: String
        if isApp {
            // App bundles — unique icon per app
            key = "app:" + item.path + "/" + item.name
        } else if item.type == 1 {
            // Regular file — same icon per extension
            let ext = (item.name as NSString).pathExtension.lowercased()
            key = "ext:" + (ext.isEmpty ? "__no_ext__" : ext)
        } else {
            // Dir, symlink, other — one icon per type
            key = "type:\(item.type)"
        }

        let nsKey = key as NSString
        if let cached = cache.object(forKey: nsKey) {
            return cached
        }

        let fullPath = item.path + "/" + item.name
        let nsImage = NSWorkspace.shared.icon(forFile: fullPath).copy() as! NSImage
        nsImage.size = NSSize(width: 24, height: 24)
        cache.setObject(nsImage, forKey: nsKey)
        return nsImage
    }

    func icon(forPath path: String) -> NSImage {
        let ext = (path as NSString).pathExtension.lowercased()
        let key = "ext:" + (ext.isEmpty ? "__no_ext__" : ext) as NSString
        if let cached = cache.object(forKey: key) {
            return cached
        }
        let nsImage = NSWorkspace.shared.icon(forFile: path).copy() as! NSImage
        nsImage.size = NSSize(width: 24, height: 24)
        cache.setObject(nsImage, forKey: key)
        return nsImage
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
    @EnvironmentObject private var columnLayout: ResultColumnLayout
    @State private var isHovered = false
    @State private var localRenaming = false
    @State private var editingName = ""
    @State private var lastSelectTime: Date?
    @State private var renameError: String?
    @State private var showRenameError = false

    var body: some View {
        let dense = settings.resultDensity == .compact
        GeometryReader { proxy in
            rowContent(dense: dense, widths: columnLayout.resolvedWidths(
                showPath: settings.showPath,
                showExtension: settings.showExtension,
                showSize: settings.showSize,
                showModifiedDate: settings.showModifiedDate,
                availableWidth: proxy.size.width
            ))
        }
        .frame(height: dense ? 28 : 38)
        .accessibilityIdentifier("resultRow")
    }

    private func rowContent(dense: Bool, widths: ResolvedResultColumnWidths) -> some View {
        let highlighted = highlightCrossMatches(
            path: item.path, name: item.name, hints: hints,
            nameFont: .subheadline, nameColor: .primary,
            pathFont: .subheadline, pathColor: .secondary)

        return HStack(spacing: 10) {
            HStack(spacing: 8) {
                fileIcon(for: item)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: dense ? 18 : 22, height: dense ? 18 : 22)
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
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .help(item.fileExtension)
                    .frame(width: widths.ext, alignment: .leading)
            }

            if settings.showSize {
                Text(item.type == 1 && item.size > 0 ? formatSize(item.size) : "")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .frame(width: widths.size, alignment: .trailing)
            }

            if settings.showModifiedDate {
                Text(formatDate(item.modTime))
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .frame(width: widths.modified, alignment: .leading)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, dense ? 2 : 5)
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
            let actionItems = contextActionItems
            Button(L10n.tr("Open")) { openFile(item) }
            Button(L10n.tr("Reveal in Finder")) { revealInFinder(item) }
            Button(L10n.tr("Open in Finder")) { openInFinder(item) }
            Button(L10n.tr("Quick Look")) { quickLook(actionItems) }
            Button(L10n.tr("Rename")) { startRename() }
                .disabled(actionItems.count != 1)
            Divider()
            Button(L10n.tr("Copy File")) { copyFiles(actionItems) }
            Button(L10n.tr("Copy Filename")) { copyFilenames(actionItems) }
            Divider()
            Button(L10n.tr("Move to Trash")) { trashFiles(actionItems) }
            Button(L10n.tr("Open in Terminal")) { openInTerminal(item) }
        }
        .onDrag {
            let fullPath = item.path + "/" + item.name
            return NSItemProvider(object: NSURL(fileURLWithPath: fullPath))
        }
        .onTapGesture(count: 2) {
            cancelRename()
            onSelect(false, false)
            if NSEvent.modifierFlags.contains(.command) {
                revealInFinder(item)
            } else {
                openFile(item)
            }
        }
        .onTapGesture(count: 1) {
            if NSEvent.modifierFlags.contains(.command) {
                onSelect(false, true)
                return
            }
            if NSEvent.modifierFlags.contains(.shift) {
                onSelect(true, false)
                return
            }
            if isSelected, let last = lastSelectTime, Date().timeIntervalSince(last) > 0.5 {
                startRename()
            } else {
                cancelRename()
                onSelect(false, false)
            }
            lastSelectTime = Date()
        }
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

    private func fileIcon(for item: FileItem) -> Image {
        Image(nsImage: FileIconCache.shared.icon(for: item))
    }

    private var rowBackground: Color {
        if isSelected {
            return Color.accentColor.opacity(0.24)
        }
        return isHovered ? Color.accentColor.opacity(0.12) : Color.clear
    }

    private var contextActionItems: [FileItem] {
        selectedItems.isEmpty || !selectedItems.contains(where: { $0.id == item.id })
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

    private func openFile(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        if item.type == 5 {
            let url = URL(fileURLWithPath: fullPath)
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
        } else {
            if !NSWorkspace.shared.open(URL(fileURLWithPath: fullPath)) {
                NSSound.beep()
            }
        }
    }

    private func revealInFinder(_ item: FileItem) {
        NSWorkspace.shared.selectFile(item.fullPath, inFileViewerRootedAtPath: "")
    }

    private func openInFinder(_ item: FileItem) {
        NSWorkspace.shared.open(URL(fileURLWithPath: item.path))
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

    private func copyFiles(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects(items.map { NSURL(fileURLWithPath: $0.fullPath) })
    }

    private func copyFilename(_ item: FileItem) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(item.name, forType: .string)
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

    private func openInTerminal(_ item: FileItem) {
        let dirPath = item.path
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-a", "Terminal", dirPath]
        try? process.run()
    }

    private func quickLook(_ items: [FileItem]) {
        QuickLookPreviewController.shared.open(urls: items.map { URL(fileURLWithPath: $0.fullPath) })
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
