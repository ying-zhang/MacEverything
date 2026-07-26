import AppKit
import SwiftUI

@MainActor
enum FileActions {
    static func open(_ item: FileItem) {
        open(URL(fileURLWithPath: item.fullPath), isApplication: item.isApplication)
    }

    static func open(_ url: URL, isApplication: Bool = false) {
        let configuration = NSWorkspace.OpenConfiguration()
        let completion: @Sendable (NSRunningApplication?, Error?) -> Void = { _, error in
            guard let error else { return }
            AppLogger.error("FileActions", "Failed to open \(url.path): \(error.localizedDescription)")
            Task { @MainActor in
                NSSound.beep()
            }
        }

        if isApplication {
            NSWorkspace.shared.openApplication(
                at: url,
                configuration: configuration,
                completionHandler: completion
            )
        } else {
            NSWorkspace.shared.open(
                url,
                configuration: configuration,
                completionHandler: completion
            )
        }
    }

    static func revealInFinder(_ item: FileItem) {
        NSWorkspace.shared.selectFile(item.fullPath, inFileViewerRootedAtPath: "")
    }

    static func openFolder(_ item: FileItem) {
        open(URL(fileURLWithPath: item.fullPath))
    }

    static func openInTerminal(_ item: FileItem) {
        let directory = item.isFolder ? item.fullPath : item.path
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-a", "Terminal", directory]
        do {
            try process.run()
        } catch {
            AppLogger.error("FileActions", "Failed to open Terminal: \(error.localizedDescription)")
            NSSound.beep()
        }
    }

    static func copyFiles(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects(items.map { NSURL(fileURLWithPath: $0.fullPath) })
    }

    static func copyFilenames(_ items: [FileItem]) {
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(items.map(\.name).joined(separator: "\n"), forType: .string)
    }

    static func moveToTrash(_ items: [FileItem]) -> [String] {
        var removed: [String] = []
        for item in items {
            do {
                try FileManager.default.trashItem(
                    at: URL(fileURLWithPath: item.fullPath),
                    resultingItemURL: nil
                )
                removed.append(item.id)
            } catch {
                AppLogger.error("FileActions", "Failed to trash \(item.fullPath): \(error.localizedDescription)")
                NSSound.beep()
            }
        }
        return removed
    }

    static func quickLook(_ items: [FileItem]) {
        QuickLookPreviewController.shared.open(
            urls: items.map { URL(fileURLWithPath: $0.fullPath) }
        )
    }
}

struct FileItemContextMenu: View {
    let item: FileItem
    let actionItems: [FileItem]
    var onRename: (() -> Void)?
    var onDeleteItems: ((_ ids: [String]) -> Void)?

    var body: some View {
        Button(L10n.tr("Open")) { FileActions.open(item) }
        Button(L10n.tr("Reveal in Finder")) { FileActions.revealInFinder(item) }
        if item.isFolder {
            Button(L10n.tr("Open Folder")) { FileActions.openFolder(item) }
        }
        Button(L10n.tr("Quick Look")) { FileActions.quickLook(actionItems) }
        if let onRename {
            Button(L10n.tr("Rename"), action: onRename)
                .disabled(actionItems.count != 1)
        }
        Divider()
        Button(L10n.tr("Copy Path")) { FileActions.copyFiles(actionItems) }
        Button(L10n.tr("Copy Filename")) { FileActions.copyFilenames(actionItems) }
        Divider()
        Button(L10n.tr("Open in Terminal")) { FileActions.openInTerminal(item) }
        Button(role: .destructive) {
            let removed = FileActions.moveToTrash(actionItems)
            if !removed.isEmpty { onDeleteItems?(removed) }
        } label: {
            Text(L10n.tr("Move to Trash"))
        }
    }
}
