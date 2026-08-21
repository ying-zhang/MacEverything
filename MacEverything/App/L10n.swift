import Foundation

enum AppLanguage: String, CaseIterable, Codable, Identifiable {
    case system
    case english
    case simplifiedChinese

    var id: String { rawValue }
    var title: String {
        switch self {
        case .system: return L10n.tr("Follow System")
        case .english: return "English"
        case .simplifiedChinese: return "简体中文"
        }
    }
    fileprivate var resourceName: String? {
        switch self {
        case .system: return nil
        case .english: return "en"
        case .simplifiedChinese: return "zh-Hans"
        }
    }
}

enum L10n {
    private static let languageDefaultsKey = "settings.language"

    static var selectedLanguage: AppLanguage {
        guard let rawValue = UserDefaults.standard.string(forKey: languageDefaultsKey),
              let language = AppLanguage(rawValue: rawValue) else { return .system }
        return language
    }

    static func setLanguage(_ language: AppLanguage) {
        UserDefaults.standard.set(language.rawValue, forKey: languageDefaultsKey)
    }

    static func tr(_ key: String, _ args: CVarArg...) -> String {
        let format: String
        if let resourceName = selectedLanguage.resourceName,
           let path = Bundle.main.path(forResource: resourceName, ofType: "lproj"),
           let bundle = Bundle(path: path) {
            format = bundle.localizedString(forKey: key, value: key, table: nil)
        } else {
            format = NSLocalizedString(key, comment: "")
        }
        guard !args.isEmpty else { return format }
        return String(format: format, locale: formattingLocale, arguments: args)
    }

    static var formattingLocale: Locale {
        switch selectedLanguage {
        case .system:
            return .autoupdatingCurrent
        case .english:
            return Locale(identifier: "en_US")
        case .simplifiedChinese:
            return Locale(identifier: "zh_CN")
        }
    }

    static func formatByteCount(_ bytes: UInt64,
                                style: ByteCountFormatStyle.Style = .file) -> String {
        Int64(clamping: bytes).formatted(
            .byteCount(style: style).locale(formattingLocale)
        )
    }

    static func formatDate(_ date: Date) -> String {
        date.formatted(
            Date.FormatStyle(date: .abbreviated,
                             time: .shortened,
                             locale: formattingLocale)
        )
    }
}
