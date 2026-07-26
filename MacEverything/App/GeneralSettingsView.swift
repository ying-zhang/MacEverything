import SwiftUI
import ServiceManagement
import AppKit

struct GeneralSettingsView: View {
    private enum ColorEditorMode: String, CaseIterable, Identifiable {
        case light
        case dark

        var id: String { rawValue }

        var title: String {
            switch self {
            case .light: return L10n.tr("Light")
            case .dark: return L10n.tr("Dark")
            }
        }
    }

    @Environment(\.colorScheme) private var colorScheme
    @ObservedObject private var settings = AppSettings.shared
    @ObservedObject private var theme = ThemeManager.shared
    @ObservedObject private var mcpIntegration = MCPIntegrationModel.shared
    @State private var newIndexPath = ""
    @State private var newExcludedPath = ""
    @State private var newExcludedPattern = ""
    @State private var newContentSearchPath = ""
    @State private var newContentSearchExcludedPath = ""
    @State private var showingResetIndexDefaultsConfirmation = false
    @State private var showingClearContentIndexConfirmation = false
    @State private var showingResetAppearanceConfirmation = false
    @State private var fontSearchText = ""
    @State private var colorEditorMode: ColorEditorMode = .light
    @State private var mainIndexBaseBytes: UInt64 = 0
    @State private var mainIndexWalBytes: UInt64 = 0
    @State private var mainIndexLegacyBytes: UInt64 = 0
    @State private var mainIndexStorageBytes: UInt64 = 0
    @State private var contentIndexBaseBytes: UInt64 = 0
    @State private var contentIndexWalBytes: UInt64 = 0
    @State private var contentIndexStorageBytes: UInt64 = 0
    @State private var contentIndexedCount: UInt32 = 0
    @State private var cliInstallStatus: CLIInstallStatus = .notInstalled
    @State private var cliInstallError = ""

    var body: some View {
        TabView {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    indexingSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Index Scope")) }

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
            .tabItem { Text(L10n.tr("Search Text Content")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    indexFilesSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Index File Info")) }

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
                    appearanceSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("Appearance")) }

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    startupSection
                    shortcutSection
                    serviceSection
                    cliSection
                    mcpSection
                    maintenanceSection
                }
                .padding()
            }
            .tabItem { Text(L10n.tr("General")) }
        }
        .frame(width: 720, height: 660)
        .alert(L10n.tr("Reset Index Defaults?"), isPresented: $showingResetIndexDefaultsConfirmation) {
            Button(L10n.tr("Reset"), role: .destructive) {
                settings.resetIndexDefaults()
            }
            Button(L10n.tr("Cancel"), role: .cancel) {}
        } message: {
            Text(L10n.tr("Resetting index defaults will replace indexed folders, exclusions, and related index options."))
        }
        .alert(L10n.tr("Clear Content Index?"), isPresented: $showingClearContentIndexConfirmation) {
            Button(L10n.tr("Clear"), role: .destructive) {
                NotificationCenter.default.post(name: .clearContentIndex, object: nil)
                scheduleContentIndexInfoRefresh()
            }
            Button(L10n.tr("Cancel"), role: .cancel) {}
        } message: {
            Text(L10n.tr("This removes the persisted content index files and clears current content search data."))
        }
        .alert(L10n.tr("Reset Appearance Defaults?"), isPresented: $showingResetAppearanceConfirmation) {
            Button(L10n.tr("Reset"), role: .destructive) {
                settings.resetAppearanceDefaults()
            }
            Button(L10n.tr("Cancel"), role: .cancel) {}
        } message: {
            Text(L10n.tr("This will reset theme, colors, font, font size, and row height to defaults."))
        }
        .onAppear {
            refreshContentIndexInfo()
            selectActiveColorMode()
            refreshCLIInstallStatus()
        }
        .onChange(of: settings.appearanceMode) { selectActiveColorMode() }
        .onChange(of: colorScheme) {
            if settings.appearanceMode == .system { selectActiveColorMode() }
        }
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
                Text(L10n.tr("Changes require rebuilding the index."))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Button(L10n.tr("Rebuild Index Now")) {
                    NotificationCenter.default.post(name: .rebuildIndex, object: nil)
                }
            }

            Divider()

            HStack {
                Spacer()
                Button(role: .destructive) {
                    showingResetIndexDefaultsConfirmation = true
                } label: {
                    Text(L10n.tr("Reset Index Defaults..."))
                }
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
                    NotificationCenter.default.post(name: .rebuildIndex, object: nil)
                }
            }
        }
    }

    private var contentSection: some View {
        SettingsSection(title: L10n.tr("Search Text Content")) {
            Toggle(L10n.tr("Enable text content indexing"), isOn: $settings.contentIndexingEnabled)

            HStack {
                Text(L10n.tr("Max File Size"))
                Slider(value: $settings.contentMaxFileSizeMB, in: 0.1...100.0, step: 0.1)
                Text(String(format: "%.1f MB", settings.contentMaxFileSizeMB))
                    .frame(width: 72, alignment: .trailing)
            }
            .disabled(!settings.contentIndexingEnabled)

            CompactPathListEditor(
                title: L10n.tr("Text Content Index Folders"),
                paths: $settings.contentSearchRoots,
                newPath: $newContentSearchPath,
                visibleRows: 5
            )
            .disabled(!settings.contentIndexingEnabled || settings.contentSearchUsesIndexRoots)

            HStack {
                Toggle(L10n.tr("Use main indexed folders"), isOn: $settings.contentSearchUsesIndexRoots)
            }
            .disabled(!settings.contentIndexingEnabled)

            CompactPathListEditor(
                title: L10n.tr("Text Content Excluded Folders"),
                paths: $settings.contentSearchExcludedPaths,
                newPath: $newContentSearchExcludedPath,
                visibleRows: 5
            )
            .disabled(!settings.contentIndexingEnabled || settings.contentSearchUsesIndexExclusions)

            HStack {
                Toggle(L10n.tr("Use main excluded folders"), isOn: $settings.contentSearchUsesIndexExclusions)
            }
            .disabled(!settings.contentIndexingEnabled)

            Text(L10n.tr("When main folders are enabled, text content indexing follows the main index scope. Turn them off to edit text content folders separately."))
                .font(.caption)
                .foregroundColor(.secondary)

            Divider()

            HStack {
                Button(L10n.tr("Rebuild Content Index")) {
                    NotificationCenter.default.post(name: .rebuildContentIndex, object: nil)
                    scheduleContentIndexInfoRefresh()
                }
                .disabled(!settings.contentIndexingEnabled)

                Spacer()

                Button(L10n.tr("Clear Content Index"), role: .destructive) {
                    showingClearContentIndexConfirmation = true
                }
            }

            Text(L10n.tr("Extension filters are managed in Content Settings."))
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var indexFilesSection: some View {
        SettingsSection(title: L10n.tr("Index File Info")) {
            VStack(alignment: .leading, spacing: 8) {
                Text(L10n.tr("Filename and Path Index Files"))
                    .font(.subheadline)
                    .foregroundColor(.secondary)

                LabeledValueRow(title: L10n.tr("Index File Path"), value: SearchViewModel.v6Path)
                LabeledValueRow(title: L10n.tr("Index File Size"), value: formattedBytes(mainIndexBaseBytes))
                LabeledValueRow(title: L10n.tr("WAL File Path"), value: SearchViewModel.walPath)
                LabeledValueRow(title: L10n.tr("WAL File Size"), value: formattedBytes(mainIndexWalBytes))
                LabeledValueRow(title: L10n.tr("Legacy Index Files"), value: formattedBytes(mainIndexLegacyBytes))
                LabeledValueRow(title: L10n.tr("Total Disk Usage"), value: formattedBytes(mainIndexStorageBytes))
                LabeledValueRow(title: L10n.tr("Indexed Files"), value: String(bridge.recordCount()))
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                Text(L10n.tr("Content Index Files"))
                    .font(.subheadline)
                    .foregroundColor(.secondary)

                LabeledValueRow(title: L10n.tr("Index File Path"), value: SearchViewModel.contentIndexPath)
                LabeledValueRow(title: L10n.tr("Index File Size"), value: formattedBytes(contentIndexBaseBytes))
                LabeledValueRow(title: L10n.tr("WAL File Path"), value: SearchViewModel.contentWalPath)
                LabeledValueRow(title: L10n.tr("WAL File Size"), value: formattedBytes(contentIndexWalBytes))
                LabeledValueRow(title: L10n.tr("Total Disk Usage"), value: formattedBytes(contentIndexStorageBytes))
                LabeledValueRow(title: L10n.tr("Indexed Files"), value: String(contentIndexedCount))
            }

            HStack {
                Button(L10n.tr("Refresh Index Info")) {
                    refreshContentIndexInfo()
                }
                Spacer()
                Button(L10n.tr("Content Settings...")) {
                    ContentSettingsWindowController.shared.showWindow()
                }
            }
        }
    }

    private func refreshContentIndexInfo() {
        let fileManager = FileManager.default
        mainIndexBaseBytes = fileSize(atPath: SearchViewModel.v6Path, fileManager: fileManager)
        mainIndexWalBytes = fileSize(atPath: SearchViewModel.walPath, fileManager: fileManager)
        mainIndexLegacyBytes =
            fileSize(atPath: SearchViewModel.cachePath, fileManager: fileManager) +
            fileSize(atPath: SearchViewModel.pagesPath, fileManager: fileManager) +
            fileSize(atPath: SearchViewModel.ptablePath, fileManager: fileManager)
        mainIndexStorageBytes = mainIndexBaseBytes + mainIndexWalBytes + mainIndexLegacyBytes

        let baseBytes = fileSize(atPath: SearchViewModel.contentIndexPath, fileManager: fileManager)
        let walBytes = fileSize(atPath: SearchViewModel.contentWalPath, fileManager: fileManager)
        contentIndexedCount = bridge.contentIndexedFileCount()
        contentIndexBaseBytes = baseBytes
        contentIndexWalBytes = walBytes
        contentIndexStorageBytes = baseBytes + walBytes
    }

    private var bridge: MacSearchBridge {
        MacSearchBridge.shared()
    }

    private func fileSize(atPath path: String, fileManager: FileManager) -> UInt64 {
        guard let attrs = try? fileManager.attributesOfItem(atPath: path),
              let size = attrs[.size] as? NSNumber else { return 0 }
        return size.uint64Value
    }


    private func formattedBytes(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
    }

    private func scheduleContentIndexInfoRefresh() {
        refreshContentIndexInfo()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            refreshContentIndexInfo()
        }
    }

    private var searchSection: some View {
        SettingsSection(title: L10n.tr("Search Options")) {
            Picker(L10n.tr("Startup Results"), selection: $settings.startupDisplayMode) {
                ForEach(StartupDisplayMode.allCases) { mode in
                    Text(mode.title).tag(mode)
                }
            }
            .pickerStyle(.segmented)

            Toggle(L10n.tr("Reset quick filter when no results are found"), isOn: $settings.autoResetQuickFilterOnEmptyResults)

            Toggle(L10n.tr("Search as you type"), isOn: $settings.searchAsYouType)

            Picker(L10n.tr("Enter Key Action"), selection: $settings.enterKeyAction) {
                ForEach(EnterKeyAction.allCases) { action in
                    Text(action.title).tag(action)
                }
            }
            .pickerStyle(.segmented)

            Toggle(L10n.tr("Pinyin Initials Search"), isOn: $settings.enablePinyinInitials)
            Toggle(L10n.tr("Accelerate Path Search"), isOn: $settings.enablePathSearchAcceleration)
            Text(L10n.tr("Disabling Accelerate Path Search can reduce memory usage."))
                .font(.caption)
                .foregroundColor(.secondary)
                .padding(.leading, 20)

            Toggle(L10n.tr("Use regular expressions (advanced)"), isOn: defaultRegexBinding)
            Text(L10n.tr("Regex uses patterns such as ^README, \\.(swift|mm|cpp)$, [0-9]{4}, and .* to match filenames more precisely."))
                .font(.caption)
                .foregroundColor(.secondary)
                .textSelection(.enabled)
                .padding(.leading, 20)
            HStack {
                Button(L10n.tr("Search Syntax Help...")) {
                    SearchSyntaxHelpWindowController.shared.showWindow()
                }
                Button(L10n.tr("Regular Expression Help...")) {
                    RegexHelpWindowController.shared.showWindow()
                }
            }
            Toggle(L10n.tr("Case Sensitive"), isOn: $settings.defaultCaseSensitive)
            Toggle(L10n.tr("Whole Word"), isOn: $settings.defaultWholeWord)
            Toggle(L10n.tr("Match Filename"), isOn: $settings.defaultMatchFilename)

            BoundedIntegerControl(
                title: L10n.tr("Maximum Results"),
                value: $settings.maxResults,
                range: 100...10_000,
                step: 100
            )

            Picker(L10n.tr("Sort Results By"), selection: $settings.sortField) {
                ForEach(SortField.allCases) { field in
                    Text(field.title).tag(field)
                }
            }
            Toggle(L10n.tr("Sort Ascending"), isOn: $settings.sortAscending)
                .disabled(settings.sortField == .relevance)
        }
    }

    private var defaultRegexBinding: Binding<Bool> {
        Binding(
            get: { settings.defaultRegex },
            set: { enabled in
                settings.defaultRegex = enabled
                if enabled {
                    RegexHelpWindowController.shared.showFirstUseIfNeeded()
                }
            }
        )
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
            Toggle(L10n.tr("Show extension"), isOn: $settings.showExtension)
            Toggle(L10n.tr("Show size"), isOn: $settings.showSize)
            Toggle(L10n.tr("Show modified date"), isOn: $settings.showModifiedDate)
            Toggle(L10n.tr("Show content snippets"), isOn: $settings.showContentSnippets)
        }
    }

    private var rowHeightIntBinding: Binding<Int> {
        Binding(
            get: { Int(settings.resultRowHeight) },
            set: { settings.resultRowHeight = CGFloat($0) }
        )
    }

    private var appearanceSection: some View {
        SettingsSection(title: L10n.tr("Appearance")) {
            Picker(L10n.tr("Theme"), selection: $settings.appearanceMode) {
                ForEach(AppearanceMode.allCases) { mode in
                    Text(mode.title).tag(mode)
                }
            }
            .pickerStyle(.segmented)

            GroupBox(L10n.tr("Colors")) {
                VStack(alignment: .leading, spacing: 10) {
                    Picker(L10n.tr("Edit Colors For"), selection: $colorEditorMode) {
                        ForEach(ColorEditorMode.allCases) { mode in
                            Text(mode.title).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)

                    HStack {
                        ColorPicker(L10n.tr("Background"), selection: editedBackgroundBinding, supportsOpacity: false)
                        Spacer()
                        ColorPicker(L10n.tr("Text Color"), selection: editedTextBinding, supportsOpacity: false)
                    }
                }
                .padding(.vertical, 4)
            }

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                Text(L10n.tr("Font Family"))
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Picker("", selection: $settings.fontFamily) {
                    Text(L10n.tr("System Default")).tag("")
                    ForEach(filteredFontFamilies, id: \.self) { family in
                        Text(family).tag(family)
                    }
                }
                .labelsHidden()
                TextField(L10n.tr("Search"), text: $fontSearchText)
                    .textFieldStyle(.roundedBorder)
            }

            HStack {
                Text(L10n.tr("Font Size"))
                Slider(value: $settings.fontSize, in: 10...24, step: 1)
                Text(String(format: "%.0f pt", settings.fontSize))
                    .frame(width: 48, alignment: .trailing)
            }

            Divider()

            VStack(alignment: .leading, spacing: 4) {
                Text(L10n.tr("Preview"))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Text(L10n.tr("The quick brown fox jumps over the lazy dog 0123456789"))
                    .font(theme.bodyFont)
                    .foregroundColor(theme.resolvedTextColor)
                    .padding(8)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(theme.resolvedBackgroundColor)
                    .cornerRadius(6)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
                    )
            }

            Divider()

            BoundedIntegerControl(
                title: L10n.tr("List Result Row Height"),
                value: rowHeightIntBinding,
                range: 20...120,
                step: 2
            )

            Toggle(L10n.tr("Show Thumbnails"), isOn: $settings.showThumbnails)
            Text(L10n.tr("Thumbnails may increase CPU, memory and disk access. Folders and applications always use system icons."))
                .font(.caption)
                .foregroundColor(.secondary)

            Divider()

            HStack {
                Text(L10n.tr("Thumbnail Size"))
                Slider(
                    value: settings.gridIconSizeLevelBinding,
                    in: 0...Double(AppSettings.gridIconSizes.count - 1),
                    step: 1
                )
                Text(String(format: "%.0f pt", settings.gridIconSize))
                    .frame(width: 48, alignment: .trailing)
            }
            Text(L10n.tr("Only applies in icon mode"))
                .font(.caption)
                .foregroundColor(.secondary)

            Divider()

            HStack {
                Spacer()
                Button(role: .destructive) {
                    showingResetAppearanceConfirmation = true
                } label: {
                    Text(L10n.tr("Reset Appearance Defaults"))
                }
            }
        }
    }

    private var filteredFontFamilies: [String] {
        let families = NSFontManager.shared.availableFontFamilies
        if fontSearchText.isEmpty { return families }
        return families.filter { $0.localizedCaseInsensitiveContains(fontSearchText) }
    }

    private var editedBackgroundBinding: Binding<Color> {
        colorEditorMode == .light ? lightBgBinding : darkBgBinding
    }

    private var editedTextBinding: Binding<Color> {
        colorEditorMode == .light ? lightTextBinding : darkTextBinding
    }

    private func selectActiveColorMode() {
        switch settings.appearanceMode {
        case .light: colorEditorMode = .light
        case .dark: colorEditorMode = .dark
        case .system: colorEditorMode = colorScheme == .dark ? .dark : .light
        }
    }

    private var lightBgBinding: Binding<Color> {
        Binding(
            get: { Color(rgbaString: settings.lightBackgroundColor) ?? Color(nsColor: .windowBackgroundColor) },
            set: { settings.lightBackgroundColor = $0.rgbaString ?? "" }
        )
    }

    private var lightTextBinding: Binding<Color> {
        Binding(
            get: { Color(rgbaString: settings.lightTextColor) ?? .black },
            set: { settings.lightTextColor = $0.rgbaString ?? "" }
        )
    }

    private var darkBgBinding: Binding<Color> {
        Binding(
            get: { Color(rgbaString: settings.darkBackgroundColor) ?? Color(nsColor: .windowBackgroundColor) },
            set: { settings.darkBackgroundColor = $0.rgbaString ?? "" }
        )
    }

    private var darkTextBinding: Binding<Color> {
        Binding(
            get: { Color(rgbaString: settings.darkTextColor) ?? .white },
            set: { settings.darkTextColor = $0.rgbaString ?? "" }
        )
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
            Text(L10n.tr("Dock visibility changes may require restarting MacEverything to fully take effect."))
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var shortcutSection: some View {
        SettingsSection(title: L10n.tr("Shortcuts")) {
            HStack {
                Text(L10n.tr("Configure the global shortcut used to show or hide MacEverything."))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Button(L10n.tr("Shortcut Settings...")) {
                    ShortcutSettingsWindowController.shared.showWindow()
                }
            }
        }
    }

    private var serviceSection: some View {
        SettingsSection(title: L10n.tr("HTTP API")) {
            Toggle(L10n.tr("Enable HTTP API"), isOn: $settings.httpServerEnabled)
                .onChange(of: settings.httpServerEnabled) {
                    SearchServiceModel.shared.applyRuntimeConfiguration()
                }
            BoundedIntegerControl(
                title: L10n.tr("Port"),
                value: $settings.httpPort,
                range: 1024...65535,
                step: 1
            )
            .disabled(!settings.httpServerEnabled)
            .onChange(of: settings.httpPort) {
                guard settings.httpServerEnabled else { return }
                SearchServiceModel.shared.applyRuntimeConfiguration()
            }
            VStack(alignment: .leading, spacing: 4) {
                Text(L10n.tr("HTTP API listens on local loopback only: %@", "http://127.0.0.1:\(settings.httpPort)"))
                Text(L10n.tr("Example: curl \"%@/api/search?q=readme&limit=10\"", "http://127.0.0.1:\(settings.httpPort)"))
                Text(L10n.tr("Useful endpoints: /api/search, /api/search/content, /api/recent, /api/status, /api/memory"))
            }
            .font(.caption)
            .foregroundColor(.secondary)
            .textSelection(.enabled)
            .disabled(!settings.httpServerEnabled)
        }
    }

    private var mcpSection: some View {
        SettingsSection(title: L10n.tr("MCP Integration")) {
            ForEach(MCPClient.allCases, id: \.self) { client in
                Toggle(client.displayName, isOn: Binding(
                    get: { mcpIntegration.isEnabled(client) },
                    set: { mcpIntegration.setEnabled($0, for: client) }
                ))
                .disabled(mcpIntegration.updating.contains(client))
                if let error = mcpIntegration.errors[client] {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                }
            }
            Text(L10n.tr("Enable MacEverything MCP integration for supported AI clients."))
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var cliSection: some View {
        SettingsSection(title: L10n.tr("Command-Line Client")) {
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text(cliStatusText)
                    Text(CLIInstallManager.installURL().path)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .textSelection(.enabled)
                }
                Spacer()
                Button(cliActionTitle) {
                    updateCLIInstallation()
                }
                if cliInstallStatus == .conflict {
                    Button(L10n.tr("Reveal")) {
                        NSWorkspace.shared.activateFileViewerSelecting([
                            CLIInstallManager.installURL()
                        ])
                    }
                }
            }

            if !CLIInstallManager.pathIsConfigured() {
                Text(L10n.tr("Add ~/.local/bin to PATH to run mace from a terminal."))
                    .font(.caption)
                    .foregroundColor(.orange)
            }
            if !cliInstallError.isEmpty {
                Text(cliInstallError)
                    .font(.caption)
                    .foregroundColor(.red)
            }
        }
    }

    private var cliStatusText: String {
        switch cliInstallStatus {
        case .notInstalled: return L10n.tr("mace is not installed")
        case .installed: return L10n.tr("mace is installed")
        case .stale: return L10n.tr("mace points to an older MacEverything app")
        case .conflict: return L10n.tr("Another file already exists at the command path")
        }
    }

    private var cliActionTitle: String {
        switch cliInstallStatus {
        case .installed: return L10n.tr("Uninstall")
        case .stale: return L10n.tr("Update")
        case .notInstalled, .conflict: return L10n.tr("Install")
        }
    }

    private func refreshCLIInstallStatus() {
        cliInstallStatus = CLIInstallManager.status()
    }

    private func updateCLIInstallation() {
        cliInstallError = ""
        do {
            if cliInstallStatus == .installed {
                try CLIInstallManager.uninstall()
            } else {
                try CLIInstallManager.install()
            }
        } catch {
            cliInstallError = error.localizedDescription
        }
        refreshCLIInstallStatus()
    }

    private var maintenanceSection: some View {
        SettingsSection(title: L10n.tr("Maintenance")) {
            Toggle(L10n.tr("Automatic index maintenance"), isOn: $settings.automaticMaintenanceEnabled)
            Button(L10n.tr("Rebuild Index")) {
                NotificationCenter.default.post(name: .rebuildIndex, object: nil)
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

private struct LabeledValueRow: View {
    let title: String
    let value: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(title)
                .foregroundColor(.secondary)
                .frame(width: 120, alignment: .leading)
            Text(value)
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .font(.callout)
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
                ForEach(sortedPaths, id: \.self) { path in
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

    private var sortedPaths: [String] {
        paths.sorted { lhs, rhs in
            lhs.localizedStandardCompare(rhs) == .orderedAscending
        }
    }

    private func addTypedPath() {
        let path = newPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !path.isEmpty else { return }
        paths = sorted(normalizedPaths(paths + [path]))
        newPath = ""
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            paths = sorted(normalizedPaths(paths + panel.urls.map(\.path)))
        }
    }

    private func sorted(_ values: [String]) -> [String] {
        values.sorted { lhs, rhs in
            lhs.localizedStandardCompare(rhs) == .orderedAscending
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
                ForEach(sortedItems, id: \.self) { item in
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

    private var sortedItems: [String] {
        items.sorted { lhs, rhs in
            lhs.localizedStandardCompare(rhs) == .orderedAscending
        }
    }

    private func addItem() {
        let item = newItem.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !item.isEmpty, !items.contains(item) else { return }
        items = (items + [item]).sorted { lhs, rhs in
            lhs.localizedStandardCompare(rhs) == .orderedAscending
        }
        newItem = ""
    }
}

private struct BoundedIntegerControl: View {
    let title: String
    @Binding var value: Int
    let range: ClosedRange<Int>
    let step: Int

    @State private var text: String = ""
    @FocusState private var isFocused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Text(title)
                .frame(minWidth: 120, alignment: .leading)
            TextField("", text: $text)
                .textFieldStyle(.roundedBorder)
                .frame(width: 96)
                .focused($isFocused)
                .onSubmit(commitText)
                .onChange(of: isFocused) {
                    if !isFocused {
                        commitText()
                    }
                }
                .onChange(of: value) {
                    guard !isFocused else { return }
                    text = String(value)
                }
            Stepper("", value: $value, in: range, step: step)
                .labelsHidden()
            Text("\(range.lowerBound)-\(range.upperBound)")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .onAppear {
            text = String(value)
        }
    }

    private func commitText() {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let parsed = Int(trimmed) ?? value
        value = min(max(parsed, range.lowerBound), range.upperBound)
        text = String(value)
    }
}
