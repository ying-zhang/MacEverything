import SwiftUI

@main
struct MacEverythingApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var appSettings = AppSettings.shared
    @StateObject private var searchOptions = SearchOptions.shared
    @StateObject private var mcpIntegration = MCPIntegrationModel.shared
    @FocusedValue(\.exportSearchResults) private var exportSearchResults
    @FocusedValue(\.canExportSearchResults) private var canExportSearchResults
    @FocusedValue(\.toggleInformationPanel) private var toggleInformationPanel
    @FocusedValue(\.informationPanelVisible) private var informationPanelVisible

    var body: some Scene {
        WindowGroup("MacEverything", id: "search") {
            ContentView()
                .environmentObject(ThemeManager.shared)
        }
        .windowStyle(.titleBar)
        .defaultSize(width: 800, height: 600)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button(L10n.tr("New Search Tab")) {
                    appDelegate.newSearchTab()
                }
                .keyboardShortcut("n", modifiers: [.command])

                Button(L10n.tr("New Search Window")) {
                    appDelegate.newSearchWindow()
                }
                .keyboardShortcut("n", modifiers: [.command, .shift])
            }

            CommandMenu(Text(L10n.tr("Search"))) {
                Button(L10n.tr("New Search Tab")) {
                    appDelegate.newSearchTab()
                }
                .keyboardShortcut("n", modifiers: [.command])

                Button(L10n.tr("New Search Window")) {
                    appDelegate.newSearchWindow()
                }
                .keyboardShortcut("n", modifiers: [.command, .shift])

                Divider()

                Toggle(L10n.tr("Regex"), isOn: $searchOptions.isRegex)
                    .keyboardShortcut("r", modifiers: [.command])
                Toggle(L10n.tr("Case Sensitive"), isOn: $searchOptions.isCaseSensitive)
                    .keyboardShortcut("c", modifiers: [.command, .shift])
                Toggle(L10n.tr("Whole Word"), isOn: $searchOptions.isWholeWord)
                    .keyboardShortcut("w", modifiers: [.command, .shift])
                Toggle(L10n.tr("Match Filename"), isOn: $searchOptions.isMatchFilename)
                    .keyboardShortcut("f", modifiers: [.command, .shift])

                Divider()

                Button(L10n.tr("Copy Path")) {
                    SearchViewModel.copySelectedPathsInActiveWindow()
                }
                .keyboardShortcut("c", modifiers: [.command, .option])

                Divider()

                Button {
                    exportSearchResults?()
                } label: {
                    Label(L10n.tr("Export..."), systemImage: "square.and.arrow.down")
                }
                .disabled(canExportSearchResults != true)
            }

            CommandGroup(after: .toolbar) {
                Toggle(isOn: Binding(
                    get: { informationPanelVisible ?? false },
                    set: { _ in toggleInformationPanel?() }
                )) {
                    Label(L10n.tr("Information Panel"), systemImage: "sidebar.right")
                }
                .disabled(toggleInformationPanel == nil)
                .keyboardShortcut("i", modifiers: [.command, .option])

                Divider()

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
                            get: { mcpIntegration.isEnabled(client) },
                            set: { mcpIntegration.setEnabled($0, for: client) }
                        ))
                        .disabled(mcpIntegration.updating.contains(client))
                    }
                }
            }
        }
    }
}

private struct ExportSearchResultsKey: FocusedValueKey {
    typealias Value = () -> Void
}

private struct CanExportSearchResultsKey: FocusedValueKey {
    typealias Value = Bool
}

struct ToggleInformationPanelKey: FocusedValueKey {
    typealias Value = () -> Void
}

struct InformationPanelVisibleKey: FocusedValueKey {
    typealias Value = Bool
}

extension FocusedValues {
    var exportSearchResults: (() -> Void)? {
        get { self[ExportSearchResultsKey.self] }
        set { self[ExportSearchResultsKey.self] = newValue }
    }

    var canExportSearchResults: Bool? {
        get { self[CanExportSearchResultsKey.self] }
        set { self[CanExportSearchResultsKey.self] = newValue }
    }

    var toggleInformationPanel: (() -> Void)? {
        get { self[ToggleInformationPanelKey.self] }
        set { self[ToggleInformationPanelKey.self] = newValue }
    }

    var informationPanelVisible: Bool? {
        get { self[InformationPanelVisibleKey.self] }
        set { self[InformationPanelVisibleKey.self] = newValue }
    }
}

extension Notification.Name {
    static let rebuildIndex = Notification.Name("rebuildIndex")
    static let rebuildContentIndex = Notification.Name("rebuildContentIndex")
    static let clearContentIndex = Notification.Name("clearContentIndex")
    static let searchServiceDidRefresh = Notification.Name("searchServiceDidRefresh")
    static let searchWindowDidRestore = Notification.Name("searchWindowDidRestore")
}

@MainActor
class GeneralSettingsWindowController: NSObject, NSWindowDelegate {
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
        win.delegate = self
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    func windowWillClose(_ notification: Notification) {
        window = nil
    }

    func refreshLocalization() {
        window?.title = L10n.tr("Settings")
    }
}

@MainActor
class ShortcutSettingsWindowController: NSObject, NSWindowDelegate {
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
        win.delegate = self
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    func windowWillClose(_ notification: Notification) {
        window = nil
    }

    func refreshLocalization() {
        window?.title = L10n.tr("Shortcut Settings")
    }
}

@MainActor
class ContentSettingsWindowController: NSObject, NSWindowDelegate {
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
        win.delegate = self
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    func windowWillClose(_ notification: Notification) {
        window = nil
    }

    func refreshLocalization() {
        window?.title = L10n.tr("Content Settings")
    }
}

@MainActor
class SearchSyntaxHelpWindowController: NSObject, NSWindowDelegate {
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
        win.delegate = self
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    func windowWillClose(_ notification: Notification) {
        window = nil
    }

    func refreshLocalization() {
        window?.title = L10n.tr("Search Syntax Help")
    }
}

@MainActor
class RegexHelpWindowController: NSObject, NSWindowDelegate {
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
        win.delegate = self
        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    func windowWillClose(_ notification: Notification) {
        window = nil
    }

    func refreshLocalization() {
        window?.title = L10n.tr("Regular Expression Help")
    }
}
