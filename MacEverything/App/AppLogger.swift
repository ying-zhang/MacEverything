import Foundation

enum AppLogger {
    static func debug(_ module: String, _ msg: String) {
        MacSearchBridge.logMessage(msg, level: 0, module: module)
    }

    static func info(_ module: String, _ msg: String) {
        MacSearchBridge.logMessage(msg, level: 1, module: module)
    }

    static func warn(_ module: String, _ msg: String) {
        MacSearchBridge.logMessage(msg, level: 2, module: module)
    }

    static func error(_ module: String, _ msg: String) {
        MacSearchBridge.logMessage(msg, level: 3, module: module)
    }
}
