import Foundation
import Combine
import SwiftUI

enum StartupDisplayMode: String, CaseIterable, Codable, Identifiable {
    case empty
    case recent

    var id: String { rawValue }

    var title: String {
        switch self {
        case .empty: return L10n.tr("Empty List")
        case .recent: return L10n.tr("Recent Changes")
        }
    }
}

enum SortField: String, CaseIterable, Codable, Identifiable {
    case relevance
    case name
    case path
    case size
    case modified

    var id: String { rawValue }

    var title: String {
        switch self {
        case .relevance: return L10n.tr("Relevance")
        case .name: return L10n.tr("Name")
        case .path: return L10n.tr("Path")
        case .size: return L10n.tr("Size")
        case .modified: return L10n.tr("Modified Date")
        }
    }
}

enum RefreshMode: String, CaseIterable, Codable, Identifiable {
    case realtime
    case manual

    var id: String { rawValue }

    var title: String {
        switch self {
        case .realtime: return L10n.tr("Realtime")
        case .manual: return L10n.tr("Manual")
        }
    }
}

enum ResultDensity: String, CaseIterable, Codable, Identifiable {
    case comfortable
    case compact

    var id: String { rawValue }

    var title: String {
        switch self {
        case .comfortable: return L10n.tr("Comfortable")
        case .compact: return L10n.tr("Compact")
        }
    }
}

enum EnterKeyAction: String, CaseIterable, Codable, Identifiable {
    case openFile
    case rename

    var id: String { rawValue }

    var title: String {
        switch self {
        case .openFile: return L10n.tr("Open File")
        case .rename: return L10n.tr("Rename")
        }
    }
}

struct AppSettingsSnapshot {
    var indexRoots: [String]
    var excludedPaths: [String]
    var excludedPatterns: [String]
    var indexHiddenFiles: Bool
    var indexSystemFiles: Bool
    var indexAppBundleContents: Bool
    var refreshMode: RefreshMode
    var startupDisplayMode: StartupDisplayMode
    var searchAsYouType: Bool
    var defaultRegex: Bool
    var defaultCaseSensitive: Bool
    var defaultWholeWord: Bool
    var defaultMatchFilename: Bool
    var maxResults: Int
    var sortField: SortField
    var sortAscending: Bool
    var contentIndexingEnabled: Bool
    var contentSearchUsesIndexRoots: Bool
    var contentSearchUsesIndexExclusions: Bool
    var contentSearchRoots: [String]
    var contentSearchExcludedPaths: [String]
    var contentIndexRoots: [String]
    var contentExcludedPaths: [String]
    var contentMaxFileSizeMB: Double
    var searchHistoryEnabled: Bool
    var searchHistoryLimit: Int
    var showPath: Bool
    var showSize: Bool
    var showModifiedDate: Bool
    var showContentSnippets: Bool
    var resultDensity: ResultDensity
    var httpServerEnabled: Bool
    var httpPort: Int
    var hideDockIcon: Bool
    var automaticMaintenanceEnabled: Bool
    var enablePinyinInitials: Bool
    var enablePathSearchAcceleration: Bool
    var enterKeyAction: EnterKeyAction
}

@MainActor
final class AppSettings: ObservableObject {
    static let shared = AppSettings()

    private enum Key {
        static let indexRoots = "settings.indexRoots"
        static let excludedPaths = "settings.excludedPaths"
        static let excludedPatterns = "settings.excludedPatterns"
        static let indexHiddenFiles = "settings.indexHiddenFiles"
        static let indexSystemFiles = "settings.indexSystemFiles"
        static let indexAppBundleContents = "settings.indexAppBundleContents"
        static let refreshMode = "settings.refreshMode"
        static let startupDisplayMode = "settings.startupDisplayMode"
        static let searchAsYouType = "settings.searchAsYouType"
        static let defaultRegex = "settings.defaultRegex"
        static let defaultCaseSensitive = "settings.defaultCaseSensitive"
        static let defaultWholeWord = "settings.defaultWholeWord"
        static let defaultMatchFilename = "settings.defaultMatchFilename"
        static let maxResults = "settings.maxResults"
        static let sortField = "settings.sortField"
        static let sortAscending = "settings.sortAscending"
        static let contentIndexingEnabled = "settings.contentIndexingEnabled"
        static let contentSearchUsesIndexRoots = "settings.contentSearchUsesIndexRoots"
        static let contentSearchUsesIndexExclusions = "settings.contentSearchUsesIndexExclusions"
        static let contentSearchRoots = "settings.contentSearchRoots"
        static let contentSearchExcludedPaths = "settings.contentSearchExcludedPaths"
        static let contentIndexRoots = "settings.contentIndexRoots"
        static let contentExcludedPaths = "settings.contentExcludedPaths"
        static let contentMaxFileSizeMB = "settings.contentMaxFileSizeMB"
        static let searchHistoryEnabled = "settings.searchHistoryEnabled"
        static let searchHistoryLimit = "settings.searchHistoryLimit"
        static let showPath = "settings.showPath"
        static let showSize = "settings.showSize"
        static let showModifiedDate = "settings.showModifiedDate"
        static let showContentSnippets = "settings.showContentSnippets"
        static let resultDensity = "settings.resultDensity"
        static let httpServerEnabled = "settings.httpServerEnabled"
        static let httpPort = "settings.httpPort"
        static let hideDockIcon = "settings.hideDockIcon"
        static let automaticMaintenanceEnabled = "settings.automaticMaintenanceEnabled"
        static let lowMemoryMode = "settings.lowMemoryMode"
        static let enablePinyinInitials = "settings.enablePinyinInitials"
        static let enablePathSearchAcceleration = "settings.enablePathSearchAcceleration"
        static let enterKeyAction = "settings.enterKeyAction"
        static let settingsSchemaVersion = "settings.schemaVersion"
    }

    @Published var indexRoots: [String] { didSet { saveArray(indexRoots, Key.indexRoots) } }
    @Published var excludedPaths: [String] { didSet { saveArray(excludedPaths, Key.excludedPaths) } }
    @Published var excludedPatterns: [String] { didSet { saveArray(excludedPatterns, Key.excludedPatterns) } }
    @Published var indexHiddenFiles: Bool { didSet { save(indexHiddenFiles, Key.indexHiddenFiles) } }
    @Published var indexSystemFiles: Bool { didSet { save(indexSystemFiles, Key.indexSystemFiles) } }
    @Published var indexAppBundleContents: Bool { didSet { save(indexAppBundleContents, Key.indexAppBundleContents) } }
    @Published var refreshMode: RefreshMode { didSet { save(refreshMode.rawValue, Key.refreshMode) } }
    @Published var startupDisplayMode: StartupDisplayMode { didSet { save(startupDisplayMode.rawValue, Key.startupDisplayMode) } }
    @Published var searchAsYouType: Bool { didSet { save(searchAsYouType, Key.searchAsYouType) } }
    @Published var defaultRegex: Bool { didSet { save(defaultRegex, Key.defaultRegex) } }
    @Published var defaultCaseSensitive: Bool { didSet { save(defaultCaseSensitive, Key.defaultCaseSensitive) } }
    @Published var defaultWholeWord: Bool { didSet { save(defaultWholeWord, Key.defaultWholeWord) } }
    @Published var defaultMatchFilename: Bool { didSet { save(defaultMatchFilename, Key.defaultMatchFilename) } }
    @Published var maxResults: Int {
        didSet {
            let value = clamped(maxResults, 100, 10_000)
            if value != maxResults { maxResults = value; return }
            save(value, Key.maxResults)
        }
    }
    @Published var sortField: SortField { didSet { save(sortField.rawValue, Key.sortField) } }
    @Published var sortAscending: Bool { didSet { save(sortAscending, Key.sortAscending) } }
    @Published var contentIndexingEnabled: Bool { didSet { save(contentIndexingEnabled, Key.contentIndexingEnabled) } }
    @Published var contentSearchUsesIndexRoots: Bool { didSet { save(contentSearchUsesIndexRoots, Key.contentSearchUsesIndexRoots) } }
    @Published var contentSearchUsesIndexExclusions: Bool { didSet { save(contentSearchUsesIndexExclusions, Key.contentSearchUsesIndexExclusions) } }
    @Published var contentSearchRoots: [String] { didSet { saveArray(contentSearchRoots, Key.contentSearchRoots) } }
    @Published var contentSearchExcludedPaths: [String] { didSet { saveArray(contentSearchExcludedPaths, Key.contentSearchExcludedPaths) } }
    @Published var contentIndexRoots: [String] { didSet { saveArray(contentIndexRoots, Key.contentIndexRoots) } }
    @Published var contentExcludedPaths: [String] { didSet { saveArray(contentExcludedPaths, Key.contentExcludedPaths) } }
    @Published var contentMaxFileSizeMB: Double {
        didSet {
            let value = min(max(contentMaxFileSizeMB, 0.1), 100.0)
            if value != contentMaxFileSizeMB { contentMaxFileSizeMB = value; return }
            save(value, Key.contentMaxFileSizeMB)
        }
    }
    @Published var searchHistoryEnabled: Bool { didSet { save(searchHistoryEnabled, Key.searchHistoryEnabled) } }
    @Published var searchHistoryLimit: Int {
        didSet {
            let value = clamped(searchHistoryLimit, 0, 5000)
            if value != searchHistoryLimit { searchHistoryLimit = value; return }
            save(value, Key.searchHistoryLimit)
        }
    }
    @Published var showPath: Bool { didSet { save(showPath, Key.showPath) } }
    @Published var showSize: Bool { didSet { save(showSize, Key.showSize) } }
    @Published var showModifiedDate: Bool { didSet { save(showModifiedDate, Key.showModifiedDate) } }
    @Published var showContentSnippets: Bool { didSet { save(showContentSnippets, Key.showContentSnippets) } }
    @Published var resultDensity: ResultDensity { didSet { save(resultDensity.rawValue, Key.resultDensity) } }
    @Published var httpServerEnabled: Bool { didSet { save(httpServerEnabled, Key.httpServerEnabled) } }
    @Published var httpPort: Int {
        didSet {
            let value = clamped(httpPort, 1024, 65535)
            if value != httpPort { httpPort = value; return }
            save(value, Key.httpPort)
        }
    }
    @Published var automaticMaintenanceEnabled: Bool { didSet { save(automaticMaintenanceEnabled, Key.automaticMaintenanceEnabled) } }
    @Published var enablePinyinInitials: Bool { didSet { save(enablePinyinInitials, Key.enablePinyinInitials) } }
    @Published var enablePathSearchAcceleration: Bool { didSet { save(enablePathSearchAcceleration, Key.enablePathSearchAcceleration) } }
    @Published var enterKeyAction: EnterKeyAction { didSet { save(enterKeyAction.rawValue, Key.enterKeyAction) } }
    @Published var hideDockIcon: Bool { didSet { save(hideDockIcon, Key.hideDockIcon) } }

    private let defaults = UserDefaults.standard

    private init() {
        let roots = Self.defaultIndexRoots()
        let fallbackRoots = roots.isEmpty ? [FileManager.default.homeDirectoryForCurrentUser.path] : roots
        let excluded = Self.defaultExcludedPaths()

        indexRoots = defaults.stringArray(forKey: Key.indexRoots) ?? fallbackRoots
        excludedPaths = defaults.stringArray(forKey: Key.excludedPaths) ?? excluded
        excludedPatterns = defaults.stringArray(forKey: Key.excludedPatterns) ?? Self.defaultExcludedPatterns()
        indexHiddenFiles = defaults.object(forKey: Key.indexHiddenFiles) as? Bool ?? false
        indexSystemFiles = defaults.object(forKey: Key.indexSystemFiles) as? Bool ?? false
        indexAppBundleContents = defaults.object(forKey: Key.indexAppBundleContents) as? Bool ?? false
        refreshMode = RefreshMode(rawValue: defaults.string(forKey: Key.refreshMode) ?? "") ?? .realtime
        startupDisplayMode = StartupDisplayMode(rawValue: defaults.string(forKey: Key.startupDisplayMode) ?? "") ?? .recent
        searchAsYouType = defaults.object(forKey: Key.searchAsYouType) as? Bool ?? true
        defaultRegex = defaults.object(forKey: Key.defaultRegex) as? Bool ?? false
        defaultCaseSensitive = defaults.object(forKey: Key.defaultCaseSensitive) as? Bool ?? false
        defaultWholeWord = defaults.object(forKey: Key.defaultWholeWord) as? Bool ?? false
        defaultMatchFilename = defaults.object(forKey: Key.defaultMatchFilename) as? Bool ?? false
        maxResults = clamped(defaults.object(forKey: Key.maxResults) as? Int ?? 10_000, 100, 10_000)
        sortField = SortField(rawValue: defaults.string(forKey: Key.sortField) ?? "") ?? .relevance
        sortAscending = defaults.object(forKey: Key.sortAscending) as? Bool ?? false
        contentIndexingEnabled = defaults.object(forKey: Key.contentIndexingEnabled) as? Bool ?? true
        contentSearchUsesIndexRoots = defaults.object(forKey: Key.contentSearchUsesIndexRoots) as? Bool ?? true
        contentSearchUsesIndexExclusions = defaults.object(forKey: Key.contentSearchUsesIndexExclusions) as? Bool ?? true
        contentSearchRoots = defaults.stringArray(forKey: Key.contentSearchRoots) ?? fallbackRoots
        contentSearchExcludedPaths = defaults.stringArray(forKey: Key.contentSearchExcludedPaths) ?? excluded
        contentIndexRoots = defaults.stringArray(forKey: Key.contentIndexRoots) ?? fallbackRoots
        contentExcludedPaths = defaults.stringArray(forKey: Key.contentExcludedPaths) ?? excluded
        contentMaxFileSizeMB = defaults.object(forKey: Key.contentMaxFileSizeMB) as? Double ?? 1.0
        searchHistoryEnabled = defaults.object(forKey: Key.searchHistoryEnabled) as? Bool ?? true
        searchHistoryLimit = defaults.object(forKey: Key.searchHistoryLimit) as? Int ?? 200
        showPath = defaults.object(forKey: Key.showPath) as? Bool ?? true
        showSize = defaults.object(forKey: Key.showSize) as? Bool ?? true
        showModifiedDate = defaults.object(forKey: Key.showModifiedDate) as? Bool ?? false
        showContentSnippets = defaults.object(forKey: Key.showContentSnippets) as? Bool ?? true
        resultDensity = ResultDensity(rawValue: defaults.string(forKey: Key.resultDensity) ?? "") ?? .comfortable
        httpServerEnabled = defaults.object(forKey: Key.httpServerEnabled) as? Bool ?? true
        httpPort = defaults.object(forKey: Key.httpPort) as? Int ?? 19_860
        automaticMaintenanceEnabled = defaults.object(forKey: Key.automaticMaintenanceEnabled) as? Bool ?? true
        let oldLowMemoryMode = defaults.object(forKey: Key.lowMemoryMode) as? Bool ?? false
        enablePinyinInitials = defaults.object(forKey: Key.enablePinyinInitials) as? Bool ?? !oldLowMemoryMode
        enablePathSearchAcceleration = defaults.object(forKey: Key.enablePathSearchAcceleration) as? Bool ?? !oldLowMemoryMode
        enterKeyAction = EnterKeyAction(rawValue: defaults.string(forKey: Key.enterKeyAction) ?? "") ?? .openFile
        hideDockIcon = defaults.object(forKey: Key.hideDockIcon) as? Bool ?? false

        migrateSettingsIfNeeded()
    }

    var snapshot: AppSettingsSnapshot {
        AppSettingsSnapshot(
            indexRoots: normalizedExistingPaths(indexRoots),
            excludedPaths: normalizedPaths(excludedPaths),
            excludedPatterns: excludedPatterns,
            indexHiddenFiles: indexHiddenFiles,
            indexSystemFiles: indexSystemFiles,
            indexAppBundleContents: indexAppBundleContents,
            refreshMode: refreshMode,
            startupDisplayMode: startupDisplayMode,
            searchAsYouType: searchAsYouType,
            defaultRegex: defaultRegex,
            defaultCaseSensitive: defaultCaseSensitive,
            defaultWholeWord: defaultWholeWord,
            defaultMatchFilename: defaultMatchFilename,
            maxResults: clamped(maxResults, 100, 10_000),
            sortField: sortField,
            sortAscending: sortAscending,
            contentIndexingEnabled: contentIndexingEnabled,
            contentSearchUsesIndexRoots: contentSearchUsesIndexRoots,
            contentSearchUsesIndexExclusions: contentSearchUsesIndexExclusions,
            contentSearchRoots: normalizedExistingPaths(contentSearchUsesIndexRoots ? indexRoots : contentSearchRoots),
            contentSearchExcludedPaths: normalizedPaths(contentSearchUsesIndexExclusions ? excludedPaths : contentSearchExcludedPaths),
            contentIndexRoots: normalizedExistingPaths(contentSearchUsesIndexRoots ? indexRoots : contentSearchRoots),
            contentExcludedPaths: normalizedPaths(contentSearchUsesIndexExclusions ? excludedPaths : contentSearchExcludedPaths),
            contentMaxFileSizeMB: min(max(contentMaxFileSizeMB, 0.1), 100.0),
            searchHistoryEnabled: searchHistoryEnabled,
            searchHistoryLimit: clamped(searchHistoryLimit, 0, 5000),
            showPath: showPath,
            showSize: showSize,
            showModifiedDate: showModifiedDate,
            showContentSnippets: showContentSnippets,
            resultDensity: resultDensity,
            httpServerEnabled: httpServerEnabled,
            httpPort: clamped(httpPort, 1024, 65535),
            hideDockIcon: hideDockIcon,
            automaticMaintenanceEnabled: automaticMaintenanceEnabled,
            enablePinyinInitials: enablePinyinInitials,
            enablePathSearchAcceleration: enablePathSearchAcceleration,
            enterKeyAction: enterKeyAction
        )
    }

    func resetIndexDefaults() {
        indexRoots = Self.defaultIndexRoots()
        excludedPaths = Self.defaultExcludedPaths()
        excludedPatterns = Self.defaultExcludedPatterns()
        indexHiddenFiles = false
        indexSystemFiles = false
        indexAppBundleContents = false
        contentSearchUsesIndexRoots = true
        contentSearchUsesIndexExclusions = true
        contentSearchRoots = Self.defaultIndexRoots()
        contentSearchExcludedPaths = Self.defaultExcludedPaths()
        contentIndexRoots = Self.defaultIndexRoots()
        contentExcludedPaths = Self.defaultExcludedPaths()
    }

    func clearSearchHistory() {
        UserDefaults.standard.removeObject(forKey: SearchHistoryStore.defaultsKey)
    }

    private func migrateSettingsIfNeeded() {
        let version = defaults.object(forKey: Key.settingsSchemaVersion) as? Int ?? 0

        if version < 1 {
            let appRoots = Self.defaultApplicationRoots()
            indexRoots = normalizedPaths(appRoots + indexRoots)
        }

        if version < 2 {
            let supplementalRoots = Self.defaultSupplementalIndexRoots()
            indexRoots = normalizedExistingPaths(indexRoots + supplementalRoots)
            if contentSearchUsesIndexRoots {
                contentSearchRoots = normalizedExistingPaths(contentSearchRoots + supplementalRoots)
                contentIndexRoots = normalizedExistingPaths(contentIndexRoots + supplementalRoots)
            }
        }

        if version < 3,
           defaults.object(forKey: Key.enablePinyinInitials) == nil,
           defaults.object(forKey: Key.enablePathSearchAcceleration) == nil,
           (defaults.object(forKey: Key.lowMemoryMode) as? Bool) == true {
            enablePinyinInitials = false
            enablePathSearchAcceleration = false
        }

        defaults.set(3, forKey: Key.settingsSchemaVersion)
    }

    private func save(_ value: Bool, _ key: String) {
        defaults.set(value, forKey: key)
    }

    private func save(_ value: Int, _ key: String) {
        defaults.set(value, forKey: key)
    }

    private func save(_ value: Double, _ key: String) {
        defaults.set(value, forKey: key)
    }

    private func save(_ value: String, _ key: String) {
        defaults.set(value, forKey: key)
    }

    private func saveArray(_ value: [String], _ key: String) {
        defaults.set(sortedSettingsList(normalizedPaths(value)), forKey: key)
    }

    private static func defaultIndexRoots() -> [String] {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let names = ["Desktop", "Downloads", "Documents", "Pictures", "Movies", "Music"]
        let userFolders = names
            .map { home.appendingPathComponent($0).path }
            .filter { FileManager.default.fileExists(atPath: $0) }
        return normalizedExistingPaths(defaultApplicationRoots() + userFolders + defaultSupplementalIndexRoots())
    }

    private static func defaultApplicationRoots() -> [String] {
        normalizedExistingPaths(["/Applications", "/System/Applications"])
    }

    private static func defaultSupplementalIndexRoots() -> [String] {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return normalizedExistingPaths([
            home.appendingPathComponent("Library/Mobile Documents/com~apple~CloudDocs").path,
            home.appendingPathComponent("Public").path
        ])
    }

    private static func defaultExcludedPaths() -> [String] {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        return [
            "\(home)/Library",
            "\(home)/.Trash",
            "\(home)/.cache",
            "\(home)/.npm",
            "\(home)/.cargo",
            "\(home)/.rustup",
            "/Library/Caches",
            "/System",
            "/private/var",
            "/Volumes"
        ]
    }

    private static func defaultExcludedPatterns() -> [String] {
        [
            ".git",
            ".svn",
            ".hg",
            "node_modules",
            "Pods",
            ".build",
            "DerivedData",
            "__pycache__",
            ".pytest_cache",
            ".DS_Store",
            "*.tmp",
            "*.temp",
            "*.cache",
            "*.download"
        ]
    }
}

func normalizedPaths(_ paths: [String]) -> [String] {
    var seen = Set<String>()
    var result: [String] = []
    for raw in paths {
        let expanded = (raw as NSString).expandingTildeInPath
        let standardized = URL(fileURLWithPath: expanded).standardizedFileURL.path
        guard !standardized.isEmpty, !seen.contains(standardized) else { continue }
        seen.insert(standardized)
        result.append(standardized)
    }
    return result
}

func sortedSettingsList(_ values: [String]) -> [String] {
    values.sorted { lhs, rhs in
        lhs.localizedStandardCompare(rhs) == .orderedAscending
    }
}

func normalizedExistingPaths(_ paths: [String]) -> [String] {
    normalizedPaths(paths).filter { FileManager.default.fileExists(atPath: $0) }
}

func clamped<T: Comparable>(_ value: T, _ lower: T, _ upper: T) -> T {
    min(max(value, lower), upper)
}
