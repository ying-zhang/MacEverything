import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = SearchViewModel()
    @ObservedObject private var searchOptions = SearchOptions.shared
    @State private var scrollViewID = 0
    @FocusState private var isSearchFieldFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            // Permission banner
            PermissionView()

            // Search bar (Alfred-style)
            HStack(spacing: 12) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 26, weight: .medium))
                    .foregroundColor(.blue)
                HighlightedSearchField(
                    text: $viewModel.searchText,
                    placeholder: L10n.tr("Search files... (infile: for content search)"),
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
                    }
                )
                .frame(height: 36)
                .onChange(of: viewModel.searchText) {
                    viewModel.onSearchTextChanged()
                }
                SearchOptionBadges(options: searchOptions)
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

            Divider()

            // Status bar
            HStack {
                if viewModel.isScanning {
                    ProgressView()
                        .controlSize(.small)
                    Text(L10n.tr("Scanning... %d items scanned", Int(viewModel.scannedCount)))
                        .foregroundColor(.secondary)
                } else if viewModel.scanComplete {
                    if viewModel.isSyncing {
                        ProgressView()
                            .controlSize(.small)
                        Text(L10n.tr("Syncing..."))
                            .foregroundColor(.orange)
                    } else if viewModel.isMonitoring {
                        Circle()
                            .fill(.green)
                            .frame(width: 6, height: 6)
                        Text(L10n.tr("Live"))
                            .foregroundColor(.green)
                            .fontWeight(.medium)
                    }
                    Text(L10n.tr("%d files indexed", Int(viewModel.totalRecords)))
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("indexedCount")
                    if viewModel.isContentIndexing, let progress = viewModel.contentIndexProgress {
                        Text("·")
                            .foregroundColor(.secondary)
                        ProgressView()
                            .controlSize(.small)
                        Text(L10n.tr("Content indexing %d/%d", Int(progress.indexed), Int(progress.total)))
                            .foregroundColor(.orange)
                    }
                    if viewModel.totalMatches > 0 {
                        Text("·")
                            .foregroundColor(.secondary)
                        Text(L10n.tr("%d matches", viewModel.totalMatches))
                            .foregroundColor(.secondary)
                            .accessibilityIdentifier("matchCount")
                        Text("·")
                            .foregroundColor(.secondary)
                        Text(String(format: "%.1fms", viewModel.queryTimeMs))
                            .foregroundColor(.secondary)
                    }
                }
                Spacer()
            }
            .font(.callout)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Color(nsColor: .controlBackgroundColor))
            .accessibilityIdentifier("statusBar")

            Divider()

            // Results list
            if viewModel.isScanning {
                VStack(spacing: 12) {
                    Spacer()
                    ProgressView()
                        .controlSize(.large)
                    Text(L10n.tr("Indexing files... %d items scanned", Int(viewModel.scannedCount)))
                        .font(.callout)
                        .foregroundColor(.secondary)
                    Spacer()
                }
            } else if viewModel.isContentSearch {
                // Content search results
                if viewModel.contentResults.isEmpty && !viewModel.contentKeyword.isEmpty && viewModel.scanComplete {
                    VStack(spacing: 8) {
                        Spacer()
                        if viewModel.isContentIndexing {
                            ProgressView()
                                .controlSize(.large)
                                .padding(.bottom, 4)
                            Text(L10n.tr("Content index is building..."))
                                .font(.headline)
                                .foregroundColor(.orange)
                            if let progress = viewModel.contentIndexProgress {
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
                            if viewModel.contentIndexedCount == 0 {
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
                                ContentResultRow(item: item, keyword: viewModel.contentKeyword)
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 2)
                            }
                        }
                    }
                    .id(scrollViewID)
                    .accessibilityIdentifier("contentResultsList")

                    if viewModel.totalMatches > 0 {
                        HStack {
                            Spacer()
                            Text(L10n.tr("%d content matches", viewModel.contentResults.count))
                                .font(.callout)
                                .foregroundColor(.secondary)
                                .padding(8)
                            Spacer()
                        }
                        .background(Color(nsColor: .controlBackgroundColor))
                    }
                }
            } else if viewModel.displayItems.isEmpty && !viewModel.searchText.isEmpty && viewModel.scanComplete {
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
                    ResultHeaderView(viewModel: viewModel)
                        .padding(.horizontal, 8)
                }

                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(viewModel.displayItems) { item in
                            ResultRow(
                                item: item,
                                hints: viewModel.highlightHints,
                                isSelected: viewModel.selectedItemID == item.id,
                                onSelect: {
                                    viewModel.select(item)
                                }
                            )
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .id(item.id)
                        }
                        if viewModel.hasMoreResults {
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
                            .onAppear {
                                viewModel.loadMore()
                            }
                        }
                    }
                }
                .id(scrollViewID)
                .accessibilityIdentifier("fileResultsList")
                .animation(.easeInOut(duration: 0.25), value: viewModel.showingRecent)

                if viewModel.totalMatches > 0 {
                    HStack {
                        Spacer()
                        Text(L10n.tr("Showing %d of %d results", viewModel.displayItems.count, viewModel.totalMatches))
                            .font(.callout)
                            .foregroundColor(.secondary)
                            .padding(8)
                        Spacer()
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
                }
            }
        }
        .frame(minWidth: 600, minHeight: 400)
        .onReceive(NotificationCenter.default.publisher(for: .rebuildIndex)) { _ in
            viewModel.rebuildIndex()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(true)
            isSearchFieldFocused = true
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(false)
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didDeminiaturizeNotification)) { _ in
            scrollViewID += 1
        }
    }
}

private struct ResultHeaderView: View {
    @ObservedObject var viewModel: SearchViewModel
    @ObservedObject private var settings = AppSettings.shared

    var body: some View {
        HStack(spacing: 0) {
            columnButton(L10n.tr("Name"), field: .name, width: ResultColumnLayout.nameWidth)

            if settings.showPath {
                columnSeparator
                columnButton(L10n.tr("Path"), field: .path)
            }

            if settings.showSize {
                columnSeparator
                columnButton(L10n.tr("Size"), field: .size, width: ResultColumnLayout.sizeWidth, alignment: .trailing)
            }

            if settings.showModifiedDate {
                columnSeparator
                columnButton(L10n.tr("Modified Date"), field: .modified, width: ResultColumnLayout.modifiedWidth)
            }
        }
        .padding(.horizontal, 6)
        .frame(minHeight: 30)
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
                .padding(.vertical, 4)
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
            .frame(width: width, minHeight: 30, alignment: alignment)
            .frame(maxWidth: width == nil ? .infinity : nil, minHeight: 30, alignment: alignment)
            .background(settings.sortField == field ? Color.accentColor.opacity(0.10) : Color.clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}
