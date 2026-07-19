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

        if failures > 0 {
            exit(1)
        }
        print("Result interaction tests passed")
    }
}
