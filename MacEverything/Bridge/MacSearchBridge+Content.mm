#import "MacSearchBridge_Internal.h"
#import "MacSearchBridge+Content.h"
#include "Logger.h"

@implementation MacSearchBridge (Content)

- (NSArray<MEContentResult *> *)queryContent:(NSString *)keyword maxResults:(uint32_t)maxResults {
    auto queryStart = std::chrono::steady_clock::now();
    auto engine = _serviceEngine->safeEngine();
    auto contentIndex = _serviceEngine->safeContentIndex();
    if (!engine || !contentIndex) return @[];

    std::string key([keyword UTF8String]);
    if (key.empty()) return @[];

    auto service = _serviceEngine;
    auto matches = contentIndex->query(key, maxResults,
        [engine, service](uint32_t fileIndex, std::string& fullPath) {
            auto record = engine->getRecord(fileIndex);
            if (record.type != 1) return false;
            fullPath = SearchEngine::makeFullPath(record.path, record.name);
            return service->isContentPathAllowed(fullPath);
        });
    if (matches.empty()) return @[];

    // Pre-resolve file paths
    struct CandidateInfo {
        uint32_t fileIndex;
        std::string name;
        std::string fullPath;
        std::string snippet;
        uint32_t matchOffset;
        uint8_t fileType;
    };
    std::vector<CandidateInfo> candidates;
    candidates.reserve(matches.size());
    for (const auto& match : matches) {
        auto record = engine->getRecord(match.fileIndex);
        if (record.type == 0) continue;
        CandidateInfo info;
        info.fileIndex = match.fileIndex;
        info.name = std::move(record.name);
        info.fullPath = SearchEngine::makeFullPath(record.path, info.name);
        info.snippet = match.snippet;
        info.matchOffset = match.matchOffset;
        info.fileType = record.type;
        candidates.push_back(std::move(info));
    }

    if (candidates.empty()) return @[];

    NSMutableArray<MEContentResult *> *results = [NSMutableArray arrayWithCapacity:candidates.size()];
    for (size_t i = 0; i < candidates.size(); i++) {
        NSString *nsFileName = [NSString stringWithUTF8String:candidates[i].name.c_str()];
        NSString *nsFilePath = [NSString stringWithUTF8String:candidates[i].fullPath.c_str()];
        NSString *nsSnippet = [NSString stringWithUTF8String:candidates[i].snippet.c_str()];
        if (!nsFileName || !nsFilePath || !nsSnippet) continue;
        [results addObject:[[MEContentResult alloc]
            initWithFileName:nsFileName
                    filePath:nsFilePath
                     snippet:nsSnippet
                 matchOffset:candidates[i].matchOffset
                    fileType:candidates[i].fileType]];
    }

    auto queryElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - queryStart).count();
    if (queryElapsed > 0.1) {
        LOG_INFO("Bridge", "queryContent(\"" << key << "\") returned " << (uint32_t)results.count << " results in " << queryElapsed << "s");
    }

    return results;
}

- (void)setContentExtensions:(NSArray<NSString *> *)extensions {
    auto contentIndex = _serviceEngine->safeContentIndex();
    if (!contentIndex) return;
    std::vector<std::string> exts;
    exts.reserve(extensions.count);
    for (NSString *ext in extensions) {
        exts.push_back(std::string([ext UTF8String]));
    }
    contentIndex->setExtensions(exts);
}

- (void)setContentMaxFileSize:(uint64_t)bytes {
    auto contentIndex = _serviceEngine->safeContentIndex();
    if (contentIndex) {
        contentIndex->setMaxFileSize(bytes);
    }
}

- (uint32_t)contentIndexedFileCount {
    auto contentIndex = _serviceEngine->safeContentIndex();
    return contentIndex ? contentIndex->indexedFileCount() : 0;
}

- (NSArray<NSString *> *)contentGetExtensions {
    auto contentIndex = _serviceEngine->safeContentIndex();
    if (!contentIndex) return @[];
    auto exts = contentIndex->getExtensions();
    NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:exts.size()];
    for (const auto& ext : exts) {
        NSString *str = [NSString stringWithUTF8String:ext.c_str()];
        if (!str) continue;
        [result addObject:str];
    }
    return result;
}

- (uint64_t)contentGetMaxFileSize {
    auto contentIndex = _serviceEngine->safeContentIndex();
    return contentIndex ? contentIndex->getMaxFileSize() : (1 * 1024 * 1024);
}

- (void)rebuildContentIndex {
    _serviceEngine->rebuildContentIndex();
}

- (void)clearContentIndex {
    _serviceEngine->clearContentIndex();
}

@end
