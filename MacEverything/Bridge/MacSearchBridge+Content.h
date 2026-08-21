#import "MacSearchBridge.h"

NS_ASSUME_NONNULL_BEGIN

/// Category exposing content-search methods on MacSearchBridge.
@interface MacSearchBridge (Content)

/// Search file contents for the given keyword. Returns content results with snippets.
- (NSArray<MEContentResult *> *)queryContent:(NSString *)keyword maxResults:(uint32_t)maxResults;

/// Set allowed file extensions for content indexing (lowercase, without dot).
- (void)setContentExtensions:(NSArray<NSString *> *)extensions;

/// Set maximum file size for content indexing (bytes).
- (void)setContentMaxFileSize:(uint64_t)bytes;

/// Number of content-indexed files.
- (uint32_t)contentIndexedFileCount;

/// Get current content indexing extensions.
- (NSArray<NSString *> *)contentGetExtensions;

/// Get current max file size for content indexing (bytes).
- (uint64_t)contentGetMaxFileSize;

/// Rebuild content index with current settings (clears old index, re-indexes all files).
- (void)rebuildContentIndex;

/// Clear the persisted content index and reset in-memory content index state.
- (void)clearContentIndex;

@end

NS_ASSUME_NONNULL_END
