#pragma once

#include "PostingListIntersection.h"

#include <algorithm>
#include <random>
#include <set>

static std::vector<Trigram> referenceTrigrams(const std::string& text) {
    std::set<Trigram> unique;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        auto lower = [](unsigned char value) -> uint8_t {
            if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
            return static_cast<uint8_t>(value);
        };
        unique.insert(ContentIndex::makeTrigram(
            lower(static_cast<unsigned char>(text[i])),
            lower(static_cast<unsigned char>(text[i + 1])),
            lower(static_cast<unsigned char>(text[i + 2]))));
    }
    return {unique.begin(), unique.end()};
}

static void runAdaptiveIndexKernelTests() {
    std::cout << "═══ Part 79: Adaptive Index Kernels ═══\n\n";

    const std::vector<std::string> trigramInputs = {
        "",
        "ab",
        "ABCabcABC",
        "MacEverything-README.txt",
        "中文文件搜索-MacEverything.txt",
        std::string(511, 'A'),
        std::string(512, 'B'),
        std::string(513, 'C'),
        std::string(8192, 'x'),
    };
    for (size_t i = 0; i < trigramInputs.size(); ++i) {
        auto actual = ContentIndex::extractTrigrams(trigramInputs[i]);
        std::sort(actual.begin(), actual.end());
        check(actual == referenceTrigrams(trigramInputs[i]),
              ("79.1 rolling trigram equivalence case " + std::to_string(i)).c_str());
    }

    check(me::postingIntersectionStrategy(1024, 1024) ==
              me::PostingIntersectionStrategy::ScalarMerge,
          "79.2 balanced postings select scalar merge");
    check(me::postingIntersectionStrategy(64, 1024) ==
              me::PostingIntersectionStrategy::ScalarMerge,
          "79.3 moderately skewed postings keep scalar merge");
    check(me::postingIntersectionStrategy(64, 4096) ==
              me::PostingIntersectionStrategy::SkewedSetIntersection,
          "79.4 highly skewed postings select std::set_intersection");

    std::mt19937 generator(0x4d454b49U);
    std::uniform_int_distribution<size_t> balancedSize(0, 2048);
    std::uniform_int_distribution<uint32_t> increment(1, 8);
    bool randomEquivalent = true;
    for (size_t testIndex = 0; testIndex < 500; ++testIndex) {
        size_t leftSize = balancedSize(generator);
        size_t rightSize = balancedSize(generator);
        if (testIndex % 5 == 0) rightSize *= 16;

        std::vector<uint32_t> left;
        std::vector<uint32_t> right;
        left.reserve(leftSize);
        right.reserve(rightSize);
        uint32_t value = 0;
        for (size_t i = 0; i < leftSize; ++i) {
            value += increment(generator);
            left.push_back(value);
        }
        value = 0;
        for (size_t i = 0; i < rightSize; ++i) {
            value += increment(generator);
            right.push_back(value);
        }

        std::vector<uint32_t> expected;
        expected.reserve(std::min(left.size(), right.size()));
        std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
                              std::back_inserter(expected));
        if (me::intersectSortedPostingLists(left, right) != expected ||
            me::intersectSortedPostingLists(right, left) != expected) {
            randomEquivalent = false;
            break;
        }
    }
    check(randomEquivalent,
          "79.5 adaptive posting intersection matches std::set_intersection on 500 random cases");

    std::cout << "\n";
}
