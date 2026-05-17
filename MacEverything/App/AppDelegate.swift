import Cocoa
import ServiceManagement
import Combine
import SwiftUI

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
            self?.activeSearchWindow.map { SearchWindowSupport.configure($0, searchText: "") }
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
        menu.addItem(NSMenuItem(title: L10n.tr("New Search"), action: #selector(newSearchWindow), keyEquivalent: ""))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: L10n.tr("Rebuild Index"), action: #selector(rebuildIndex), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Shortcut Settings..."), action: #selector(openShortcutSettings), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Settings..."), action: #selector(openSettings), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: L10n.tr("Search Syntax Help..."), action: #selector(openSearchSyntaxHelp), keyEquivalent: ""))

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

    private static func makeStatusBarIcon() -> NSImage {
        let size = NSSize(width: 18, height: 18)
        let image = NSImage(size: size)
        image.isTemplate = true
        image.lockFocus()
        defer { image.unlockFocus() }

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

        return image
    }

    // MARK: - NSMenuDelegate

    func menuWillOpen(_ menu: NSMenu) {
        activeSearchWindow = frontmostSearchWindow() ?? activeSearchWindow
        if let item = menu.items.first {
            let isVisible = activeSearchWindow?.isVisible ?? false
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
        if let window = activeSearchWindow, window.isVisible {
            NSApp.hide(nil)
        } else {
            NSApp.activate(ignoringOtherApps: true)
            if let window = activeSearchWindow {
                window.makeKeyAndOrderFront(nil)
            } else {
                newSearchWindow()
            }
        }
    }

    @objc func newSearchWindow() {
        NSApp.activate(ignoringOtherApps: true)
        let hostingController = NSHostingController(rootView: ContentView())
        let win = NSWindow(contentViewController: hostingController)
        win.styleMask = NSWindow.StyleMask([.titled, .closable, .miniaturizable, .resizable])
        win.setContentSize(NSSize(width: 800, height: 600))
        win.minSize = NSSize(width: 600, height: 400)
        win.delegate = self
        SearchWindowSupport.configure(win, searchText: "")
        win.center()
        win.makeKeyAndOrderFront(self)
        auxiliarySearchWindows.append(win)
        activeSearchWindow = win
    }

    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        auxiliarySearchWindows.removeAll { $0 === window }
        if activeSearchWindow === window {
            activeSearchWindow = frontmostSearchWindow()
        }
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

enum SearchWindowSupport {
    static let windowIdentifier = NSUserInterfaceItemIdentifier("MacEverything.searchWindow")
    static let tabbingIdentifier = NSWindow.TabbingIdentifier("MacEverything.search")
    nonisolated(unsafe) private static var tabBarRequestedWindows: Set<ObjectIdentifier> = []

    static func isSearchWindow(_ window: NSWindow) -> Bool {
        window.identifier == windowIdentifier ||
        window.tabbingIdentifier == tabbingIdentifier ||
        window.title == "MacEverything" ||
        window.title.hasSuffix(" - MacEverything")
    }

    static func configure(_ window: NSWindow, searchText: String) {
        window.identifier = windowIdentifier
        window.tabbingIdentifier = tabbingIdentifier
        window.tabbingMode = .preferred
        window.title = title(for: searchText)
        requestTabBarIfNeeded(for: window)
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
