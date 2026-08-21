#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>
#include "TrigramExtraction.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ME_BENCH_HAS_NEON 1
#else
#define ME_BENCH_HAS_NEON 0
#endif

namespace {

using Clock = std::chrono::steady_clock;

volatile uint64_t benchSink = 0;

struct Result {
    double medianMs;
    double gibPerSecond;
    uint64_t checksum;
};

uint32_t pack3(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

std::vector<uint32_t> extractTrigramsCurrent(const std::string& text) {
    if (text.size() < 3) return {};

    static constexpr size_t kBitmapSize = 1 << 24;
    thread_local std::vector<bool> seen(kBitmapSize, false);
    thread_local std::vector<uint32_t> dirty;
    for (uint32_t trigram : dirty) seen[trigram] = false;
    dirty.clear();

    std::vector<uint32_t> result;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        const uint8_t a = static_cast<uint8_t>(
            std::tolower(static_cast<unsigned char>(text[i])));
        const uint8_t b = static_cast<uint8_t>(
            std::tolower(static_cast<unsigned char>(text[i + 1])));
        const uint8_t c = static_cast<uint8_t>(
            std::tolower(static_cast<unsigned char>(text[i + 2])));
        const uint32_t trigram = (static_cast<uint32_t>(a) << 16) |
                                 (static_cast<uint32_t>(b) << 8) | c;
        if (!seen[trigram]) {
            seen[trigram] = true;
            dirty.push_back(trigram);
            result.push_back(trigram);
        }
    }
    return result;
}

__attribute__((noinline))
uint64_t packScalar(const uint8_t* data, size_t size, uint32_t* output) {
    const size_t count = size >= 3 ? size - 2 : 0;
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
    for (size_t i = 0; i < count; ++i) {
        output[i] = pack3(data + i);
    }
    return count == 0 ? 0 : output[count / 2];
}

__attribute__((noinline))
uint64_t packAutoVectorized(const uint8_t* data, size_t size, uint32_t* output) {
    const size_t count = size >= 3 ? size - 2 : 0;
    for (size_t i = 0; i < count; ++i) {
        output[i] = pack3(data + i);
    }
    return count == 0 ? 0 : output[count / 2];
}

#if ME_BENCH_HAS_NEON
inline void packEight(uint8x8_t a, uint8x8_t b, uint8x8_t c, uint32_t* output) {
    const uint16x8_t a16 = vmovl_u8(a);
    const uint16x8_t b16 = vmovl_u8(b);
    const uint16x8_t c16 = vmovl_u8(c);

    uint32x4_t lo = vshlq_n_u32(vmovl_u16(vget_low_u16(a16)), 16);
    lo = vorrq_u32(lo, vshlq_n_u32(vmovl_u16(vget_low_u16(b16)), 8));
    lo = vorrq_u32(lo, vmovl_u16(vget_low_u16(c16)));
    vst1q_u32(output, lo);

    uint32x4_t hi = vshlq_n_u32(vmovl_u16(vget_high_u16(a16)), 16);
    hi = vorrq_u32(hi, vshlq_n_u32(vmovl_u16(vget_high_u16(b16)), 8));
    hi = vorrq_u32(hi, vmovl_u16(vget_high_u16(c16)));
    vst1q_u32(output + 4, hi);
}
#endif

__attribute__((noinline))
uint64_t packNeon(const uint8_t* data, size_t size, uint32_t* output) {
    const size_t count = size >= 3 ? size - 2 : 0;
    size_t i = 0;
#if ME_BENCH_HAS_NEON
    for (; i + 16 <= count; i += 16) {
        const uint8x16_t a = vld1q_u8(data + i);
        const uint8x16_t b = vld1q_u8(data + i + 1);
        const uint8x16_t c = vld1q_u8(data + i + 2);
        packEight(vget_low_u8(a), vget_low_u8(b), vget_low_u8(c), output + i);
        packEight(vget_high_u8(a), vget_high_u8(b), vget_high_u8(c), output + i + 8);
    }
#endif
    for (; i < count; ++i) {
        output[i] = pack3(data + i);
    }
    return count == 0 ? 0 : output[count / 2];
}

template <typename Function>
Result measurePacking(Function&& function, const std::string& input,
                      std::vector<uint32_t>& output, int iterations) {
    std::array<double, 9> samples{};
    uint64_t checksum = 0;
    function(reinterpret_cast<const uint8_t*>(input.data()), input.size(), output.data());

    for (double& sample : samples) {
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            checksum += function(reinterpret_cast<const uint8_t*>(input.data()),
                                 input.size(), output.data());
        }
        const auto end = Clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::sort(samples.begin(), samples.end());
    benchSink = checksum;
    const double medianMs = samples[samples.size() / 2];
    const double bytes = static_cast<double>(input.size()) * iterations;
    return {medianMs, bytes / (medianMs / 1000.0) / (1024.0 * 1024.0 * 1024.0), checksum};
}

Result measureProductionExtraction(const std::vector<std::string>& names, int iterations) {
    std::array<double, 9> samples{};
    uint64_t checksum = 0;
    for (double& sample : samples) {
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            for (const auto& name : names) {
                auto trigrams = extractTrigramsCurrent(name);
                checksum += trigrams.size();
                if (!trigrams.empty()) checksum += trigrams[trigrams.size() / 2];
            }
        }
        const auto end = Clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::sort(samples.begin(), samples.end());
    benchSink = checksum;
    const size_t bytesPerIteration = std::accumulate(
        names.begin(), names.end(), size_t{0},
        [](size_t sum, const std::string& value) { return sum + value.size(); });
    const double medianMs = samples[samples.size() / 2];
    const double bytes = static_cast<double>(bytesPerIteration) * iterations;
    return {medianMs, bytes / (medianMs / 1000.0) / (1024.0 * 1024.0 * 1024.0), checksum};
}

Result measureOptimizedExtraction(const std::vector<std::string>& names, int iterations) {
    std::array<double, 9> samples{};
    uint64_t checksum = 0;
    for (double& sample : samples) {
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            for (const auto& name : names) {
                auto trigrams = me::extractByteTrigrams(name);
                checksum += trigrams.size();
                if (!trigrams.empty()) checksum += trigrams[trigrams.size() / 2];
            }
        }
        const auto end = Clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::sort(samples.begin(), samples.end());
    benchSink = checksum;
    const size_t bytesPerIteration = std::accumulate(
        names.begin(), names.end(), size_t{0},
        [](size_t sum, const std::string& value) { return sum + value.size(); });
    const double medianMs = samples[samples.size() / 2];
    const double bytes = static_cast<double>(bytesPerIteration) * iterations;
    return {medianMs, bytes / (medianMs / 1000.0) / (1024.0 * 1024.0 * 1024.0), checksum};
}

std::string repeatToSize(std::string_view pattern, size_t bytes) {
    std::string result;
    result.reserve(bytes + pattern.size());
    while (result.size() < bytes) result.append(pattern);
    result.resize(bytes);
    return result;
}

std::vector<std::string> makeNames(std::string_view pattern, size_t count) {
    std::vector<std::string> names;
    names.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string name(pattern);
        name += std::to_string(i);
        name += ".txt";
        names.push_back(std::move(name));
    }
    return names;
}

void printResult(std::string_view label, const Result& result, double scalarGiBps = 0.0) {
    std::cout << "  " << std::left << std::setw(28) << label
              << std::right << std::setw(9) << std::fixed << std::setprecision(3)
              << result.medianMs << " ms"
              << std::setw(11) << std::setprecision(2) << result.gibPerSecond << " GiB/s";
    if (scalarGiBps > 0.0) {
        std::cout << std::setw(9) << std::setprecision(2)
                  << result.gibPerSecond / scalarGiBps << "x";
    }
    std::cout << '\n';
}

void runPackingCase(std::string_view label, const std::string& input, int iterations) {
    std::vector<uint32_t> scalarOutput(input.size());
    std::vector<uint32_t> testOutput(input.size());

    packScalar(reinterpret_cast<const uint8_t*>(input.data()), input.size(), scalarOutput.data());
    packAutoVectorized(reinterpret_cast<const uint8_t*>(input.data()), input.size(), testOutput.data());
    const size_t trigramCount = input.size() >= 3 ? input.size() - 2 : 0;
    if (!std::equal(scalarOutput.begin(), scalarOutput.begin() + trigramCount,
                    testOutput.begin())) {
        std::cerr << "Auto-vectorized validation failed for " << label << '\n';
        std::exit(2);
    }
    packNeon(reinterpret_cast<const uint8_t*>(input.data()), input.size(), testOutput.data());
    if (!std::equal(scalarOutput.begin(), scalarOutput.begin() + trigramCount,
                    testOutput.begin())) {
        std::cerr << "NEON validation failed for " << label << '\n';
        std::exit(2);
    }

    std::cout << "\n" << label << " (" << input.size() / (1024 * 1024) << " MiB, median of 9)\n";
    const Result scalar = measurePacking(packScalar, input, scalarOutput, iterations);
    const Result automatic = measurePacking(packAutoVectorized, input, testOutput, iterations);
    const Result neon = measurePacking(packNeon, input, testOutput, iterations);
    printResult("scalar (vectorization off)", scalar);
    printResult("compiler auto-vectorized", automatic, scalar.gibPerSecond);
    printResult("explicit NEON", neon, scalar.gibPerSecond);
}

} // namespace

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 32;
    constexpr size_t kInputBytes = 8 * 1024 * 1024;

    const std::string ascii = repeatToSize(
        "MacEverything-readme-source-application-config-index-2026.txt/", kInputBytes);
    const std::string cjk = repeatToSize(
        "\xE4\xB8\xAD\xE6\x96\x87\xE6\x96\x87\xE4\xBB\xB6\xE6\x90\x9C\xE7\xB4\xA2"
        "-MacEverything-2026.txt/",
        kInputBytes);

    std::cout << "MacEverything trigram packing microbenchmark\n"
              << "Architecture: "
#if ME_BENCH_HAS_NEON
              << "ARM64 NEON\n";
#else
              << "non-NEON (explicit NEON row uses scalar fallback)\n";
#endif

    runPackingCase("ASCII byte trigrams", ascii, iterations);
    runPackingCase("CJK UTF-8 byte trigrams", cjk, iterations);

    constexpr size_t kNameCount = 100000;
    const auto asciiNames = makeNames("MacEverything-readme-config-", kNameCount);
    const auto cjkNames = makeNames(
        "\xE4\xB8\xAD\xE6\x96\x87\xE6\x96\x87\xE4\xBB\xB6\xE6\x90\x9C\xE7\xB4\xA2-", kNameCount);
    std::cout << "\nCurrent extractTrigrams algorithm (" << kNameCount
              << " filenames, median of 9)\n";
    printResult("ASCII filenames", measureProductionExtraction(asciiNames, 1));
    printResult("CJK UTF-8 filenames", measureProductionExtraction(cjkNames, 1));
    std::cout << "\nOptimized rolling extraction with local dedup\n";
    printResult("ASCII filenames", measureOptimizedExtraction(asciiNames, 1));
    printResult("CJK UTF-8 filenames", measureOptimizedExtraction(cjkNames, 1));

    std::cout << "\nThe packing rows exclude deduplication, allocation, hash lookup, and posting-list work.\n";
    return 0;
}
