#import "MacSearchBridge_Internal.h"
#import "MacSearchBridge+Content.h"
#include "Logger.h"
#include <unordered_map>

@implementation MacSearchBridge (Content)

- (NSArray<MEContentResult *> *)queryContent:(NSString *)keyword maxResults:(uint32_t)maxResults {
    auto queryStart = std::chrono::steady_clock::now();
    auto engine = _serviceEngine->safeEngine();
    auto contentIndex = _serviceEngine->safeContentIndex();
    if (!engine || !contentIndex) return @[];

    std::string key([keyword UTF8String]);
    if (key.empty()) return @[];

    auto service = _serviceEngine;
    NSMutableArray<MEContentResult *> *results = nil;
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint64_t contentGeneration = contentIndex->mappingGeneration();
        if ((contentGeneration & 1U) != 0) continue;
        uint64_t generation = engine->compactionGeneration();
        auto matches = contentIndex->query(key, maxResults,
            [service](uint32_t, std::string& fullPath) {
                if (fullPath.empty()) return false;
                return service->isContentPathAllowed(fullPath);
            });
        if (contentIndex->mappingGeneration() != contentGeneration) continue;
        if (matches.empty()) {
            if (engine->compactionGeneration() == generation) return @[];
            continue;
        }

        std::vector<uint32_t> indices;
        indices.reserve(matches.size());
        std::unordered_map<uint32_t, const ContentMatch *> matchByIndex;
        matchByIndex.reserve(matches.size());
        for (const auto& match : matches) {
            indices.push_back(match.fileIndex);
            matchByIndex[match.fileIndex] = &match;
        }

        results = [NSMutableArray arrayWithCapacity:matches.size()];
        bool stable = engine->forEachRecordWithPathIfGeneration(
            indices, generation, [&](uint32_t idx, const FileRecord& record, const std::string& path) {
                if (record.type != 1) return;
                auto matchIt = matchByIndex.find(idx);
                if (matchIt == matchByIndex.end()) return;
                const auto& match = *matchIt->second;
                std::string fullPath = SearchEngine::makeFullPath(path, record.name);
                NSString *nsFileName = [NSString stringWithUTF8String:record.name.c_str()];
                NSString *nsFilePath = [NSString stringWithUTF8String:fullPath.c_str()];
                NSString *nsSnippet = [NSString stringWithUTF8String:match.snippet.c_str()];
                if (!nsFileName || !nsFilePath || !nsSnippet) return;
                [results addObject:[[MEContentResult alloc]
                    initWithFileName:nsFileName
                            filePath:nsFilePath
                             snippet:nsSnippet
                         matchOffset:match.matchOffset
                            fileType:record.type]];
            });
        if (stable && contentIndex->mappingGeneration() == contentGeneration) break;
        results = nil;
    }
    if (!results) {
        LOG_WARN("Bridge", "queryContent: compaction changed indices during retries");
        return @[];
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
