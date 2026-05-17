import Cocoa
import ServiceManagement
import Combine

class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var hotkeyManager: HotkeyManager?
    private var statusItem: NSStatusItem?
    private var launchAtLoginItem: NSMenuItem?
    private var hideDockItem: NSMenuItem?
    private var mcpMenuItems: [MCPClient: NSMenuItem] = [:]
    private(set) var mainSearchWindow: NSWindow?
    private var settingsSink: AnyCancellable?

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
            button.image = Self.makeStatusBarIcon()
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
        if let item = menu.items.first {
            let isVisible = mainSearchWindow?.isVisible ?? false
            item.title = isVisible ? L10n.tr("Hide MacEverything") : L10n.tr("Show MacEverything")
        }
        launchAtLoginItem?.state = SMAppService.mainApp.status == .enabled ? .on : .off
        hideDockItem?.state = AppSettings.shared.hideDockIcon ? .on : .off
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

    @MainActor
    @objc private func toggleHideDockIcon() {
        AppSettings.shared.hideDockIcon.toggle()
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
