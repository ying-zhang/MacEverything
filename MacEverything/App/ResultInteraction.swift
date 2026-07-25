import AppKit
import SwiftUI

enum AdvancedFileSizeFilter: String, CaseIterable, Identifiable {
    case any
    case underOneMB
    case oneToHundredMB
    case overHundredMB
    case custom

    var id: String { rawValue }
}

enum AdvancedModifiedDateFilter: String, CaseIterable, Identifiable {
    case any
    case today
    case lastSevenDays
    case lastThirtyDays
    case custom

    var id: String { rawValue }
}

struct AdvancedFilterState: Equatable {
    var fileSize: AdvancedFileSizeFilter = .any
    var customMinimumSizeMB = 1
    var customMaximumSizeMB = 100
    var modifiedDate: AdvancedModifiedDateFilter = .any
    var customModifiedFrom: Date
    var customModifiedTo: Date

    init(now: Date = Date(), calendar: Calendar = .current) {
        customModifiedTo = now
        customModifiedFrom = calendar.date(byAdding: .day, value: -7, to: now) ?? now
    }

    var activeFilterCount: Int {
        (fileSize == .any ? 0 : 1) + (modifiedDate == .any ? 0 : 1)
    }

    func hasSamePresets(as other: AdvancedFilterState) -> Bool {
        fileSize == other.fileSize && modifiedDate == other.modifiedDate
    }

    var queryTokens: [String] {
        var tokens: [String] = []

        switch fileSize {
        case .any:
            break
        case .underOneMB:
            tokens.append("size:<1mb")
        case .oneToHundredMB:
            tokens.append("size:1mb..100mb")
        case .overHundredMB:
            tokens.append("size:>100mb")
        case .custom:
            let lower = max(0, min(customMinimumSizeMB, customMaximumSizeMB))
            let upper = max(0, max(customMinimumSizeMB, customMaximumSizeMB))
            tokens.append("size:\(lower)mb..\(upper)mb")
        }

        switch modifiedDate {
        case .any:
            break
        case .today:
            tokens.append("dm:today")
        case .lastSevenDays:
            tokens.append("dm:last7days")
        case .lastThirtyDays:
            tokens.append("dm:last30days")
        case .custom:
            let lower = min(customModifiedFrom, customModifiedTo)
            let upper = max(customModifiedFrom, customModifiedTo)
            tokens.append("dm:\(Self.queryDate(lower))..\(Self.queryDate(upper))")
        }

        return tokens
    }

    private static func queryDate(_ date: Date) -> String {
        let components = Calendar.current.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d",
                      components.year ?? 1970,
                      components.month ?? 1,
                      components.day ?? 1)
    }
}

enum ResultRowMetrics {
    static func effectiveHeight(configuredHeight: CGFloat, fontLineHeight: CGFloat) -> CGFloat {
        max(configuredHeight, ceil(fontLineHeight) + 4)
    }
}

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
