import AppKit
import SwiftUI
import UniformTypeIdentifiers

enum SearchExportFormat: String, CaseIterable, Identifiable, Sendable {
    case csv
    case txt

    var id: String { rawValue }
    var title: String { rawValue.uppercased() }
}

enum SearchExportColumn: String, CaseIterable, Identifiable, Sendable {
    case name
    case path
    case ext
    case size
    case modified
    case snippet

    var id: String { rawValue }

    var title: String {
        switch self {
        case .name: return L10n.tr("Name")
        case .path: return L10n.tr("Path")
        case .ext: return L10n.tr("Ext")
        case .size: return L10n.tr("Size")
        case .modified: return L10n.tr("Modified Date")
        case .snippet: return L10n.tr("Snippet")
        }
    }
}

struct SearchResultExportPopover: View {
    @ObservedObject var viewModel: SearchViewModel
    @ObservedObject private var settings = AppSettings.shared
    @State private var format: SearchExportFormat = .csv
    @State private var selectedColumns: Set<SearchExportColumn>
    @State private var errorMessage = ""
    @State private var isExporting = false

    init(viewModel: SearchViewModel) {
        self.viewModel = viewModel
        let settings = AppSettings.shared
        var columns: Set<SearchExportColumn> = [.name, .path]
        if viewModel.isContentSearch {
            columns.insert(.snippet)
        } else {
            if settings.showExtension { columns.insert(.ext) }
            if settings.showSize { columns.insert(.size) }
            if settings.showModifiedDate { columns.insert(.modified) }
        }
        _selectedColumns = State(initialValue: columns)
    }

    private var availableColumns: [SearchExportColumn] {
        viewModel.isContentSearch
            ? [.name, .path, .snippet]
            : [.name, .path, .ext, .size, .modified]
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(L10n.tr("Export Search Results"))
                .font(.headline)

            Picker(L10n.tr("Format"), selection: $format) {
                ForEach(SearchExportFormat.allCases) { option in
                    Text(option.title).tag(option)
                }
            }
            .pickerStyle(.segmented)

            Text(L10n.tr("Columns"))
                .font(.subheadline)
                .foregroundColor(.secondary)
            ForEach(availableColumns) { column in
                Toggle(column.title, isOn: Binding(
                    get: { selectedColumns.contains(column) },
                    set: { enabled in
                        if enabled { selectedColumns.insert(column) }
                        else { selectedColumns.remove(column) }
                    }
                ))
            }

            if !errorMessage.isEmpty {
                Text(errorMessage)
                    .font(.caption)
                    .foregroundColor(.red)
            }

            HStack {
                Text(L10n.tr("%d results", viewModel.totalMatches))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Button {
                    exportResults()
                } label: {
                    if isExporting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Text(L10n.tr("Export..."))
                    }
                }
                    .disabled(selectedColumns.isEmpty || isExporting)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(16)
        .frame(width: 300)
    }

    private func exportResults() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [format == .csv ? .commaSeparatedText : .plainText]
        panel.nameFieldStringValue = "MacEverything-results.\(format.rawValue)"
        guard panel.runModal() == .OK, let url = panel.url else { return }

        let orderedColumns = availableColumns.filter(selectedColumns.contains)
        let headers = orderedColumns.map(\.title)
        let exportFormat = format
        let fileItems = viewModel.isContentSearch ? [] : viewModel.fileItemsForExport()
        let contentItems = viewModel.isContentSearch ? viewModel.contentItemsForExport() : []
        isExporting = true
        errorMessage = ""

        Task { @MainActor in
            do {
                try await Task.detached(priority: .userInitiated) {
                    let output = Self.buildOutput(format: exportFormat,
                                                  columns: orderedColumns,
                                                  headers: headers,
                                                  fileItems: fileItems,
                                                  contentItems: contentItems)
                    try output.write(to: url, atomically: true, encoding: .utf8)
                }.value
            } catch {
                errorMessage = L10n.tr("Failed to export results: %@", error.localizedDescription)
            }
            isExporting = false
        }
    }

    nonisolated private static func buildOutput(
        format: SearchExportFormat,
        columns: [SearchExportColumn],
        headers: [String],
        fileItems: [FileItem],
        contentItems: [ContentFileItem]
    ) -> String {
        let formatter = ISO8601DateFormatter()
        var lines: [String] = []
        lines.reserveCapacity(1 + max(fileItems.count, contentItems.count))
        lines.append(serialize(headers, format: format))
        if contentItems.isEmpty {
            for item in fileItems {
                lines.append(serialize(columns.map { value(for: $0, fileItem: item, formatter: formatter) },
                                       format: format))
            }
        } else {
            for item in contentItems {
                lines.append(serialize(columns.map { value(for: $0, contentItem: item) }, format: format))
            }
        }
        let prefix = format == .csv ? SearchExportSerializer.utf8BOM : ""
        return prefix + lines.joined(separator: "\n") + "\n"
    }

    nonisolated private static func serialize(_ row: [String], format: SearchExportFormat) -> String {
        switch format {
        case .csv: return row.map(SearchExportSerializer.csvField).joined(separator: ",")
        case .txt: return row.map(SearchExportSerializer.txtField).joined(separator: "\t")
        }
    }

    nonisolated private static func value(for column: SearchExportColumn,
                                          fileItem item: FileItem,
                                          formatter: ISO8601DateFormatter) -> String {
        switch column {
        case .name: return item.name
        case .path: return item.path
        case .ext: return item.fileExtension
        case .size: return String(item.size)
        case .modified:
            return formatter.string(
                from: Date(timeIntervalSince1970: TimeInterval(item.modTime)))
        case .snippet: return ""
        }
    }

    nonisolated private static func value(for column: SearchExportColumn,
                                          contentItem item: ContentFileItem) -> String {
        switch column {
        case .name: return item.fileName
        case .path: return item.filePath
        case .snippet: return item.snippet
        case .ext, .size, .modified: return ""
        }
    }
}
