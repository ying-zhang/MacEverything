#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace me {

enum class PostingIntersectionStrategy {
    ScalarMerge,
    SkewedSetIntersection,
};

inline PostingIntersectionStrategy postingIntersectionStrategy(size_t leftSize,
                                                               size_t rightSize) {
    constexpr size_t kSkewRatio = 32;
    const size_t smaller = std::min(leftSize, rightSize);
    const size_t larger = std::max(leftSize, rightSize);
    if (smaller != 0 && larger / smaller >= kSkewRatio) {
        return PostingIntersectionStrategy::SkewedSetIntersection;
    }
    return PostingIntersectionStrategy::ScalarMerge;
}

inline size_t intersectScalarMerge(const uint32_t* left, size_t leftSize,
                                   const uint32_t* right, size_t rightSize,
                                   uint32_t* output) {
    size_t leftIndex = 0;
    size_t rightIndex = 0;
    size_t outputCount = 0;
    while (leftIndex < leftSize && rightIndex < rightSize) {
        const uint32_t leftValue = left[leftIndex];
        const uint32_t rightValue = right[rightIndex];
        if (leftValue < rightValue) {
            ++leftIndex;
        } else if (rightValue < leftValue) {
            ++rightIndex;
        } else {
            output[outputCount++] = leftValue;
            ++leftIndex;
            ++rightIndex;
        }
    }
    return outputCount;
}

inline size_t intersectSkewedSetIntersection(const uint32_t* left, size_t leftSize,
                                             const uint32_t* right, size_t rightSize,
                                             uint32_t* output) {
    return static_cast<size_t>(std::set_intersection(
        left, left + leftSize, right, right + rightSize, output) - output);
}

inline void intersectSortedPostingLists(const std::vector<uint32_t>& left,
                                        const std::vector<uint32_t>& right,
                                        std::vector<uint32_t>& output) {
    const size_t capacity = std::min(left.size(), right.size());
    output.resize(capacity);
    if (capacity == 0) return;

    size_t outputCount = 0;
    if (postingIntersectionStrategy(left.size(), right.size()) ==
        PostingIntersectionStrategy::SkewedSetIntersection) {
        outputCount = intersectSkewedSetIntersection(left.data(), left.size(),
                                                     right.data(), right.size(), output.data());
    } else {
        outputCount = intersectScalarMerge(left.data(), left.size(),
                                           right.data(), right.size(), output.data());
    }
    output.resize(outputCount);
}

inline std::vector<uint32_t> intersectSortedPostingLists(
    const std::vector<uint32_t>& left,
    const std::vector<uint32_t>& right) {
    std::vector<uint32_t> output;
    intersectSortedPostingLists(left, right, output);
    return output;
}

} // namespace me
