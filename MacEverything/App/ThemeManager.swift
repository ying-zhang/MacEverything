import SwiftUI
import Combine
import AppKit

@MainActor
final class ThemeManager: ObservableObject {
    static let shared = ThemeManager()

    @Published private(set) var resolvedBackgroundColor: Color = Color(nsColor: .windowBackgroundColor)
    @Published private(set) var resolvedTextColor: Color = .primary
    @Published private(set) var bodyFont: Font = .system(size: 13)
    @Published private(set) var bodyNSFont: NSFont = .systemFont(ofSize: 13)
    @Published private(set) var searchFieldNSFont: NSFont = .systemFont(ofSize: 26)

    private var settingsSink: AnyCancellable?
    private var appearanceObserver: NSObjectProtocol?

    private init() {
        settingsSink = AppSettings.shared.objectWillChange
            .receive(on: RunLoop.main)
            .sink { [weak self] _ in
                DispatchQueue.main.async { [weak self] in
                    self?.resolve()
                }
            }

        appearanceObserver = NotificationCenter.default.addObserver(
            forName: NSNotification.Name("AppleInterfaceThemeChangedNotification"),
            object: nil,
            queue: .main
        ) { [weak self] _ in
            DispatchQueue.main.async { [weak self] in
                self?.resolve()
            }
        }

        resolve()
    }

    deinit {
        if let observer = appearanceObserver {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    private func resolve() {
        let settings = AppSettings.shared

        switch settings.appearanceMode {
        case .system: NSApp.appearance = nil
        case .light: NSApp.appearance = NSAppearance(named: .aqua)
        case .dark: NSApp.appearance = NSAppearance(named: .darkAqua)
        }

        let isDark = effectiveIsDark()

        let bgString = isDark ? settings.darkBackgroundColor : settings.lightBackgroundColor
        resolvedBackgroundColor = Color(rgbaString: bgString) ?? Color(nsColor: .windowBackgroundColor)

        let textString = isDark ? settings.darkTextColor : settings.lightTextColor
        resolvedTextColor = Color(rgbaString: textString) ?? .primary

        let family = settings.fontFamily
        let size = settings.fontSize

        if family.isEmpty || !NSFontManager.shared.availableFontFamilies.contains(family) {
            bodyFont = .system(size: size)
            bodyNSFont = .systemFont(ofSize: size)
            searchFieldNSFont = .systemFont(ofSize: 26)
        } else {
            bodyFont = .custom(family, size: size)
            bodyNSFont = NSFont(name: family, size: size) ?? .systemFont(ofSize: size)
            searchFieldNSFont = NSFont(name: family, size: 26) ?? .systemFont(ofSize: 26)
        }
    }

    private func effectiveIsDark() -> Bool {
        let appearance = NSApp.effectiveAppearance
        return appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
    }
}
