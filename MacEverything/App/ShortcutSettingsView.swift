import SwiftUI
import Carbon.HIToolbox

struct ShortcutSettingsView: View {
    @ObservedObject private var settings = AppSettings.shared
    @State private var isRecording = false
    @State private var currentShortcut: String = ""
    @State private var modifiers: UInt32 = UInt32(optionKey)
    @State private var keyCode: UInt32 = UInt32(kVK_Space)

    var body: some View {
        VStack(spacing: 20) {
            Text(L10n.tr("Global Hotkey Settings"))
                .font(.title2)
                .fontWeight(.semibold)

            Text(L10n.tr("Click the button below and press your desired key combination to set the global hotkey for showing/hiding MacEverything."))
                .font(.body)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 360)

            VStack(spacing: 12) {
                Text(L10n.tr("Current Shortcut"))
                    .font(.subheadline)
                    .foregroundColor(.secondary)

                Button(action: {
                    isRecording.toggle()
                }) {
                    Text(isRecording ? L10n.tr("Press a key combination...") : currentShortcut)
                        .font(.title3)
                        .fontWeight(.medium)
                        .foregroundColor(isRecording ? .accentColor : .primary)
                        .frame(minWidth: 200, minHeight: 40)
                        .background(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(isRecording ? Color.accentColor : Color.gray.opacity(0.4), lineWidth: isRecording ? 2 : 1)
                                .background(
                                    RoundedRectangle(cornerRadius: 8)
                                        .fill(Color(nsColor: .controlBackgroundColor))
                                )
                        )
                }
                .buttonStyle(.plain)
            }

            if isRecording {
                ShortcutRecorderView(
                    onKeyCombo: { newKeyCode, newModifiers in
                        keyCode = newKeyCode
                        modifiers = newModifiers
                        currentShortcut = describeShortcut(keyCode: newKeyCode, modifiers: newModifiers)
                        isRecording = false
                        saveAndApply()
                    },
                    onCancel: {
                        isRecording = false
                    }
                )
                .frame(width: 0, height: 0)
            }

            HStack(spacing: 16) {
                Button(L10n.tr("Reset to Default")) {
                    keyCode = UInt32(kVK_Space)
                    modifiers = UInt32(optionKey)
                    currentShortcut = describeShortcut(keyCode: keyCode, modifiers: modifiers)
                    saveAndApply()
                }

                Button(L10n.tr("Close")) {
                    NSApp.keyWindow?.close()
                }
                .keyboardShortcut(.cancelAction)
            }
        }
        .padding(30)
        .frame(width: 440, height: 280)
        .onAppear {
            loadSaved()
        }
    }

    private func loadSaved() {
        let defaults = UserDefaults.standard
        if defaults.object(forKey: "hotkeyKeyCode") != nil {
            keyCode = UInt32(defaults.integer(forKey: "hotkeyKeyCode"))
            modifiers = UInt32(defaults.integer(forKey: "hotkeyModifiers"))
        }
        currentShortcut = describeShortcut(keyCode: keyCode, modifiers: modifiers)
    }

    private func saveAndApply() {
        UserDefaults.standard.set(Int(keyCode), forKey: "hotkeyKeyCode")
        UserDefaults.standard.set(Int(modifiers), forKey: "hotkeyModifiers")
        NotificationCenter.default.post(name: .hotkeyChanged, object: nil)
    }
}

func describeShortcut(keyCode: UInt32, modifiers: UInt32) -> String {
    var parts: [String] = []

    if modifiers & UInt32(controlKey) != 0 { parts.append("⌃") }
    if modifiers & UInt32(optionKey) != 0 { parts.append("⌥") }
    if modifiers & UInt32(shiftKey) != 0 { parts.append("⇧") }
    if modifiers & UInt32(cmdKey) != 0 { parts.append("⌘") }

    let keyName: String
    switch Int(keyCode) {
    case kVK_Space: keyName = L10n.tr("Space")
    case kVK_Return: keyName = L10n.tr("Return")
    case kVK_Tab: keyName = "Tab"
    case kVK_Escape: keyName = "Esc"
    case kVK_Delete: keyName = L10n.tr("Delete")
    case kVK_ForwardDelete: keyName = L10n.tr("Fwd Delete")
    case kVK_UpArrow: keyName = "↑"
    case kVK_DownArrow: keyName = "↓"
    case kVK_LeftArrow: keyName = "←"
    case kVK_RightArrow: keyName = "→"
    case kVK_ANSI_A: keyName = "A"
    case kVK_ANSI_B: keyName = "B"
    case kVK_ANSI_C: keyName = "C"
    case kVK_ANSI_D: keyName = "D"
    case kVK_ANSI_E: keyName = "E"
    case kVK_ANSI_F: keyName = "F"
    case kVK_ANSI_G: keyName = "G"
    case kVK_ANSI_H: keyName = "H"
    case kVK_ANSI_I: keyName = "I"
    case kVK_ANSI_J: keyName = "J"
    case kVK_ANSI_K: keyName = "K"
    case kVK_ANSI_L: keyName = "L"
    case kVK_ANSI_M: keyName = "M"
    case kVK_ANSI_N: keyName = "N"
    case kVK_ANSI_O: keyName = "O"
    case kVK_ANSI_P: keyName = "P"
    case kVK_ANSI_Q: keyName = "Q"
    case kVK_ANSI_R: keyName = "R"
    case kVK_ANSI_S: keyName = "S"
    case kVK_ANSI_T: keyName = "T"
    case kVK_ANSI_U: keyName = "U"
    case kVK_ANSI_V: keyName = "V"
    case kVK_ANSI_W: keyName = "W"
    case kVK_ANSI_X: keyName = "X"
    case kVK_ANSI_Y: keyName = "Y"
    case kVK_ANSI_Z: keyName = "Z"
    case kVK_ANSI_0: keyName = "0"
    case kVK_ANSI_1: keyName = "1"
    case kVK_ANSI_2: keyName = "2"
    case kVK_ANSI_3: keyName = "3"
    case kVK_ANSI_4: keyName = "4"
    case kVK_ANSI_5: keyName = "5"
    case kVK_ANSI_6: keyName = "6"
    case kVK_ANSI_7: keyName = "7"
    case kVK_ANSI_8: keyName = "8"
    case kVK_ANSI_9: keyName = "9"
    case kVK_F1: keyName = "F1"
    case kVK_F2: keyName = "F2"
    case kVK_F3: keyName = "F3"
    case kVK_F4: keyName = "F4"
    case kVK_F5: keyName = "F5"
    case kVK_F6: keyName = "F6"
    case kVK_F7: keyName = "F7"
    case kVK_F8: keyName = "F8"
    case kVK_F9: keyName = "F9"
    case kVK_F10: keyName = "F10"
    case kVK_F11: keyName = "F11"
    case kVK_F12: keyName = "F12"
    default: keyName = "Key(\(keyCode))"
    }

    parts.append(keyName)
    return parts.joined(separator: "")
}

// NSView-based key recorder to capture raw key events
struct ShortcutRecorderView: NSViewRepresentable {
    var onKeyCombo: (UInt32, UInt32) -> Void
    var onCancel: () -> Void

    func makeNSView(context: Context) -> ShortcutRecorderNSView {
        let view = ShortcutRecorderNSView()
        view.onKeyCombo = onKeyCombo
        view.onCancel = onCancel
        DispatchQueue.main.async {
            view.window?.makeFirstResponder(view)
        }
        return view
    }

    func updateNSView(_ nsView: ShortcutRecorderNSView, context: Context) {
        nsView.onKeyCombo = onKeyCombo
        nsView.onCancel = onCancel
    }
}

class ShortcutRecorderNSView: NSView {
    var onKeyCombo: ((UInt32, UInt32) -> Void)?
    var onCancel: (() -> Void)?

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)

        // Escape without modifiers cancels
        if event.keyCode == UInt16(kVK_Escape) && flags.isEmpty {
            onCancel?()
            return
        }

        // Require at least one modifier
        var carbonModifiers: UInt32 = 0
        if flags.contains(.command) { carbonModifiers |= UInt32(cmdKey) }
        if flags.contains(.option) { carbonModifiers |= UInt32(optionKey) }
        if flags.contains(.control) { carbonModifiers |= UInt32(controlKey) }
        if flags.contains(.shift) { carbonModifiers |= UInt32(shiftKey) }

        if carbonModifiers == 0 {
            NSSound.beep()
            return
        }

        if Self.isReservedShortcut(keyCode: UInt32(event.keyCode), modifiers: carbonModifiers) {
            NSSound.beep()
            return
        }

        onKeyCombo?(UInt32(event.keyCode), carbonModifiers)
    }

    override func flagsChanged(with event: NSEvent) {
        // Ignore pure modifier key presses
    }

    private static func isReservedShortcut(keyCode: UInt32, modifiers: UInt32) -> Bool {
        let cmdOnly = UInt32(cmdKey)
        let cmdShift = UInt32(cmdKey) | UInt32(shiftKey)
        let reserved: [(UInt32, UInt32)] = [
            (UInt32(kVK_ANSI_Q), cmdOnly),     // Cmd+Q
            (UInt32(kVK_ANSI_W), cmdOnly),     // Cmd+W
            (UInt32(kVK_Tab), cmdOnly),         // Cmd+Tab
            (UInt32(kVK_Space), cmdOnly),       // Cmd+Space (Spotlight)
            (UInt32(kVK_ANSI_H), cmdOnly),     // Cmd+H
            (UInt32(kVK_ANSI_M), cmdOnly),     // Cmd+M
            (UInt32(kVK_ANSI_Q), cmdShift),    // Cmd+Shift+Q (logout)
        ]
        return reserved.contains { $0.0 == keyCode && $0.1 == modifiers }
    }
}

extension Notification.Name {
    static let hotkeyChanged = Notification.Name("hotkeyChanged")
}
