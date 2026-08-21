#pragma once

#include "ContentIndex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace me {

inline std::vector<Trigram> extractByteTrigrams(std::string_view text) {
    if (text.size() < 3) return {};

    const auto lowerAsciiByte = [](unsigned char value) -> uint8_t {
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        return static_cast<uint8_t>(value);
    };
    const size_t trigramCount = text.size() - 2;
    std::vector<Trigram> result;
    result.reserve(std::min<size_t>(trigramCount, 4096));

    uint8_t a = lowerAsciiByte(static_cast<unsigned char>(text[0]));
    uint8_t b = lowerAsciiByte(static_cast<unsigned char>(text[1]));

    // Filenames, paths, and search terms are normally short. Keeping their
    // trigrams contiguous and sorting locally is cheaper than touching the
    // 2 MiB global bitmap at effectively random offsets.
    constexpr size_t kLocalDedupMaxBytes = 512;
    if (text.size() <= kLocalDedupMaxBytes) {
        for (size_t i = 2; i < text.size(); ++i) {
            const uint8_t c = lowerAsciiByte(static_cast<unsigned char>(text[i]));
            result.push_back(ContentIndex::makeTrigram(a, b, c));
            a = b;
            b = c;
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    // Large content uses a thread-local bitmap. Only touched bits are cleared,
    // so reset cost remains proportional to the number of unique trigrams.
    static constexpr size_t kBitmapSize = 1 << 24;
    thread_local std::vector<bool> seen(kBitmapSize, false);
    thread_local std::vector<Trigram> dirty;
    for (Trigram trigram : dirty) seen[trigram] = false;
    dirty.clear();

    for (size_t i = 2; i < text.size(); ++i) {
        const uint8_t c = lowerAsciiByte(static_cast<unsigned char>(text[i]));
        const Trigram trigram = ContentIndex::makeTrigram(a, b, c);
        if (!seen[trigram]) {
            seen[trigram] = true;
            dirty.push_back(trigram);
            result.push_back(trigram);
        }
        a = b;
        b = c;
    }
    return result;
}

} // namespace me
