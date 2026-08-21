import Foundation

enum SearchExportSerializer {
    static let utf8BOM = "\u{FEFF}"

    static func csvField(_ value: String) -> String {
        var sanitized = value
        if let first = sanitized.first, "=+-@\t\r".contains(first) {
            sanitized.insert("'", at: sanitized.startIndex)
        }
        return "\"" + sanitized.replacingOccurrences(of: "\"", with: "\"\"") + "\""
    }

    static func txtField(_ value: String) -> String {
        value.replacingOccurrences(of: "\t", with: " ")
            .replacingOccurrences(of: "\r", with: " ")
            .replacingOccurrences(of: "\n", with: " ")
    }
}

enum SearchExportSnapshot {
    /// Export the complete filtered result cache, not only the currently
    /// materialized GUI page.
    static func completeResults<Result>(cached: [Result], visible: [Result]) -> [Result] {
        _ = visible
        return cached
    }
}
