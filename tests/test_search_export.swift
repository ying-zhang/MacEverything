import Foundation

private var failures = 0

private func expect(_ actual: String, _ expected: String, _ message: String) {
    guard actual != expected else { return }
    fputs("FAIL: \(message): expected \(expected), got \(actual)\n", stderr)
    failures += 1
}

@main
struct SearchExportTests {
    static func main() {
        expect(SearchExportSerializer.utf8BOM, "\u{FEFF}", "UTF-8 CSV BOM")
        expect(SearchExportSerializer.csvField("plain"), "\"plain\"", "plain CSV field")
        expect(SearchExportSerializer.csvField("a\"b"), "\"a\"\"b\"", "quote escaping")
        expect(SearchExportSerializer.csvField("=1+1"), "\"'=1+1\"", "formula prefix")
        expect(SearchExportSerializer.csvField("+SUM(A1)"), "\"'+SUM(A1)\"", "plus formula")
        expect(SearchExportSerializer.csvField("-2+3"), "\"'-2+3\"", "minus formula")
        expect(SearchExportSerializer.csvField("@cmd"), "\"'@cmd\"", "at formula")
        expect(SearchExportSerializer.csvField("\t=1+1"), "\"'\t=1+1\"", "tab formula prefix")
        expect(SearchExportSerializer.csvField("\r=1+1"), "\"'\r=1+1\"", "carriage-return formula prefix")
        expect(SearchExportSerializer.txtField("a\tb\nc\rd"), "a b c d", "TXT controls")

        let cachedResults = Array(0..<350)
        let visiblePage = Array(cachedResults.prefix(100))
        let exportedResults = SearchExportSnapshot.completeResults(
            cached: cachedResults,
            visible: visiblePage)
        expect(String(exportedResults.count), String(cachedResults.count),
               "export includes the complete result cache")
        expect(String(exportedResults.last ?? -1), "349",
               "export includes results beyond the visible GUI page")

        if failures > 0 { exit(1) }
        print("Search export tests passed")
    }
}
