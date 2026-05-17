#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Lightweight wrapper exposing a single file/directory result to Swift.
@interface MEFileResult : NSObject
@property (nonatomic, readonly) NSString *name;
@property (nonatomic, readonly) NSString *path;
@property (nonatomic, readonly) uint8_t type;      // 1=file, 2=dir, 3=symlink, 4=other
@property (nonatomic, readonly) uint64_t size;
@property (nonatomic, readonly) time_t modTime;
- (instancetype)initWithName:(NSString *)name
                        path:(NSString *)path
                        type:(uint8_t)type
                        size:(uint64_t)size
                     modTime:(time_t)modTime;
@end

/// Lightweight wrapper exposing a content search result to Swift.
@interface MEContentResult : NSObject
@property (nonatomic, readonly) NSString *fileName;
@property (nonatomic, readonly) NSString *filePath;
@property (nonatomic, readonly) NSString *snippet;      // matched context with ellipsis
@property (nonatomic, readonly) uint32_t matchOffset;    // byte offset of match in file
@property (nonatomic, readonly) uint8_t fileType;        // 1=file, 2=dir, 3=symlink, 4=other
- (instancetype)initWithFileName:(NSString *)fileName
                        filePath:(NSString *)filePath
                         snippet:(NSString *)snippet
                     matchOffset:(uint32_t)matchOffset
                        fileType:(uint8_t)fileType;
@end

/// Highlight hint extracted from the query AST.
@interface MEHighlightHint : NSObject
@property (nonatomic, readonly) NSString *text;
@property (nonatomic, readonly) uint8_t field;       // 0=NAME, 1=PATH, 2=ANY
@property (nonatomic, readonly) uint8_t matchMode;   // 0=SUBSTRING, 1=GLOB, 2=REGEX, 3=WHOLEWORD, 4=WHOLEFILENAME
@property (nonatomic, readonly) BOOL caseSensitive;
- (instancetype)initWithText:(NSString *)text
                       field:(uint8_t)field
                   matchMode:(uint8_t)matchMode
               caseSensitive:(BOOL)caseSensitive;
@end

/// Bridge between the C++ search engine and Swift/SwiftUI.
@interface MacSearchBridge : NSObject

+ (instancetype)shared;

/// Start a full-disk scan on a background queue.
/// The completion block is called on the main queue when done.
/// File system monitoring starts automatically after scan completes.
- (void)startScanFrom:(NSString *)rootPath
           completion:(void (^)(uint32_t totalRecords))completion;

/// Start with incremental loading: load cached index + WAL, replay FSEvents since last save.
/// Falls back to full scan if FSEvents journal is unavailable.
/// Completion reports total records and whether a full scan was needed.
- (void)startIncrementalFrom:(NSString *)rootPath
                   cachePath:(NSString *)cachePath
                     walPath:(NSString *)walPath
                  completion:(void (^)(uint32_t totalRecords, BOOL didFullScan))completion;

- (void)updateConfigurationWithScanRoots:(NSArray<NSString *> *)scanRoots
                           excludedPaths:(NSArray<NSString *> *)excludedPaths
                         excludedPatterns:(NSArray<NSString *> *)excludedPatterns
                             contentRoots:(NSArray<NSString *> *)contentRoots
                      contentExcludedPaths:(NSArray<NSString *> *)contentExcludedPaths
                              includeHidden:(BOOL)includeHidden
                              includeSystem:(BOOL)includeSystem
                 includeAppBundleContents:(BOOL)includeAppBundleContents
                         realtimeMonitoring:(BOOL)realtimeMonitoring
                    contentIndexingEnabled:(BOOL)contentIndexingEnabled
                 automaticMaintenanceEnabled:(BOOL)automaticMaintenanceEnabled
                       enablePinyinInitials:(BOOL)enablePinyinInitials
               enablePathSearchAcceleration:(BOOL)enablePathSearchAcceleration
                                  httpPort:(uint16_t)httpPort;

/// Compact the index: write new base snapshot, clear WAL.
- (void)compactIndex;

/// Perform a case-insensitive substring search. Returns matching record indices.
- (NSArray<NSNumber *> *)queryIndices:(NSString *)keyword
                           maxResults:(uint32_t)maxResults;

/// Fetch multiple records by indices (batch).
- (NSArray<MEFileResult *> *)recordsAtIndices:(NSArray<NSNumber *> *)indices;

/// Return indices of the most recently modified files, sorted by modTime descending.
- (NSArray<NSNumber *> *)recentIndices:(uint32_t)count;

/// P-4: Perform query and return results directly, eliminating NSNumber boxing overhead.
- (NSArray<MEFileResult *> *)queryResults:(NSString *)keyword maxResults:(uint32_t)maxResults;

/// Perform query with session-scoped cancellation.
- (NSArray<MEFileResult *> *)queryResults:(NSString *)keyword
                               maxResults:(uint32_t)maxResults
                                sessionId:(uint64_t)sessionId;

/// Cancel in-flight queries for the given session.
- (void)cancelSession:(uint64_t)sessionId;

/// P-4: Return most recently modified files as results directly.
- (NSArray<MEFileResult *> *)recentResults:(uint32_t)count;

/// Parse a query string and return highlight hints extracted from the AST.
- (NSArray<MEHighlightHint *> *)parseHighlightHints:(NSString *)query;

/// Total number of indexed records (including tombstones).
- (uint32_t)recordCount;

/// Number of live (non-tombstoned) records.
- (uint32_t)liveRecordCount;

/// Approximate memory used by the in-memory filename/path index.
- (uint64_t)indexMemoryApproxBytes;

/// Stop file system monitoring.
- (void)stopMonitoring;

/// Gracefully shut down: stop monitoring, then compact index.
/// Call this from applicationWillTerminate instead of compactIndex directly.
- (void)prepareForTermination;

/// Save the current index to a binary file.
- (BOOL)saveIndexToFile:(NSString *)path;

/// Load index from a binary file. Returns YES if successful.
- (BOOL)loadIndexFromFile:(NSString *)path;

/// Rescan a directory subtree and update the index incrementally.
- (void)rescanSubtree:(NSString *)dirPath;

/// Whether a scan is currently in progress.
@property (nonatomic, readonly) BOOL isScanning;

/// Whether file system monitoring is active.
@property (nonatomic, readonly) BOOL isMonitoring;

/// Whether background sync is in progress (index is searchable but may be stale).
@property (nonatomic, readonly) BOOL isSyncing;

// --- HTTP Server ---

/// Start the embedded HTTP API server on the given port (127.0.0.1 only).
- (void)startHttpServer:(uint16_t)port;

/// Stop the embedded HTTP API server.
- (void)stopHttpServer;

// --- Logging ---

/// Initialize the logging system. Call once at app startup.
+ (void)initializeLogger;

/// Write a log message (for Swift callers). Level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR.
+ (void)logMessage:(NSString *)message level:(int)level module:(NSString *)module;

/// Get the current log file path.
+ (NSString *)logFilePath;

/// Called on the main queue when file system changes are applied to the index.
@property (nonatomic, copy, nullable) void (^onIndexChanged)(void);

/// Called on the main queue periodically during scanning with progress counts.
@property (nonatomic, copy, nullable) void (^onScanProgress)(uint64_t fileCount, uint64_t dirCount);

/// Called on the main queue periodically during content indexing with progress.
@property (nonatomic, copy, nullable) void (^onContentIndexProgress)(uint32_t indexed, uint32_t total);

/// Called on the main queue when content indexing completes.
@property (nonatomic, copy, nullable) void (^onContentIndexComplete)(uint32_t totalIndexed);

@end

NS_ASSUME_NONNULL_END
