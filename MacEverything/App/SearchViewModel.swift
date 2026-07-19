import Foundation
import Combine
import AppKit
import os

struct FileItem: Identifiable {
    static let fileTypeRegular: UInt8 = 1
    static let fileTypeDirectory: UInt8 = 2
    static let fileTypeApplication: UInt8 = 5

    let id: String      // path-based stable ID
    let index: UInt32   // engine index for record lookup
    let name: String
    let path: String
    let type: UInt8
    let size: UInt64
    let modTime: time_t

    var fullPath: String {
        (path as NSString).appendingPathComponent(name)
    }

    var isRegularFile: Bool { type == Self.fileTypeRegular }
    var isFolder: Bool { type == Self.fileTypeDirectory }
    var isApplication: Bool {
        // Preserve app handling for symlinked or legacy records not classified as type 5.
        type == Self.fileTypeApplication || name.lowercased().hasSuffix(".app")
    }

    var fileExtension: String {
        if isFolder { return "" }
        let ext = (name as NSString).pathExtension.lowercased()
        if ext.isEmpty, isApplication { return "app" }
        return ext
    }
}

enum QuickFilter: String, CaseIterable, Identifiable {
    case all
    case files
    case folders
    case documents
    case images
    case code
    case archives

    var id: String { rawValue }

    var title: String {
        switch self {
        case .all: return L10n.tr("All")
        case .files: return L10n.tr("Files")
        case .folders: return L10n.tr("Folders")
        case .documents: return L10n.tr("Docs")
        case .images: return L10n.tr("Images")
        case .code: return L10n.tr("Code")
        case .archives: return L10n.tr("Archives")
        }
    }

    var queryToken: String? {
        switch self {
        case .all: return nil
        case .files: return "type:file"
        case .folders: return "type:folder"
        case .documents: return "doc:"
        case .images: return "pic:"
        case .code: return "ext:swift;h;m;mm;cpp;c;hpp;rs;go;py;js;ts;tsx;jsx;java;kt;rb;php;sh;zsh;json;xml;yml;yaml;toml;sql;css;scss;html"
        case .archives: return "zip:"
        }
    }
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
        notificationObservers.append(
            NSWorkspace.shared.notificationCenter.addObserver(
                forName: NSWorkspace.willUnmountNotification,
                object: nil, queue: .main
            ) { [weak self] notification in
                Task { @MainActor in self?.handleVolumeWillUnmount(notification) }
            }
        )
        notificationObservers.append(
            NSWorkspace.shared.notificationCenter.addObserver(
                forName: NSWorkspace.didUnmountNotification,
                object: nil, queue: .main
            ) { [weak self] notification in
                Task { @MainActor in self?.handleVolumeUnmounted(notification) }
            }
        )
        notificationObservers.append(
            NSWorkspace.shared.notificationCenter.addObserver(
                forName: NSWorkspace.didMountNotification,
                object: nil, queue: .main
            ) { [weak self] notification in
                Task { @MainActor in self?.handleVolumeMounted(notification) }
            }
        )
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

        applyRuntimeConfiguration()
        isScanning = true
        bridge.rebuildIndex { [weak self] count in
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
        var paths = [SearchViewModel.contentIndexPath]
        if let names = try? fileManager.contentsOfDirectory(atPath: SearchViewModel.cacheDir) {
            paths.append(contentsOf: names.compactMap { name in
                guard name == "content_index.wal" || name.hasPrefix("content_index.wal.seg.") else {
                    return nil
                }
                return (SearchViewModel.cacheDir as NSString).appendingPathComponent(name)
            })
        }
        var total: UInt64 = 0
        for path in paths {
            guard let attrs = try? fileManager.attributesOfItem(atPath: path),
                  let size = attrs[.size] as? NSNumber else { continue }
            total += size.uint64Value
        }
        contentIndexedCount = bridge.contentIndexedFileCount()
        contentIndexStorageBytes = total
    }

    private static func normalizeVolumePath(_ url: URL) -> String {
        var p = url.path
        if p.count > 1 && p.hasSuffix("/") { p = String(p.dropLast()) }
        return p
    }

    private static func volumeOverlapsRoots(_ path: String, _ roots: [String]) -> Bool {
        roots.contains { root in
            if root == "/" { return true }
            return path == root || path.hasPrefix(root + "/") || root.hasPrefix(path + "/")
        }
    }

    private func handleVolumeWillUnmount(_ notification: Notification) {
        guard let url = notification.userInfo?[NSWorkspace.volumeURLUserInfoKey] as? URL else { return }
        let path = Self.normalizeVolumePath(url)
        let roots = settings.indexRoots
        guard Self.volumeOverlapsRoots(path, roots) else { return }
        AppLogger.info("VolumeMonitor", "Volume will unmount, pre-removing index: \(path)")
        bridge.markVolumeUnmounting(path)
        bridge.removeSubtree(path) { [weak self] removedCount in
            guard let self else { return }
            AppLogger.info("VolumeMonitor", "Pre-removed \(removedCount) records for unmounting volume: \(path)")
            self.performIndexRefresh()
        }
    }

    private func handleVolumeUnmounted(_ notification: Notification) {
        guard let url = notification.userInfo?[NSWorkspace.volumeURLUserInfoKey] as? URL else { return }
        let path = Self.normalizeVolumePath(url)
        let roots = settings.indexRoots
        guard Self.volumeOverlapsRoots(path, roots) else {
            AppLogger.info("VolumeMonitor", "Volume unmounted but not overlapping indexRoots: \(path)")
            return
        }
        AppLogger.info("VolumeMonitor", "Volume unmounted, cleaning up: \(path)")
        bridge.removeSubtree(path) { [weak self] removedCount in
            guard let self else { return }
            if removedCount > 0 {
                AppLogger.info("VolumeMonitor", "Removed \(removedCount) remaining records for unmounted volume: \(path)")
            }
            self.performIndexRefresh()
        }
        bridge.clearVolumeUnmounting(path)
    }

    private func handleVolumeMounted(_ notification: Notification) {
        guard let url = notification.userInfo?[NSWorkspace.volumeURLUserInfoKey] as? URL else { return }
        let path = Self.normalizeVolumePath(url)
        let roots = settings.indexRoots
        let matching = roots.filter { root in
            if root == "/" { return true }
            return root == path || root.hasPrefix(path + "/") || path.hasPrefix(root + "/")
        }
        guard !matching.isEmpty else {
            AppLogger.info("VolumeMonitor", "Volume mounted but no overlapping roots: \(path), indexRoots=\(roots)")
            return
        }
        AppLogger.info("VolumeMonitor", "Volume mounted, scheduling rescan: \(path), matching=\(matching)")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            guard let self else { return }
            self.applyRuntimeConfiguration()
            for root in matching {
                let rescanPath = root.hasPrefix(path + "/") ? root : path
                AppLogger.info("VolumeMonitor", "Rescanning: \(rescanPath)")
                self.bridge.rescanSubtree(rescanPath) { [weak self] in
                    AppLogger.info("VolumeMonitor", "Rescan complete for: \(rescanPath)")
                    self?.performIndexRefresh()
                }
            }
        }
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
        AppLogger.info("ServiceModel", "performIndexRefresh: liveRecords=\(totalRecords), posting .searchServiceDidRefresh")
        NotificationCenter.default.post(name: .searchServiceDidRefresh, object: nil)
    }
}

@MainActor
class SearchViewModel: ObservableObject {
    @Published var searchText: String = ""
    @Published var displayItems: [FileItem] = []
    /// Number of columns in the grid result view. Written by ContentView's geometry
    /// so keyboard navigation can move up/down by a whole row. Ignored in list mode.
    @Published var gridColumnCount: Int = 1
    @Published var totalMatches: Int = 0
    @Published var resultLimitReached: Bool = false
    @Published var queryTimeMs: Double = 0
    @Published var isLoadingMore: Bool = false
    @Published var showingRecent: Bool = false
    @Published var isContentSearch: Bool = false
    @Published var contentResults: [ContentFileItem] = []
    @Published var contentSearchUnavailable: Bool = false
    @Published var ghostSuggestion: String? = nil
    @Published var selectedItemID: String? = nil
    @Published var selectedItemIDs: Set<String> = []
    @Published var selectionAnchorID: String? = nil
    @Published var scrollTargetItemID: String? = nil
    @Published var renameRequestedItemID: String? = nil
    @Published var quickFilter: QuickFilter = .all
    @Published var pathFilter: String = ""

    @Published var highlightHints: [HighlightHint] = []

    private let bridge = MacSearchBridge.shared()
    private let service: SearchServiceModel
    private let settings = AppSettings.shared
    private let historyStore = SearchHistoryStore()
    private let searchOptions = SearchOptions.shared
    private var searchTask: Task<Void, Never>?
    private var recentTask: Task<Void, Never>?
    private var settledTask: Task<Void, Never>?
    private var windowRestoreRefreshTask: Task<Void, Never>?
    private var optionsSink: AnyCancellable?
    private var cachedResults: [MEFileResult] = []
    private var sourceItems: [FileItem] = []
    private var cachedItems: [FileItem] = []
    private var loadedCount: Int = 0
    private var searchGeneration: UInt64 = 0
    private let sessionId: UInt64
    private var allowQuickFilterAutoResetForCurrentSearch = false

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

    // MARK: - Active-window bridge for menu commands

    /// Maps each search window to its view model so menu-bar commands can reach
    /// the currently active window's selection. Both sides are weak because the
    /// window's hosting controller owns the view model.
    private static let activeByWindow: NSMapTable<NSWindow, AnyObject> =
        NSMapTable<NSWindow, AnyObject>.weakToWeakObjects()

    /// Called by ContentView once it knows its hosting window.
    func registerActiveWindow(_ window: NSWindow) {
        Self.activeByWindow.setObject(self, forKey: window)
    }

    func unregisterActiveWindow(_ window: NSWindow) {
        if (Self.activeByWindow.object(forKey: window) as? SearchViewModel) === self {
            Self.activeByWindow.removeObject(forKey: window)
        }
    }

    /// Invoked by the menu-bar command: locates the view model for the
    /// frontmost search window and copies its selected paths.
    @MainActor
    static func copySelectedPathsInActiveWindow() {
        var candidates: [NSWindow] = []
        if let keyWindow = NSApp.keyWindow { candidates.append(keyWindow) }
        if let mainWindow = NSApp.mainWindow, !candidates.contains(where: { $0 === mainWindow }) {
            candidates.append(mainWindow)
        }
        let remainingWindows = NSApp.orderedWindows.filter { candidate in
            !candidates.contains(where: { $0 === candidate })
        }
        candidates.append(contentsOf: remainingWindows)

        for window in candidates {
            if let viewModel = activeByWindow.object(forKey: window) as? SearchViewModel {
                viewModel.copySelectedFile()
                return
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
        let query = composedQuery(for: searchText)
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
        bridge.cancelSession(sessionId)
        isLoadingMore = false
        clearSelection()
        updateHighlightHints()
        let text = searchText
        allowQuickFilterAutoResetForCurrentSearch = true

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
            contentSearchUnavailable = false
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
        let isContentPrefix = lowerText.hasPrefix("infile:") || lowerText.hasPrefix("content:")
        if isContentPrefix, settings.snapshot.contentIndexingEnabled {
            contentSearchUnavailable = false
            isContentSearch = true
            displayItems = []
            cachedResults = []
            sourceItems = []
            cachedItems = []
            loadedCount = 0
            clearSelection()

            let prefixLen = lowerText.hasPrefix("infile:") ? 7 : 8
            let keyword = String(text.dropFirst(prefixLen))
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
        } else if isContentPrefix {
            isContentSearch = true
            contentSearchUnavailable = true
            contentResults = []
            contentKeyword = currentContentSearchKeyword()
            totalMatches = 0
            resultLimitReached = false
            queryTimeMs = 0
            clearSelection()
        } else {
            isContentSearch = false
            contentSearchUnavailable = false
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
        let query = composedQuery(for: keyword)
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
                guard let self else { return }
                let currentGen = self.searchGeneration
                guard currentGen == gen else {
                    AppLogger.info("Search", "Discarding results for '\(query)': gen \(gen) != current \(currentGen), \(results.count) results dropped")
                    return
                }
                if finalItems.isEmpty,
                   self.quickFilter != .all,
                   self.settings.snapshot.autoResetQuickFilterOnEmptyResults,
                   self.allowQuickFilterAutoResetForCurrentSearch {
                    self.allowQuickFilterAutoResetForCurrentSearch = false
                    self.quickFilter = .all
                    AppLogger.info("Search", "Auto-resetting quickFilter for '\(query)' (0 results with filter)")
                    return
                }
                self.allowQuickFilterAutoResetForCurrentSearch = false
                self.cachedResults = results
                self.sourceItems = finalItems
                self.applySortedResults(pageSize: pageSize)
                self.totalMatches = self.cachedItems.count
                self.resultLimitReached = results.count >= Int(maxResults)
                self.queryTimeMs = elapsed
                AppLogger.info("Search", "Displaying \(self.totalMatches) results for '\(query)' (engine returned \(results.count), gen=\(gen))")
            }
        }
    }

    private static let contentDisplayLimit = 200

    var effectiveMaxResults: Int {
        isContentSearch ? Self.contentDisplayLimit : Int(settings.snapshot.maxResults)
    }

    private func performContentSearch(_ keyword: String) {
        let bridge = self.bridge
        let gen = searchGeneration
        Task.detached { [weak self] in
            let start = CFAbsoluteTimeGetCurrent()
            let results = bridge.queryContent(keyword, maxResults: UInt32(Self.contentDisplayLimit + 1))
            let elapsed = (CFAbsoluteTimeGetCurrent() - start) * 1000

            var items: [ContentFileItem] = []
            items.reserveCapacity(min(results.count, Self.contentDisplayLimit))
            for r in results.prefix(Self.contentDisplayLimit) {
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
            let limitReached = results.count > Self.contentDisplayLimit

            await MainActor.run { [weak self] in
                guard let self, self.searchGeneration == gen else { return }
                self.contentResults = finalItems
                self.totalMatches = finalItems.count
                self.resultLimitReached = limitReached
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
        contentSearchUnavailable = false
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
                let quickFilteredItems = self.filteredByQuickFilter(finalItems)
                self.sourceItems = quickFilteredItems
                self.applySortedResults(pageSize: Self.pageSize)
                self.totalMatches = self.cachedItems.count
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
        selectedItemIDs = [item.id]
        selectedItemID = item.id
        selectionAnchorID = item.id
    }

    func select(_ item: FileItem, extending: Bool, toggling: Bool) {
        if extending, let anchorID = selectionAnchorID,
           let anchorIndex = displayItems.firstIndex(where: { $0.id == anchorID }),
           let targetIndex = displayItems.firstIndex(where: { $0.id == item.id }) {
            let range = min(anchorIndex, targetIndex)...max(anchorIndex, targetIndex)
            selectedItemIDs = Set(displayItems[range].map(\.id))
            selectedItemID = item.id
            return
        }

        if toggling {
            if selectedItemIDs.contains(item.id) {
                selectedItemIDs.remove(item.id)
                selectedItemID = selectedItemIDs.first
            } else {
                selectedItemIDs.insert(item.id)
                selectedItemID = item.id
                selectionAnchorID = selectionAnchorID ?? item.id
            }
            return
        }

        select(item)
    }

    func prepareContextMenu(for item: FileItem) {
        if !selectedItemIDs.contains(item.id) {
            select(item)
        } else {
            selectedItemID = item.id
        }
    }

    func selectedItemsInDisplayOrder() -> [FileItem] {
        let ids = selectedItemIDs
        guard !ids.isEmpty else { return [] }
        return displayItems.filter { ids.contains($0.id) }
    }

    func clearSelection() {
        selectedItemID = nil
        selectedItemIDs = []
        selectionAnchorID = nil
        scrollTargetItemID = nil
    }

    func selectNext() -> Bool {
        guard !displayItems.isEmpty else { return false }
        if let currentID = selectedItemID,
           let idx = displayItems.firstIndex(where: { $0.id == currentID }) {
            let nextIdx = min(idx + 1, displayItems.count - 1)
            selectForKeyboard(displayItems[nextIdx])
        } else {
            if let first = displayItems.first { selectForKeyboard(first) }
        }
        return true
    }

    func selectPrevious() -> Bool {
        guard !displayItems.isEmpty else { return false }
        if let currentID = selectedItemID,
           let idx = displayItems.firstIndex(where: { $0.id == currentID }) {
            if idx == 0 { return false }
            selectForKeyboard(displayItems[idx - 1])
        } else {
            if let last = displayItems.last { selectForKeyboard(last) }
        }
        return true
    }

    func selectGridDown() -> Bool {
        guard !displayItems.isEmpty else { return false }
        let currentIndex = selectedItemID.flatMap { currentID in
            displayItems.firstIndex(where: { $0.id == currentID })
        }
        guard let nextIndex = GridNavigation.downIndex(
            currentIndex: currentIndex,
            itemCount: displayItems.count,
            columns: gridColumnCount
        ) else { return false }
        selectForKeyboard(displayItems[nextIndex])
        return true
    }

    func selectGridUp() -> Bool {
        guard !displayItems.isEmpty else { return false }
        let currentIndex = selectedItemID.flatMap { currentID in
            displayItems.firstIndex(where: { $0.id == currentID })
        }
        guard let nextIndex = GridNavigation.upIndex(
            currentIndex: currentIndex,
            itemCount: displayItems.count,
            columns: gridColumnCount
        ) else { return false }
        selectForKeyboard(displayItems[nextIndex])
        return true
    }

    func selectGridRight() -> Bool {
        guard !displayItems.isEmpty else { return false }
        let currentIndex = selectedItemID.flatMap { currentID in
            displayItems.firstIndex(where: { $0.id == currentID })
        }
        guard let nextIndex = GridNavigation.rightIndex(
            currentIndex: currentIndex,
            itemCount: displayItems.count
        ) else { return false }
        selectForKeyboard(displayItems[nextIndex])
        return true
    }

    func selectGridLeft() -> Bool {
        guard !displayItems.isEmpty else { return false }
        let currentIndex = selectedItemID.flatMap { currentID in
            displayItems.firstIndex(where: { $0.id == currentID })
        }
        guard let nextIndex = GridNavigation.leftIndex(
            currentIndex: currentIndex,
            itemCount: displayItems.count
        ) else { return false }
        selectForKeyboard(displayItems[nextIndex])
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
            select(first)
        }
        return true
    }

    func requestRenameForSelected() {
        guard let id = selectedItemID else { return }
        renameRequestedItemID = id
    }

    func deleteSelectedFile() {
        let items = selectedItemsInDisplayOrder()
        guard !items.isEmpty else { return }
        var removed: [String] = []
        for item in items {
            let fullPath = item.fullPath
            do {
                try FileManager.default.trashItem(at: URL(fileURLWithPath: fullPath), resultingItemURL: nil)
                removed.append(item.id)
            } catch {
                NSSound.beep()
            }
        }
        for id in removed {
            removeItemFromResults(id: id)
        }
    }

    func deleteItem(_ item: FileItem) {
        let fullPath = item.fullPath
        do {
            try FileManager.default.trashItem(at: URL(fileURLWithPath: fullPath), resultingItemURL: nil)
            removeItemFromResults(id: item.id)
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
        selectedItemIDs.remove(id)
        if selectionAnchorID == id {
            selectionAnchorID = selectedItemID
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
        if selectedItemIDs.remove(oldID) != nil, let newID {
            selectedItemIDs.insert(newID)
        }
        if selectionAnchorID == oldID {
            selectionAnchorID = newID
        }
    }

    func copySelectedFile() {
        let items = selectedItemsInDisplayOrder()
        guard !items.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects(items.map { NSURL(fileURLWithPath: $0.fullPath) })
    }

    func selectedFileURLs() -> [URL] {
        selectedItemsInDisplayOrder().map { URL(fileURLWithPath: $0.fullPath) }
    }

    private func applySortedResults(pageSize: Int) {
        cachedItems = sorted(filteredByPath(sourceItems))
        loadedCount = min(pageSize, cachedItems.count)
        displayItems = Array(cachedItems.prefix(loadedCount))
        selectedItemIDs = selectedItemIDs.intersection(Set(displayItems.map(\.id)))
        if let selectedItemID, !selectedItemIDs.contains(selectedItemID) {
            self.selectedItemID = selectedItemIDs.first
        }
        if let selectionAnchorID, !selectedItemIDs.contains(selectionAnchorID) {
            self.selectionAnchorID = selectedItemID
        }
    }

    private func sorted(_ items: [FileItem]) -> [FileItem] {
        let snapshot = settings.snapshot
        guard snapshot.sortField != .relevance else { return items }

        let ascending = snapshot.sortAscending
        let groupByType = snapshot.sortField != .path
        return items.sorted { lhs, rhs in
            if groupByType {
                let lhsIsDir = lhs.isFolder
                let rhsIsDir = rhs.isFolder
                if lhsIsDir != rhsIsDir { return lhsIsDir }
            }
            let result: ComparisonResult
            switch snapshot.sortField {
            case .relevance:
                result = .orderedSame
            case .name:
                result = lhs.name.localizedCaseInsensitiveCompare(rhs.name)
            case .ext:
                result = lhs.fileExtension.localizedCaseInsensitiveCompare(rhs.fileExtension)
            case .path:
                result = lhs.fullPath.localizedCaseInsensitiveCompare(rhs.fullPath)
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
        FileActions.open(item)
    }

    private func selectForKeyboard(_ item: FileItem) {
        select(item)
        scrollTargetItemID = item.id
    }

    func onWindowFocusChanged(_ focused: Bool) {
        if !focused {
            // Record search text to history when window loses focus
            let text = searchText
            let lower = text.lowercased()
            if text.count >= 2 && !lower.hasPrefix("infile:") && !lower.hasPrefix("content:") {
                historyStore.recordQuery(text)
            }
        }
        service.onWindowFocusChanged(focused)
    }

    func refreshForServiceUpdate() {
        guard service.scanComplete else {
            searchTask?.cancel()
            recentTask?.cancel()
            searchGeneration &+= 1
            bridge.cancelSession(sessionId)
            cachedResults = []
            sourceItems = []
            cachedItems = []
            displayItems = []
            contentResults = []
            loadedCount = 0
            totalMatches = 0
            resultLimitReached = false
            showingRecent = false
            clearSelection()
            AppLogger.info("Search", "Cleared results while index rebuild is in progress")
            return
        }
        let liveCount = bridge.liveRecordCount()
        AppLogger.info("Search", "refreshForServiceUpdate: liveRecordCount=\(liveCount), searchText='\(searchText)'")
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

    func refreshAfterWindowRestore() {
        guard service.scanComplete else { return }

        let textSnapshot = searchText
        let contentKeywordSnapshot = contentKeyword
        let isContentSnapshot = isContentSearch
        windowRestoreRefreshTask?.cancel()
        windowRestoreRefreshTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 100_000_000)
            guard !Task.isCancelled, let self else { return }
            guard self.searchText == textSnapshot,
                  self.contentKeyword == contentKeywordSnapshot,
                  self.isContentSearch == isContentSnapshot else { return }
            self.performWindowRestoreRefresh()
        }
    }

    private func performWindowRestoreRefresh() {
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        bridge.cancelSession(sessionId)
        updateHighlightHints()

        if !searchText.isEmpty {
            showingRecent = false
            allowQuickFilterAutoResetForCurrentSearch = false
            if isContentSearch {
                let keyword = currentContentSearchKeyword()
                contentKeyword = keyword
                if !keyword.isEmpty {
                    performContentSearch(keyword)
                }
            } else {
                performSearch(searchText)
            }
        } else if showingRecent || settings.snapshot.startupDisplayMode == .recent {
            loadRecentFiles()
        }
    }

    private func currentContentSearchKeyword() -> String {
        if !contentKeyword.isEmpty {
            return contentKeyword
        }
        let lower = searchText.lowercased()
        if lower.hasPrefix("infile:") {
            return String(searchText.dropFirst(7))
        }
        if lower.hasPrefix("content:") {
            return String(searchText.dropFirst(8))
        }
        return ""
    }

    func onQuickFilterChanged() {
        guard service.scanComplete else { return }
        if !isContentSearch {
            allowQuickFilterAutoResetForCurrentSearch = false
            rerunCurrentSearch()
        }
    }

    func onPathFilterChanged() {
        guard service.scanComplete else { return }
        applySortedResults(pageSize: max(loadedCount, Self.pageSize))
        totalMatches = cachedItems.count
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
        let lowerGhost = text.lowercased()
        guard !text.isEmpty, !lowerGhost.hasPrefix("infile:"), !lowerGhost.hasPrefix("content:") else {
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
        let lowerHist = text.lowercased()
        guard text.count >= 2, !lowerHist.hasPrefix("infile:"), !lowerHist.hasPrefix("content:") else { return }
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
        allowQuickFilterAutoResetForCurrentSearch = true
        showingRecent = false
        let lowerText = searchText.lowercased()
        let isSubmitContent = lowerText.hasPrefix("infile:") || lowerText.hasPrefix("content:")
        if isSubmitContent, settings.snapshot.contentIndexingEnabled {
            contentSearchUnavailable = false
            let prefixLen = lowerText.hasPrefix("infile:") ? 7 : 8
            let keyword = String(searchText.dropFirst(prefixLen))
            contentKeyword = keyword
            isContentSearch = true
            performContentSearch(keyword)
        } else if isSubmitContent {
            isContentSearch = true
            contentSearchUnavailable = true
            contentKeyword = currentContentSearchKeyword()
            contentResults = []
            totalMatches = 0
            resultLimitReached = false
            queryTimeMs = 0
        } else {
            isContentSearch = false
            contentSearchUnavailable = false
            performSearch(searchText)
        }
        scheduleHistoryRecord()
    }

    private func composedQuery(for keyword: String) -> String {
        var query = searchOptions.buildQuery(keyword)
        if let token = quickFilter.queryToken {
            query = query.trimmingCharacters(in: .whitespacesAndNewlines)
            query = query.isEmpty ? token : "\(query) \(token)"
        }
        return query
    }

    private func filteredByPath(_ items: [FileItem]) -> [FileItem] {
        let needle = pathFilter.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !needle.isEmpty else { return items }
        return items.filter { $0.fullPath.localizedCaseInsensitiveContains(needle) }
    }

    private func filteredByQuickFilter(_ items: [FileItem]) -> [FileItem] {
        switch quickFilter {
        case .all:
            return items
        case .files:
            return items.filter { $0.isRegularFile || $0.isApplication }
        case .folders:
            return items.filter(\.isFolder)
        case .documents:
            let exts: Set<String> = ["pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "md", "rtf", "pages", "numbers", "key"]
            return items.filter { exts.contains($0.fileExtension) }
        case .images:
            let exts: Set<String> = ["jpg", "jpeg", "png", "gif", "bmp", "tiff", "webp", "svg", "heic", "heif"]
            return items.filter { exts.contains($0.fileExtension) }
        case .code:
            let exts: Set<String> = ["swift", "h", "m", "mm", "cpp", "c", "hpp", "rs", "go", "py", "js", "ts", "tsx", "jsx", "java", "kt", "rb", "php", "sh", "zsh", "json", "xml", "yml", "yaml", "toml", "sql", "css", "scss", "html"]
            return items.filter { exts.contains($0.fileExtension) }
        case .archives:
            let exts: Set<String> = ["zip", "rar", "7z", "tar", "gz", "bz2", "xz", "tgz", "zst", "lz4"]
            return items.filter { exts.contains($0.fileExtension) }
        }
    }

    private func rerunCurrentSearch() {
        searchTask?.cancel()
        recentTask?.cancel()
        searchGeneration &+= 1
        bridge.cancelSession(sessionId)
        updateHighlightHints()
        if searchText.isEmpty {
            if settings.snapshot.startupDisplayMode == .recent {
                loadRecentFiles()
            } else {
                cachedResults = []
                sourceItems = []
                cachedItems = []
                loadedCount = 0
                displayItems = []
                showingRecent = false
                totalMatches = 0
                resultLimitReached = false
                queryTimeMs = 0
                clearSelection()
            }
        } else {
            performSearch(searchText)
        }
    }
}
