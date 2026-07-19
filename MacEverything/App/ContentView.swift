import SwiftUI
import Quartz

struct ContentView: View {
    @StateObject private var viewModel = SearchViewModel()
    @StateObject private var columnLayout = ResultColumnLayout()
    @ObservedObject private var service = SearchServiceModel.shared
    @ObservedObject private var searchOptions = SearchOptions.shared
    @ObservedObject private var settings = AppSettings.shared
    @EnvironmentObject private var theme: ThemeManager
    @State private var scrollViewID = 0
    @State private var hostWindow: NSWindow?
    @FocusState private var isSearchFieldFocused: Bool
    @State private var resultListFocused = false
    @State private var showPathFilter = false
    @State private var searchFieldFocusRequest = 0
    @State private var gridWidth: CGFloat = 800

    var body: some View {
        VStack(spacing: 0) {
            WindowReader { window in
                hostWindow = window
                viewModel.registerActiveWindow(window)
                if SearchWindowSupport.isSearchWindow(window) {
                    window.delegate = NSApp.delegate as? NSWindowDelegate
                }
                updateSearchWindowTitle()
            }
            .frame(width: 0, height: 0)

            // Permission banner
            PermissionView()

            // Search bar (Alfred-style)
            HStack(spacing: 12) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 26, weight: .medium))
                    .foregroundColor(.blue)
                HighlightedSearchField(
                    text: $viewModel.searchText,
                    placeholder: settings.contentIndexingEnabled
                        ? L10n.tr("Search filenames (Chinese initials supported; Cmd+N for new tab; infile: for content)")
                        : L10n.tr("Search filenames (Chinese initials supported; Cmd+N for new tab)"),
                    ghostSuggestion: viewModel.ghostSuggestion,
                    isFocused: $isSearchFieldFocused,
                    onTab: {
                        if viewModel.ghostSuggestion != nil {
                            viewModel.acceptGhostSuggestion()
                            return true
                        }
                        return false
                    },
                    onSubmit: {
                        viewModel.submitSearch()
                    },
                    onF2: {
                        viewModel.requestRenameForSelected()
                    },
                    onCmdDelete: {
                        viewModel.deleteSelectedFile()
                    },
                    onArrowDown: {
                        guard !viewModel.isContentSearch, !viewModel.displayItems.isEmpty else { return false }
                        resultListFocused = true
                        return true
                    },
                    onEscape: {
                        handleEscape()
                    },
                    focusRequest: searchFieldFocusRequest
                )
                .frame(height: 36)
                .onChange(of: viewModel.searchText) {
                    viewModel.onSearchTextChanged()
                    updateSearchWindowTitle()
                }
                SearchOptionBadges(options: searchOptions)
                Button {
                    NSApp.activate(ignoringOtherApps: true)
                    GeneralSettingsWindowController.shared.showWindow()
                } label: {
                    Image(systemName: "gearshape")
                        .font(.system(size: 17))
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
                .help(L10n.tr("Settings..."))
                .accessibilityLabel(L10n.tr("Settings..."))
                if !viewModel.searchText.isEmpty {
                    Button {
                        viewModel.searchText = ""
                        // H-8: .onChange(of: searchText) will trigger onSearchTextChanged() automatically
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .font(.system(size: 18))
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("clearButton")
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 16)
            .background(.ultraThinMaterial)
            .cornerRadius(10)
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(Color.blue, lineWidth: 2)
            )
            .padding(.horizontal, 8)
            .padding(.top, 8)

            if !viewModel.isContentSearch {
                QuickFilterBar(
                    quickFilter: $viewModel.quickFilter,
                    pathFilter: $viewModel.pathFilter,
                    showPathFilter: $showPathFilter,
                    displayControlsDisabled: service.isScanning || viewModel.displayItems.isEmpty
                )
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
            }

            Divider()

            // Results list
            if service.isScanning {
                VStack(spacing: 12) {
                    Spacer()
                    ProgressView()
                        .controlSize(.large)
                    Text(L10n.tr("Indexing files... %d items scanned", Int(service.scannedCount)))
                        .font(.callout)
                        .foregroundColor(.secondary)
                    Spacer()
                }
            } else if viewModel.isContentSearch {
                // Content search results
                if viewModel.contentSearchUnavailable {
                    VStack(spacing: 8) {
                        Spacer()
                        Image(systemName: "text.magnifyingglass")
                            .font(.system(size: 36))
                            .foregroundColor(.secondary.opacity(0.5))
                        Text(L10n.tr("Content indexing is disabled"))
                            .font(.headline)
                        Text(L10n.tr("Enable text content indexing in Settings to use content search."))
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Spacer()
                    }
                } else if viewModel.contentResults.isEmpty && !viewModel.contentKeyword.isEmpty && service.scanComplete {
                    VStack(spacing: 8) {
                        Spacer()
                        if service.isContentIndexing {
                            ProgressView()
                                .controlSize(.large)
                                .padding(.bottom, 4)
                            Text(L10n.tr("Content index is building..."))
                                .font(.headline)
                                .foregroundColor(.orange)
                            if let progress = service.contentIndexProgress {
                                Text(L10n.tr("Indexed %d / %d files", Int(progress.indexed), Int(progress.total)))
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                            }
                            Text(L10n.tr("Search results will appear after indexing completes"))
                                .font(.caption)
                                .foregroundColor(.secondary)
                        } else {
                            Image(systemName: "doc.text.magnifyingglass")
                                .font(.system(size: 36))
                                .foregroundColor(.secondary.opacity(0.5))
                                .padding(.bottom, 4)
                            Text(L10n.tr("No content matches found"))
                                .foregroundColor(.secondary)
                            if service.contentIndexedCount == 0 {
                                Text(L10n.tr("No files indexed. Configure extensions in Content Settings."))
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                        Spacer()
                    }
                } else if viewModel.contentKeyword.isEmpty {
                    VStack {
                        Spacer()
                        Text(L10n.tr("Type a keyword after infile: to search file contents"))
                            .foregroundColor(.secondary)
                        Spacer()
                    }
                } else {
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(viewModel.contentResults) { item in
                                ContentResultRow(item: item, hints: viewModel.highlightHints,
                                    onDelete: {
                                        viewModel.removeContentItem(id: item.id)
                                    })
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 2)
                            }
                        }
                    }
                    .id(scrollViewID)
                    .accessibilityIdentifier("contentResultsList")
                }
            } else if viewModel.displayItems.isEmpty && !viewModel.searchText.isEmpty && service.scanComplete {
                VStack {
                    Spacer()
                    Text(L10n.tr("No results found"))
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("noResultsLabel")
                    Spacer()
                }
            } else {
                if viewModel.showingRecent && !viewModel.displayItems.isEmpty {
                    HStack {
                        HStack(spacing: 4) {
                            Image(systemName: "clock")
                            Text(L10n.tr("Recent Files"))
                                .font(.callout)
                                .fontWeight(.medium)
                        }
                        .foregroundColor(.white)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(
                            RoundedRectangle(cornerRadius: 5)
                                .fill(Color.orange)
                        )
                        Spacer()
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 4)
                }

                if !viewModel.displayItems.isEmpty {
                    if settings.resultDisplayMode == .list {
                        ResultHeaderView(viewModel: viewModel)
                            .environmentObject(columnLayout)
                            .padding(.horizontal, 8)
                    }
                }

                ScrollViewReader { scrollProxy in
                    let selectedItems = viewModel.selectedItemsInDisplayOrder()
                    ScrollView {
                        if settings.resultDisplayMode == .list {
                        LazyVStack(spacing: 0) {
                            ForEach(viewModel.displayItems) { item in
                                ResultRow(
                                    item: item,
                                    hints: viewModel.highlightHints,
                                    isSelected: viewModel.selectedItemIDs.contains(item.id),
                                    requestedRename: viewModel.renameRequestedItemID == item.id,
                                    selectedItems: selectedItems,
                                    onSelect: { extending, toggling in
                                        viewModel.select(item, extending: extending, toggling: toggling)
                                        resultListFocused = true
                                        isSearchFieldFocused = false
                                    },
                                    onRenameComplete: {
                                        viewModel.renameRequestedItemID = nil
                                    },
                                    onRenameSuccess: { oldID, newName in
                                        viewModel.updateItemName(oldID: oldID, newName: newName)
                                    },
                                    onDeleteItems: { ids in
                                        for id in ids {
                                            viewModel.removeItemFromResults(id: id)
                                        }
                                    }
                                )
                                    .environmentObject(columnLayout)
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 2)
                                    .id(item.id)
                            }
                            if viewModel.hasMoreResults {
                                LoadingMoreResultsFooter(onAppear: viewModel.loadMore)
                            }
                        }
                        } else {
                        let cellWidth = settings.gridIconSize + 24
                        let columns = max(1, Int(floor(max(1, gridWidth - 16) / cellWidth)))
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: columns), spacing: 8) {
                            ForEach(viewModel.displayItems) { item in
                                ResultGridCell(
                                    item: item,
                                    hints: viewModel.highlightHints,
                                    isSelected: viewModel.selectedItemIDs.contains(item.id),
                                    selectedItems: selectedItems,
                                    onSelect: { extending, toggling in
                                        viewModel.select(item, extending: extending, toggling: toggling)
                                        resultListFocused = true
                                        isSearchFieldFocused = false
                                    },
                                    onDeleteItems: { ids in
                                        for id in ids {
                                            viewModel.removeItemFromResults(id: id)
                                        }
                                    }
                                )
                                .id(item.id)
                            }
                        }
                        .padding(.horizontal, 8)
                        .padding(.vertical, 8)
                        .background(
                            GeometryReader { proxy in
                                Color.clear
                                    .onAppear { gridWidth = proxy.size.width }
                                    .onChange(of: proxy.size.width) { gridWidth = proxy.size.width }
                            }
                        )
                        .onChange(of: columns) { viewModel.gridColumnCount = columns }
                        .onAppear { viewModel.gridColumnCount = columns }
                        if viewModel.hasMoreResults {
                            LoadingMoreResultsFooter(onAppear: viewModel.loadMore)
                        }
                        }
                    }
                    .id(scrollViewID)
                    .accessibilityIdentifier("fileResultsList")
                    .animation(.easeInOut(duration: 0.25), value: viewModel.showingRecent)
                    .background(
                        ResultListKeyHandler(
                            viewModel: viewModel,
                            wantsFocus: $resultListFocused,
                            onReturnFocusToSearch: {
                                resultListFocused = false
                                isSearchFieldFocused = true
                            },
                            onEscape: {
                                handleEscape()
                            },
                            onForwardCharacter: { chars in
                                resultListFocused = false
                                viewModel.searchText += chars
                                isSearchFieldFocused = true
                            }
                        )
                        .frame(width: 0, height: 0)
                    )
                    .onChange(of: viewModel.scrollTargetItemID) {
                        if let id = viewModel.scrollTargetItemID {
                            scrollProxy.scrollTo(id, anchor: .center)
                        }
                    }
                }
            }

            Divider()
            SearchStatusBar(viewModel: viewModel)
        }
        .frame(minWidth: 600, minHeight: 400)
        .background(theme.resolvedBackgroundColor)
        .onAppear {
            updateSearchWindowTitle()
        }
        .onDisappear {
            if let window = hostWindow {
                viewModel.unregisterActiveWindow(window)
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .clearContentIndex)) { _ in
            viewModel.clearContentResults()
        }
        .onReceive(NotificationCenter.default.publisher(for: .searchServiceDidRefresh)) { _ in
            viewModel.refreshForServiceUpdate()
        }
        .onReceive(NotificationCenter.default.publisher(for: .searchWindowDidRestore)) { notification in
            guard let window = notification.object as? NSWindow,
                  hostWindow === window else { return }
            restoreSearchFocus()
        }
        .onChange(of: viewModel.quickFilter) {
            viewModel.onQuickFilterChanged()
        }
        .onChange(of: viewModel.pathFilter) {
            viewModel.onPathFilterChanged()
        }
        .onChange(of: viewModel.advancedFilters) {
            viewModel.onAdvancedFiltersChanged()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(true)
            restoreSearchFocus()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(false)
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didDeminiaturizeNotification)) { _ in
            restoreSearchInteraction()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { notification in
            guard let window = notification.object as? NSWindow,
                  hostWindow === window else { return }
            restoreSearchFocus()
        }
    }

    private func restoreSearchInteraction() {
        scrollViewID += 1
        restoreSearchFocus()
        viewModel.refreshAfterWindowRestore()
    }

    private func restoreSearchFocus() {
        resultListFocused = false
        isSearchFieldFocused = false
        searchFieldFocusRequest += 1
        isSearchFieldFocused = true
    }

    private func updateSearchWindowTitle() {
        guard let window = hostWindow,
              SearchWindowSupport.isSearchWindow(window) else { return }
        SearchWindowSupport.updateTitle(window, searchText: viewModel.searchText)
    }

    private func handleEscape() {
        if QLPreviewPanel.sharedPreviewPanelExists() && QLPreviewPanel.shared().isVisible {
            QLPreviewPanel.shared().orderOut(nil)
            if resultListFocused {
                resultListFocused = false
                isSearchFieldFocused = true
            }
            return
        }
        if resultListFocused {
            resultListFocused = false
            isSearchFieldFocused = true
            return
        }
        if !viewModel.searchText.isEmpty {
            viewModel.searchText = ""
            return
        }
        NSApp.hide(nil)
    }
}

private struct SearchStatusBar: View {
    @ObservedObject var viewModel: SearchViewModel
    @ObservedObject private var service = SearchServiceModel.shared
    @State private var showingAdvancedFilters = false

    var body: some View {
        HStack(spacing: 8) {
            HStack(spacing: 8) {
                if service.isScanning {
                    ProgressView()
                        .controlSize(.small)
                    Text(L10n.tr("Scanning... %d items scanned", Int(service.scannedCount)))
                        .foregroundColor(.secondary)
                } else if service.scanComplete {
                    if service.isSyncing {
                        ProgressView()
                            .controlSize(.small)
                        Text(L10n.tr("Syncing..."))
                            .foregroundColor(.orange)
                    } else if service.isMonitoring {
                        Circle()
                            .fill(.green)
                            .frame(width: 6, height: 6)
                        Text(L10n.tr("Live"))
                            .foregroundColor(.green)
                            .fontWeight(.medium)
                    }

                    Text(L10n.tr("%d files indexed (memory %@)",
                                 Int(service.totalRecords),
                                 formattedIndexMemory(service.indexMemoryBytes)))
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("indexedCount")

                    if service.isContentIndexing, let progress = service.contentIndexProgress {
                        separator
                        ProgressView()
                            .controlSize(.small)
                        Text(L10n.tr("Content indexing %d/%d", Int(progress.indexed), Int(progress.total)))
                            .foregroundColor(.orange)
                    }

                    if viewModel.totalMatches > 0 {
                        separator
                        resultSummary
                            .foregroundColor(.secondary)
                            .accessibilityIdentifier("matchCount")
                        separator
                        Text(String(format: "%.1fms", viewModel.queryTimeMs))
                            .foregroundColor(.secondary)
                    }
                }
            }
            .lineLimit(1)
            .layoutPriority(1)

            Spacer(minLength: 8)

            HStack(spacing: 4) {
                Text(L10n.tr("Advanced Filters"))
                    .foregroundColor((!service.scanComplete || viewModel.isContentSearch)
                                     ? Color.secondary.opacity(0.5)
                                     : Color.secondary)
                    .lineLimit(1)

                Button {
                    showingAdvancedFilters.toggle()
                } label: {
                    HStack(spacing: 3) {
                        Image(systemName: viewModel.advancedFilters.activeFilterCount > 0
                              ? "line.3.horizontal.decrease.circle.fill"
                              : "line.3.horizontal.decrease.circle")
                        if viewModel.advancedFilters.activeFilterCount > 0 {
                            Text("\(viewModel.advancedFilters.activeFilterCount)")
                                .font(.caption2.monospacedDigit())
                        }
                    }
                    .foregroundColor(viewModel.advancedFilters.activeFilterCount > 0 ? .accentColor : .secondary)
                    .frame(width: 36, height: 22)
                }
                .buttonStyle(.borderless)
                .help(L10n.tr("File Size and Modified Date Filters"))
                .accessibilityLabel(L10n.tr("File Size and Modified Date Filters"))
                .accessibilityValue(Text(L10n.tr("%d active filters", viewModel.advancedFilters.activeFilterCount)))
                .popover(isPresented: $showingAdvancedFilters, arrowEdge: .bottom) {
                    AdvancedFilterPopover(viewModel: viewModel)
                }
            }
            .disabled(!service.scanComplete || viewModel.isContentSearch)
            .fixedSize(horizontal: true, vertical: false)
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .frame(minHeight: 30)
        .background(Color(nsColor: .controlBackgroundColor))
        .accessibilityIdentifier("statusBar")
    }

    @ViewBuilder
    private var resultSummary: some View {
        if viewModel.isContentSearch {
            Text(viewModel.resultLimitReached
                 ? L10n.tr("More than %d results", viewModel.effectiveMaxResults)
                 : L10n.tr("%d content matches", viewModel.contentResults.count))
        } else {
            Text(viewModel.resultLimitReached
                 ? L10n.tr("Result %d, total results limited to %d; add search terms.",
                           viewModel.displayItems.count, viewModel.effectiveMaxResults)
                 : L10n.tr("Showing %d of %d results", viewModel.displayItems.count, viewModel.totalMatches))
        }
    }

    private var separator: some View {
        Text("·").foregroundColor(.secondary)
    }

    private func formattedIndexMemory(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .memory)
    }
}

private struct AdvancedFilterPopover: View {
    @ObservedObject var viewModel: SearchViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(L10n.tr("Advanced Filters"))
                .font(.headline)

            filterPicker(
                title: L10n.tr("File Size"),
                selection: $viewModel.advancedFilters.fileSize,
                options: AdvancedFileSizeFilter.allCases
            )

            if viewModel.advancedFilters.fileSize == .custom {
                HStack(spacing: 8) {
                    Text(L10n.tr("From"))
                    TextField("", value: $viewModel.advancedFilters.customMinimumSizeMB, format: .number)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 64)
                    Text("MB")
                        .foregroundColor(.secondary)
                    Text(L10n.tr("To"))
                    TextField("", value: $viewModel.advancedFilters.customMaximumSizeMB, format: .number)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 64)
                    Text("MB")
                        .foregroundColor(.secondary)
                }
            }

            Divider()

            filterPicker(
                title: L10n.tr("Modified Date"),
                selection: $viewModel.advancedFilters.modifiedDate,
                options: AdvancedModifiedDateFilter.allCases
            )

            if viewModel.advancedFilters.modifiedDate == .custom {
                VStack(alignment: .leading, spacing: 8) {
                    DatePicker(L10n.tr("From"),
                               selection: $viewModel.advancedFilters.customModifiedFrom,
                               displayedComponents: .date)
                    DatePicker(L10n.tr("To"),
                               selection: $viewModel.advancedFilters.customModifiedTo,
                               displayedComponents: .date)
                }
            }

            Divider()

            HStack {
                Spacer()
                Button(L10n.tr("Clear Filters")) {
                    viewModel.clearAdvancedFilters()
                }
                .disabled(viewModel.advancedFilters.activeFilterCount == 0)
            }
        }
        .padding(14)
        .frame(width: 320)
    }

    private func filterPicker<Option: Hashable & Identifiable>(
        title: String,
        selection: Binding<Option>,
        options: [Option]
    ) -> some View where Option.ID == String {
        HStack {
            Text(title)
            Spacer()
            Picker("", selection: selection) {
                ForEach(options) { option in
                    Text(optionTitle(option)).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .frame(width: 160)
        }
    }

    private func optionTitle<Option>(_ option: Option) -> String {
        if let size = option as? AdvancedFileSizeFilter { return size.title }
        if let date = option as? AdvancedModifiedDateFilter { return date.title }
        return ""
    }
}

private extension AdvancedFileSizeFilter {
    var title: String {
        switch self {
        case .any: return L10n.tr("Any Size")
        case .underOneMB: return L10n.tr("Under 1 MB")
        case .oneToHundredMB: return L10n.tr("1–100 MB")
        case .overHundredMB: return L10n.tr("Over 100 MB")
        case .custom: return L10n.tr("Custom...")
        }
    }
}

private extension AdvancedModifiedDateFilter {
    var title: String {
        switch self {
        case .any: return L10n.tr("Any Date")
        case .today: return L10n.tr("Today")
        case .lastSevenDays: return L10n.tr("Last 7 Days")
        case .lastThirtyDays: return L10n.tr("Last 30 Days")
        case .custom: return L10n.tr("Custom...")
        }
    }
}

private struct WindowReader: NSViewRepresentable {
    var onWindowChange: (NSWindow) -> Void

    final class Coordinator {
        weak var lastWindow: NSWindow?
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> NSView {
        let view = NSView(frame: .zero)
        DispatchQueue.main.async {
            if let window = view.window, context.coordinator.lastWindow !== window {
                context.coordinator.lastWindow = window
                onWindowChange(window)
            }
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        DispatchQueue.main.async {
            if let window = nsView.window, context.coordinator.lastWindow !== window {
                context.coordinator.lastWindow = window
                onWindowChange(window)
            }
        }
    }
}

private struct ResultDisplayModeSwitcher: View {
    @ObservedObject private var settings = AppSettings.shared

    var body: some View {
        Picker(L10n.tr("Display Mode"), selection: $settings.resultDisplayMode) {
            Image(systemName: "list.bullet").tag(ResultDisplayMode.list)
            Image(systemName: "square.grid.2x2").tag(ResultDisplayMode.grid)
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        .help(L10n.tr("Display Mode"))
        .accessibilityIdentifier("resultDisplayModeSwitcher")
    }
}

private struct ResultDisplayControls: View {
    let isDisabled: Bool
    @ObservedObject private var settings = AppSettings.shared

    var body: some View {
        HStack(spacing: 8) {
            ResultDisplayModeSwitcher()

            Divider()
                .frame(height: 18)
            Image(systemName: "square.grid.3x3")
                .foregroundColor(.secondary)
                .accessibilityHidden(true)
            Slider(
                value: settings.gridIconSizeLevelBinding,
                in: 0...Double(AppSettings.gridIconSizes.count - 1),
                step: 1
            )
                .controlSize(.small)
                .frame(width: 80)
                .help(L10n.tr("Thumbnail Size"))
                .accessibilityLabel(L10n.tr("Thumbnail Size"))
                .accessibilityValue(Text("\(Int(settings.gridIconSize)) pt"))
                .accessibilityIdentifier("gridIconSizeSlider")
                .disabled(settings.resultDisplayMode != .grid)
            Image(systemName: "square.grid.2x2")
                .foregroundColor(.secondary)
                .accessibilityHidden(true)
        }
        .disabled(isDisabled)
    }
}

private struct LoadingMoreResultsFooter: View {
    let onAppear: () -> Void

    var body: some View {
        HStack {
            Spacer()
            ProgressView()
                .controlSize(.small)
            Text(L10n.tr("Loading more results..."))
                .font(.callout)
                .foregroundColor(.secondary)
            Spacer()
        }
        .padding(.vertical, 8)
        .onAppear(perform: onAppear)
    }
}

private struct ResultHeaderView: View {
    @ObservedObject var viewModel: SearchViewModel
    @ObservedObject private var settings = AppSettings.shared
    @EnvironmentObject private var columnLayout: ResultColumnLayout

    var body: some View {
        GeometryReader { proxy in
            let widths = columnLayout.resolvedWidths(
                showPath: settings.showPath,
                showExtension: settings.showExtension,
                showSize: settings.showSize,
                showModifiedDate: settings.showModifiedDate,
                availableWidth: proxy.size.width
            )

            HStack(spacing: 0) {
                columnButton(L10n.tr("Name"), field: .name, width: widths.name)
                    .resizableColumn(width: $columnLayout.nameWidth,
                                     range: ResultColumnLayout.nameWidthRange)

                if settings.showPath {
                    columnSeparator
                    columnButton(L10n.tr("Path"), field: .path, width: widths.path)
                        .resizableColumn(width: $columnLayout.pathWidth,
                                         range: ResultColumnLayout.pathWidthRange)
                }

                if settings.showExtension {
                    columnSeparator
                    columnButton(L10n.tr("Ext"), field: .ext, width: widths.ext)
                        .resizableColumn(width: $columnLayout.extWidth,
                                         range: ResultColumnLayout.extWidthRange)
                }

                if settings.showSize {
                    columnSeparator
                    columnButton(L10n.tr("Size"), field: .size, width: widths.size, alignment: .trailing)
                        .resizableColumn(width: $columnLayout.sizeWidth,
                                         range: ResultColumnLayout.sizeWidthRange)
                }

                if settings.showModifiedDate {
                    columnSeparator
                    columnButton(L10n.tr("Modified Date"), field: .modified, width: widths.modified)
                        .resizableColumn(width: $columnLayout.modifiedWidth,
                                         range: ResultColumnLayout.modifiedWidthRange)
                }
            }
            .padding(.horizontal, 6)
        }
        .frame(height: 22)
        .background(Color(nsColor: .controlBackgroundColor))
        .overlay(alignment: .bottom) {
            Rectangle()
                .fill(Color(nsColor: .separatorColor).opacity(0.6))
                .frame(height: 1)
        }
    }

    private var columnSeparator: some View {
        ZStack {
            Rectangle()
                .fill(Color(nsColor: .separatorColor).opacity(0.55))
                .frame(width: 1)
                .padding(.vertical, 3)
        }
        .frame(width: 10)
    }

    @ViewBuilder
    private func columnButton(
        _ title: String,
        field: SortField,
        width: CGFloat? = nil,
        alignment: Alignment = .leading
    ) -> some View {
        Button {
            viewModel.sortBy(field)
        } label: {
            HStack(spacing: 4) {
                Text(title)
                    .lineLimit(1)
                if settings.sortField == field {
                    Image(systemName: settings.sortAscending ? "chevron.up" : "chevron.down")
                        .font(.caption2)
                }
            }
            .font(.caption)
            .foregroundColor(.secondary)
            .padding(.horizontal, 5)
            .frame(width: width, alignment: alignment)
            .frame(maxWidth: width == nil ? .infinity : nil, alignment: alignment)
            .frame(height: 22, alignment: alignment)
            .background(settings.sortField == field ? Color.accentColor.opacity(0.10) : Color.clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

private struct QuickFilterBar: View {
    @Binding var quickFilter: QuickFilter
    @Binding var pathFilter: String
    @Binding var showPathFilter: Bool
    let displayControlsDisabled: Bool

    var body: some View {
        GeometryReader { proxy in
            let showExpandedPathFilter = proxy.size.width >= 980 || showPathFilter || !pathFilter.isEmpty
            let quickFilterWidth = min(270, max(200, proxy.size.width - 600))

            HStack(spacing: 8) {
                Text(L10n.tr("Quick filter"))
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .lineLimit(1)

                Picker("", selection: $quickFilter) {
                    ForEach(QuickFilter.allCases) { filter in
                        Text(filter.title).tag(filter)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .controlSize(.small)
                .frame(width: quickFilterWidth)
                .layoutPriority(1)
                .help(L10n.tr("Quick filters"))

                Button {
                    showPathFilter.toggle()
                } label: {
                    Image(systemName: showExpandedPathFilter ? "folder.fill" : "folder")
                        .font(.system(size: 14, weight: .medium))
                }
                .buttonStyle(.borderless)
                .help(L10n.tr("Path filter"))

                if showExpandedPathFilter {
                    HStack(spacing: 6) {
                        Image(systemName: "line.3.horizontal.decrease.circle")
                            .foregroundColor(.secondary)
                        TextField(L10n.tr("Filter by path..."), text: $pathFilter)
                            .textFieldStyle(.plain)
                        if !pathFilter.isEmpty {
                            Button {
                                pathFilter = ""
                            } label: {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundColor(.secondary)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .font(.callout)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 5)
                    .background(
                        RoundedRectangle(cornerRadius: 6)
                            .fill(Color(nsColor: .controlBackgroundColor))
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .stroke(Color(nsColor: .separatorColor).opacity(0.7))
                    )
                    .frame(minWidth: 120, idealWidth: 220, maxWidth: 300)
                }

                Spacer(minLength: 8)

                Text(L10n.tr("Display Style"))
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .lineLimit(1)

                ResultDisplayControls(isDisabled: displayControlsDisabled)
                    .fixedSize(horizontal: true, vertical: false)
                    .layoutPriority(2)
            }
            .frame(width: proxy.size.width, height: proxy.size.height, alignment: .center)
        }
        .frame(height: 32)
    }
}

private struct ColumnResizeHandle: ViewModifier {
    @Binding var width: CGFloat
    let range: ClosedRange<CGFloat>
    @State private var dragStartWidth: CGFloat?
    @State private var isCursorPushed = false

    func body(content: Content) -> some View {
        content.overlay(alignment: .trailing) {
            Rectangle()
                .fill(Color.clear)
                .frame(width: 8)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            let start = dragStartWidth ?? width
                            dragStartWidth = start
                            width = min(max(start + value.translation.width, range.lowerBound), range.upperBound)
                        }
                        .onEnded { _ in
                            dragStartWidth = nil
                        }
                )
                .onHover { hovering in
                    if hovering, !isCursorPushed {
                        NSCursor.resizeLeftRight.push()
                        isCursorPushed = true
                    } else if !hovering, isCursorPushed {
                        NSCursor.pop()
                        isCursorPushed = false
                    }
                }
                .onDisappear {
                    if isCursorPushed {
                        NSCursor.pop()
                        isCursorPushed = false
                    }
                }
        }
    }
}

private extension View {
    func resizableColumn(width: Binding<CGFloat>, range: ClosedRange<CGFloat>) -> some View {
        modifier(ColumnResizeHandle(width: width, range: range))
    }
}
