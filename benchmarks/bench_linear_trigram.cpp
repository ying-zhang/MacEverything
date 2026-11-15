#include "SearchEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Sample {
    double medianUs;
    double p95Us;
    std::string searchPath;
    size_t candidates;
};

double percentile(const std::vector<double>& sorted, double fraction) {
    const size_t rank = static_cast<size_t>(std::ceil(fraction * sorted.size()));
    const size_t index = std::max<size_t>(1, rank) - 1;
    return sorted[index];
}

Sample measure(const SearchEngine& engine, const std::string& query,
               bool useTrigram) {
    constexpr int kWarmups = 5;
    constexpr int kSamples = 25;
    constexpr int kBatch = 3;

    for (int i = 0; i < kWarmups; ++i) {
        QueryTimingInfo ignored;
        (void)engine.queryAdvanced(query, 100, useTrigram, ignored);
    }

    std::vector<double> times;
    times.reserve(kSamples);
    std::string searchPath;
    size_t candidates = 0;
    for (int i = 0; i < kSamples; ++i) {
        const auto start = Clock::now();
        for (int j = 0; j < kBatch; ++j) {
            QueryTimingInfo timing;
            (void)engine.queryAdvanced(query, 100, useTrigram, timing);
            searchPath = timing.searchPath;
            candidates = timing.candidates;
        }
        const auto end = Clock::now();
        times.push_back(std::chrono::duration<double, std::micro>(end - start).count() / kBatch);
    }
    std::sort(times.begin(), times.end());
    return {percentile(times, 0.50), percentile(times, 0.95), searchPath, candidates};
}

std::vector<FileRecord> makeRecords(uint32_t count) {
    std::vector<FileRecord> records;
    records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        FileRecord record;
        if (i % 100 == 0) {
            record.name = "needle_marker_target_" + std::to_string(i) + ".swift";
        } else if (i % 4 == 0) {
            record.name = "report_component_" + std::to_string(i) + ".txt";
        } else {
            record.name = "ordinary_component_" + std::to_string(i) + ".dat";
        }
        record.path = "/synthetic/directory_" + std::to_string(i % 1000);
        record.type = 1;
        record.size = i;
        record.modTime = static_cast<time_t>(1700000000 + i);
        records.push_back(std::move(record));
    }
    return records;
}

} // namespace

int main() {
    const std::vector<uint32_t> sizes = {
        100, 250, 500, 1000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000,
    };
    const std::vector<std::string> queries = {
        "needle_marker_target", // about 1% of records; selective long term
        "needle_marker_absent", // absent literal; trigram should short-circuit
        "component",            // common term; adaptive path should prefer scan
        "xy",                   // short term; no usable trigram
    };

    std::cout << "Linear scan vs adaptive Trigram; limit=100; each query result is checked.\n"
              << "Warm-cache median/P95 per query over 25 batches x 3 calls.\n"
              << "Calls use queryAdvanced directly to avoid UI narrow-query reuse.\n\n";
    std::cout << std::left << std::setw(9) << "records" << std::setw(25) << "query"
              << std::setw(19) << "linear med / p95" << std::setw(19) << "adaptive med / p95"
              << std::setw(11) << "speedup" << std::setw(23) << "adaptive path"
              << "candidates\n";

    for (uint32_t size : sizes) {
        SearchEngine engine;
        engine.loadRecords(makeRecords(size));

        for (const auto& query : queries) {
            QueryTimingInfo linearPathTiming;
            QueryTimingInfo adaptivePathTiming;
            const auto linearCheck = engine.queryAdvanced(query, 100, false, linearPathTiming);
            const auto adaptiveCheck = engine.queryAdvanced(query, 100, true, adaptivePathTiming);
            if (linearCheck != adaptiveCheck) {
                std::cerr << "Result mismatch at N=" << size << " query=\"" << query << "\"\n";
                return 2;
            }

            // Alternate measurement order to reduce systematic order bias.
            Sample linear;
            Sample adaptive;
            if ((size / 1000 + query.size()) % 2 == 0) {
                linear = measure(engine, query, false);
                adaptive = measure(engine, query, true);
            } else {
                adaptive = measure(engine, query, true);
                linear = measure(engine, query, false);
            }

            const double speedup = adaptive.medianUs > 0.0
                ? linear.medianUs / adaptive.medianUs : 0.0;
            std::cout << std::left << std::setw(9) << size << std::setw(25) << query
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(7) << linear.medianUs << "/" << std::setw(7) << linear.p95Us << " us"
                      << std::setw(7) << adaptive.medianUs << "/" << std::setw(7) << adaptive.p95Us << " us"
                      << std::setw(8) << speedup << "x  "
                      << std::left << std::setw(23) << adaptive.searchPath
                      << adaptive.candidates << '\n';
        }
    }
    return 0;
}
