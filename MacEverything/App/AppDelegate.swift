import Cocoa
import ServiceManagement

class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var hotkeyManager: HotkeyManager?
    private var statusItem: NSStatusItem?
    private var launchAtLoginItem: NSMenuItem?
    private var mcpMenuItems: [MCPClient: NSMenuItem] = [:]
    private(set) var mainSearchWindow: NSWindow?

    func applicationDidFinishLaunching(_ notification: Notification) {
        MacSearchBridge.initializeLogger()

        let shouldMinimize = Self.shouldStartMinimized()

        // Delay by one frame to let SwiftUI create the window
        DispatchQueue.main.async { [weak self] in
            self?.mainSearchWindow = NSApp.windows.first { $0.title == "MacEverything" }
            if shouldMinimize {
                self?.mainSearchWindow?.orderOut(nil)
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
            button.image = NSImage(systemSymbolName: "magnifyingglass", accessibilityDescription: "MacEverything")
        }

        let menu = NSMenu()
        menu.delegate = self
        menu.addItem(NSMenuItem(title: L10n.tr("Show MacEverything"), action: #selector(toggleWindow), keyEquivalent: ""))
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
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: L10n.tr("Quit MacEverything"), action: #selector(quitApp), keyEquivalent: "q"))
        statusItem?.menu = menu
    }

    // MARK: - NSMenuDelegate

    func menuWillOpen(_ menu: NSMenu) {
        if let item = menu.items.first {
            let isVisible = mainSearchWindow?.isVisible ?? false
            item.title = isVisible ? L10n.tr("Hide MacEverything") : L10n.tr("Show MacEverything")
        }
        launchAtLoginItem?.state = SMAppService.mainApp.status == .enabled ? .on : .off
        for (client, item) in mcpMenuItems {
            item.state = MCPConfigManager.isEnabled(for: client) ? .on : .off
        }
    }

    // MARK: - Menu Actions

    @objc private func toggleWindow() {
        if let window = mainSearchWindow, window.isVisible {
            NSApp.hide(nil)
        } else {
            NSApp.activate(ignoringOtherApps: true)
            mainSearchWindow?.makeKeyAndOrderFront(nil)
        }
    }

    @objc private func rebuildIndex() {
        NSApp.activate(ignoringOtherApps: true)
        mainSearchWindow?.makeKeyAndOrderFront(nil)
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

    @objc private func quitApp() {
        NSApp.terminate(nil)
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
