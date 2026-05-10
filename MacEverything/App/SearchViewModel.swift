import Foundation
import Combine
import AppKit

struct FileItem: Identifiable {
    let id: String      // path-based stable ID
    let index: UInt32   // engine index for record lookup
    let name: String
    let path: String
    let type: UInt8
    let size: UInt64
    let modTime: time_t
}

struct ContentFileItem: Identifiable {
    let id: String      // stable ID: filePath + ":" + matchOffset
    let fileName: String
    let filePath: String
    let snippet: String
    let matchOffset: UInt32
    let fileType: UInt8
}

nonisolated private func fileItem(from result: MEFileResult) -> FileItem {
    FileItem(
        id: "\(result.path)/\(result.name)", index: 0,
        name: result.name, path: result.path,
        type: result.type, size: result.size, modTime: result.modTime
    )
}

@MainActor
class SearchViewModel: ObservableObject {
    @Published var searchText: String = ""
    @Published var displayItems: [FileItem] = []
    @Published var totalMatches: Int = 0
    @Published var isScanning: Bool = false
    @Published var scanComplete: Bool = false
    @Published var totalRecords: UInt32 = 0
    @Published var queryTimeMs: Double = 0
    @Published var isMonitoring: Bool = false
    @Published var scannedCount: UInt64 = 0
    var isLoadingMore: Bool = false
    @Published var showingRecent: Bool = false
    @Published var isContentSearch: Bool = false
    @Published var contentResults: [ContentFileItem] = []
    @Published var isContentIndexing: Bool = false
    @Published var contentIndexProgress: (indexed: UInt32, total: UInt32)?
    @Published var contentIndexedCount: UInt32 = 0
    @Published var isSyncing: Bool = false
    @Published var ghostSuggestion: String? = nil
    @Published var selectedItemID: String? = nil

    /// Structured highlight hints extracted from the C++ query AST.
    /// Replaces the old keyword-based approach with field-aware, mode-aware hints.
    var highlightHints: [HighlightHint] {
        let query = searchOptions.buildQuery(searchText)
        guard !query.isEmpty else { return [] }
        return bridge.parseHighlightHints(query).map { HighlightHint(from: $0) }
    }

    private let bridge = MacSearchBridge.shared()
    private let settings = AppSettings.shared
    private let historyStore = SearchHistoryStore()
    private let searchOptions = SearchOptions.shared
    private var searchTask: Task<Void, Never>?
    private var recentTask: Task<Void, Never>?
    private var settledTask: Task<Void, Never>?
    private var optionsSink: AnyCancellable?
    private var cachedResults: [MEFileResult] = []
    private var sourceItems: [FileItem] = []
    private var cachedItems: [FileItem] = []
    private var loadedCount: Int = 0
    private var searchGeneration: UInt64 = 0
    nonisolated private static let guiSessionId: UInt64 = 1

    private static let pageSize: Int = 100
    private static let indexChangeThrottleNs: UInt64 = 5_000_000_000 // 5 seconds

    private var indexChangeTask: Task<Void, Never>?
    let refreshThrottle = IndexRefreshThrottle()

    static var cacheDir: String {
        let base = NSSearchPathForDirectoriesInDomains(
            .cachesDirectory, .userDomainMask, true
        ).first ?? NSTemporaryDirectory()
        let appCache = (base as NSString).appendingPathComponent("com.maceverything.app")
        try? FileManager.default.createDirectory(
            atPath: appCache, withIntermediateDirectories: true
        )
        return appCache
    }

    static var cachePath: String {
        return (cacheDir as NSString).appendingPathComponent("index.bin")
    }

    static var walPath: String {
        return (cacheDir as NSString).appendingPathComponent("index.wal")
    }

    static var pagesPath: String {
        return (cacheDir as NSString).appendingPathComponent("index.pages")
    }

    static var ptablePath: String {
        return (cacheDir as NSString).appendingPathComponent("index.ptable")
    }

    static var v6Path: String {
        return (cacheDir as NSString).appendingPathComponent("index.v6")
    }

    static var contentIndexPath: String {
        return (cacheDir as NSString).appendingPathComponent("content_index.bin")
    }

    static var contentWalPath: String {
        return (cacheDir as NSString).appendingPathComponent("content_index.wal")
    }

    init() {
        optionsSink = searchOptions.objectWillChange.sink { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.onSearchOptionsChanged()
            }
        }
        startIncremental()
    }

    private var maxResults: UInt32 {
        UInt32(settings.snapshot.maxResults)
    }

    private func onSearchOptionsChanged() {
        guard scanComplete, !searchText.isEmpty, !isContentSearch else { return }
        searchTask?.cancel()
        searchGeneration &+= 1
        bridge.cancelSession(Self.guiSessionId)
        performSearch(searchText)
    }

    func startIncremental() {
        isScanning = true
        scannedCount = 0

        bridge.onScanProgress = { [weak self] fileCount, dirCount in
            Task { @MainActor in
                self?.scannedCount = fileCount + dirCount
            }
        }

        bridge.onContentIndexProgress = { [weak self] indexed, total in
            Task { @MainActor in
                guard let self = self else { return }
                guard self.settings.snapshot.contentIndexingEnabled else { return }
                self.isContentIndexing = true
                self.contentIndexProgress = (indexed, total)
            }
        }

        bridge.onContentIndexComplete = { [weak self] totalIndexed in
            Task { @MainActor in
                guard let self = self else { return }
                self.isContentIndexing = false
                self.contentIndexProgress = nil
                self.contentIndexedCount = totalIndexed
                // Auto-refresh content search results after indexing completes
                if self.isContentSearch && !self.contentKeyword.isEmpty {
                    self.performContentSearch(self.contentKeyword)
                }
            }
        }

        // Set up FSEvents change callback before starting
        bridge.onIndexChanged = { [weak self] in
            Task { @MainActor in
                self?.onIndexChanged()
            }
        }

        isContentIndexing = false
        contentIndexProgress = nil
        applyRuntimeConfiguration()

        bridge.startIncremental(from: "/",
                                cachePath: Self.cachePath,
                                walPath: Self.walPath) { [weak self] count, didFullScan in
            Task { @MainActor in
                guard let self = self else { return }
                self.totalRecords = count
                self.isScanning = false
                self.scanComplete = true
                self.isMonitoring = self.bridge.isMonitoring
                self.isSyncing = self.bridge.isSyncing

                if !self.searchText.isEmpty {
                    self.performSearch(self.searchText)
                } else if self.settings.snapshot.startupDisplayMode == .recent {
                    self.loadRecentFiles()
                } else {
                    self.displayItems = []
                    self.showingRecent = false
                }
            }
        }
    }

    private func applyRuntimeConfiguration() {
        let snapshot = settings.snapshot
        bridge.updateConfiguration(
            withScanRoots: snapshot.indexRoots,
            excludedPaths: snapshot.excludedPaths,
            excludedPatterns: snapshot.excludedPatterns,
            contentRoots: snapshot.contentIndexRoots,
            contentExcludedPaths: snapshot.contentExcludedPaths,
            includeHidden: snapshot.indexHiddenFiles,
            includeSystem: snapshot.indexSystemFiles,
            includeAppBundleContents: snapshot.indexAppBundleContents,
            realtimeMonitoring: snapshot.refreshMode == .realtime,
            contentIndexingEnabled: snapshot.contentIndexingEnabled,
            automaticMaintenanceEnabled: snapshot.automaticMaintenanceEnabled,
            httpPort: snapshot.httpServerEnabled ? UInt16(snapshot.httpPort) : UInt16(0)
        )
    }

    func rebuildIndex() {
        guard !isScanning else { return }
        searchTask?.cancel()
        recentTask?.cancel()
        indexChangeTask?.cancel()
        searchGeneration &+= 1
        scanComplete = false
        displayItems = []
        sourceItems = []
        cachedItems = []
        cachedResults = []
        selectedItemID = nil
        totalMatches = 0
        queryTimeMs = 0
        contentResults = []
        isContentSearch = false
        contentKeyword = ""

        // Delete cached index files so startIncremental does a full scan
        try? FileManager.default.removeItem(atPath: Self.cachePath)
        try? FileManager.default.removeItem(atPath: Self.walPath)
        try? FileManager.default.removeItem(atPath: Self.pagesPath)
        try? FileManager.default.removeItem(atPath: Self.ptablePath)
        try? FileManager.default.removeItem(atPath: Self.v6Path)
        try? FileManager.default.removeItem(atPath: Self.contentIndexPath)
        try? FileManager.default.removeItem(atPath: Self.contentWalPath)

        applyRuntimeConfiguration()
        startIncremental()
    }

    func onSearchTextChanged() {
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        isLoadingMore = false
        selectedItemID = nil
        let text = searchText

        if text.isEmpty {
            totalMatches = 0
            queryTimeMs = 0
            cachedResults = []
            sourceItems = []
            cachedItems = []
            loadedCount = 0
            selectedItemID = nil
            isContentSearch = false
            contentResults = []
            contentKeyword = "" // H-9: reset cached keyword
            ghostSuggestion = nil
            settledTask?.cancel()
            // Cancel any in-flight queries for this GUI session
            bridge.cancelSession(Self.guiSessionId)
            if scanComplete {
                // Slight delay so the stale query's dispatch_apply threads
                // detect the generation change and exit before we compete for the thread pool
                recentTask = Task { @MainActor [weak self] in
                    try? await Task.sleep(nanoseconds: 20_000_000) // 20ms
                    guard !Task.isCancelled, let self else { return }
                    if self.settings.snapshot.startupDisplayMode == .recent {
                        self.loadRecentFiles()
                    } else {
                        self.displayItems = []
                        self.showingRecent = false
                    }
                }
            } else {
                displayItems = []
                showingRecent = false
            }
            return
        }
        showingRecent = false

        guard settings.snapshot.searchAsYouType else {
            updateGhostSuggestion()
            return
        }

        let lowerText = text.lowercased()
        if lowerText.hasPrefix("infile:") {
            isContentSearch = true
            displayItems = []
            cachedResults = []
            sourceItems = []
            cachedItems = []
            loadedCount = 0
            selectedItemID = nil

            let keyword = String(text.dropFirst(7))
            contentKeyword = keyword // H-9: cache computed keyword
            guard !keyword.isEmpty else {
                contentResults = []
                totalMatches = 0
                queryTimeMs = 0
                return
            }

            searchTask = Task { @MainActor in
                // 300ms debounce for content search (heavier)
                try? await Task.sleep(nanoseconds: 300_000_000)
                guard !Task.isCancelled else { return }
                performContentSearch(keyword)
            }
        } else {
            isContentSearch = false
            contentResults = []
            contentKeyword = "" // H-9: reset cached keyword

            searchTask = Task { @MainActor in
                // 80ms debounce
                try? await Task.sleep(nanoseconds: 80_000_000)
                guard !Task.isCancelled else { return }
                performSearch(text)
            }
        }

        updateGhostSuggestion()
        scheduleHistoryRecord()
    }

    private func performSearch(_ keyword: String) {
        let bridge = self.bridge
        let maxResults = self.maxResults
        let pageSize = Self.pageSize
        let gen = searchGeneration
        let query = searchOptions.buildQuery(keyword)
        Task.detached { [weak self] in
            let start = CFAbsoluteTimeGetCurrent()
            // P-4: Use batch method — single engine lock, no NSNumber boxing
            let results = bridge.queryResults(query, maxResults: maxResults, sessionId: Self.guiSessionId)
            let elapsed = (CFAbsoluteTimeGetCurrent() - start) * 1000
            var items: [FileItem] = []
            items.reserveCapacity(results.count)
            for r in results {
                items.append(fileItem(from: r))
            }
            let finalItems = items

            await MainActor.run { [weak self] in
                guard let self, self.searchGeneration == gen else { return }
                self.cachedResults = results
                self.sourceItems = finalItems
                self.cachedItems = finalItems
                self.applySortedResults(pageSize: pageSize)
                self.totalMatches = finalItems.count
                self.queryTimeMs = elapsed
            }
        }
    }

    private func performContentSearch(_ keyword: String) {
        let bridge = self.bridge
        let gen = searchGeneration
        Task.detached { [weak self] in
            let start = CFAbsoluteTimeGetCurrent()
            let results = bridge.queryContent(keyword, maxResults: 200)
            let elapsed = (CFAbsoluteTimeGetCurrent() - start) * 1000

            var items: [ContentFileItem] = []
            items.reserveCapacity(results.count)
            for r in results {
                items.append(ContentFileItem(
                    id: "\(r.filePath):\(r.matchOffset)",
                    fileName: r.fileName,
                    filePath: r.filePath,
                    snippet: r.snippet,
                    matchOffset: r.matchOffset,
                    fileType: r.fileType
                ))
            }
            let finalItems = items

            await MainActor.run { [weak self] in
                guard let self, self.searchGeneration == gen else { return }
                self.contentResults = finalItems
                self.totalMatches = finalItems.count
                self.queryTimeMs = elapsed
            }
        }
    }

    func loadMore() {
        guard !isLoadingMore else { return }
        guard loadedCount < cachedItems.count else { return }

        isLoadingMore = true
        let currentLoaded = loadedCount
        let pageSize = Self.pageSize
        let gen = searchGeneration

        Task { @MainActor [weak self] in
            guard let self, self.searchGeneration == gen else {
                self?.isLoadingMore = false
                return
            }
            let nextEnd = min(currentLoaded + pageSize, self.cachedItems.count)
            self.displayItems.append(contentsOf: self.cachedItems[currentLoaded..<nextEnd])
            self.loadedCount = nextEnd
            self.isLoadingMore = false
        }
    }

    private func loadRecentFiles() {
        recentTask?.cancel()
        let bridge = self.bridge
        let gen = searchGeneration
        recentTask = Task.detached { [weak self] in
            // P-4: Use batch method — single engine lock, no NSNumber boxing
            let results = bridge.recentResults(100)
            var items: [FileItem] = []
            items.reserveCapacity(results.count)
            for r in results {
                guard !Task.isCancelled else { return }
                items.append(fileItem(from: r))
            }
            let finalItems = items
            await MainActor.run { [weak self] in
                guard let self, self.searchGeneration == gen else { return }
                self.cachedResults = results
                self.sourceItems = finalItems
                self.cachedItems = finalItems
                self.applySortedResults(pageSize: Self.pageSize)
                self.totalMatches = finalItems.count
                self.showingRecent = true
                self.queryTimeMs = 0
            }
        }
    }

    func sortBy(_ field: SortField) {
        if settings.sortField == field {
            settings.sortAscending.toggle()
        } else {
            settings.sortField = field
            settings.sortAscending = field == .modified || field == .size ? false : true
        }
        applySortedResults(pageSize: max(loadedCount, Self.pageSize))
    }

    func select(_ item: FileItem) {
        selectedItemID = item.id
    }

    func activateSelectedOrFirstResult() -> Bool {
        guard let first = displayItems.first else { return false }
        if let selectedItemID,
           let selected = displayItems.first(where: { $0.id == selectedItemID }) {
            openFile(selected)
        } else {
            selectedItemID = first.id
        }
        return true
    }

    private func applySortedResults(pageSize: Int) {
        cachedItems = sorted(sourceItems)
        loadedCount = min(pageSize, cachedItems.count)
        displayItems = Array(cachedItems.prefix(loadedCount))
        if let selectedItemID, !displayItems.contains(where: { $0.id == selectedItemID }) {
            self.selectedItemID = nil
        }
    }

    private func sorted(_ items: [FileItem]) -> [FileItem] {
        let snapshot = settings.snapshot
        guard snapshot.sortField != .relevance else { return items }

        let ascending = snapshot.sortAscending
        return items.sorted { lhs, rhs in
            let result: ComparisonResult
            switch snapshot.sortField {
            case .relevance:
                result = .orderedSame
            case .name:
                result = lhs.name.localizedCaseInsensitiveCompare(rhs.name)
            case .path:
                result = (lhs.path + "/" + lhs.name).localizedCaseInsensitiveCompare(rhs.path + "/" + rhs.name)
            case .size:
                result = lhs.size == rhs.size ? .orderedSame : (lhs.size < rhs.size ? .orderedAscending : .orderedDescending)
            case .modified:
                result = lhs.modTime == rhs.modTime ? .orderedSame : (lhs.modTime < rhs.modTime ? .orderedAscending : .orderedDescending)
            }
            if result == .orderedSame {
                let fallback = (lhs.path + "/" + lhs.name).localizedCaseInsensitiveCompare(rhs.path + "/" + rhs.name)
                return fallback == .orderedAscending
            }
            return ascending ? result == .orderedAscending : result == .orderedDescending
        }
    }

    private func openFile(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        if item.type == 5 {
            let url = URL(fileURLWithPath: fullPath)
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
        } else if !NSWorkspace.shared.open(URL(fileURLWithPath: fullPath)) {
            NSSound.beep()
        }
    }

    private func onIndexChanged() {
        if refreshThrottle.indexChanged() {
            performIndexRefresh()
            scheduleCooldown()
        }
    }

    func onWindowFocusChanged(_ focused: Bool) {
        if !focused {
            // Record search text to history when window loses focus
            let text = searchText
            if text.count >= 2 && !text.lowercased().hasPrefix("infile:") {
                historyStore.recordQuery(text)
            }
        }
        if refreshThrottle.focusChanged(focused) {
            performIndexRefresh()
            scheduleCooldown()
        }
    }

    private func scheduleCooldown() {
        indexChangeTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: Self.indexChangeThrottleNs)
            guard let self else { return }
            self.indexChangeTask = nil
            if self.refreshThrottle.cooldownExpired() {
                self.performIndexRefresh()
                self.scheduleCooldown()
            }
        }
    }

    private func performIndexRefresh() {
        totalRecords = bridge.liveRecordCount()
        isMonitoring = bridge.isMonitoring
        isSyncing = bridge.isSyncing
        contentIndexedCount = bridge.contentIndexedFileCount()

        // Skip expensive search/query when app is not focused.
        // Results will refresh on focus regain via onWindowFocusChanged.
        guard refreshThrottle.isFocused else { return }

        if !searchText.isEmpty && !isContentSearch {
            performSearch(searchText)
        } else if isContentSearch && !contentKeyword.isEmpty {
            performContentSearch(contentKeyword)
        } else if showingRecent && settings.snapshot.startupDisplayMode == .recent {
            loadRecentFiles()
        }
    }

    var hasMoreResults: Bool {
        loadedCount < cachedItems.count
    }

    // H-9: Cached to avoid recomputing lowercased() + hasPrefix on every access
    private(set) var contentKeyword: String = ""

    // MARK: - Ghost text autocomplete

    private static let systemKeywords: [String] = [
        // Basic filters (most common first)
        "ext:", "size:", "file:", "folder:", "path:", "nopath:", "parent:", "depth:", "len:",
        // Date filters
        "dm:", "dc:", "da:", "datemodified:", "datecreated:", "dateaccessed:",
        // Modifiers
        "case:", "nocase:", "regex:", "ww:", "wfn:", "wholeword:", "wholefilename:",
        // Macros
        "audio:", "video:", "pic:", "doc:", "exe:", "zip:",
        // Other
        "content:", "type:",
    ]

    private static func systemKeywordMatch(for text: String) -> String? {
        let lower = text.lowercased()
        // Only match single partial words: no spaces, no colon already present
        guard !lower.isEmpty, !lower.contains(" "), !lower.contains(":") else { return nil }

        let matches = systemKeywords.filter { $0.hasPrefix(lower) }
        // Prefer shorter keywords, then alphabetical
        return matches.min { a, b in
            if a.count != b.count { return a.count < b.count }
            return a < b
        }
    }

    private func updateGhostSuggestion() {
        let text = searchText
        guard !text.isEmpty, !text.lowercased().hasPrefix("infile:") else {
            ghostSuggestion = nil
            return
        }
        // Priority 1: search history match
        if let match = historyStore.bestMatch(for: text),
           match.lowercased() != text.lowercased() {
            ghostSuggestion = text + match.dropFirst(text.count)
            return
        }
        // Priority 2: system keyword match
        if let keyword = Self.systemKeywordMatch(for: text) {
            ghostSuggestion = text + keyword.dropFirst(text.count)
            return
        }
        ghostSuggestion = nil
    }

    private func scheduleHistoryRecord() {
        settledTask?.cancel()
        let text = searchText
        guard text.count >= 2, !text.lowercased().hasPrefix("infile:") else { return }
        settledTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 2_000_000_000) // 2 seconds
            guard !Task.isCancelled, let self, self.searchText == text else { return }
            self.historyStore.recordQuery(text)
        }
    }

    func acceptGhostSuggestion() {
        guard let suggestion = ghostSuggestion else { return }
        searchText = suggestion
        ghostSuggestion = nil
        historyStore.recordQuery(suggestion)
        if !settings.snapshot.searchAsYouType {
            onSearchTextChanged()
            if !suggestion.isEmpty {
                performSearch(suggestion)
            }
        }
    }

    func submitSearch() {
        guard !searchText.isEmpty else { return }
        if settings.snapshot.searchAsYouType, activateSelectedOrFirstResult() {
            return
        }
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        showingRecent = false
        let lowerText = searchText.lowercased()
        if lowerText.hasPrefix("infile:") {
            let keyword = String(searchText.dropFirst(7))
            contentKeyword = keyword
            isContentSearch = true
            performContentSearch(keyword)
        } else {
            isContentSearch = false
            performSearch(searchText)
        }
        scheduleHistoryRecord()
    }
}
