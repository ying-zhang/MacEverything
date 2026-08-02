/// Tests for IndexRefreshThrottle — the focus-aware index refresh state machine.
/// Compile & run:  swiftc -o test_throttle MacEverything/App/IndexRefreshThrottle.swift tests/test_index_refresh_throttle.swift && ./test_throttle

import Foundation

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

var testsPassed = 0
var testsFailed = 0

func check(_ condition: Bool, _ msg: String, file: String = #file, line: Int = #line) {
    if condition {
        testsPassed += 1
    } else {
        testsFailed += 1
        print("  FAIL [\(file):\(line)] \(msg)")
    }
}

func assertEqual<T: Equatable>(_ a: T, _ b: T, _ msg: String = "", file: String = #file, line: Int = #line) {
    if a == b {
        testsPassed += 1
    } else {
        testsFailed += 1
        print("  FAIL [\(file):\(line)] expected \(b), got \(a). \(msg)")
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

@MainActor func testFocusedFirstEvent() {
    print("  test: focused first event triggers immediate refresh")
    let t = IndexRefreshThrottle()
    let triggered = t.indexChanged()
    check(triggered, "should trigger refresh")
    assertEqual(t.refreshCount, 1, "refreshCount")
    check(t.isCooldownActive, "cooldown should be active")
    check(!t.isPending, "nothing pending")
}

@MainActor func testFocusedCoalesceDuringCooldown() {
    print("  test: focused events during cooldown are coalesced")
    let t = IndexRefreshThrottle()
    t.indexChanged()  // first — triggers refresh
    let r2 = t.indexChanged()  // during cooldown
    let r3 = t.indexChanged()  // during cooldown
    check(!r2, "second should not trigger")
    check(!r3, "third should not trigger")
    assertEqual(t.refreshCount, 1, "only one refresh so far")
    check(t.isPending, "pending should be set")
}

@MainActor func testCooldownExpiredWithPending() {
    print("  test: cooldown expiry with pending triggers trailing refresh")
    let t = IndexRefreshThrottle()
    t.indexChanged()  // refresh #1
    t.indexChanged()  // coalesced, sets pending
    let trailing = t.cooldownExpired()
    check(trailing, "trailing refresh should fire")
    assertEqual(t.refreshCount, 2, "two refreshes total")
    check(t.isCooldownActive, "new cooldown started for trailing")
    check(!t.isPending, "pending cleared")
}

@MainActor func testCooldownExpiredNoPending() {
    print("  test: cooldown expiry without pending does nothing")
    let t = IndexRefreshThrottle()
    t.indexChanged()
    let trailing = t.cooldownExpired()
    check(!trailing, "no trailing refresh")
    assertEqual(t.refreshCount, 1, "still one refresh")
    check(!t.isCooldownActive, "cooldown ended")
}

@MainActor func testUnfocusedEventsDeferred() {
    print("  test: unfocused events are deferred, never refresh")
    let t = IndexRefreshThrottle()
    t.focusChanged(false)  // lose focus
    let r1 = t.indexChanged()
    let r2 = t.indexChanged()
    let r3 = t.indexChanged()
    check(!r1 && !r2 && !r3, "no refresh when unfocused")
    assertEqual(t.refreshCount, 0, "zero refreshes")
    check(t.isPending, "pending set")
    check(!t.isCooldownActive, "no cooldown")
}

@MainActor func testFocusRegainWithPending() {
    print("  test: focus regain with pending triggers catch-up refresh")
    let t = IndexRefreshThrottle()
    t.focusChanged(false)
    t.indexChanged()  // deferred
    t.indexChanged()  // deferred
    assertEqual(t.refreshCount, 0, "no refresh yet")
    let catchUp = t.focusChanged(true)
    check(catchUp, "catch-up refresh should fire")
    assertEqual(t.refreshCount, 1, "one refresh on regain")
    check(!t.isPending, "pending cleared")
    check(t.isCooldownActive, "cooldown should be active after focus-regain refresh")
}

@MainActor func testFocusRegainWithoutPending() {
    print("  test: focus regain without pending does nothing")
    let t = IndexRefreshThrottle()
    t.focusChanged(false)
    let catchUp = t.focusChanged(true)
    check(!catchUp, "no catch-up needed")
    assertEqual(t.refreshCount, 0, "zero refreshes")
}

@MainActor func testFocusedThenUnfocusedDuringCooldown() {
    print("  test: losing focus during cooldown, events deferred")
    let t = IndexRefreshThrottle()
    t.indexChanged()        // refresh #1, cooldown starts
    t.focusChanged(false)   // lose focus
    t.indexChanged()        // deferred (unfocused)
    assertEqual(t.refreshCount, 1, "only initial refresh")
    check(t.isPending, "pending from unfocused event")
    // Cooldown expires while unfocused — pending stays but no refresh
    let trailing = t.cooldownExpired()
    check(!trailing, "cooldown expiry while unfocused should not refresh")
    check(t.isPending, "pending preserved")
    // Regain focus
    let catchUp = t.focusChanged(true)
    check(catchUp, "catch-up on regain")
    assertEqual(t.refreshCount, 2, "two total refreshes")
}

@MainActor func testCallbackInvoked() {
    print("  test: onRefresh callback invoked on each refresh")
    let t = IndexRefreshThrottle()
    var callbackCount = 0
    t.onRefresh = { callbackCount += 1 }
    t.indexChanged()
    t.indexChanged()  // coalesced
    t.cooldownExpired()  // trailing
    assertEqual(callbackCount, 2, "callback called twice")
    assertEqual(t.refreshCount, 2, "refreshCount matches")
}

@MainActor func testFullCycle() {
    print("  test: full cycle — focused, unfocused, regain, events throughout")
    let t = IndexRefreshThrottle()

    // Phase 1: Focused, two events
    t.indexChanged()  // refresh #1
    t.indexChanged()  // coalesced
    t.cooldownExpired()  // refresh #2 (trailing)
    t.cooldownExpired()  // nothing
    assertEqual(t.refreshCount, 2)

    // Phase 2: Lose focus, events pile up
    t.focusChanged(false)
    t.indexChanged()
    t.indexChanged()
    t.indexChanged()
    assertEqual(t.refreshCount, 2, "no refreshes while unfocused")

    // Phase 3: Regain focus
    t.focusChanged(true)  // refresh #3 (catch-up), cooldown starts
    assertEqual(t.refreshCount, 3)
    check(t.isCooldownActive, "cooldown active after focus regain")

    // Phase 4: Normal focused operation resumes — must expire cooldown first
    t.cooldownExpired()  // no pending, cooldown ends
    check(!t.isCooldownActive, "cooldown ended")
    t.indexChanged()  // refresh #4
    assertEqual(t.refreshCount, 4)
}

@MainActor func testInitialState() {
    print("  test: initial state is focused, no pending, no cooldown")
    let t = IndexRefreshThrottle()
    check(t.isFocused, "starts focused")
    check(!t.isPending, "no pending")
    check(!t.isCooldownActive, "no cooldown")
    assertEqual(t.refreshCount, 0, "zero refreshes")
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

@main
struct TestRunner {
    @MainActor static func main() {
        print("Running IndexRefreshThrottle tests...")
        testInitialState()
        testFocusedFirstEvent()
        testFocusedCoalesceDuringCooldown()
        testCooldownExpiredWithPending()
        testCooldownExpiredNoPending()
        testUnfocusedEventsDeferred()
        testFocusRegainWithPending()
        testFocusRegainWithoutPending()
        testFocusedThenUnfocusedDuringCooldown()
        testCallbackInvoked()
        testFullCycle()

        print("\nResults: \(testsPassed) passed, \(testsFailed) failed")
        if testsFailed > 0 {
            exit(1)
        } else {
            print("All tests passed!")
            exit(0)
        }
    }
}
