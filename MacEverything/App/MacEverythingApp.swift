import SwiftUI

@main
struct MacEverythingApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @Environment(\.openWindow) private var openWindow
    @ObservedObject private var appSettings = AppSettings.shared
    @ObservedObject private var searchOptions = SearchOptions.shared

    var body: some Scene {
        WindowGroup("MacEverything", id: "search") {
            ContentView()
        }
        .windowStyle(.titleBar)
        .defaultSize(width: 800, height: 600)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button(L10n.tr("New Search")) {
                    openWindow(id: "search")
                }
                .keyboardShortcut("n", modifiers: [.command])
            }

            CommandMenu(Text(L10n.tr("Search"))) {
                Button(L10n.tr("New Search")) {
                    openWindow(id: "search")
                }

                Divider()

                Toggle(L10n.tr("Regex"), isOn: $searchOptions.isRegex)
                    .keyboardShortcut("r", modifiers: [.command])
                Toggle(L10n.tr("Case Sensitive"), isOn: $searchOptions.isCaseSensitive)
                    .keyboardShortcut("c", modifiers: [.command, .shift])
                Toggle(L10n.tr("Whole Word"), isOn: $searchOptions.isWholeWord)
                    .keyboardShortcut("w", modifiers: [.command, .shift])
                Toggle(L10n.tr("Match Filename"), isOn: $searchOptions.isMatchFilename)
                    .keyboardShortcut("f", modifiers: [.command, .shift])
            }

            CommandGroup(after: .toolbar) {
                Toggle(L10n.tr("Hide Dock Icon"), isOn: $appSettings.hideDockIcon)
            }

            CommandGroup(replacing: .help) {
                Button(L10n.tr("Search Syntax Help")) {
                    SearchSyntaxHelpWindowController.shared.showWindow()
                }
                .keyboardShortcut("/", modifiers: [.command, .shift])

                Button(L10n.tr("Regular Expression Help")) {
                    RegexHelpWindowController.shared.showWindow()
                }
            }

            CommandGroup(after: .appSettings) {
                Button(L10n.tr("Rebuild Index")) {
                    NotificationCenter.default.post(name: .rebuildIndex, object: nil)
                }
                .keyboardShortcut("r", modifiers: [.command, .shift])

                Button(L10n.tr("Shortcut Settings...")) {
                    ShortcutSettingsWindowController.shared.showWindow()
                }
                .keyboardShortcut(",", modifiers: [.command])

                Button(L10n.tr("Settings...")) {
                    GeneralSettingsWindowController.shared.showWindow()
                }

                Divider()

                Menu(L10n.tr("MCP Integration")) {
                    ForEach(MCPClient.allCases, id: \.self) { client in
                        Toggle(client.displayName, isOn: Binding(
                            get: { MCPConfigManager.isEnabled(for: client) },
                            set: { MCPConfigManager.setEnabled($0, for: client) }
                        ))
                    }
                }
            }
        }
    }
}

extension Notification.Name {
    static let rebuildIndex = Notification.Name("rebuildIndex")
    static let rebuildContentIndex = Notification.Name("rebuildContentIndex")
    static let clearContentIndex = Notification.Name("clearContentIndex")
    static let searchServiceDidRefresh = Notification.Name("searchServiceDidRefresh")
}

class GeneralSettingsWindowController {
    static let shared = GeneralSettingsWindowController()
    private var window: NSWindow?

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            return
        }

        let settingsView = GeneralSettingsView()
        let hostingController = NSHostingController(rootView: settingsView)
        let win = NSWindow(contentViewController: hostingController)
        win.title = L10n.tr("Settings")
        win.styleMask = [.titled, .closable, .resizable]
        win.minSize = NSSize(width: 620, height: 520)
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }
}

class ShortcutSettingsWindowController {
    static let shared = ShortcutSettingsWindowController()
    private var window: NSWindow?

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            return
        }

        let settingsView = ShortcutSettingsView()
        let hostingController = NSHostingController(rootView: settingsView)
        let win = NSWindow(contentViewController: hostingController)
        win.title = L10n.tr("Shortcut Settings")
        win.styleMask = [.titled, .closable]
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }
}

class ContentSettingsWindowController {
    static let shared = ContentSettingsWindowController()
    private var window: NSWindow?

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            return
        }

        let settingsView = ContentSettingsView()
        let hostingController = NSHostingController(rootView: settingsView)
        let win = NSWindow(contentViewController: hostingController)
        win.title = L10n.tr("Content Settings")
        win.styleMask = [.titled, .closable, .resizable]
        win.minSize = NSSize(width: 420, height: 440)
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }
}

class SearchSyntaxHelpWindowController {
    static let shared = SearchSyntaxHelpWindowController()
    private var window: NSWindow?

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            return
        }

        let helpView = SearchSyntaxHelpView()
        let hostingController = NSHostingController(rootView: helpView)
        let win = NSWindow(contentViewController: hostingController)
        win.title = L10n.tr("Search Syntax Help")
        win.styleMask = [.titled, .closable, .resizable]
        win.setContentSize(NSSize(width: 580, height: 720))
        win.minSize = NSSize(width: 450, height: 400)
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }
}

@MainActor
class RegexHelpWindowController {
    static let shared = RegexHelpWindowController()
    private static let firstUseKey = "help.regex.firstUseShown"
    private var window: NSWindow?

    func showFirstUseIfNeeded() {
        guard !UserDefaults.standard.bool(forKey: Self.firstUseKey) else { return }
        UserDefaults.standard.set(true, forKey: Self.firstUseKey)
        showWindow()
    }

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            return
        }

        let helpView = RegexHelpView()
        let hostingController = NSHostingController(rootView: helpView)
        let win = NSWindow(contentViewController: hostingController)
        win.title = L10n.tr("Regular Expression Help")
        win.styleMask = [.titled, .closable, .resizable]
        win.setContentSize(NSSize(width: 620, height: 640))
        win.minSize = NSSize(width: 480, height: 360)
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }
}
