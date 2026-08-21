import AppKit
import Combine
import QuickLookThumbnailing

@MainActor
final class ThumbnailService: ObservableObject {
    static let shared = ThumbnailService()

    @Published private(set) var revision: UInt64 = 0

    private let cache = NSCache<NSString, NSImage>()
    private let generator = QLThumbnailGenerator.shared
    private var inFlightKeys: Set<String> = []
    private var settingsSink: AnyCancellable?
    private var generation: UInt64 = 0
    private static let failedSentinel: NSImage = {
        let img = NSImage(size: NSSize(width: 1, height: 1))
        return img
    }()

    private init() {
        cache.countLimit = 300
        cache.totalCostLimit = 50 * 1024 * 1024

        settingsSink = AppSettings.shared.$showThumbnails
            .removeDuplicates()
            .sink { [weak self] enabled in
                if !enabled {
                    self?.cancelAll()
                }
            }
    }

    func thumbnail(for path: String, modTime: time_t, pixelSize: CGFloat) -> NSImage? {
        let scale = NSScreen.main?.backingScaleFactor ?? 2.0
        let key = cacheKey(path: path, modTime: modTime, pixelSize: pixelSize, scale: scale)

        if let cached = cache.object(forKey: key as NSString) {
            return cached === Self.failedSentinel ? nil : cached
        }

        guard inFlightKeys.insert(key).inserted else { return nil }

        let url = URL(fileURLWithPath: path)
        let size = CGSize(width: pixelSize * scale, height: pixelSize * scale)
        let request = QLThumbnailGenerator.Request(
            fileAt: url,
            size: size,
            scale: 1.0,
            representationTypes: .thumbnail
        )

        let gen = generation
        generator.generateRepresentations(for: request) { [weak self] thumbnail, _, _ in
            DispatchQueue.main.async {
                guard let self, self.generation == gen else { return }
                self.inFlightKeys.remove(key)

                if let thumbnail {
                    let image = thumbnail.nsImage
                    image.size = Self.aspectFitSize(image.size, maximumDimension: pixelSize)
                    let cost = Int(image.size.width * image.size.height * scale * scale * 4)
                    self.cache.setObject(image, forKey: key as NSString, cost: cost)
                    self.revision &+= 1
                } else {
                    self.cache.setObject(Self.failedSentinel, forKey: key as NSString, cost: 1)
                }
            }
        }

        return nil
    }

    func cancelAll() {
        generation &+= 1
        inFlightKeys.removeAll()
        cache.removeAllObjects()
        revision &+= 1
    }

    private func cacheKey(path: String, modTime: time_t, pixelSize: CGFloat, scale: CGFloat) -> String {
        let normalized = URL(fileURLWithPath: path).standardizedFileURL.path
        return "\(normalized)|\(modTime)|\(Int(pixelSize))|\(Int(scale))"
    }

    private static func aspectFitSize(_ size: NSSize, maximumDimension: CGFloat) -> NSSize {
        guard size.width > 0, size.height > 0, maximumDimension > 0 else {
            return NSSize(width: maximumDimension, height: maximumDimension)
        }
        let scale = min(maximumDimension / size.width, maximumDimension / size.height)
        return NSSize(width: size.width * scale, height: size.height * scale)
    }
}
