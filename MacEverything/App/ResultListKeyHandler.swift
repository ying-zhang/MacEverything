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

        var previewURLs: [URL] {
            parent.viewModel.selectedFileURLs()
        }

        var isQLVisible: Bool {
            QLPreviewPanel.sharedPreviewPanelExists() && QLPreviewPanel.shared().isVisible
        }

        func toggleQuickLook() {
            guard QLPreviewPanel.sharedPreviewPanelExists() || !previewURLs.isEmpty else { return }
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
            return previewURLs.count
        }

        func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> (any QLPreviewItem)! {
            let urls = previewURLs
            guard urls.indices.contains(index) else { return nil }
            return urls[index] as NSURL
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

        let gridMode = AppSettings.shared.resultDisplayMode == .grid

        switch event.keyCode {
        case 125: // Down arrow
            _ = gridMode
                ? coordinator.parent.viewModel.selectGridDown()
                : coordinator.parent.viewModel.selectNext()
            coordinator.refreshQuickLook()
        case 126: // Up arrow
            let moved = gridMode
                ? coordinator.parent.viewModel.selectGridUp()
                : coordinator.parent.viewModel.selectPrevious()
            if moved {
                coordinator.refreshQuickLook()
            } else {
                coordinator.parent.wantsFocus = false
                coordinator.parent.onReturnFocusToSearch?()
            }
        case 123: // Left arrow (grid only)
            if gridMode {
                _ = coordinator.parent.viewModel.selectGridLeft()
                coordinator.refreshQuickLook()
            } else {
                super.keyDown(with: event)
            }
        case 124: // Right arrow (grid only)
            if gridMode {
                _ = coordinator.parent.viewModel.selectGridRight()
                coordinator.refreshQuickLook()
            } else {
                super.keyDown(with: event)
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
