import SwiftUI
import Combine

@MainActor
class SearchOptions: ObservableObject {
    static let shared = SearchOptions()

    @Published var isRegex: Bool = AppSettings.shared.snapshot.defaultRegex {
        didSet {
            if isRegex {
                isWholeWord = false
                isMatchFilename = false
                RegexHelpWindowController.shared.showFirstUseIfNeeded()
            }
        }
    }
    @Published var isCaseSensitive: Bool = AppSettings.shared.snapshot.defaultCaseSensitive
    @Published var isWholeWord: Bool = AppSettings.shared.snapshot.defaultWholeWord {
        didSet {
            if isWholeWord {
                isRegex = false
                isMatchFilename = false
            }
        }
    }
    @Published var isMatchFilename: Bool = AppSettings.shared.snapshot.defaultMatchFilename {
        didSet {
            if isMatchFilename {
                isRegex = false
                isWholeWord = false
            }
        }
    }

    private init() {}

    var hasActiveOptions: Bool {
        isRegex || isCaseSensitive || isWholeWord || isMatchFilename
    }

    /// Build query string with prefixes for the C++ engine
    func buildQuery(_ keyword: String) -> String {
        guard !keyword.isEmpty else { return keyword }
        var prefix = ""
        if isRegex { prefix += "regex:" }
        if isCaseSensitive { prefix += "case:" }
        if isWholeWord { prefix += "ww:" }
        if isMatchFilename { prefix += "wfn:" }
        return prefix + keyword
    }
}

struct SearchOptionBadges: View {
    @ObservedObject var options: SearchOptions

    var body: some View {
        if options.hasActiveOptions {
            HStack(spacing: 4) {
                if options.isRegex {
                    badge(".*", color: .purple) { options.isRegex.toggle() }
                }
                if options.isCaseSensitive {
                    badge("Aa", color: .orange) { options.isCaseSensitive.toggle() }
                }
                if options.isWholeWord {
                    badge("W", color: .blue) { options.isWholeWord.toggle() }
                }
                if options.isMatchFilename {
                    badge("FN", color: .green) { options.isMatchFilename.toggle() }
                }
            }
        }
    }

    private func badge(_ text: String, color: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundColor(.white)
                .padding(.horizontal, 5)
                .padding(.vertical, 2)
                .background(RoundedRectangle(cornerRadius: 4).fill(color))
        }
        .buttonStyle(.plain)
    }
}
