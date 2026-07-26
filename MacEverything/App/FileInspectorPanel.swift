import AppKit
import Quartz
import SwiftUI

struct FileInspectorPanel: View {
    let items: [FileItem]
    let onRename: (_ oldID: String, _ newName: String) -> Void
    let onRemoveItems: ([String]) -> Void

    @State private var creationDate: Date?
    @State private var editingName = ""
    @State private var isRenaming = false
    @State private var renameError = ""
    @State private var showingRenameError = false
    @FocusState private var renameFieldFocused: Bool

    var body: some View {
        Group {
            if items.count == 1, let item = items.first {
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        preview(item)
                        Divider()
                        actions(items)
                        Divider()
                        information(item)
                    }
                }
                .task(id: item.id) {
                    editingName = item.name
                    isRenaming = false
                    creationDate = try? URL(fileURLWithPath: item.fullPath)
                        .resourceValues(forKeys: [.creationDateKey]).creationDate
                }
            } else if !items.isEmpty {
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        multipleSelectionSummary(items)
                        Divider()
                        actions(items)
                        Divider()
                        multipleSelectionInformation(items)
                    }
                }
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "sidebar.right")
                        .font(.system(size: 30))
                        .foregroundColor(.secondary)
                    Text(L10n.tr("Select a result to view its information"))
                        .font(.callout)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                }
                .padding(24)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(minWidth: 280, idealWidth: 310, maxWidth: 340, maxHeight: .infinity)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.45))
        .accessibilityIdentifier("fileInspectorPanel")
        .alert(L10n.tr("Rename Failed"), isPresented: $showingRenameError) {
            Button(L10n.tr("OK"), role: .cancel) {}
        } message: {
            Text(renameError)
        }
    }

    private func information(_ item: FileItem) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            sectionTitle("File Information")
            nameRow(item)
            infoRow("Path", item.path)
            infoRow("Size", item.isFolder ? L10n.tr("Folder") : formatSize(item.size))
            infoRow("Type", typeName(item))
            infoRow("Modified Date", formatDate(Date(timeIntervalSince1970: TimeInterval(item.modTime))))
            if let creationDate {
                infoRow("Created Date", formatDate(creationDate))
            }
        }
        .padding(12)
    }

    private func actions(_ items: [FileItem]) -> some View {
        let count = items.count
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                actionButton(
                    fileActionTitle("Open", multi: "Open %d Items", count: count),
                    symbol: "arrow.up.forward.app"
                ) { FileActions.open(items) }
                actionButton(
                    fileActionTitle("Copy Filename", multi: "Copy %d Filenames", count: count),
                    symbol: "doc.text"
                ) { FileActions.copyFilenames(items) }
                actionButton(
                    fileActionTitle("Copy Path", multi: "Copy %d Full Paths", count: count),
                    symbol: "link"
                ) { FileActions.copyFiles(items) }
            }
            HStack(spacing: 8) {
                actionButton(
                    fileActionTitle("Reveal in Finder", multi: "Reveal %d Items in Finder", count: count),
                    symbol: "folder"
                ) { FileActions.revealInFinder(items) }
                actionButton(
                    fileActionTitle("Move to Trash", multi: "Move %d Items to Trash", count: count),
                    symbol: "trash",
                    role: .destructive
                ) {
                    let removed = FileActions.moveToTrash(items)
                    if !removed.isEmpty { onRemoveItems(removed) }
                }
            }
        }
        .padding(12)
    }

    private func multipleSelectionSummary(_ items: [FileItem]) -> some View {
        VStack(spacing: 8) {
            Image(systemName: "square.stack.3d.up")
                .font(.system(size: 30))
                .foregroundColor(.secondary)
            Text(L10n.tr("%d Items Selected", items.count))
                .font(.headline)
        }
        .padding(20)
        .frame(maxWidth: .infinity)
    }

    private func multipleSelectionInformation(_ items: [FileItem]) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            sectionTitle("File Information")
            HStack(alignment: .top, spacing: 8) {
                Text(L10n.tr("Name"))
                    .foregroundColor(.secondary)
                    .frame(width: 58, alignment: .trailing)
                Text(L10n.tr("%d Items Selected", items.count))
                    .foregroundStyle(Color(nsColor: .labelColor))
                Spacer(minLength: 0)
                Button {} label: {
                    Image(systemName: "pencil")
                        .font(.system(size: 12, weight: .semibold))
                        .frame(width: 22, height: 20)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .disabled(true)
                .help(L10n.tr("Rename is only available for a single item"))
                .accessibilityLabel(L10n.tr("Rename is only available for a single item"))
            }
            .font(.caption)
            infoRow("Combined File Size", formatSize(combinedFileSize(items)))
        }
        .padding(12)
    }

    private func nameRow(_ item: FileItem) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(L10n.tr("Name"))
                .foregroundColor(.secondary)
                .frame(width: 58, alignment: .trailing)
            if isRenaming {
                TextField(L10n.tr("Name"), text: $editingName)
                    .textFieldStyle(.roundedBorder)
                    .focused($renameFieldFocused)
                    .onSubmit { commitRename(item) }
                    .onExitCommand { cancelRename(item) }
                    .onAppear {
                        renameFieldFocused = true
                    }
            } else {
                Text(item.name)
                    .foregroundStyle(Color(nsColor: .labelColor))
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
                    .help(item.name)
            }
            Spacer(minLength: 0)
            Button {
                if isRenaming {
                    commitRename(item)
                } else {
                    editingName = item.name
                    isRenaming = true
                }
            } label: {
                Image(systemName: isRenaming ? "checkmark" : "pencil")
                    .font(.system(size: 12, weight: .semibold))
                    .frame(width: 22, height: 20)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .help(L10n.tr(isRenaming ? "Save" : "Rename"))
            .accessibilityLabel(L10n.tr(isRenaming ? "Save" : "Rename"))
        }
        .font(.caption)
    }

    private func preview(_ item: FileItem) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            sectionTitle("Preview")
            QuickLookEmbeddedPreview(url: URL(fileURLWithPath: item.fullPath))
                .frame(height: 210)
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
                )
        }
        .padding(12)
    }

    private func sectionTitle(_ key: String) -> some View {
        Text(L10n.tr(key))
            .font(.caption)
            .fontWeight(.semibold)
            .foregroundColor(.secondary)
            .textCase(.uppercase)
    }

    private func infoRow(_ key: String, _ value: String, lineLimit: Int? = 2) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(L10n.tr(key))
                .foregroundColor(.secondary)
                .frame(width: 58, alignment: .trailing)
            Text(value)
                .foregroundStyle(Color(nsColor: .labelColor))
                .textSelection(.enabled)
                .lineLimit(lineLimit)
                .help(value)
            Spacer(minLength: 0)
        }
        .font(.caption)
    }

    @ViewBuilder
    private func actionButton(
        _ title: String,
        symbol: String,
        role: ButtonRole? = nil,
        action: @escaping () -> Void
    ) -> some View {
        if let role {
            Button(role: role, action: action) {
                actionLabel(title, symbol: symbol)
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
            .help(title)
            .accessibilityLabel(title)
        } else {
            Button(action: action) {
                actionLabel(title, symbol: symbol)
            }
            .buttonStyle(.bordered)
            .help(title)
            .accessibilityLabel(title)
        }
    }

    private func actionLabel(_ title: String, symbol: String) -> some View {
        Label(title, systemImage: symbol)
            .font(.caption)
            .frame(maxWidth: .infinity, minHeight: 26, alignment: .leading)
    }

    private func combinedFileSize(_ items: [FileItem]) -> UInt64 {
        items.reduce(into: UInt64(0)) { total, item in
            let (sum, overflow) = total.addingReportingOverflow(item.isFolder ? 0 : item.size)
            total = overflow ? UInt64.max : sum
        }
    }

    private func typeName(_ item: FileItem) -> String {
        if item.isFolder { return L10n.tr("Folder") }
        if item.isApplication { return L10n.tr("Application") }
        if !item.fileExtension.isEmpty { return item.fileExtension.uppercased() }
        return L10n.tr("File")
    }

    private func formatSize(_ bytes: UInt64) -> String {
        L10n.formatByteCount(bytes)
    }

    private func formatDate(_ date: Date) -> String {
        L10n.formatDate(date)
    }

    private func cancelRename(_ item: FileItem) {
        editingName = item.name
        isRenaming = false
    }

    private func commitRename(_ item: FileItem) {
        let newName = editingName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !newName.isEmpty, newName != item.name else {
            cancelRename(item)
            return
        }
        guard !newName.contains("/"), !newName.contains("\0") else {
            renameError = L10n.tr("Filename cannot contain / or null characters")
            showingRenameError = true
            cancelRename(item)
            return
        }

        let destination = (item.path as NSString).appendingPathComponent(newName)
        do {
            try FileManager.default.moveItem(atPath: item.fullPath, toPath: destination)
            isRenaming = false
            onRename(item.id, newName)
        } catch {
            renameError = error.localizedDescription
            showingRenameError = true
            cancelRename(item)
        }
    }
}

private struct QuickLookEmbeddedPreview: NSViewRepresentable {
    let url: URL

    func makeNSView(context: Context) -> NSView {
        let container = NSView()
        guard let view = QLPreviewView(frame: .zero, style: .normal) else {
            return container
        }
        view.autostarts = true
        view.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(view)
        NSLayoutConstraint.activate([
            view.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            view.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            view.topAnchor.constraint(equalTo: container.topAnchor),
            view.bottomAnchor.constraint(equalTo: container.bottomAnchor)
        ])
        return container
    }

    func updateNSView(_ container: NSView, context: Context) {
        (container.subviews.first { $0 is QLPreviewView } as? QLPreviewView)?
            .previewItem = url as QLPreviewItem
    }
}
