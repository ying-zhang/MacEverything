import SwiftUI
import ServiceManagement
import AppKit

struct GeneralSettingsView: View {
    @ObservedObject private var settings = AppSettings.shared
    @State private var newIndexPath = ""
    @State private var newExcludedPath = ""
    @State private var newExcludedPattern = ""
    @State private var newContentPath = ""
    @State private var newContentExcludedPath = ""

    var body: some View {
        TabView {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    indexingSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Indexes")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    exclusionsSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Exclusions")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    contentSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Content")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    searchSection
                    historySection
                    resultsSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Search & Results")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    startupSection
                    serviceSection
                    maintenanceSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("General")) }
        }
        .frame(width: 720, height: 620)
    }

    private var indexingSection: some View {
        SettingsSection(title: L10n.tr("Indexing Scope")) {
            CompactPathListEditor(
                title: L10n.tr("Indexed Folders"),
                paths: $settings.indexRoots,
                newPath: $newIndexPath,
                visibleRows: 8
            )

            Divider()

            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading, spacing: 8) {
                Toggle(L10n.tr("Index hidden files and folders"), isOn: $settings.indexHiddenFiles)
                Toggle(L10n.tr("Index system files"), isOn: $settings.indexSystemFiles)
                Toggle(L10n.tr("Index inside application bundles"), isOn: $settings.indexAppBundleContents)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text(L10n.tr("Refresh Mode"))
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Picker(L10n.tr("Refresh Mode"), selection: $settings.refreshMode) {
                    ForEach(RefreshMode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.radioGroup)
                .labelsHidden()
            }

            HStack {
                Button(L10n.tr("Reset Index Defaults")) {
                    settings.resetIndexDefaults()
                }
                Spacer()
                Text(L10n.tr("Changes require rebuilding the index."))
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var exclusionsSection: some View {
        SettingsSection(title: L10n.tr("Exclusions")) {
            CompactPathListEditor(
                title: L10n.tr("Excluded Folders"),
                paths: $settings.excludedPaths,
                newPath: $newExcludedPath,
                visibleRows: 8
            )
            StringListEditor(
                title: L10n.tr("Excluded Name Patterns"),
                items: $settings.excludedPatterns,
                newItem: $newExcludedPattern,
                placeholder: L10n.tr("Add pattern...")
            )
            HStack {
                Text(L10n.tr("Exclusion changes take effect after rebuilding the index."))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Button(L10n.tr("Rebuild Index Now")) {
                    NotificationCenter.default.post(name: Notification.Name("rebuildIndex"), object: nil)
                }
            }
        }
    }

    private var contentSection: some View {
        SettingsSection(title: L10n.tr("Content Indexing")) {
            Toggle(L10n.tr("Enable content indexing"), isOn: $settings.contentIndexingEnabled)

            HStack {
                Text(L10n.tr("Max File Size"))
                Slider(value: $settings.contentMaxFileSizeMB, in: 0.1...100.0, step: 0.1)
                Text(String(format: "%.1f MB", settings.contentMaxFileSizeMB))
                    .frame(width: 72, alignment: .trailing)
            }
            .disabled(!settings.contentIndexingEnabled)

            CompactPathListEditor(
                title: L10n.tr("Content Indexed Folders"),
                paths: $settings.contentIndexRoots,
                newPath: $newContentPath,
                visibleRows: 5
            )
            .disabled(!settings.contentIndexingEnabled)

            CompactPathListEditor(
                title: L10n.tr("Content Excluded Folders"),
                paths: $settings.contentExcludedPaths,
                newPath: $newContentExcludedPath,
                visibleRows: 5
            )
            .disabled(!settings.contentIndexingEnabled)
        }
    }

    private var searchSection: some View {
        SettingsSection(title: L10n.tr("Search Defaults")) {
            Picker(L10n.tr("Startup Results"), selection: $settings.startupDisplayMode) {
                ForEach(StartupDisplayMode.allCases) { mode in
                    Text(mode.title).tag(mode)
                }
            }
            .pickerStyle(.segmented)

            Toggle(L10n.tr("Search as you type"), isOn: $settings.searchAsYouType)
            Toggle(L10n.tr("Default Regex"), isOn: $settings.defaultRegex)
            Toggle(L10n.tr("Default Case Sensitive"), isOn: $settings.defaultCaseSensitive)
            Toggle(L10n.tr("Default Whole Word"), isOn: $settings.defaultWholeWord)
            Toggle(L10n.tr("Default Match Filename"), isOn: $settings.defaultMatchFilename)

            Stepper(value: $settings.maxResults, in: 100...100_000, step: 100) {
                Text(L10n.tr("Maximum Results: %d", settings.maxResults))
            }

            Picker(L10n.tr("Default Sort"), selection: $settings.sortField) {
                ForEach(SortField.allCases) { field in
                    Text(field.title).tag(field)
                }
            }
            Toggle(L10n.tr("Sort Ascending"), isOn: $settings.sortAscending)
                .disabled(settings.sortField == .relevance)
        }
    }

    private var historySection: some View {
        SettingsSection(title: L10n.tr("History")) {
            Toggle(L10n.tr("Enable search history"), isOn: $settings.searchHistoryEnabled)
            Stepper(value: $settings.searchHistoryLimit, in: 0...5000, step: 50) {
                Text(L10n.tr("History Limit: %d", settings.searchHistoryLimit))
            }
            .disabled(!settings.searchHistoryEnabled)

            Button(L10n.tr("Clear Search History")) {
                settings.clearSearchHistory()
            }
        }
    }

    private var resultsSection: some View {
        SettingsSection(title: L10n.tr("Results View")) {
            Toggle(L10n.tr("Show path"), isOn: $settings.showPath)
            Toggle(L10n.tr("Show size"), isOn: $settings.showSize)
            Toggle(L10n.tr("Show modified date"), isOn: $settings.showModifiedDate)
            Toggle(L10n.tr("Show content snippets"), isOn: $settings.showContentSnippets)

            Picker(L10n.tr("Result Density"), selection: $settings.resultDensity) {
                ForEach(ResultDensity.allCases) { density in
                    Text(density.title).tag(density)
                }
            }
            .pickerStyle(.segmented)
        }
    }

    private var startupSection: some View {
        SettingsSection(title: L10n.tr("Startup & Window")) {
            Toggle(L10n.tr("Launch at Login"), isOn: Binding(
                get: { SMAppService.mainApp.status == .enabled },
                set: { enabled in
                    do {
                        if enabled {
                            try SMAppService.mainApp.register()
                        } else {
                            try SMAppService.mainApp.unregister()
                        }
                    } catch {
                        NSLog("MacEverything settings: failed to update launch at login: \(error)")
                    }
                }
            ))
            Toggle(L10n.tr("Hide Dock Icon"), isOn: $settings.hideDockIcon)
            Text(L10n.tr("When hidden from Dock, use the menu bar icon or global shortcut to show MacEverything."))
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var serviceSection: some View {
        SettingsSection(title: L10n.tr("HTTP API")) {
            Toggle(L10n.tr("Enable HTTP API"), isOn: $settings.httpServerEnabled)
            Stepper(value: $settings.httpPort, in: 1024...65535, step: 1) {
                Text(L10n.tr("Port: %d", settings.httpPort))
            }
            .disabled(!settings.httpServerEnabled)
            VStack(alignment: .leading, spacing: 4) {
                Text(L10n.tr("HTTP API listens on local loopback only: http://127.0.0.1:%d", settings.httpPort))
                Text(L10n.tr("Example: curl \"http://127.0.0.1:%d/api/search?q=readme&limit=10\"", settings.httpPort))
                Text(L10n.tr("Useful endpoints: /api/search, /api/search/content, /api/recent, /api/status"))
            }
            .font(.caption)
            .foregroundColor(.secondary)
            .textSelection(.enabled)
            .disabled(!settings.httpServerEnabled)
        }
    }

    private var maintenanceSection: some View {
        SettingsSection(title: L10n.tr("Maintenance")) {
            Toggle(L10n.tr("Automatic index maintenance"), isOn: $settings.automaticMaintenanceEnabled)
            Button(L10n.tr("Rebuild Index")) {
                NotificationCenter.default.post(name: Notification.Name("rebuildIndex"), object: nil)
            }
        }
    }
}

private struct SettingsSection<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.headline)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct CompactPathListEditor: View {
    let title: String
    @Binding var paths: [String]
    @Binding var newPath: String
    let visibleRows: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title)
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Spacer()
                Button {
                    chooseFolder()
                } label: {
                    Image(systemName: "folder.badge.plus")
                }
                Button {
                    addTypedPath()
                } label: {
                    Image(systemName: "plus.circle")
                }
                .disabled(newPath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }

            HStack {
                TextField(L10n.tr("Add folder path..."), text: $newPath)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(addTypedPath)
            }

            List {
                ForEach(paths, id: \.self) { path in
                    HStack {
                        Text((path as NSString).abbreviatingWithTildeInPath)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Spacer()
                        Button {
                            paths.removeAll { $0 == path }
                        } label: {
                            Image(systemName: "minus.circle")
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .frame(height: CGFloat(max(3, visibleRows)) * 24)
        }
    }

    private func addTypedPath() {
        let path = newPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !path.isEmpty else { return }
        paths = normalizedPaths(paths + [path])
        newPath = ""
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            paths = normalizedPaths(paths + panel.urls.map(\.path))
        }
    }
}

private struct StringListEditor: View {
    let title: String
    @Binding var items: [String]
    @Binding var newItem: String
    let placeholder: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.subheadline)
                .foregroundColor(.secondary)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 92), spacing: 4)], alignment: .leading, spacing: 4) {
                ForEach(items, id: \.self) { item in
                    HStack(spacing: 4) {
                        Text(item)
                            .font(.caption)
                        Button {
                            items.removeAll { $0 == item }
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .font(.caption2)
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(RoundedRectangle(cornerRadius: 5).fill(Color.secondary.opacity(0.16)))
                }
            }
            .frame(minHeight: 34, alignment: .leading)

            HStack {
                TextField(placeholder, text: $newItem)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(addItem)
                Button(L10n.tr("Add")) {
                    addItem()
                }
                .disabled(newItem.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
    }

    private func addItem() {
        let item = newItem.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !item.isEmpty, !items.contains(item) else { return }
        items.append(item)
        newItem = ""
    }
}
