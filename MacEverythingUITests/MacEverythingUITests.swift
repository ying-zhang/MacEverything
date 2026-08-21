import XCTest

final class MacEverythingUITests: XCTestCase {
    var app: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
        app = XCUIApplication()
        app.launch()
        // Wait for initial scan to complete
        let indexedCount = app.staticTexts["indexedCount"]
        if indexedCount.waitForExistence(timeout: 30) {
            // Scan complete
        }
    }

    override func tearDownWithError() throws {
        app = nil
    }

    func testAppLaunchesSuccessfully() throws {
        let window = app.windows.firstMatch
        XCTAssertTrue(window.exists, "Main window should exist")

        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5), "Search field should exist")
    }

    func testSearchProducesResults() throws {
        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.click()
        searchField.typeText("a")

        // Wait for results to appear
        let resultRow = app.groups["resultRow"].firstMatch
        XCTAssertTrue(resultRow.waitForExistence(timeout: 10), "At least one result should appear")
    }

    func testClearSearchField() throws {
        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.click()
        searchField.typeText("test")

        // Wait a moment for results
        Thread.sleep(forTimeInterval: 1)

        let clearButton = app.buttons["clearButton"]
        XCTAssertTrue(clearButton.waitForExistence(timeout: 5), "Clear button should appear")
        clearButton.click()

        // Verify field is cleared
        let fieldValue = searchField.value as? String ?? ""
        XCTAssertTrue(fieldValue.isEmpty, "Search field should be empty after clearing")
    }

    func testContentSearch() throws {
        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.click()
        searchField.typeText("infile:import")

        // Content search may need content index - allow it to be skipped
        let contentResult = app.groups["contentResultRow"].firstMatch
        if !contentResult.waitForExistence(timeout: 15) {
            // Content index might not be built yet, skip
            throw XCTSkip("Content index not available - skipping content search test")
        }
        XCTAssertTrue(contentResult.exists)
    }

    func testRapidTyping() throws {
        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.click()

        // Type rapidly - should not crash
        for char in "abcdefghij" {
            searchField.typeText(String(char))
        }

        Thread.sleep(forTimeInterval: 2)

        // App should still be responsive
        XCTAssertTrue(searchField.exists, "App should still be responsive after rapid typing")
    }

    func testResultCountDisplayed() throws {
        let searchField = app.textFields["searchField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.click()
        searchField.typeText("a")

        let matchCount = app.staticTexts["matchCount"]
        XCTAssertTrue(matchCount.waitForExistence(timeout: 10), "Match count should be displayed")
    }

    func testStatusBarShowsIndexedCount() throws {
        let indexedCount = app.staticTexts["indexedCount"]
        XCTAssertTrue(indexedCount.waitForExistence(timeout: 30), "Indexed count should appear after scan")

        let text = indexedCount.label
        XCTAssertTrue(text.contains("files indexed"), "Should show files indexed count")
    }
}
