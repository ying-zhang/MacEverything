import Cocoa
import ServiceManagement
import Combine
import SwiftUI

@MainActor
class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate, NSWindowDelegate {
    private var hotkeyManager: HotkeyManager?
    private var statusItem: NSStatusItem?
    private var launchAtLoginItem: NSMenuItem?
    private var hideDockItem: NSMenuItem?
    private var mcpMenuItems: [MCPClient: NSMenuItem] = [:]
    private(set) var activeSearchWindow: NSWindow?
    private var auxiliarySearchWindows: [NSWindow] = []
    private var settingsSink: AnyCancellable?

    func applicationWillFinishLaunching(_ notification: Notification) {
        NSWindow.allowsAutomaticWindowTabbing = true
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        MacSearchBridge.initializeLogger()
        applyDockVisibility()
        settingsSink = AppSettings.shared.$hideDockIcon
            .removeDuplicates()
            .sink { [weak self] _ in
                self?.applyDockVisibility()
            }

        let shouldMinimize = Self.shouldStartMinimized()

        // Delay by one frame to let SwiftUI create the window
        DispatchQueue.main.async { [weak self] in
            self?.activeSearchWindow = self?.frontmostSearchWindow()
            self?.activeSearchWindow.map { window in
                window.delegate = self
                SearchWindowSupport.configure(window, searchText: "", presentation: .window)
            }
            if shouldMinimize {
                self?.activeSearchWindow?.orderOut(nil)
                NSApp.hide(nil)
            }
        }
        hotkeyManager = HotkeyManager()
        hotkeyManager?.register()
        setupStatusBar()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        DispatchQueue.global(qos: .userInitiated).async {
            MacSearchBridge.shared().prepareForTermination()
            DispatchQueue.main.async {
                NSApp.reply(toApplicationShouldTerminate: true)
            }
        }
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Safety net: shutdown is idempotent (compare_exchange_strong guard)
        MacSearchBridge.shared().prepareForTermination()
    }

    // MARK: - Status Bar

    private func setupStatusBar() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let button = statusItem?.button {
            button.image = Self.makeStatusBarIcon()
        }

        let menu = NSMenu()
        menu.delegate = self
        menu.addItem(NSMenuItem(title: L10n.tr("Show MacEverything"), action: #selector(toggleWindow), keyEquivalent: ""))
        menu.addItem(Self.menuItem(title: L10n.tr("New Search Tab"),
                                   action: #selector(newSearchTab),
                                   keyEquivalent: "n",
                                   modifiers: [.command]))
        menu.addItem(Self.menuItem(title: L10n.tr("New Search Window"),
                                   action: #selector(newSearchWindow),
                                   keyEquivalent: "n",
                                   modifiers: [.command, .shift]))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: L10n.tr("Settings..."), action: #selector(openSettings), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Rebuild Index"), action: #selector(rebuildIndex), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Shortcut Settings..."), action: #selector(openShortcutSettings), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Search Syntax Help..."), action: #selector(openSearchSyntaxHelp), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Regular Expression Help..."), action: #selector(openRegexHelp), keyEquivalent: ""))

        let mcpSubmenu = NSMenu(title: L10n.tr("MCP Integration"))
        for client in MCPClient.allCases {
            let item = NSMenuItem(title: client.displayName, action: #selector(toggleMCPClient(_:)), keyEquivalent: "")
            item.representedObject = client
            mcpMenuItems[client] = item
            mcpSubmenu.addItem(item)
        }
        let mcpItem = NSMenuItem(title: L10n.tr("MCP Integration"), action: nil, keyEquivalent: "")
        mcpItem.submenu = mcpSubmenu
        menu.addItem(mcpItem)

        menu.addItem(.separator())
        let loginItem = NSMenuItem(title: L10n.tr("Launch at Login"), action: #selector(toggleLaunchAtLogin), keyEquivalent: "")
        launchAtLoginItem = loginItem
        menu.addItem(loginItem)
        let hideDockMenuItem = NSMenuItem(title: L10n.tr("Hide Dock Icon"), action: #selector(toggleHideDockIcon), keyEquivalent: "")
        hideDockItem = hideDockMenuItem
        menu.addItem(hideDockMenuItem)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: L10n.tr("Quit MacEverything"), action: #selector(quitApp), keyEquivalent: "q"))
        statusItem?.menu = menu
    }

    private static func menuItem(title: String,
                                 action: Selector,
                                 keyEquivalent: String,
                                 modifiers: NSEvent.ModifierFlags) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: keyEquivalent)
        item.keyEquivalentModifierMask = modifiers
        return item
    }

    private static func makeStatusBarIcon() -> NSImage {
        if let assetImage = NSImage(named: "StatusBarIcon") {
            assetImage.size = NSSize(width: 18, height: 18)
            assetImage.isTemplate = true
            return assetImage
        }

        let size = NSSize(width: 18, height: 18)
        let image = NSImage(size: size, flipped: false) { _ in
            NSColor.black.setStroke()

            let lens = NSBezierPath(ovalIn: NSRect(x: 2.1, y: 5.7, width: 10.9, height: 10.9))
            lens.lineWidth = 1.8
            lens.lineCapStyle = .round
            lens.lineJoinStyle = .round
            lens.stroke()

            let handle = NSBezierPath()
            handle.move(to: NSPoint(x: 11.8, y: 6.2))
            handle.line(to: NSPoint(x: 16.0, y: 2.0))
            handle.lineWidth = 2.1
            handle.lineCapStyle = .round
            handle.lineJoinStyle = .round
            handle.stroke()

            let window = NSBezierPath(roundedRect: NSRect(x: 5.0, y: 9.3, width: 5.4, height: 3.8), xRadius: 0.55, yRadius: 0.55)
            window.lineWidth = 1.1
            window.lineCapStyle = .round
            window.lineJoinStyle = .round
            window.stroke()

            let titleBar = NSBezierPath()
            titleBar.move(to: NSPoint(x: 5.2, y: 12.0))
            titleBar.line(to: NSPoint(x: 10.2, y: 12.0))
            titleBar.lineWidth = 0.9
            titleBar.lineCapStyle = .round
            titleBar.stroke()

            let contentLine = NSBezierPath()
            contentLine.move(to: NSPoint(x: 6.2, y: 10.6))
            contentLine.line(to: NSPoint(x: 9.2, y: 10.6))
            contentLine.lineWidth = 0.9
            contentLine.lineCapStyle = .round
            contentLine.stroke()

            return true
        }
        image.isTemplate = true
        return image
    }

    // MARK: - NSMenuDelegate

    func menuWillOpen(_ menu: NSMenu) {
        activeSearchWindow = frontmostSearchWindow() ?? activeSearchWindow
        if let item = menu.items.first {
            let isVisible = (activeSearchWindow?.isVisible ?? false) && !NSApp.isHidden
            item.title = isVisible ? L10n.tr("Hide MacEverything") : L10n.tr("Show MacEverything")
        }
        launchAtLoginItem?.state = SMAppService.mainApp.status == .enabled ? .on : .off
        hideDockItem?.state = isDockIconHidden ? .on : .off
        for (client, item) in mcpMenuItems {
            item.state = MCPConfigManager.isEnabled(for: client) ? .on : .off
        }
    }

    // MARK: - Menu Actions

    @objc private func toggleWindow() {
        activeSearchWindow = frontmostSearchWindow() ?? activeSearchWindow
        if let window = activeSearchWindow, window.isVisible, !NSApp.isHidden {
            NSApp.hide(nil)
        } else {
            NSApp.activate(ignoringOtherApps: true)
            if let window = activeSearchWindow {
                SearchWindowSupport.restore(window)
            } else {
                newSearchTab()
            }
        }
    }

    @objc func newSearchTab() {
        createSearchWindow(presentation: .tab)
    }

    @objc func newSearchWindow() {
        createSearchWindow(presentation: .window)
    }

    @discardableResult
    private func createSearchWindow(presentation: SearchWindowSupport.Presentation) -> NSWindow {
        NSApp.activate(ignoringOtherApps: true)
        let tabTarget = presentation == .tab ? targetSearchWindowForNewTab() : nil
        let hostingController = NSHostingController(rootView: ContentView())
        let win = NSWindow(contentViewController: hostingController)
        win.styleMask = NSWindow.StyleMask([.titled, .closable, .miniaturizable, .resizable])
        win.setContentSize(NSSize(width: 800, height: 600))
        win.minSize = NSSize(width: 600, height: 400)
        win.delegate = self
        SearchWindowSupport.configure(win, searchText: "", presentation: presentation, tabTarget: tabTarget)
        if let tabTarget {
            SearchWindowSupport.restore(tabTarget)
            tabTarget.addTabbedWindow(win, ordered: .above)
        } else {
            win.center()
        }
        SearchWindowSupport.restore(win)
        auxiliarySearchWindows.append(win)
        activeSearchWindow = win
        return win
    }

    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        auxiliarySearchWindows.removeAll { $0 === window }
        if activeSearchWindow === window {
            activeSearchWindow = frontmostSearchWindow()
        }
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard SearchWindowSupport.isSearchWindow(sender) else { return true }
        activeSearchWindow = sender
        if hasOtherSearchWindows(than: sender) {
            return true
        }
        NSApp.hide(nil)
        return false
    }

    @objc private func rebuildIndex() {
        NSApp.activate(ignoringOtherApps: true)
        activeSearchWindow?.makeKeyAndOrderFront(nil)
        NotificationCenter.default.post(name: .rebuildIndex, object: nil)
    }

    @objc private func openShortcutSettings() {
        NSApp.activate(ignoringOtherApps: true)
        ShortcutSettingsWindowController.shared.showWindow()
    }

    @objc private func openSettings() {
        NSApp.activate(ignoringOtherApps: true)
        GeneralSettingsWindowController.shared.showWindow()
    }

    @objc private func openSearchSyntaxHelp() {
        NSApp.activate(ignoringOtherApps: true)
        SearchSyntaxHelpWindowController.shared.showWindow()
    }

    @objc private func openRegexHelp() {
        NSApp.activate(ignoringOtherApps: true)
        Task { @MainActor in
            RegexHelpWindowController.shared.showWindow()
        }
    }

    @objc private func toggleMCPClient(_ sender: NSMenuItem) {
        guard let client = sender.representedObject as? MCPClient else { return }
        let currentlyEnabled = MCPConfigManager.isEnabled(for: client)
        MCPConfigManager.setEnabled(!currentlyEnabled, for: client)
        sender.state = !currentlyEnabled ? .on : .off
    }

    @objc private func toggleLaunchAtLogin() {
        do {
            if SMAppService.mainApp.status == .enabled {
                try SMAppService.mainApp.unregister()
            } else {
                try SMAppService.mainApp.register()
            }
        } catch {
            AppLogger.error("App", "Failed to toggle launch at login: \(error)")
        }
    }

    @MainActor
    @objc func toggleHideDockIcon() {
        AppSettings.shared.hideDockIcon = !isDockIconHidden
        applyDockVisibility()
    }

    @objc private func quitApp() {
        NSApp.terminate(nil)
    }

    @MainActor
    private func applyDockVisibility() {
        let policy: NSApplication.ActivationPolicy = AppSettings.shared.hideDockIcon ? .accessory : .regular
        if NSApp.activationPolicy() != policy {
            NSApp.setActivationPolicy(policy)
        }
    }

    private var isDockIconHidden: Bool {
        NSApp.activationPolicy() == .accessory || NSApp.activationPolicy() == .prohibited
    }

    private func frontmostSearchWindow() -> NSWindow? {
        NSApp.orderedWindows.first { window in
            SearchWindowSupport.isSearchWindow(window)
        }
    }

    private func targetSearchWindowForNewTab() -> NSWindow? {
        if let keyWindow = NSApp.keyWindow,
           SearchWindowSupport.isSearchWindow(keyWindow) {
            return keyWindow
        }
        if let mainWindow = NSApp.mainWindow,
           SearchWindowSupport.isSearchWindow(mainWindow) {
            return mainWindow
        }
        return frontmostSearchWindow() ?? activeSearchWindow
    }

    private func hasOtherSearchWindows(than closingWindow: NSWindow) -> Bool {
        NSApp.windows.contains { window in
            window !== closingWindow && SearchWindowSupport.isSearchWindow(window)
        }
    }

    // MARK: - Launch Mode Detection

    private static func shouldStartMinimized() -> Bool {
        if CommandLine.arguments.contains("--minimized") {
            return true
        }
        if let event = NSAppleEventManager.shared().currentAppleEvent,
           event.eventID == kAEOpenApplication,
           event.paramDescriptor(forKeyword: keyAEPropData)?.stringValue == "com.apple.loginwindow" {
            return true
        }
        return false
    }
}

@MainActor
enum SearchWindowSupport {
    enum Presentation {
        case tab
        case window
    }

    static let windowIdentifier = NSUserInterfaceItemIdentifier("MacEverything.searchWindow")
    static let legacyTabbingIdentifier = NSWindow.TabbingIdentifier("MacEverything.search")
    private static var tabBarRequestedWindows: Set<ObjectIdentifier> = []

    static func isSearchWindow(_ window: NSWindow) -> Bool {
        window.identifier == windowIdentifier ||
        window.tabbingIdentifier == legacyTabbingIdentifier ||
        window.title == "MacEverything" ||
        window.title.hasSuffix(" - MacEverything")
    }

    static func configure(_ window: NSWindow,
                          searchText: String,
                          presentation: Presentation = .window,
                          tabTarget: NSWindow? = nil) {
        window.identifier = windowIdentifier
        updateTitle(window, searchText: searchText)

        switch presentation {
        case .tab:
            window.tabbingIdentifier = tabbingIdentifier(forNewTabIn: tabTarget)
            window.tabbingMode = .preferred
            requestTabBarIfNeeded(for: window)
        case .window:
            window.tabbingIdentifier = newTabbingIdentifier()
            window.tabbingMode = .preferred
        }
    }

    static func updateTitle(_ window: NSWindow, searchText: String) {
        window.title = title(for: searchText)
    }

    static func restore(_ window: NSWindow) {
        if window.isMiniaturized {
            window.deminiaturize(nil)
        }
        NSApp.unhide(nil)
        window.orderFrontRegardless()
        window.makeKeyAndOrderFront(nil)

        DispatchQueue.main.async {
            window.makeKeyAndOrderFront(nil)
            NotificationCenter.default.post(name: .searchWindowDidRestore, object: window)
        }
    }

    private static func title(for searchText: String) -> String {
        let compact = searchText
            .split(whereSeparator: { $0.isWhitespace || $0.isNewline })
            .joined(separator: " ")
            .trimmingCharacters(in: .whitespacesAndNewlines)

        guard !compact.isEmpty else { return "MacEverything" }

        let maxTitleKeywordLength = 18
        let keyword = compact.count > maxTitleKeywordLength
            ? String(compact.prefix(maxTitleKeywordLength)) + "..."
            : compact
        return "\(keyword) - MacEverything"
    }

    private static func tabbingIdentifier(forNewTabIn target: NSWindow?) -> NSWindow.TabbingIdentifier {
        if let target,
           !target.tabbingIdentifier.isEmpty {
            return target.tabbingIdentifier
        }
        return newTabbingIdentifier()
    }

    private static func newTabbingIdentifier() -> NSWindow.TabbingIdentifier {
        NSWindow.TabbingIdentifier("MacEverything.search.\(UUID().uuidString)")
    }

    private static func requestTabBarIfNeeded(for window: NSWindow) {
        let windowID = ObjectIdentifier(window)
        guard !tabBarRequestedWindows.contains(windowID) else { return }
        tabBarRequestedWindows.insert(windowID)

        DispatchQueue.main.async {
            guard window.isVisible else { return }
            if window.tabGroup?.isTabBarVisible != true {
                window.toggleTabBar(nil)
            }
        }
    }
}
