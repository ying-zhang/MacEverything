import AppKit
import Foundation

private var failures = 0

private func expect(_ actual: [ResultClickAction],
                    _ expected: [ResultClickAction],
                    _ message: String) {
    if actual != expected {
        fputs("FAIL: \(message): expected \(expected), got \(actual)\n", stderr)
        failures += 1
    }
}

private func expect(_ actual: Int?, _ expected: Int?, _ message: String) {
    if actual != expected {
        fputs("FAIL: \(message): expected \(String(describing: expected)), got \(String(describing: actual))\n", stderr)
        failures += 1
    }
}

private func expect(_ actual: [String], _ expected: [String], _ message: String) {
    if actual != expected {
        fputs("FAIL: \(message): expected \(expected), got \(actual)\n", stderr)
        failures += 1
    }
}

private func expect(_ actual: Bool, _ expected: Bool, _ message: String) {
    if actual != expected {
        fputs("FAIL: \(message): expected \(expected), got \(actual)\n", stderr)
        failures += 1
    }
}

private func expect(_ actual: CGFloat, _ expected: CGFloat, _ message: String) {
    if actual != expected {
        fputs("FAIL: \(message): expected \(expected), got \(actual)\n", stderr)
        failures += 1
    }
}

@main
struct ResultInteractionTests {
    static func main() {
        expect(ResultClickResolver.actions(clickCount: 1, modifiers: [], supportsSelection: true),
               [.selectExclusive], "plain single click selects immediately")
        expect(ResultClickResolver.actions(clickCount: 1, modifiers: [.command], supportsSelection: true),
               [.toggleSelection], "command click toggles selection")
        expect(ResultClickResolver.actions(clickCount: 1, modifiers: [.shift], supportsSelection: true),
               [.selectRange], "shift click extends selection")
        expect(ResultClickResolver.actions(clickCount: 2, modifiers: [], supportsSelection: true),
               [.selectExclusive, .open], "double click selects and opens")
        expect(ResultClickResolver.actions(clickCount: 2, modifiers: [.command], supportsSelection: true),
               [.selectExclusive, .reveal], "command double click reveals")
        expect(ResultClickResolver.actions(clickCount: 1, modifiers: [], supportsSelection: false),
               [], "content single click remains passive")
        expect(ResultClickResolver.actions(clickCount: 2, modifiers: [], supportsSelection: false),
               [.open], "content double click opens")
        expect(ResultClickResolver.actions(clickCount: 2, modifiers: [.command], supportsSelection: false),
               [.reveal], "content command double click reveals")
        expect(ResultClickResolver.actions(clickCount: 3, modifiers: [], supportsSelection: true),
               [], "extra clicks do not repeat actions")

        expect(GridNavigation.downIndex(currentIndex: nil, itemCount: 11, columns: 5),
               0, "down selects the first item when selection is empty")
        expect(GridNavigation.downIndex(currentIndex: 2, itemCount: 11, columns: 5),
               7, "down preserves the column when the next row has that column")
        expect(GridNavigation.downIndex(currentIndex: 7, itemCount: 11, columns: 5),
               nil, "down does not jump sideways in an incomplete last row")
        expect(GridNavigation.downIndex(currentIndex: 10, itemCount: 11, columns: 5),
               nil, "down does not move past the last row")
        expect(GridNavigation.downIndex(currentIndex: nil, itemCount: 0, columns: 5),
               nil, "down has no target for an empty grid")
        expect(GridNavigation.downIndex(currentIndex: 0, itemCount: 2, columns: 0),
               1, "down treats an invalid column count as one")

        expect(GridNavigation.upIndex(currentIndex: nil, itemCount: 11, columns: 5),
               10, "up selects the last item when selection is empty")
        expect(GridNavigation.upIndex(currentIndex: 10, itemCount: 11, columns: 5),
               5, "up preserves the column from an incomplete last row")
        expect(GridNavigation.upIndex(currentIndex: 9, itemCount: 11, columns: 5),
               4, "up preserves the column from the preceding item")
        expect(GridNavigation.upIndex(currentIndex: 4, itemCount: 11, columns: 5),
               nil, "up does not move before the first row")
        expect(GridNavigation.upIndex(currentIndex: nil, itemCount: 0, columns: 5),
               nil, "up has no target for an empty grid")
        expect(GridNavigation.upIndex(currentIndex: 1, itemCount: 2, columns: 0),
               0, "up treats an invalid column count as one")

        expect(GridNavigation.rightIndex(currentIndex: nil, itemCount: 3),
               0, "right selects the first item when selection is empty")
        expect(GridNavigation.rightIndex(currentIndex: 1, itemCount: 3),
               2, "right advances one item")
        expect(GridNavigation.rightIndex(currentIndex: 2, itemCount: 3),
               nil, "right does not move past the last item")
        expect(GridNavigation.rightIndex(currentIndex: nil, itemCount: 0),
               nil, "right has no target for an empty grid")

        expect(GridNavigation.leftIndex(currentIndex: 2, itemCount: 3),
               1, "left moves back one item")
        expect(GridNavigation.leftIndex(currentIndex: 0, itemCount: 3),
               nil, "left does not move before the first item")
        expect(GridNavigation.leftIndex(currentIndex: nil, itemCount: 3),
               nil, "left does not create a selection")
        expect(GridNavigation.leftIndex(currentIndex: 3, itemCount: 3),
               nil, "left rejects an invalid selection")

        expect(ResultRowMetrics.effectiveHeight(configuredHeight: 38, fontLineHeight: 17.2),
               38, "configured row height is preserved when the font fits")
        expect(ResultRowMetrics.effectiveHeight(configuredHeight: 20, fontLineHeight: 28.1),
               33, "row height expands enough to avoid clipping a large font")

        var filters = AdvancedFilterState()
        let defaultFilters = filters
        expect(filters.queryTokens, [], "default advanced filters add no query tokens")
        filters.fileSize = .oneToHundredMB
        expect(filters.hasSamePresets(as: defaultFilters), false,
               "changing a filter preset requires an immediate search")
        filters.modifiedDate = .lastSevenDays
        expect(filters.queryTokens, ["size:1mb..100mb", "dm:last7days"],
               "size and modified-date presets compose as engine filters")

        filters.fileSize = .custom
        filters.customMinimumSizeMB = 500
        filters.customMaximumSizeMB = 10
        var editedCustomValues = filters
        editedCustomValues.customMaximumSizeMB = 20
        expect(filters.hasSamePresets(as: editedCustomValues), true,
               "editing custom values can use a debounced search")
        expect(filters.queryTokens.first.map { [$0] } ?? [], ["size:10mb..500mb"],
               "custom size range normalizes reversed bounds")

        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        let from = calendar.date(from: DateComponents(year: 2026, month: 7, day: 20, hour: 12))!
        let to = calendar.date(from: DateComponents(year: 2026, month: 7, day: 1, hour: 12))!
        filters.customModifiedFrom = from
        filters.customModifiedTo = to
        filters.modifiedDate = .custom
        expect(filters.queryTokens.last.map { [$0] } ?? [], ["dm:2026-07-01..2026-07-20"],
               "custom date range normalizes reversed bounds")

        expect(ResultPagination.nextEnd(currentCount: 0, totalCount: 750, pageSize: 200),
               200, "content results load the first 200-item batch")
        expect(ResultPagination.nextEnd(currentCount: 600, totalCount: 750, pageSize: 200),
               750, "the final content batch stops at the complete result count")
        expect(ResultPagination.nextEnd(currentCount: 750, totalCount: 750, pageSize: 200),
               750, "completed pagination does not advance")

        if failures > 0 {
            exit(1)
        }
        print("Result interaction tests passed")
    }
}
