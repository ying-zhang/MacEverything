import AppKit

@MainActor
enum FileActions {
    static func open(_ url: URL, isApplication: Bool = false) {
        let configuration = NSWorkspace.OpenConfiguration()
        let completion: (NSRunningApplication?, Error?) -> Void = { _, error in
            guard let error else { return }
            AppLogger.error("FileActions", "Failed to open \(url.path): \(error.localizedDescription)")
            DispatchQueue.main.async {
                NSSound.beep()
            }
        }

        if isApplication {
            NSWorkspace.shared.openApplication(
                at: url,
                configuration: configuration,
                completionHandler: completion
            )
        } else {
            NSWorkspace.shared.open(
                url,
                configuration: configuration,
                completionHandler: completion
            )
        }
    }
}
