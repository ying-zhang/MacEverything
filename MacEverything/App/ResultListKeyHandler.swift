import SwiftUI
import Quartz

// MARK: - ResultListKeyHandler (NSViewRepresentable)

struct ResultListKeyHandler: NSViewRepresentable {
    @ObservedObject var viewModel: SearchViewModel
    @Binding var wantsFocus: Bool
    var onReturnFocusToSearch: (() -> Void)?
    var onEscape: (() -> Void)?
    var onForwardCharacter: ((String) -> Void)?

    func makeCoordinator() -> Coordinator {
        Coordinator(self)
    }

    func makeNSView(context: Context) -> ResultListKeyView {
        let view = ResultListKeyView()
        view.coordinator = context.coordinator
        context.coordinator.keyView = view
        return view
    }

    func updateNSView(_ nsView: ResultListKeyView, context: Context) {
        context.coordinator.parent = self

        if wantsFocus, nsView.window != nil {
            if nsView.window?.firstResponder !== nsView {
                nsView.window?.makeFirstResponder(nsView)
            }
        }
    }

    @MainActor
    class Coordinator: NSObject, QLPreviewPanelDataSource, QLPreviewPanelDelegate {
        var parent: ResultListKeyHandler
        weak var keyView: ResultListKeyView?

        init(_ parent: ResultListKeyHandler) {
            self.parent = parent
        }

        var previewURL: URL? {
            parent.viewModel.selectedFileURL()
        }

        var isQLVisible: Bool {
            QLPreviewPanel.sharedPreviewPanelExists() && QLPreviewPanel.shared().isVisible
        }

        func toggleQuickLook() {
            guard QLPreviewPanel.sharedPreviewPanelExists() || parent.viewModel.selectedFileURL() != nil else { return }
            let panel = QLPreviewPanel.shared()!
            if panel.isVisible {
                panel.orderOut(nil)
            } else {
                panel.makeKeyAndOrderFront(nil)
            }
        }

        func closeQuickLook() {
            guard QLPreviewPanel.sharedPreviewPanelExists() else { return }
            let panel = QLPreviewPanel.shared()!
            if panel.isVisible {
                panel.orderOut(nil)
            }
        }

        func refreshQuickLook() {
            guard isQLVisible else { return }
            QLPreviewPanel.shared().reloadData()
        }

        // MARK: - QLPreviewPanelDataSource

        func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int {
            return previewURL != nil ? 1 : 0
        }

        func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> (any QLPreviewItem)! {
            return previewURL as? NSURL
        }

        // MARK: - QLPreviewPanelDelegate

        func previewPanel(_ panel: QLPreviewPanel!, handle event: NSEvent!) -> Bool {
            if event.type == .keyDown {
                keyView?.keyDown(with: event)
                return true
            }
            return false
        }
    }
}

// MARK: - ResultListKeyView

class ResultListKeyView: NSView {
    weak var coordinator: ResultListKeyHandler.Coordinator?

    override var acceptsFirstResponder: Bool { true }

    override func becomeFirstResponder() -> Bool {
        let result = super.becomeFirstResponder()
        if result, coordinator?.parent.viewModel.selectedItemID == nil {
            _ = coordinator?.parent.viewModel.selectNext()
        }
        return result
    }

    override func keyDown(with event: NSEvent) {
        guard let coordinator else {
            super.keyDown(with: event)
            return
        }

        switch event.keyCode {
        case 125: // Down arrow
            _ = coordinator.parent.viewModel.selectNext()
            coordinator.refreshQuickLook()
        case 126: // Up arrow
            if coordinator.parent.viewModel.selectPrevious() {
                coordinator.refreshQuickLook()
            } else {
                coordinator.parent.wantsFocus = false
                coordinator.parent.onReturnFocusToSearch?()
            }
        case 49: // Space - toggle Quick Look
            coordinator.toggleQuickLook()
        case 36, 76: // Return / Enter - open file
            coordinator.parent.viewModel.openSelectedOrFirst()
        case 53: // Escape
            coordinator.parent.onEscape?()
        default:
            if let chars = event.characters, !chars.isEmpty,
               !event.modifierFlags.contains(.command),
               !event.modifierFlags.contains(.control) {
                let c = chars.unicodeScalars.first!
                if c.properties.isAlphabetic || CharacterSet.decimalDigits.contains(c)
                    || CharacterSet.punctuationCharacters.contains(c)
                    || CharacterSet.symbols.contains(c) {
                    coordinator.parent.wantsFocus = false
                    coordinator.parent.onForwardCharacter?(chars)
                    return
                }
            }
            super.keyDown(with: event)
        }
    }

    // MARK: - QLPreviewPanel Controller

    override func acceptsPreviewPanelControl(_ panel: QLPreviewPanel!) -> Bool {
        return true
    }

    override func beginPreviewPanelControl(_ panel: QLPreviewPanel!) {
        panel.dataSource = coordinator
        panel.delegate = coordinator
    }

    override func endPreviewPanelControl(_ panel: QLPreviewPanel!) {
        panel.dataSource = nil
        panel.delegate = nil
    }

}
