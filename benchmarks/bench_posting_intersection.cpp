#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <cassert>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "PostingListIntersection.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ME_BENCH_HAS_NEON 1
#else
#define ME_BENCH_HAS_NEON 0
#endif

namespace {

using Clock = std::chrono::steady_clock;
using IntersectionFn = size_t (*)(const uint32_t*, size_t, const uint32_t*, size_t, uint32_t*);

volatile uint64_t benchSink = 0;

struct Result {
    double milliseconds;
    double millionInputsPerSecond;
    size_t outputCount;
};

struct TestCase {
    std::string label;
    std::vector<uint32_t> left;
    std::vector<uint32_t> right;
};

__attribute__((noinline))
size_t intersectStdRaw(const uint32_t* left, size_t leftSize,
                       const uint32_t* right, size_t rightSize,
                       uint32_t* output) {
    return static_cast<size_t>(std::set_intersection(
        left, left + leftSize, right, right + rightSize, output) - output);
}

__attribute__((noinline))
size_t intersectScalar(const uint32_t* left, size_t leftSize,
                       const uint32_t* right, size_t rightSize,
                       uint32_t* output) {
    size_t i = 0;
    size_t j = 0;
    size_t count = 0;
    while (i < leftSize && j < rightSize) {
        if (left[i] < right[j]) {
            ++i;
        } else if (right[j] < left[i]) {
            ++j;
        } else {
            output[count++] = left[i];
            ++i;
            ++j;
        }
    }
    return count;
}

#if ME_BENCH_HAS_NEON
constexpr std::array<std::array<uint8_t, 16>, 16> makeCompressTable() {
    std::array<std::array<uint8_t, 16>, 16> table{};
    for (size_t mask = 0; mask < table.size(); ++mask) {
        size_t outputByte = 0;
        for (size_t lane = 0; lane < 4; ++lane) {
            if ((mask & (1U << lane)) == 0) continue;
            for (size_t byte = 0; byte < 4; ++byte) {
                table[mask][outputByte++] = static_cast<uint8_t>(lane * 4 + byte);
            }
        }
    }
    return table;
}

alignas(16) constexpr auto kCompressTable = makeCompressTable();
#endif

__attribute__((noinline))
size_t intersectNeon(const uint32_t* left, size_t leftSize,
                     const uint32_t* right, size_t rightSize,
                     uint32_t* output) {
    size_t i = 0;
    size_t j = 0;
    size_t count = 0;

#if ME_BENCH_HAS_NEON
    const uint32x4_t weights = {1, 2, 4, 8};
    while (i + 4 <= leftSize && j + 4 <= rightSize) {
        const uint32x4_t a = vld1q_u32(left + i);
        const uint32x4_t b = vld1q_u32(right + j);
        uint32x4_t matches = vceqq_u32(a, vdupq_laneq_u32(b, 0));
        matches = vorrq_u32(matches, vceqq_u32(a, vdupq_laneq_u32(b, 1)));
        matches = vorrq_u32(matches, vceqq_u32(a, vdupq_laneq_u32(b, 2)));
        matches = vorrq_u32(matches, vceqq_u32(a, vdupq_laneq_u32(b, 3)));

        const uint32_t mask = vaddvq_u32(vandq_u32(matches, weights));
        if (mask != 0) {
            const uint8x16_t indices = vld1q_u8(kCompressTable[mask].data());
            const uint8x16_t packed = vqtbl1q_u8(vreinterpretq_u8_u32(a), indices);
            vst1q_u32(output + count, vreinterpretq_u32_u8(packed));
            count += std::popcount(mask);
        }

        const uint32_t leftMax = left[i + 3];
        const uint32_t rightMax = right[j + 3];
        if (leftMax <= rightMax) i += 4;
        if (rightMax <= leftMax) j += 4;
    }
#endif

    count += intersectScalar(left + i, leftSize - i, right + j, rightSize - j,
                             output + count);
    return count;
}

__attribute__((noinline))
size_t intersectAdaptive(const uint32_t* left, size_t leftSize,
                         const uint32_t* right, size_t rightSize,
                         uint32_t* output) {
    if (me::postingIntersectionStrategy(leftSize, rightSize) ==
        me::PostingIntersectionStrategy::SkewedSetIntersection) {
        return me::intersectSkewedSetIntersection(left, leftSize, right, rightSize, output);
    }
    return me::intersectScalarMerge(left, leftSize, right, rightSize, output);
}

size_t intersectCurrentVector(const std::vector<uint32_t>& left,
                              const std::vector<uint32_t>& right,
                              std::vector<uint32_t>& output) {
    output.clear();
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
                          std::back_inserter(output));
    return output.size();
}

Result measureRaw(IntersectionFn function, const TestCase& test, int iterations,
                  std::vector<uint32_t>& output) {
    std::array<double, 9> samples{};
    size_t outputCount = 0;
    uint64_t checksum = 0;

    for (double& sample : samples) {
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            outputCount = function(test.left.data(), test.left.size(),
                                   test.right.data(), test.right.size(), output.data());
            checksum += outputCount;
            if (outputCount != 0) checksum += output[outputCount / 2];
        }
        const auto end = Clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    }
    std::sort(samples.begin(), samples.end());
    benchSink = checksum;
    const double medianMs = samples[samples.size() / 2];
    const double inputs = static_cast<double>(test.left.size() + test.right.size());
    return {medianMs, inputs / (medianMs / 1000.0) / 1'000'000.0, outputCount};
}

Result measureCurrentVector(const TestCase& test, int iterations,
                            std::vector<uint32_t>& output) {
    std::array<double, 9> samples{};
    size_t outputCount = 0;
    uint64_t checksum = 0;
    for (double& sample : samples) {
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            outputCount = intersectCurrentVector(test.left, test.right, output);
            checksum += outputCount;
            if (outputCount != 0) checksum += output[outputCount / 2];
        }
        const auto end = Clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    }
    std::sort(samples.begin(), samples.end());
    benchSink = checksum;
    const double medianMs = samples[samples.size() / 2];
    const double inputs = static_cast<double>(test.left.size() + test.right.size());
    return {medianMs, inputs / (medianMs / 1000.0) / 1'000'000.0, outputCount};
}

std::vector<uint32_t> arithmetic(size_t count, uint32_t start, uint32_t step) {
    std::vector<uint32_t> values(count);
    for (size_t i = 0; i < count; ++i) {
        values[i] = start + static_cast<uint32_t>(i) * step;
    }
    return values;
}

TestCase makeRareOverlap(std::string label, size_t count) {
    TestCase test{std::move(label), arithmetic(count, 0, 4), arithmetic(count, 1, 4)};
    for (size_t i = 0; i < count; i += 1024) test.right[i] = test.left[i];
    return test;
}

void validate(const TestCase& test, std::vector<uint32_t>& reference,
              std::vector<uint32_t>& candidate) {
    const auto strictlyIncreasing = [](const std::vector<uint32_t>& values) {
        return std::adjacent_find(values.begin(), values.end(),
                                  std::greater_equal<uint32_t>()) == values.end();
    };
    assert(strictlyIncreasing(test.left) && strictlyIncreasing(test.right) &&
           "posting lists must be strictly increasing and unique");
    const size_t expected = intersectStdRaw(test.left.data(), test.left.size(),
                                            test.right.data(), test.right.size(),
                                            reference.data());
    const size_t actual = intersectNeon(test.left.data(), test.left.size(),
                                        test.right.data(), test.right.size(),
                                        candidate.data());
    if (expected != actual || !std::equal(reference.begin(), reference.begin() + expected,
                                          candidate.begin())) {
        std::cerr << "NEON validation failed for " << test.label << ": expected "
                  << expected << ", got " << actual << '\n';
        std::exit(2);
    }
}

void validateRandomCases() {
    std::mt19937 generator(0x4d455349U);
    std::uniform_int_distribution<size_t> sizeDistribution(0, 4096);
    std::uniform_int_distribution<uint32_t> stepDistribution(1, 8);

    for (int caseIndex = 0; caseIndex < 500; ++caseIndex) {
        TestCase test{"random-" + std::to_string(caseIndex), {}, {}};
        const size_t leftSize = sizeDistribution(generator);
        const size_t rightSize = sizeDistribution(generator);
        test.left.reserve(leftSize);
        test.right.reserve(rightSize);

        uint32_t value = stepDistribution(generator);
        for (size_t i = 0; i < leftSize; ++i) {
            value += stepDistribution(generator);
            test.left.push_back(value);
        }
        value = stepDistribution(generator);
        for (size_t i = 0; i < rightSize; ++i) {
            value += stepDistribution(generator);
            test.right.push_back(value);
        }

        const size_t capacity = std::min(leftSize, rightSize) + 4;
        std::vector<uint32_t> reference(capacity);
        std::vector<uint32_t> candidate(capacity);
        validate(test, reference, candidate);
    }
}

void printResult(std::string_view label, const Result& result, double baselineMs) {
    std::cout << "  " << std::left << std::setw(27) << label << std::right;
    if (result.milliseconds < 0.1) {
        std::cout << std::setw(9) << std::fixed << std::setprecision(3)
                  << result.milliseconds * 1000.0 << " us";
    } else {
        std::cout << std::setw(9) << std::fixed << std::setprecision(3)
                  << result.milliseconds << " ms";
    }
    std::cout << std::setw(11) << std::setprecision(1)
              << result.millionInputsPerSecond << " M/s"
              << std::setw(9) << std::setprecision(2)
              << baselineMs / result.milliseconds << "x\n";
}

void runCase(const TestCase& test, int iterations) {
    const size_t capacity = std::min(test.left.size(), test.right.size()) + 4;
    std::vector<uint32_t> reference(capacity);
    std::vector<uint32_t> output(capacity);
    std::vector<uint32_t> vectorOutput;
    vectorOutput.reserve(capacity);
    validate(test, reference, output);

    const Result standard = measureRaw(intersectStdRaw, test, iterations, output);
    const Result scalar = measureRaw(intersectScalar, test, iterations, output);
    const Result current = measureCurrentVector(test, iterations, vectorOutput);
    const Result neon = measureRaw(intersectNeon, test, iterations, output);
    const Result adaptive = measureRaw(intersectAdaptive, test, iterations, output);

    std::cout << "\n" << test.label << " (" << test.left.size() << " x "
              << test.right.size() << ", output " << standard.outputCount << ")\n";
    printResult("std::set_intersection raw", standard, standard.milliseconds);
    printResult("handwritten scalar raw", scalar, standard.milliseconds);
    printResult("current back_inserter", current, standard.milliseconds);
    printResult("NEON 4x4 + compress", neon, standard.milliseconds);
    printResult("adaptive production", adaptive, standard.milliseconds);
}

} // namespace

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 16;
    constexpr size_t kBalancedSize = 1'000'000;

    std::cout << "MacEverything posting-list intersection microbenchmark\n"
              << "Architecture: "
#if ME_BENCH_HAS_NEON
              << "ARM64 NEON\n";
#else
              << "non-NEON (NEON row uses scalar fallback)\n";
#endif
    std::cout << "Inputs are strictly increasing unique posting lists.\n"
              << "Times are per intersection; median of 9 batches.\n";
    validateRandomCases();
    std::cout << "Validated against std::set_intersection on 500 random cases.\n";

    std::vector<TestCase> tests;
    tests.push_back({"identical small", arithmetic(256, 0, 2), arithmetic(256, 0, 2)});
    tests.push_back(makeRareOverlap("rare overlap small", 256));
    tests.push_back(makeRareOverlap("rare overlap medium", 4096));
    tests.push_back({"identical", arithmetic(kBalancedSize, 0, 2),
                     arithmetic(kBalancedSize, 0, 2)});
    tests.push_back({"dense partial overlap", arithmetic(kBalancedSize, 0, 2),
                     arithmetic(kBalancedSize, 0, 3)});
    tests.push_back({"disjoint interleaved", arithmetic(kBalancedSize, 0, 2),
                     arithmetic(kBalancedSize, 1, 2)});
    tests.push_back(makeRareOverlap("rare overlap (balanced)", kBalancedSize));
    tests.push_back({"skew boundary 64K x 256K (4x)", arithmetic(65'536, 0, 4),
                     arithmetic(262'144, 0, 1)});
    tests.push_back({"skew boundary 64K x 512K (8x)", arithmetic(65'536, 0, 8),
                     arithmetic(524'288, 0, 1)});
    tests.push_back({"skew boundary 64K x 1M (16x)", arithmetic(65'536, 0, 16),
                     arithmetic(1'048'576, 0, 1)});
    tests.push_back({"skew boundary 64K x 2M (32x)", arithmetic(65'536, 0, 32),
                     arithmetic(2'097'152, 0, 1)});
    tests.push_back({"skewed 64K x 4M", arithmetic(65'536, 0, 64),
                     arithmetic(4'000'000, 0, 1)});

    for (const auto& test : tests) {
        const size_t inputCount = test.left.size() + test.right.size();
        const int caseIterations = std::max(
            iterations, static_cast<int>(32'000'000 / std::max<size_t>(1, inputCount)));
        runCase(test, caseIterations);
    }
    return 0;
}
