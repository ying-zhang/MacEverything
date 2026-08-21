import Foundation

/// Pure state machine for index-change refresh throttling with window focus awareness.
///
/// Decisions:
/// - Window focused + no cooldown active → refresh immediately, start cooldown
/// - Window focused + cooldown active → mark pending (coalesce)
/// - Window unfocused → mark pending, never refresh
/// - Focus regained + pending → refresh immediately
///
/// This class is synchronous and all state transitions are main-actor isolated.
@MainActor
final class IndexRefreshThrottle {

    /// Called when a refresh should actually be performed.
    var onRefresh: (() -> Void)?

    /// Whether the window is currently focused.
    private(set) var isFocused: Bool = true

    /// Whether there are pending changes that haven't been refreshed yet.
    private(set) var isPending: Bool = false

    /// Whether a cooldown timer is currently active (throttle window).
    private(set) var isCooldownActive: Bool = false

    /// Count of how many times onRefresh was invoked (for testing).
    private(set) var refreshCount: Int = 0

    /// Called when an index change event arrives (e.g., from FSEvents).
    /// Returns true if a refresh was triggered, false if coalesced/deferred.
    @discardableResult
    func indexChanged() -> Bool {
        guard isFocused else {
            isPending = true
            return false
        }
        if isCooldownActive {
            isPending = true
            return false
        }
        doRefresh()
        isCooldownActive = true
        return true
    }

    /// Called when the cooldown timer fires (end of throttle window).
    /// Returns true if a trailing refresh was triggered.
    @discardableResult
    func cooldownExpired() -> Bool {
        isCooldownActive = false
        if isPending && isFocused {
            isPending = false
            doRefresh()
            isCooldownActive = true
            return true
        }
        return false
    }

    /// Called when window focus changes.
    /// Returns true if a catch-up refresh was triggered on focus regain.
    @discardableResult
    func focusChanged(_ focused: Bool) -> Bool {
        isFocused = focused
        if focused && isPending {
            isPending = false
            doRefresh()
            isCooldownActive = true
            return true
        }
        return false
    }

    private func doRefresh() {
        refreshCount += 1
        onRefresh?()
    }
}
