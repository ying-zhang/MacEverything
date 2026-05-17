import Cocoa
import Carbon.HIToolbox

class HotkeyManager {
    private var hotKeyRef: EventHotKeyRef?
    private var eventHandlerRef: EventHandlerRef?
    private var observer: NSObjectProtocol?

    func register() {
        registerCurrentHotkey()

        observer = NotificationCenter.default.addObserver(
            forName: .hotkeyChanged, object: nil, queue: .main
        ) { [weak self] _ in
            self?.unregisterHotkey()
            self?.registerCurrentHotkey()
        }
    }

    private func registerCurrentHotkey() {
        let defaults = UserDefaults.standard
        let keyCode: UInt32
        let modifiers: UInt32

        if defaults.object(forKey: "hotkeyKeyCode") != nil {
            keyCode = UInt32(defaults.integer(forKey: "hotkeyKeyCode"))
            modifiers = UInt32(defaults.integer(forKey: "hotkeyModifiers"))
        } else {
            keyCode = UInt32(kVK_Space)
            modifiers = UInt32(optionKey)
        }

        let hotKeyID = EventHotKeyID(signature: OSType(0x4D455648), id: 1)

        var ref: EventHotKeyRef?
        let status = RegisterEventHotKey(keyCode, modifiers, hotKeyID,
                                          GetApplicationEventTarget(), 0, &ref)
        if status == noErr {
            hotKeyRef = ref
        } else {
            AppLogger.error("Hotkey", "Failed to register hotkey: status \(status)")
        }

        // Only install event handler once
        if eventHandlerRef == nil {
            var eventSpec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard),
                                          eventKind: UInt32(kEventHotKeyPressed))
            InstallEventHandler(GetApplicationEventTarget(), { _, event, _ -> OSStatus in
                let mainWindow = NSApp.orderedWindows.first { SearchWindowSupport.isSearchWindow($0) }
                if NSApp.isActive, let window = mainWindow, window.isVisible {
                    NSApp.hide(nil)
                } else {
                    NSApp.activate(ignoringOtherApps: true)
                    if let window = mainWindow {
                        window.makeKeyAndOrderFront(nil)
                    }
                }
                return noErr
            }, 1, &eventSpec, nil, &eventHandlerRef)
        }
    }

    private func unregisterHotkey() {
        if let ref = hotKeyRef {
            UnregisterEventHotKey(ref)
            hotKeyRef = nil
        }
    }

    deinit {
        if let obs = observer {
            NotificationCenter.default.removeObserver(obs)
        }
        unregisterHotkey()
        if let handler = eventHandlerRef {
            RemoveEventHandler(handler)
            eventHandlerRef = nil
        }
    }
}
