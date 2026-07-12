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

        if failures > 0 {
            exit(1)
        }
        print("Result interaction tests passed")
    }
}
