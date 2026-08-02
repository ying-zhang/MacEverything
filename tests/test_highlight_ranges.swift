/// Tests for hint-based highlight range computation (TextHighlight.swift).
/// Compile & run:
///   swiftc -o test_highlight_ranges \
///     -sdk $(xcrun --show-sdk-path) \
///     MacEverything/App/HighlightHint.swift \
///     MacEverything/App/TextHighlight.swift \
///     tests/test_highlight_ranges.swift && ./test_highlight_ranges

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

/// Helper: convert Range<String.Index> to Range<Int> offsets for easy assertion.
func offsets(_ text: String, _ ranges: [Range<String.Index>]) -> [(Int, Int)] {
    ranges.map { r in
        let start = text.distance(from: text.startIndex, to: r.lowerBound)
        let end = text.distance(from: text.startIndex, to: r.upperBound)
        return (start, end)
    }
}

// ---------------------------------------------------------------------------
// Tests: computeRangesForHint
// ---------------------------------------------------------------------------

func testSubstringBasic() {
    print("  test: substring match — single occurrence")
    let hint = HighlightHint(text: "hello", matchMode: .substring)
    let ranges = computeRangesForHint(in: "HelloWorld.cpp", hint: hint)
    assertEqual(ranges.count, 1, "should find 1 match")
    let o = offsets("HelloWorld.cpp", ranges)
    assertEqual(o[0].0, 0, "start offset")
    assertEqual(o[0].1, 5, "end offset")
}

func testSubstringMultiple() {
    print("  test: substring match — multiple occurrences")
    let hint = HighlightHint(text: "test", matchMode: .substring)
    let text = "test_file_test.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 2, "should find 2 matches")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "first start")
    assertEqual(o[0].1, 4, "first end")
    assertEqual(o[1].0, 10, "second start")
    assertEqual(o[1].1, 14, "second end")
}

func testCaseFoldExpansionUsesOriginalIndices() {
    print("  test: Unicode case-fold expansion keeps original indices")
    let text = "İD.png"
    let hint = HighlightHint(text: "png", matchMode: .substring)
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find suffix after expanding lowercase character")
    if let range = ranges.first {
        let o = offsets(text, [range])
        assertEqual(o[0].0, 3, "suffix start uses original string")
        assertEqual(o[0].1, 6, "suffix end uses original string")
    }
}

func testSubstringCaseSensitive() {
    print("  test: substring case-sensitive — matches only exact case")
    let hint = HighlightHint(text: "Hello", matchMode: .substring, caseSensitive: true)
    let ranges1 = computeRangesForHint(in: "HelloWorld", hint: hint)
    assertEqual(ranges1.count, 1, "should find 'Hello'")
    let ranges2 = computeRangesForHint(in: "helloworld", hint: hint)
    assertEqual(ranges2.count, 0, "should not find 'hello' (wrong case)")
}

func testSubstringNoMatch() {
    print("  test: substring — no match")
    let hint = HighlightHint(text: "xyz", matchMode: .substring)
    let ranges = computeRangesForHint(in: "HelloWorld", hint: hint)
    assertEqual(ranges.count, 0, "should find no matches")
}

func testSubstringEmpty() {
    print("  test: substring — empty hint text")
    let hint = HighlightHint(text: "", matchMode: .substring)
    let ranges = computeRangesForHint(in: "HelloWorld", hint: hint)
    assertEqual(ranges.count, 0, "empty hint should produce no ranges")
}

func testPinyinInitialsChinese() {
    print("  test: pinyin initials — Chinese filename")
    let hint = HighlightHint(text: "zywj", matchMode: .substring)
    let text = "重要文件.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find initials match")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "match starts at first Chinese character")
    assertEqual(o[0].1, 4, "match covers four Chinese characters")
}

func testPinyinInitialsMixedChineseEnglish() {
    print("  test: pinyin initials — mixed Chinese and English")
    let hint = HighlightHint(text: "zyfile", matchMode: .substring)
    let text = "重要File.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find mixed initials/ascii match")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "match starts at first Chinese character")
    assertEqual(o[0].1, 6, "match covers Chinese prefix and File")
}

func testPinyinInitialsPartialChinese() {
    print("  test: pinyin initials — partial Chinese filename")
    let hint = HighlightHint(text: "wj", matchMode: .substring)
    let text = "重要文件.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find partial initials match")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 2, "partial match starts at 文")
    assertEqual(o[0].1, 4, "partial match ends after 件")
}

func testPinyinInitialsCaseSensitiveDisabled() {
    print("  test: pinyin initials — disabled for case-sensitive hints")
    let hint = HighlightHint(text: "zywj", matchMode: .substring, caseSensitive: true)
    let ranges = computeRangesForHint(in: "重要文件.txt", hint: hint)
    assertEqual(ranges.count, 0, "case-sensitive substring should not use pinyin initials")
}

func testPinyinInitialsSkipSeparators() {
    print("  test: pinyin initials — separators are not highlighted")
    let hint = HighlightHint(text: "zywj", matchMode: .substring)
    let text = "重要-文件.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 2, "separator should split highlighted source ranges")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "first range starts at 重")
    assertEqual(o[0].1, 2, "first range ends before separator")
    assertEqual(o[1].0, 3, "second range starts after separator")
    assertEqual(o[1].1, 5, "second range ends after 件")
}

func testGlobMatch() {
    print("  test: glob match — highlights literal segments")
    let hint = HighlightHint(text: "*.cpp", matchMode: .glob)
    let text = "main.cpp"
    let ranges = computeRangesForHint(in: text, hint: hint)
    // Glob "*.cpp" → literal segment "cpp", should find at offset 5..8
    assertEqual(ranges.count, 1, "should find 1 literal match")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 4, "should match '.cpp' — the dot is a literal")
    assertEqual(o[0].1, 8, "end offset")
}

func testGlobMultipleLiterals() {
    print("  test: glob — multiple literal segments")
    let hint = HighlightHint(text: "test*file", matchMode: .glob)
    let text = "test_data_file.txt"
    let ranges = computeRangesForHint(in: text, hint: hint)
    // Literals: "test", "file"
    assertEqual(ranges.count, 2, "should find 2 literals")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "test starts at 0")
    assertEqual(o[0].1, 4, "test ends at 4")
    assertEqual(o[1].0, 10, "file starts at 10")
    assertEqual(o[1].1, 14, "file ends at 14")
}

func testGlobAllWild() {
    print("  test: glob — all wildcards, no literals")
    let hint = HighlightHint(text: "*?*", matchMode: .glob)
    let ranges = computeRangesForHint(in: "anything.txt", hint: hint)
    assertEqual(ranges.count, 0, "no literals to highlight")
}

func testRegexMatch() {
    print("  test: regex match")
    let hint = HighlightHint(text: "test\\d+", matchMode: .regex)
    let text = "file_test123_data"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find 1 regex match")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 5, "start of test123")
    assertEqual(o[0].1, 12, "end of test123")
}

func testRegexCaseInsensitive() {
    print("  test: regex case-insensitive (default)")
    let hint = HighlightHint(text: "hello", matchMode: .regex)
    let ranges = computeRangesForHint(in: "HELLO_world", hint: hint)
    assertEqual(ranges.count, 1, "should match HELLO case-insensitively")
}

func testRegexCaseSensitive() {
    print("  test: regex case-sensitive")
    let hint = HighlightHint(text: "Hello", matchMode: .regex, caseSensitive: true)
    let ranges1 = computeRangesForHint(in: "Hello_world", hint: hint)
    assertEqual(ranges1.count, 1, "should match Hello")
    let ranges2 = computeRangesForHint(in: "hello_world", hint: hint)
    assertEqual(ranges2.count, 0, "should not match hello")
}

func testRegexInvalid() {
    print("  test: regex — invalid pattern returns empty")
    let hint = HighlightHint(text: "[invalid", matchMode: .regex)
    let ranges = computeRangesForHint(in: "test", hint: hint)
    assertEqual(ranges.count, 0, "invalid regex should produce no ranges")
}

func testWholeWordMatch() {
    print("  test: whole word match")
    let hint = HighlightHint(text: "test", matchMode: .wholeWord)
    let text = "run test now"
    let ranges = computeRangesForHint(in: text, hint: hint)
    assertEqual(ranges.count, 1, "should find whole word 'test'")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 4, "start of 'test'")
    assertEqual(o[0].1, 8, "end of 'test'")
}

func testWholeWordNoPartial() {
    print("  test: whole word — no partial match")
    let hint = HighlightHint(text: "test", matchMode: .wholeWord)
    let ranges = computeRangesForHint(in: "testing", hint: hint)
    assertEqual(ranges.count, 0, "should not match partial word 'testing'")
}

func testWholeFilenameMatch() {
    print("  test: whole filename — exact match")
    let hint = HighlightHint(text: "readme", matchMode: .wholeFilename)
    let ranges = computeRangesForHint(in: "readme", hint: hint)
    assertEqual(ranges.count, 1, "should match entire text")
    let o = offsets("readme", ranges)
    assertEqual(o[0].0, 0, "start")
    assertEqual(o[0].1, 6, "end")
}

func testWholeFilenameNoPartial() {
    print("  test: whole filename — no partial")
    let hint = HighlightHint(text: "readme", matchMode: .wholeFilename)
    let ranges = computeRangesForHint(in: "readme.md", hint: hint)
    assertEqual(ranges.count, 0, "should not match partial filename")
}

func testWholeFilenameCaseInsensitive() {
    print("  test: whole filename — case insensitive")
    let hint = HighlightHint(text: "README", matchMode: .wholeFilename)
    let ranges = computeRangesForHint(in: "readme", hint: hint)
    assertEqual(ranges.count, 1, "should match case-insensitively")
}

func testWholeFilenameCaseSensitive() {
    print("  test: whole filename — case sensitive")
    let hint = HighlightHint(text: "README", matchMode: .wholeFilename, caseSensitive: true)
    let ranges = computeRangesForHint(in: "readme", hint: hint)
    assertEqual(ranges.count, 0, "should not match wrong case")
}

// ---------------------------------------------------------------------------
// Tests: computeHighlightRanges (multi-hint merging)
// ---------------------------------------------------------------------------

func testMultiHintMerge() {
    print("  test: multiple hints — ranges merge correctly")
    let hints = [
        HighlightHint(text: "hello", matchMode: .substring),
        HighlightHint(text: "world", matchMode: .substring)
    ]
    let text = "hello_world"
    let ranges = computeHighlightRanges(in: text, hints: hints)
    assertEqual(ranges.count, 2, "should produce 2 merged ranges")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "hello starts at 0")
    assertEqual(o[0].1, 5, "hello ends at 5")
    assertEqual(o[1].0, 6, "world starts at 6")
    assertEqual(o[1].1, 11, "world ends at 11")
}

func testMultiHintOverlap() {
    print("  test: overlapping hints merge into single range")
    let hints = [
        HighlightHint(text: "hell", matchMode: .substring),
        HighlightHint(text: "ello", matchMode: .substring)
    ]
    let text = "hello"
    let ranges = computeHighlightRanges(in: text, hints: hints)
    // "hell" = 0..4, "ello" = 1..5, merged = 0..5
    assertEqual(ranges.count, 1, "overlapping ranges should merge")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "merged start")
    assertEqual(o[0].1, 5, "merged end")
}

func testEmptyHints() {
    print("  test: empty hints — no ranges")
    let ranges = computeHighlightRanges(in: "hello world", hints: [])
    assertEqual(ranges.count, 0, "empty hints should produce no ranges")
}

func testMixedModes() {
    print("  test: mixed modes — substring + regex")
    let hints = [
        HighlightHint(text: "hello", matchMode: .substring),
        HighlightHint(text: "\\d+", matchMode: .regex)
    ]
    let text = "hello_123_world"
    let ranges = computeHighlightRanges(in: text, hints: hints)
    assertEqual(ranges.count, 2, "should find substring and regex matches")
    let o = offsets(text, ranges)
    assertEqual(o[0].0, 0, "hello at 0")
    assertEqual(o[0].1, 5, "hello to 5")
    assertEqual(o[1].0, 6, "123 at 6")
    assertEqual(o[1].1, 9, "123 to 9")
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

@main
struct TestRunner {
    static func main() {
        print("Running highlight range computation tests...")

        // computeRangesForHint
        testSubstringBasic()
        testSubstringMultiple()
        testCaseFoldExpansionUsesOriginalIndices()
        testSubstringCaseSensitive()
        testSubstringNoMatch()
        testSubstringEmpty()
        testPinyinInitialsChinese()
        testPinyinInitialsMixedChineseEnglish()
        testPinyinInitialsPartialChinese()
        testPinyinInitialsCaseSensitiveDisabled()
        testPinyinInitialsSkipSeparators()
        testGlobMatch()
        testGlobMultipleLiterals()
        testGlobAllWild()
        testRegexMatch()
        testRegexCaseInsensitive()
        testRegexCaseSensitive()
        testRegexInvalid()
        testWholeWordMatch()
        testWholeWordNoPartial()
        testWholeFilenameMatch()
        testWholeFilenameNoPartial()
        testWholeFilenameCaseInsensitive()
        testWholeFilenameCaseSensitive()

        // computeHighlightRanges
        testMultiHintMerge()
        testMultiHintOverlap()
        testEmptyHints()
        testMixedModes()

        print("\nResults: \(testsPassed) passed, \(testsFailed) failed")
        if testsFailed > 0 {
            exit(1)
        } else {
            print("All tests passed!")
            exit(0)
        }
    }
}
