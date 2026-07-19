import AppKit
import SwiftUI

enum ResultClickAction: Equatable {
    case selectExclusive
    case selectRange
    case toggleSelection
    case open
    case reveal
}

enum ResultClickResolver {
    static func actions(clickCount: Int,
                        modifiers: NSEvent.ModifierFlags,
                        supportsSelection: Bool) -> [ResultClickAction] {
        let relevantModifiers = modifiers.intersection([.command, .shift])

        switch clickCount {
        case 1:
            if relevantModifiers.contains(.command) {
                return supportsSelection ? [.toggleSelection] : [.reveal]
            }
            if relevantModifiers.contains(.shift), supportsSelection {
                return [.selectRange]
            }
            return supportsSelection ? [.selectExclusive] : []

        case 2:
            var actions: [ResultClickAction] = supportsSelection ? [.selectExclusive] : []
            actions.append(relevantModifiers.contains(.command) ? .reveal : .open)
            return actions

        default:
            return []
        }
    }
}

enum GridNavigation {
    static func downIndex(currentIndex: Int?, itemCount: Int, columns: Int) -> Int? {
        guard itemCount > 0 else { return nil }
        guard let currentIndex, (0..<itemCount).contains(currentIndex) else { return 0 }

        let candidate = currentIndex + max(1, columns)
        return candidate < itemCount ? candidate : nil
    }

    static func upIndex(currentIndex: Int?, itemCount: Int, columns: Int) -> Int? {
        guard itemCount > 0 else { return nil }
        guard let currentIndex, (0..<itemCount).contains(currentIndex) else {
            return itemCount - 1
        }

        let candidate = currentIndex - max(1, columns)
        return candidate >= 0 ? candidate : nil
    }

    static func rightIndex(currentIndex: Int?, itemCount: Int) -> Int? {
        guard itemCount > 0 else { return nil }
        guard let currentIndex, (0..<itemCount).contains(currentIndex) else { return 0 }
        let candidate = currentIndex + 1
        return candidate < itemCount ? candidate : nil
    }

    static func leftIndex(currentIndex: Int?, itemCount: Int) -> Int? {
        guard itemCount > 0,
              let currentIndex,
              (0..<itemCount).contains(currentIndex),
              currentIndex > 0 else { return nil }
        return currentIndex - 1
    }
}

/// Observes left mouse-down events without taking event ownership from SwiftUI.
/// This preserves context menus and dragging while avoiding tap-gesture arbitration.
struct ResultClickMonitor: NSViewRepresentable {
    let enabled: Bool
    let onMouseDown: (_ clickCount: Int, _ modifiers: NSEvent.ModifierFlags) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(parent: self)
    }

    func makeNSView(context: Context) -> NSView {
        let view = PassthroughView()
        context.coordinator.view = view
        context.coordinator.installMonitor()
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        context.coordinator.parent = self
    }

    static func dismantleNSView(_ nsView: NSView, coordinator: Coordinator) {
        coordinator.removeMonitor()
    }

    final class Coordinator {
        var parent: ResultClickMonitor
        weak var view: NSView?
        private var monitor: Any?

        init(parent: ResultClickMonitor) {
            self.parent = parent
        }

        func installMonitor() {
            guard monitor == nil else { return }
            monitor = NSEvent.addLocalMonitorForEvents(matching: .leftMouseDown) { [weak self] event in
                guard let self, self.parent.enabled,
                      let view = self.view,
                      event.window === view.window else { return event }

                let location = view.convert(event.locationInWindow, from: nil)
                guard view.bounds.contains(location) else { return event }
                self.parent.onMouseDown(event.clickCount, event.modifierFlags)
                return event
            }
        }

        func removeMonitor() {
            if let monitor {
                NSEvent.removeMonitor(monitor)
                self.monitor = nil
            }
        }

        deinit {
            removeMonitor()
        }
    }

    private final class PassthroughView: NSView {
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
    }
}
