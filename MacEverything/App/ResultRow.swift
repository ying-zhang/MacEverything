import SwiftUI
import AppKit

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
    let onSelect: () -> Void
    var onRenameComplete: (() -> Void)?
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
                    TextField("", text: $editingName)
                        .textFieldStyle(.roundedBorder)
                        .font(.subheadline)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .onSubmit { commitRename() }
                        .onExitCommand { cancelRename() }
                        .onAppear {
                            editingName = item.name
                        }
                } else {
                    highlighted.nameText.lineLimit(1)
                }
            }
            .frame(width: widths.name, alignment: .leading)

            if settings.showPath {
                highlighted.pathText
                    .lineLimit(1)
                    .frame(width: widths.path, alignment: .leading)
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
            Button(L10n.tr("Open")) { openFile(item) }
            Button(L10n.tr("Reveal in Finder")) { revealInFinder(item) }
            Button(L10n.tr("Quick Look")) { quickLook(item) }
            Button(L10n.tr("Rename")) { startRename() }
            Divider()
            Button(L10n.tr("Copy File")) { copyFile(item) }
            Button(L10n.tr("Copy Filename")) { copyFilename(item) }
            Divider()
            Button(L10n.tr("Move to Trash")) { trashFile(item) }
            Button(L10n.tr("Open in Terminal")) { openInTerminal(item) }
        }
        .onDrag {
            let fullPath = item.path + "/" + item.name
            return NSItemProvider(object: NSURL(fileURLWithPath: fullPath))
        }
        .onTapGesture(count: 2) {
            cancelRename()
            onSelect()
            if NSEvent.modifierFlags.contains(.command) {
                revealInFinder(item)
            } else {
                openFile(item)
            }
        }
        .onTapGesture(count: 1) {
            if NSEvent.modifierFlags.contains(.command) {
                onSelect()
                revealInFinder(item)
                return
            }
            if isSelected, let last = lastSelectTime, Date().timeIntervalSince(last) > 0.5 {
                startRename()
            } else {
                cancelRename()
                onSelect()
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
        let fullPath = item.path + "/" + item.name
        NSWorkspace.shared.selectFile(fullPath, inFileViewerRootedAtPath: "")
    }

    private func copyPath(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(fullPath, forType: .string)
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
        let oldPath = item.path + "/" + item.name
        let newPath = item.path + "/" + newName
        do {
            try FileManager.default.moveItem(atPath: oldPath, toPath: newPath)
        } catch {
            NSSound.beep()
            renameError = error.localizedDescription
            showRenameError = true
        }
    }

    private func copyFile(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects([NSURL(fileURLWithPath: fullPath)])
    }

    private func copyFilename(_ item: FileItem) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(item.name, forType: .string)
    }

    private func trashFile(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        let url = URL(fileURLWithPath: fullPath)
        do {
            try FileManager.default.trashItem(at: url, resultingItemURL: nil)
        } catch {
            NSSound.beep()
        }
    }

    private func openInTerminal(_ item: FileItem) {
        let dirPath = item.path
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-a", "Terminal", dirPath]
        try? process.run()
    }

    private func quickLook(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/qlmanage")
        process.arguments = ["-p", fullPath]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try? process.run()
    }
}

struct ResolvedResultColumnWidths {
    let name: CGFloat
    let path: CGFloat
    let size: CGFloat
    let modified: CGFloat
}

final class ResultColumnLayout: ObservableObject {
    static let nameWidthRange: ClosedRange<CGFloat> = 160...520
    static let pathWidthRange: ClosedRange<CGFloat> = 180...800
    static let sizeWidthRange: ClosedRange<CGFloat> = 72...150
    static let modifiedWidthRange: ClosedRange<CGFloat> = 120...240

    @Published var nameWidth: CGFloat = 260
    @Published var pathWidth: CGFloat = 260
    @Published var sizeWidth: CGFloat = 92
    @Published var modifiedWidth: CGFloat = 150

    func resolvedWidths(showPath: Bool,
                        showSize: Bool,
                        showModifiedDate: Bool,
                        availableWidth: CGFloat) -> ResolvedResultColumnWidths {
        let visibleColumns = 1 + (showPath ? 1 : 0) + (showSize ? 1 : 0) + (showModifiedDate ? 1 : 0)
        let interColumnSpace = CGFloat(max(0, visibleColumns - 1)) * 10
        let contentWidth = max(0, availableWidth - 12)
        let fixedTrailingWidth = (showSize ? sizeWidth : 0) + (showModifiedDate ? modifiedWidth : 0)

        if showPath {
            let remainingPathWidth = contentWidth - interColumnSpace - nameWidth - fixedTrailingWidth
            return ResolvedResultColumnWidths(
                name: nameWidth,
                path: max(Self.pathWidthRange.lowerBound, remainingPathWidth),
                size: sizeWidth,
                modified: modifiedWidth
            )
        }

        let remainingNameWidth = contentWidth - interColumnSpace - fixedTrailingWidth
        return ResolvedResultColumnWidths(
            name: max(Self.nameWidthRange.lowerBound, max(nameWidth, remainingNameWidth)),
            path: pathWidth,
            size: sizeWidth,
            modified: modifiedWidth
        )
    }
}
