import SwiftUI
import Foundation

/// Shared highlight function used by ResultRow and ContentResultRow.
/// Finds all case-insensitive, non-overlapping occurrences of `keyword`
/// in `text` and renders matched segments in bold + `highlightColor`.
/// For glob-style keywords containing `*` or `?`, highlights the literal
/// (non-wildcard) segments extracted from the pattern.
func highlightMatches(in text: String, keyword: String,
                      font: Font, color: Color,
                      highlightColor: Color = .accentColor) -> Text {
    guard !keyword.isEmpty else {
        return Text(text).font(font).foregroundColor(color)
    }

    let lowerText = text.lowercased()
    let lowerKey = keyword.lowercased()

    // Glob patterns — highlight literal (non-wildcard) segments
    if lowerKey.contains("*") || lowerKey.contains("?") {
        let literals = extractGlobLiterals(lowerKey)
        if literals.isEmpty {
            return Text(text).font(font).foregroundColor(color)
        }
        let ranges = findAllLiteralRanges(in: lowerText, literals: literals)
        return buildHighlightedText(text: text, ranges: ranges, font: font, color: color, highlightColor: highlightColor)
    }

    // Multi-keyword: split by spaces and find all matches for each word
    let words = lowerKey.components(separatedBy: " ").filter { !$0.isEmpty }
    if words.isEmpty {
        return Text(text).font(font).foregroundColor(color)
    }

    let ranges: [Range<String.Index>]
    if words.count == 1 {
        // Single keyword — simple forward scan
        var singleRanges: [Range<String.Index>] = []
        var searchStart = lowerText.startIndex
        while searchStart < lowerText.endIndex,
              let range = lowerText.range(of: words[0], range: searchStart..<lowerText.endIndex) {
            singleRanges.append(range)
            searchStart = range.upperBound
        }
        ranges = singleRanges
    } else {
        // Multiple keywords — find all matches for each, merge ranges
        ranges = findAllLiteralRanges(in: lowerText, literals: words)
    }

    return buildHighlightedText(text: text, ranges: ranges, font: font, color: color, highlightColor: highlightColor)
}

/// Cross-boundary highlight: matches keyword against `path + "/" + name` (the full path),
/// then maps matched ranges back to path and name individually.
/// This handles keywords like "test/goodprice" where "test" is at the end of `path`
/// and "goodprice" is the `name`.
///
/// Only activates cross-boundary logic when keyword contains "/"; otherwise falls back
/// to independent matching on each component.
func highlightCrossMatches(path: String, name: String, keyword: String,
                           nameFont: Font, nameColor: Color,
                           pathFont: Font, pathColor: Color,
                           highlightColor: Color = .accentColor) -> (nameText: Text, pathText: Text) {
    // Fast path: no keyword or no "/" in keyword — independent matching works fine
    if keyword.isEmpty || !keyword.contains("/") ||
       keyword.lowercased().contains("*") || keyword.lowercased().contains("?") {
        let nameText = highlightMatches(in: name, keyword: keyword, font: nameFont, color: nameColor, highlightColor: highlightColor)
        let pathText = highlightMatches(in: path, keyword: keyword, font: pathFont, color: pathColor, highlightColor: highlightColor)
        return (nameText, pathText)
    }

    let fullPath = path + "/" + name
    let lowerFull = fullPath.lowercased()
    let lowerKey = keyword.lowercased()

    // Find all matches in fullPath (supports multi-keyword via space splitting)
    let words = lowerKey.components(separatedBy: " ").filter { !$0.isEmpty }
    var fullRanges: [Range<String.Index>]
    if words.count <= 1 {
        fullRanges = []
        var searchStart = lowerFull.startIndex
        while searchStart < lowerFull.endIndex,
              let range = lowerFull.range(of: lowerKey, range: searchStart..<lowerFull.endIndex) {
            fullRanges.append(range)
            searchStart = range.upperBound
        }
    } else {
        fullRanges = findAllLiteralRanges(in: lowerFull, literals: words)
    }

    if fullRanges.isEmpty {
        // No matches at all — return plain text
        return (Text(name).font(nameFont).foregroundColor(nameColor),
                Text(path).font(pathFont).foregroundColor(pathColor))
    }

    // The boundary between path and name in fullPath is at index path.count + 1 (the "/" separator)
    // fullPath = path + "/" + name
    //            0..pathLen-1  pathLen  pathLen+1..end
    // pathLen = path.count, separator at path.endIndex, name starts at separatorEnd
    let separatorIndex = fullPath.index(fullPath.startIndex, offsetBy: path.count)
    let nameStartInFull = fullPath.index(after: separatorIndex)

    // Map fullPath ranges to path ranges and name ranges
    var pathRanges: [Range<String.Index>] = []
    var nameRanges: [Range<String.Index>] = []

    for range in fullRanges {
        let matchStart = range.lowerBound
        let matchEnd = range.upperBound

        // Entirely within path (including the "/" separator shown as part of path display is tricky,
        // but the separator is not displayed in either path or name — path ends before "/",
        // name starts after "/". We clip to each component.)
        if matchEnd <= separatorIndex {
            // Entirely within path portion — remap to path's own indices
            let offset = fullPath.distance(from: fullPath.startIndex, to: matchStart)
            let length = fullPath.distance(from: matchStart, to: matchEnd)
            let pStart = path.index(path.startIndex, offsetBy: offset)
            let pEnd = path.index(pStart, offsetBy: length)
            pathRanges.append(pStart..<pEnd)
        } else if matchStart >= nameStartInFull {
            // Entirely within name portion — remap to name's own indices
            let offset = fullPath.distance(from: nameStartInFull, to: matchStart)
            let length = fullPath.distance(from: matchStart, to: matchEnd)
            let nameStart = name.index(name.startIndex, offsetBy: offset)
            let nameEnd = name.index(nameStart, offsetBy: length)
            nameRanges.append(nameStart..<nameEnd)
        } else {
            // Cross-boundary: split into path part and name part
            if matchStart < separatorIndex {
                let offset = fullPath.distance(from: fullPath.startIndex, to: matchStart)
                let pStart = path.index(path.startIndex, offsetBy: offset)
                pathRanges.append(pStart..<path.endIndex)
            }
            // Name part: 0 ..< (matchEnd - nameStartInFull)
            if matchEnd > nameStartInFull {
                let nameLen = fullPath.distance(from: nameStartInFull, to: matchEnd)
                let nameEnd = name.index(name.startIndex, offsetBy: nameLen)
                nameRanges.append(name.startIndex..<nameEnd)
            }
        }
    }

    // Build highlighted Text for name
    let nameText = buildHighlightedText(text: name, ranges: nameRanges, font: nameFont, color: nameColor, highlightColor: highlightColor)
    // Build highlighted Text for path
    let pathText = buildHighlightedText(text: path, ranges: pathRanges, font: pathFont, color: pathColor, highlightColor: highlightColor)

    return (nameText, pathText)
}

/// Extract literal (non-wildcard) segments from a glob pattern.
/// Splits by `*` and `?`, returns non-empty segments.
func extractGlobLiterals(_ pattern: String) -> [String] {
    pattern.components(separatedBy: CharacterSet(charactersIn: "*?"))
           .filter { !$0.isEmpty }
}

/// Find all case-insensitive occurrences of any literal in `text`,
/// merge overlapping ranges, and return sorted non-overlapping ranges.
func findAllLiteralRanges(in lowerText: String, literals: [String]) -> [Range<String.Index>] {
    var allRanges: [Range<String.Index>] = []
    for literal in literals {
        var searchStart = lowerText.startIndex
        while searchStart < lowerText.endIndex,
              let range = lowerText.range(of: literal, range: searchStart..<lowerText.endIndex) {
            allRanges.append(range)
            searchStart = range.upperBound
        }
    }
    guard !allRanges.isEmpty else { return [] }

    // Sort by start position, then merge overlapping/adjacent ranges
    allRanges.sort { $0.lowerBound < $1.lowerBound }
    var merged: [Range<String.Index>] = [allRanges[0]]
    for range in allRanges.dropFirst() {
        if range.lowerBound <= merged[merged.count - 1].upperBound {
            let last = merged[merged.count - 1]
            merged[merged.count - 1] = last.lowerBound..<max(last.upperBound, range.upperBound)
        } else {
            merged.append(range)
        }
    }
    return merged
}

/// Helper: build a SwiftUI Text with specified ranges highlighted.
func buildHighlightedText(text: String, ranges: [Range<String.Index>],
                                  font: Font, color: Color,
                                  highlightColor: Color) -> Text {
    if ranges.isEmpty {
        return Text(text).font(font).foregroundColor(color)
    }

    var result = Text("")
    var currentIndex = text.startIndex

    for range in ranges {
        if currentIndex < range.lowerBound {
            result = result + Text(text[currentIndex..<range.lowerBound])
                .font(font).foregroundColor(color)
        }
        result = result + Text(text[range])
            .font(font).foregroundColor(highlightColor).bold()
        currentIndex = range.upperBound
    }

    if currentIndex < text.endIndex {
        result = result + Text(text[currentIndex..<text.endIndex])
            .font(font).foregroundColor(color)
    }

    return result
}

// MARK: - Hint-based highlighting

/// Compute matching ranges for a single HighlightHint in the given text.
func computeRangesForHint(in text: String, hint: HighlightHint) -> [Range<String.Index>] {
    guard !hint.text.isEmpty else { return [] }

    let searchText: String
    let searchKey: String
    if hint.caseSensitive {
        searchText = text
        searchKey = hint.text
    } else {
        searchText = text.lowercased()
        searchKey = hint.text.lowercased()
    }

    switch hint.matchMode {
    case .substring:
        var ranges: [Range<String.Index>] = []
        var start = searchText.startIndex
        while start < searchText.endIndex,
              let range = searchText.range(of: searchKey, range: start..<searchText.endIndex) {
            ranges.append(range)
            start = range.upperBound
        }
        if !hint.caseSensitive && isAsciiAlphaNumericQuery(searchKey) {
            ranges.append(contentsOf: computePinyinInitialRanges(in: text, key: searchKey))
        }
        return mergeRanges(ranges, in: text)

    case .glob:
        let literals = extractGlobLiterals(searchKey)
        if literals.isEmpty { return [] }
        return findAllLiteralRanges(in: searchText, literals: literals)

    case .regex:
        var options: NSRegularExpression.Options = []
        if !hint.caseSensitive { options.insert(.caseInsensitive) }
        guard let regex = try? NSRegularExpression(pattern: hint.text, options: options) else {
            return []
        }
        let nsRange = NSRange(text.startIndex..<text.endIndex, in: text)
        let matches = regex.matches(in: text, range: nsRange)
        return matches.compactMap { Range($0.range, in: text) }

    case .wholeWord:
        let escaped = NSRegularExpression.escapedPattern(for: hint.text)
        let pattern = "\\b\(escaped)\\b"
        var options: NSRegularExpression.Options = []
        if !hint.caseSensitive { options.insert(.caseInsensitive) }
        guard let regex = try? NSRegularExpression(pattern: pattern, options: options) else {
            return []
        }
        let nsRange = NSRange(text.startIndex..<text.endIndex, in: text)
        let matches = regex.matches(in: text, range: nsRange)
        return matches.compactMap { Range($0.range, in: text) }

    case .wholeFilename:
        if hint.caseSensitive {
            return text == hint.text ? [text.startIndex..<text.endIndex] : []
        } else {
            return text.lowercased() == hint.text.lowercased()
                ? [text.startIndex..<text.endIndex] : []
        }
    }
}

/// Mirrors the engine's pinyin-initial search key rules for UI highlighting.
/// Han characters contribute their Mandarin initial; ASCII alphanumeric
/// characters are kept lowercased; punctuation and separators are skipped.
private func computePinyinInitialRanges(in text: String, key: String) -> [Range<String.Index>] {
    let mapped = mandarinInitialMapping(for: text)
    guard !mapped.key.isEmpty else { return [] }

    var ranges: [Range<String.Index>] = []
    var searchStart = mapped.key.startIndex
    while searchStart < mapped.key.endIndex,
          let keyRange = mapped.key.range(of: key, range: searchStart..<mapped.key.endIndex) {
        let lowerOffset = mapped.key.distance(from: mapped.key.startIndex, to: keyRange.lowerBound)
        let upperOffset = mapped.key.distance(from: mapped.key.startIndex, to: keyRange.upperBound)
        if lowerOffset < upperOffset,
           lowerOffset >= 0,
           upperOffset <= mapped.sourceRanges.count {
            ranges.append(contentsOf: sourceRangesForMappedKeyMatch(
                mapped.sourceRanges[lowerOffset..<upperOffset]))
        }
        searchStart = keyRange.upperBound
    }
    return ranges
}

private func sourceRangesForMappedKeyMatch(
    _ mappedRanges: ArraySlice<Range<String.Index>>
) -> [Range<String.Index>] {
    guard var current = mappedRanges.first else { return [] }
    var ranges: [Range<String.Index>] = []
    for sourceRange in mappedRanges.dropFirst() {
        if current.upperBound == sourceRange.lowerBound {
            current = current.lowerBound..<sourceRange.upperBound
        } else {
            ranges.append(current)
            current = sourceRange
        }
    }
    ranges.append(current)
    return ranges
}

private func mandarinInitialMapping(for text: String) -> (key: String, sourceRanges: [Range<String.Index>]) {
    guard text.unicodeScalars.contains(where: isCJKIdeograph) else {
        return ("", [])
    }

    var key = ""
    var sourceRanges: [Range<String.Index>] = []
    var index = text.startIndex
    while index < text.endIndex {
        let nextIndex = text.index(after: index)
        let character = text[index..<nextIndex]
        if let scalar = character.unicodeScalars.first,
           character.unicodeScalars.count == 1,
           isCJKIdeograph(scalar),
           let initial = mandarinInitial(for: String(character)) {
            key.append(initial)
            sourceRanges.append(index..<nextIndex)
        } else if let ascii = asciiAlphaNumericLowercase(character) {
            key.append(ascii)
            sourceRanges.append(index..<nextIndex)
        }
        index = nextIndex
    }
    return (key, sourceRanges)
}

private var pinyinCache: [Character: Character?] = [:]

private func mandarinInitial(for character: String) -> Character? {
    let char = character.first!
    if let cached = pinyinCache[char] { return cached }
    let mutable = NSMutableString(string: character)
    guard CFStringTransform(mutable, nil, kCFStringTransformMandarinLatin, false) else {
        pinyinCache[char] = .some(nil)
        return nil
    }
    CFStringTransform(mutable, nil, kCFStringTransformStripDiacritics, false)
    for scalar in String(mutable).unicodeScalars {
        guard scalar.value < 128 else { continue }
        let value = UInt8(scalar.value)
        if value >= 65 && value <= 90 {
            let result = Character(UnicodeScalar(value + 32))
            pinyinCache[char] = result
            return result
        }
        if value >= 97 && value <= 122 {
            let result = Character(scalar)
            pinyinCache[char] = result
            return result
        }
    }
    pinyinCache[char] = .some(nil)
    return nil
}

private func isCJKIdeograph(_ scalar: UnicodeScalar) -> Bool {
    (scalar.value >= 0x3400 && scalar.value <= 0x4DBF) ||
    (scalar.value >= 0x4E00 && scalar.value <= 0x9FFF) ||
    (scalar.value >= 0xF900 && scalar.value <= 0xFAFF)
}

private func asciiAlphaNumericLowercase(_ character: Substring) -> Character? {
    guard character.unicodeScalars.count == 1,
          let scalar = character.unicodeScalars.first,
          scalar.value < 128 else {
        return nil
    }
    let value = UInt8(scalar.value)
    if value >= 48 && value <= 57 {
        return Character(scalar)
    }
    if value >= 65 && value <= 90 {
        return Character(UnicodeScalar(value + 32))
    }
    if value >= 97 && value <= 122 {
        return Character(scalar)
    }
    return nil
}

private func isAsciiAlphaNumericQuery(_ key: String) -> Bool {
    guard !key.isEmpty else { return false }
    return key.unicodeScalars.allSatisfy { scalar in
        guard scalar.value < 128 else { return false }
        let value = UInt8(scalar.value)
        return (value >= 48 && value <= 57) ||
               (value >= 65 && value <= 90) ||
               (value >= 97 && value <= 122)
    }
}

/// Compute merged highlight ranges from multiple hints.
func computeHighlightRanges(in text: String, hints: [HighlightHint]) -> [Range<String.Index>] {
    guard !hints.isEmpty else { return [] }
    var allRanges: [Range<String.Index>] = []
    for hint in hints {
        allRanges.append(contentsOf: computeRangesForHint(in: text, hint: hint))
    }
    return mergeRanges(allRanges, in: text)
}

/// Merge overlapping/adjacent ranges, sorted by start position.
private func mergeRanges(_ ranges: [Range<String.Index>], in text: String) -> [Range<String.Index>] {
    guard !ranges.isEmpty else { return [] }
    let sorted = ranges.sorted { $0.lowerBound < $1.lowerBound }
    var merged: [Range<String.Index>] = [sorted[0]]
    for range in sorted.dropFirst() {
        let last = merged[merged.count - 1]
        if range.lowerBound <= last.upperBound {
            merged[merged.count - 1] = last.lowerBound..<max(last.upperBound, range.upperBound)
        } else {
            merged.append(range)
        }
    }
    return merged
}

/// Hint-based highlight: applies all matching hints to render highlighted Text.
func highlightMatches(in text: String, hints: [HighlightHint],
                      font: Font, color: Color,
                      highlightColor: Color = .accentColor) -> Text {
    let ranges = computeHighlightRanges(in: text, hints: hints)
    return buildHighlightedText(text: text, ranges: ranges, font: font,
                                color: color, highlightColor: highlightColor)
}

/// Hint-based cross-boundary highlight with field-aware dispatching.
/// NAME hints only highlight in name, PATH hints only in path,
/// ANY hints highlight across path+"/"+name boundary when text contains "/".
func highlightCrossMatches(path: String, name: String, hints: [HighlightHint],
                           nameFont: Font, nameColor: Color,
                           pathFont: Font, pathColor: Color,
                           highlightColor: Color = .accentColor) -> (nameText: Text, pathText: Text) {
    guard !hints.isEmpty else {
        return (Text(name).font(nameFont).foregroundColor(nameColor),
                Text(path).font(pathFont).foregroundColor(pathColor))
    }

    var nameRanges: [Range<String.Index>] = []
    var pathRanges: [Range<String.Index>] = []

    for hint in hints {
        switch hint.field {
        case .name:
            nameRanges.append(contentsOf: computeRangesForHint(in: name, hint: hint))
        case .path:
            pathRanges.append(contentsOf: computeRangesForHint(in: path, hint: hint))
        case .any:
            if hint.matchMode == .substring && hint.text.contains("/") {
                let fullPath = path + "/" + name
                let fullRanges = computeRangesForHint(in: fullPath, hint: hint)
                let (pRanges, nRanges) = mapFullPathRanges(fullRanges,
                    fullPath: fullPath, path: path, name: name)
                pathRanges.append(contentsOf: pRanges)
                nameRanges.append(contentsOf: nRanges)
            } else {
                nameRanges.append(contentsOf: computeRangesForHint(in: name, hint: hint))
                pathRanges.append(contentsOf: computeRangesForHint(in: path, hint: hint))
            }
        }
    }

    nameRanges = mergeRanges(nameRanges, in: name)
    pathRanges = mergeRanges(pathRanges, in: path)

    let nameText = buildHighlightedText(text: name, ranges: nameRanges,
                                        font: nameFont, color: nameColor,
                                        highlightColor: highlightColor)
    let pathText = buildHighlightedText(text: path, ranges: pathRanges,
                                        font: pathFont, color: pathColor,
                                        highlightColor: highlightColor)
    return (nameText, pathText)
}

/// Map ranges from fullPath (path+"/"+name) back to individual path and name ranges.
private func mapFullPathRanges(_ fullRanges: [Range<String.Index>],
                               fullPath: String, path: String,
                               name: String) -> (pathRanges: [Range<String.Index>],
                                                 nameRanges: [Range<String.Index>]) {
    let separatorIndex = fullPath.index(fullPath.startIndex, offsetBy: path.count)
    let nameStartInFull = fullPath.index(after: separatorIndex)

    var pathRanges: [Range<String.Index>] = []
    var nameRanges: [Range<String.Index>] = []

    for range in fullRanges {
        if range.upperBound <= separatorIndex {
            let offset = fullPath.distance(from: fullPath.startIndex, to: range.lowerBound)
            let length = fullPath.distance(from: range.lowerBound, to: range.upperBound)
            let pStart = path.index(path.startIndex, offsetBy: offset)
            let pEnd = path.index(pStart, offsetBy: length)
            pathRanges.append(pStart..<pEnd)
        } else if range.lowerBound >= nameStartInFull {
            let offset = fullPath.distance(from: nameStartInFull, to: range.lowerBound)
            let length = fullPath.distance(from: range.lowerBound, to: range.upperBound)
            let nStart = name.index(name.startIndex, offsetBy: offset)
            let nEnd = name.index(nStart, offsetBy: length)
            nameRanges.append(nStart..<nEnd)
        } else {
            if range.lowerBound < separatorIndex {
                let offset = fullPath.distance(from: fullPath.startIndex, to: range.lowerBound)
                let pStart = path.index(path.startIndex, offsetBy: offset)
                pathRanges.append(pStart..<path.endIndex)
            }
            if range.upperBound > nameStartInFull {
                let nameLen = fullPath.distance(from: nameStartInFull, to: range.upperBound)
                let nEnd = name.index(name.startIndex, offsetBy: nameLen)
                nameRanges.append(name.startIndex..<nEnd)
            }
        }
    }
    return (pathRanges, nameRanges)
}
