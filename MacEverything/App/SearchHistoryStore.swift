import Foundation

/// Persists past search queries and provides prefix-based autocomplete suggestions.
@MainActor
final class SearchHistoryStore {
    static let defaultsKey = "searchHistory"
    private static let minQueryLength = 2

    struct Entry: Codable {
        var query: String
        var lastUsed: Date
        var count: Int
    }

    private var entries: [Entry] = []

    init() {
        load()
    }

    // MARK: - Public API

    func recordQuery(_ query: String) {
        let settings = AppSettings.shared.snapshot
        guard settings.searchHistoryEnabled, settings.searchHistoryLimit > 0 else { return }

        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.count >= Self.minQueryLength,
              !trimmed.lowercased().hasPrefix("infile:"),
              !trimmed.lowercased().hasPrefix("content:") else { return }

        let lower = trimmed.lowercased()
        if let idx = entries.firstIndex(where: { $0.query.lowercased() == lower }) {
            entries[idx].lastUsed = Date()
            entries[idx].count += 1
            // Keep the most-recently-typed casing
            entries[idx].query = trimmed
        } else {
            entries.append(Entry(query: trimmed, lastUsed: Date(), count: 1))
        }

        // Evict oldest entries when over capacity
        if entries.count > settings.searchHistoryLimit {
            entries.sort { $0.lastUsed > $1.lastUsed }
            entries = Array(entries.prefix(settings.searchHistoryLimit))
        }

        save()
    }

    func bestMatch(for prefix: String) -> String? {
        guard AppSettings.shared.snapshot.searchHistoryEnabled else { return nil }
        guard !prefix.isEmpty else { return nil }
        let lowerPrefix = prefix.lowercased()

        let matches = entries.filter { $0.query.lowercased().hasPrefix(lowerPrefix) }
        // Sort by frequency desc, then recency desc
        let best = matches.max { a, b in
            if a.count != b.count { return a.count < b.count }
            return a.lastUsed < b.lastUsed
        }
        return best?.query
    }

    // MARK: - Persistence

    private func load() {
        guard let data = UserDefaults.standard.data(forKey: Self.defaultsKey) else { return }
        do {
            entries = try JSONDecoder().decode([Entry].self, from: data)
        } catch {
            AppLogger.warn("SearchHistory", "Failed to decode search history: \(error)")
            entries = []
        }
    }

    private func save() {
        guard let data = try? JSONEncoder().encode(entries) else { return }
        UserDefaults.standard.set(data, forKey: Self.defaultsKey)
    }
}
