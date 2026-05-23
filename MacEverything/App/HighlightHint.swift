import Foundation

/// Match mode for highlight hints, mirroring C++ MatchMode enum values.
enum HighlightMatchMode: UInt8, Equatable {
    case substring = 0
    case glob = 1
    case regex = 2
    case wholeWord = 3
    case wholeFilename = 4
}

/// Which field the hint applies to, mirroring C++ HintField enum values.
enum HighlightField: UInt8, Equatable {
    case name = 0
    case path = 1
    case any = 2
}

/// A structured highlight hint extracted from the C++ query AST.
struct HighlightHint: Equatable {
    let text: String
    let field: HighlightField
    let matchMode: HighlightMatchMode
    let caseSensitive: Bool

    #if !TESTING
    /// Convert from Bridge MEHighlightHint to Swift HighlightHint.
    init(from hint: MEHighlightHint) {
        self.text = hint.text
        self.field = HighlightField(rawValue: hint.field) ?? .any
        self.matchMode = HighlightMatchMode(rawValue: hint.matchMode) ?? .substring
        self.caseSensitive = hint.caseSensitive
    }
    #endif

    init(text: String, field: HighlightField = .any,
         matchMode: HighlightMatchMode = .substring,
         caseSensitive: Bool = false) {
        self.text = text
        self.field = field
        self.matchMode = matchMode
        self.caseSensitive = caseSensitive
    }
}
