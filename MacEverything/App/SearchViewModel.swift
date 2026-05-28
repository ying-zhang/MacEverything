import Foundation
import Combine
import AppKit
import os

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
        id: (result.path as NSString).appendingPathComponent(result.name), index: 0,
        name: result.name, path: result.path,
        type: result.type, size: result.size, modTime: result.modTime
    )
}

nonisolated private func isAllowedContentSearchPath(
    _ path: String,
    standardizedRoots: [String],
    standardizedExcluded: [String]
) -> Bool {
    let standardizedPath = URL(fileURLWithPath: path).standardizedFileURL.path
    let insideRoot = standardizedRoots.isEmpty || standardizedRoots.contains { root in
        standardizedPath == root || standardizedPath.hasPrefix(root + "/")
    }
    guard insideRoot else { return false }

    for excluded in standardizedExcluded {
        if standardizedPath == excluded || standardizedPath.hasPrefix(excluded + "/") {
            return false
        }
    }
    return true
}

@MainActor
final class SearchServiceModel: ObservableObject {
    static let shared = SearchServiceModel()

    @Published var isScanning: Bool = false
    @Published var scanComplete: Bool = false
    @Published var totalRecords: UInt32 = 0
    @Published var indexMemoryBytes: UInt64 = 0
    @Published var isMonitoring: Bool = false
    @Published var scannedCount: UInt64 = 0
    @Published var isContentIndexing: Bool = false
    @Published var contentIndexProgress: (indexed: UInt32, total: UInt32)?
    @Published var contentIndexedCount: UInt32 = 0
    @Published var contentIndexStorageBytes: UInt64 = 0
    @Published var isSyncing: Bool = false

    private let bridge = MacSearchBridge.shared()
    private let settings = AppSettings.shared
    private var indexChangeTask: Task<Void, Never>?
    private var notificationObservers: [NSObjectProtocol] = []
    let refreshThrottle = IndexRefreshThrottle()

    private static let indexChangeThrottleNs: UInt64 = 5_000_000_000 // 5 seconds

    private init() {
        notificationObservers.append(NotificationCenter.default.addObserver(forName: .rebuildIndex, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                self?.rebuildIndex()
            }
        })
        notificationObservers.append(NotificationCenter.default.addObserver(forName: .rebuildContentIndex, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                self?.rebuildContentIndex()
            }
        })
        notificationObservers.append(NotificationCenter.default.addObserver(forName: .clearContentIndex, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                self?.clearContentIndex()
            }
        })
        startIncremental()
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
                guard let self else { return }
                guard self.settings.snapshot.contentIndexingEnabled else { return }
                self.isContentIndexing = true
                self.contentIndexProgress = (indexed, total)
            }
        }

        bridge.onContentIndexComplete = { [weak self] totalIndexed in
            Task { @MainActor in
                guard let self else { return }
                self.isContentIndexing = false
                self.contentIndexProgress = nil
                self.contentIndexedCount = totalIndexed
                self.refreshContentIndexInfo()
                NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
            }
        }

        bridge.onIndexChanged = { [weak self] in
            Task { @MainActor in
                self?.onIndexChanged()
            }
        }

        isContentIndexing = false
        contentIndexProgress = nil
        applyRuntimeConfiguration()
        refreshContentIndexInfo()

        bridge.startIncremental(from: "/",
                                cachePath: SearchViewModel.cachePath,
                                walPath: SearchViewModel.walPath) { [weak self] count, _ in
            Task { @MainActor in
                guard let self else { return }
                self.totalRecords = count
                self.indexMemoryBytes = self.bridge.indexMemoryApproxBytes()
                self.isScanning = false
                self.scanComplete = true
                self.isMonitoring = self.bridge.isMonitoring
                self.isSyncing = self.bridge.isSyncing
                self.refreshContentIndexInfo()
                NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
            }
        }
    }

    func applyRuntimeConfiguration() {
        let snapshot = settings.snapshot
        bridge.setContentMaxFileSize(UInt64(snapshot.contentMaxFileSizeMB * 1024 * 1024))
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
            enablePinyinInitials: snapshot.enablePinyinInitials,
            enablePathSearchAcceleration: snapshot.enablePathSearchAcceleration,
            httpPort: snapshot.httpServerEnabled ? UInt16(snapshot.httpPort) : UInt16(0)
        )
    }

    func rebuildIndex() {
        guard !isScanning else { return }
        indexChangeTask?.cancel()
        scanComplete = false
        totalRecords = 0
        indexMemoryBytes = 0
        isMonitoring = false
        isSyncing = false

        try? FileManager.default.removeItem(atPath: SearchViewModel.cachePath)
        try? FileManager.default.removeItem(atPath: SearchViewModel.walPath)
        try? FileManager.default.removeItem(atPath: SearchViewModel.pagesPath)
        try? FileManager.default.removeItem(atPath: SearchViewModel.ptablePath)
        try? FileManager.default.removeItem(atPath: SearchViewModel.v6Path)
        try? FileManager.default.removeItem(atPath: SearchViewModel.contentIndexPath)
        try? FileManager.default.removeItem(atPath: SearchViewModel.contentWalPath)

        applyRuntimeConfiguration()
        startIncremental()
        NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
    }

    func rebuildContentIndex() {
        applyRuntimeConfiguration()
        isContentIndexing = settings.snapshot.contentIndexingEnabled
        contentIndexProgress = nil
        DispatchQueue.global(qos: .utility).async { [bridge] in
            bridge.rebuildContentIndex()
        }
    }

    func clearContentIndex() {
        isContentIndexing = false
        contentIndexProgress = nil
        contentIndexedCount = 0
        contentIndexStorageBytes = 0
        DispatchQueue.global(qos: .utility).async { [bridge] in
            bridge.clearContentIndex()
        }
        NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
    }

    func refreshContentIndexInfo() {
        let fileManager = FileManager.default
        let paths = [SearchViewModel.contentIndexPath, SearchViewModel.contentWalPath]
        var total: UInt64 = 0
        for path in paths {
            guard let attrs = try? fileManager.attributesOfItem(atPath: path),
                  let size = attrs[.size] as? NSNumber else { continue }
            total += size.uint64Value
        }
        contentIndexedCount = bridge.contentIndexedFileCount()
        contentIndexStorageBytes = total
    }

    func onWindowFocusChanged(_ focused: Bool) {
        if refreshThrottle.focusChanged(focused) {
            performIndexRefresh()
            scheduleCooldown()
        }
    }

    private func onIndexChanged() {
        if refreshThrottle.indexChanged() {
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
        indexMemoryBytes = bridge.indexMemoryApproxBytes()
        isMonitoring = bridge.isMonitoring
        isSyncing = bridge.isSyncing
        refreshContentIndexInfo()
        NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
    }
}

@MainActor
class SearchViewModel: ObservableObject {
    @Published var searchText: String = ""
    @Published var displayItems: [FileItem] = []
    @Published var totalMatches: Int = 0
    @Published var resultLimitReached: Bool = false
    @Published var queryTimeMs: Double = 0
    @Published var isLoadingMore: Bool = false
    @Published var showingRecent: Bool = false
    @Published var isContentSearch: Bool = false
    @Published var contentResults: [ContentFileItem] = []
    @Published var ghostSuggestion: String? = nil
    @Published var selectedItemID: String? = nil
    @Published var renameRequestedItemID: String? = nil

    @Published var highlightHints: [HighlightHint] = []

    private let bridge = MacSearchBridge.shared()
    private let service: SearchServiceModel
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
    private let sessionId: UInt64

    private static let pageSize: Int = 100

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
        self.service = SearchServiceModel.shared
        self.sessionId = Self.nextSessionId()
        optionsSink = searchOptions.objectWillChange.sink { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.onSearchOptionsChanged()
            }
        }
    }

    private var maxResults: UInt32 {
        UInt32(settings.snapshot.maxResults)
    }

    private func onSearchOptionsChanged() {
        guard service.scanComplete, !searchText.isEmpty, !isContentSearch else { return }
        searchTask?.cancel()
        searchGeneration &+= 1
        bridge.cancelSession(sessionId)
        updateHighlightHints()
        performSearch(searchText)
    }

    private func updateHighlightHints() {
        let query = searchOptions.buildQuery(searchText)
        guard !query.isEmpty else { highlightHints = []; return }
        highlightHints = bridge.parseHighlightHints(query).map { HighlightHint(from: $0) }
    }

    private nonisolated static let sessionCounter = OSAllocatedUnfairLock(initialState: UInt64(0))

    private nonisolated static func nextSessionId() -> UInt64 {
        sessionCounter.withLock { value in
            value &+= 1
            return value
        }
    }

    func onSearchTextChanged() {
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        isLoadingMore = false
        selectedItemID = nil
        updateHighlightHints()
        let text = searchText

        if text.isEmpty {
            totalMatches = 0
            resultLimitReached = false
            queryTimeMs = 0
            cachedResults = []
            sourceItems = []
            cachedItems = []
            loadedCount = 0
            displayItems = []
            showingRecent = false
            isContentSearch = false
            contentResults = []
            contentKeyword = "" // H-9: reset cached keyword
            ghostSuggestion = nil
            settledTask?.cancel()
            // Cancel any in-flight queries for this GUI session
            bridge.cancelSession(sessionId)
            if service.scanComplete {
                if settings.snapshot.startupDisplayMode == .recent {
                    // Slight delay so the stale query's dispatch_apply threads
                    // detect the generation change and exit before we compete for the thread pool
                    recentTask = Task { @MainActor [weak self] in
                        try? await Task.sleep(nanoseconds: 20_000_000) // 20ms
                        guard !Task.isCancelled, let self else { return }
                        self.loadRecentFiles()
                    }
                }
            }
            return
        }
        showingRecent = false

        guard settings.snapshot.searchAsYouType else {
            updateGhostSuggestion()
            return
        }

        let lowerText = text.lowercased()
        if lowerText.hasPrefix("infile:"), settings.snapshot.contentIndexingEnabled {
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
                resultLimitReached = false
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
        let sessionId = self.sessionId
        let query = searchOptions.buildQuery(keyword)
        Task.detached { [weak self] in
            let start = CFAbsoluteTimeGetCurrent()
            // P-4: Use batch method — single engine lock, no NSNumber boxing
            let results = bridge.queryResults(query, maxResults: maxResults, sessionId: sessionId)
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
                self.resultLimitReached = results.count >= Int(maxResults)
                self.queryTimeMs = elapsed
            }
        }
    }

    private func performContentSearch(_ keyword: String) {
        let bridge = self.bridge
        let gen = searchGeneration
        let snapshot = settings.snapshot
        let stdRoots = snapshot.contentSearchRoots.map { URL(fileURLWithPath: $0).standardizedFileURL.path }
        let stdExcluded = snapshot.contentSearchExcludedPaths.map { URL(fileURLWithPath: $0).standardizedFileURL.path }
        Task.detached { [weak self] in
            let start = CFAbsoluteTimeGetCurrent()
            let results = bridge.queryContent(keyword, maxResults: 200)
            let elapsed = (CFAbsoluteTimeGetCurrent() - start) * 1000

            var items: [ContentFileItem] = []
            items.reserveCapacity(results.count)
            for r in results {
                guard isAllowedContentSearchPath(
                    r.filePath,
                    standardizedRoots: stdRoots,
                    standardizedExcluded: stdExcluded
                ) else { continue }
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
                self.resultLimitReached = false
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

    func clearContentResults() {
        contentResults = []
        totalMatches = 0
        resultLimitReached = false
        queryTimeMs = 0
        if isContentSearch {
            contentKeyword = ""
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
                self.resultLimitReached = false
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

    func selectNext() -> Bool {
        guard !displayItems.isEmpty else { return false }
        if let currentID = selectedItemID,
           let idx = displayItems.firstIndex(where: { $0.id == currentID }) {
            let nextIdx = min(idx + 1, displayItems.count - 1)
            selectedItemID = displayItems[nextIdx].id
        } else {
            selectedItemID = displayItems.first?.id
        }
        return true
    }

    func selectPrevious() -> Bool {
        guard !displayItems.isEmpty else { return false }
        if let currentID = selectedItemID,
           let idx = displayItems.firstIndex(where: { $0.id == currentID }) {
            if idx == 0 { return false }
            selectedItemID = displayItems[idx - 1].id
        } else {
            selectedItemID = displayItems.last?.id
        }
        return true
    }

    func openSelectedOrFirst() {
        if let selectedItemID,
           let selected = displayItems.first(where: { $0.id == selectedItemID }) {
            openFile(selected)
        } else if let first = displayItems.first {
            openFile(first)
        }
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

    func requestRenameForSelected() {
        guard let id = selectedItemID else { return }
        renameRequestedItemID = id
    }

    func deleteSelectedFile() {
        guard let id = selectedItemID,
              let item = displayItems.first(where: { $0.id == id }) else { return }
        let fullPath = item.path + "/" + item.name
        do {
            try FileManager.default.trashItem(at: URL(fileURLWithPath: fullPath), resultingItemURL: nil)
            removeItemFromResults(id: id)
        } catch {
            NSSound.beep()
        }
    }

    func removeContentItem(id: String) {
        contentResults.removeAll { $0.id == id }
        totalMatches = max(0, totalMatches - 1)
    }

    func removeItemFromResults(id: String) {
        displayItems.removeAll { $0.id == id }
        cachedItems.removeAll { $0.id == id }
        sourceItems.removeAll { $0.id == id }
        totalMatches = max(0, totalMatches - 1)
        if selectedItemID == id {
            selectedItemID = nil
        }
    }

    func updateItemName(oldID: String, newName: String) {
        var newID: String?
        let updateIn = { (items: inout [FileItem]) in
            if let idx = items.firstIndex(where: { $0.id == oldID }) {
                let old = items[idx]
                let computedID = (old.path as NSString).appendingPathComponent(newName)
                newID = computedID
                items[idx] = FileItem(id: computedID, index: old.index, name: newName,
                                      path: old.path, type: old.type,
                                      size: old.size, modTime: old.modTime)
            }
        }
        updateIn(&displayItems)
        updateIn(&cachedItems)
        updateIn(&sourceItems)
        if selectedItemID == oldID {
            selectedItemID = newID
        }
    }

    func copySelectedFile() {
        guard let id = selectedItemID,
              let item = displayItems.first(where: { $0.id == id }) else { return }
        let fullPath = item.path + "/" + item.name
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects([NSURL(fileURLWithPath: fullPath)])
    }

    func selectedFileURL() -> URL? {
        guard let id = selectedItemID,
              let item = displayItems.first(where: { $0.id == id }) else { return nil }
        return URL(fileURLWithPath: item.path + "/" + item.name)
    }

    private func applySortedResults(pageSize: Int) {
        cachedItems = sorted(sourceItems)
        loadedCount = min(pageSize, cachedItems.count)
        displayItems = Array(cachedItems.prefix(loadedCount))
        if let selectedItemID, !displayItems.contains(where: { $0.id == selectedItemID }) {
            self.selectedItemID = nil
        }
    }

    private static let fileTypeDirectory: UInt8 = 2

    private func sorted(_ items: [FileItem]) -> [FileItem] {
        let snapshot = settings.snapshot
        guard snapshot.sortField != .relevance else { return items }

        let ascending = snapshot.sortAscending
        let groupByType = snapshot.sortField != .path
        return items.sorted { lhs, rhs in
            if groupByType {
                let lhsIsDir = lhs.type == Self.fileTypeDirectory
                let rhsIsDir = rhs.type == Self.fileTypeDirectory
                if lhsIsDir != rhsIsDir { return lhsIsDir }
            }
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

    private static let fileTypeApplication: UInt8 = 5

    private func openFile(_ item: FileItem) {
        let fullPath = item.path + "/" + item.name
        if item.type == Self.fileTypeApplication {
            let url = URL(fileURLWithPath: fullPath)
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration()) { _, error in
                if error != nil { NSSound.beep() }
            }
        } else if !NSWorkspace.shared.open(URL(fileURLWithPath: fullPath)) {
            NSSound.beep()
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
        service.onWindowFocusChanged(focused)
    }

    func refreshForServiceUpdate() {
        guard service.scanComplete else { return }
        if !searchText.isEmpty && !isContentSearch {
            searchTask?.cancel()
            recentTask?.cancel()
            searchGeneration &+= 1
            bridge.cancelSession(sessionId)
            updateHighlightHints()
            performSearch(searchText)
        } else if isContentSearch && !contentKeyword.isEmpty {
            searchTask?.cancel()
            recentTask?.cancel()
            searchGeneration &+= 1
            updateHighlightHints()
            performContentSearch(contentKeyword)
        } else if showingRecent && settings.snapshot.startupDisplayMode == .recent {
            loadRecentFiles()
        } else if searchText.isEmpty && displayItems.isEmpty && settings.snapshot.startupDisplayMode == .recent {
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
        if settings.snapshot.searchAsYouType {
            if settings.snapshot.enterKeyAction == .rename && selectedItemID != nil {
                requestRenameForSelected()
                return
            }
            if activateSelectedOrFirstResult() {
                return
            }
        }
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        showingRecent = false
        let lowerText = searchText.lowercased()
        if lowerText.hasPrefix("infile:"), settings.snapshot.contentIndexingEnabled {
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
