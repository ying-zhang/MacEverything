import Foundation

private var failures = 0

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() {
        fputs("FAIL: \(message)\n", stderr)
        failures += 1
    }
}

@main
struct L10nFormattingTests {
    static func main() {
        let previousLanguage = L10n.selectedLanguage
        defer { L10n.setLanguage(previousLanguage) }

        let date = Date(timeIntervalSince1970: 1_784_042_220)

        L10n.setLanguage(.english)
        let englishBytes = L10n.formatByteCount(293)
        let englishDate = L10n.formatDate(date)
        expect(englishBytes.contains("bytes"), "English byte count uses an English unit")
        expect(!englishBytes.contains("字节"), "English byte count excludes Chinese units")
        expect(!englishDate.contains("年") && !englishDate.contains("月") && !englishDate.contains("日"),
               "English date excludes Chinese date markers")

        L10n.setLanguage(.simplifiedChinese)
        let chineseBytes = L10n.formatByteCount(293)
        let chineseDate = L10n.formatDate(date)
        expect(chineseBytes.contains("字节"), "Chinese byte count keeps the Chinese unit")
        expect(chineseDate.contains("年") && chineseDate.contains("月") && chineseDate.contains("日"),
               "Chinese date keeps Chinese date markers")

        if failures > 0 { exit(1) }
        print("Localization formatting tests passed")
    }
}
