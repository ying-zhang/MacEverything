#pragma once
// Part 9: ContentIndex sorted posting list benchmark

static void runContentIndexQueryBenchmark() {
    std::cout << "========================================\n";
    std::cout << "  Part 9: ContentIndex Sorted Posting Lists\n";
    std::cout << "========================================\n\n";

    // Test: sorted posting lists enable O(n+m) set_intersection
    // Build a ContentIndex with many files sharing overlapping trigrams
    std::string tmpDir = "/tmp/maceverything_ci_bench_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    ContentIndex ci;
    ci.setExtensions({"txt"});

    // Create 500 files with varied content to populate posting lists
    const int numFiles = 500;
    for (int i = 0; i < numFiles; i++) {
        std::string path = tmpDir + "/file_" + std::to_string(i) + ".txt";
        {
            std::ofstream ofs(path);
            // Write varied content so trigrams differ across files
            ofs << "content file number " << i << " with searchable keywords ";
            if (i % 3 == 0) ofs << "alpha beta gamma ";
            if (i % 5 == 0) ofs << "delta epsilon zeta ";
            if (i % 7 == 0) ofs << "searchable unique pattern ";
            ofs << "padding text to ensure enough trigrams are generated for the index.";
        }
        ci.indexFile(static_cast<uint32_t>(i), path);
    }
    auto resolver = [&](uint32_t idx, std::string& path) {
        if (idx >= static_cast<uint32_t>(numFiles)) return false;
        path = tmpDir + "/file_" + std::to_string(idx) + ".txt";
        return true;
    };

    check(ci.indexedFileCount() == numFiles,
          "CI-Bench: all files indexed");

    // Benchmark: query that requires intersecting multiple posting lists
    auto t0 = std::chrono::steady_clock::now();
    const int iterations = 1000;
    int totalMatches = 0;
    for (int run = 0; run < iterations; run++) {
        auto results = ci.query("alpha", 100, resolver);
        totalMatches += results.size();
    }
    auto t1 = std::chrono::steady_clock::now();
    double queryTime = std::chrono::duration<double>(t1 - t0).count() * 1000;

    std::cout << "  " << iterations << "x query 'alpha' (" << (totalMatches / iterations)
              << " matches/query): " << std::fixed << std::setprecision(2)
              << queryTime << "ms (" << queryTime / iterations << "ms/query)\n";

    // Verify correctness: 'alpha' appears in files where i%3==0
    auto results = ci.query("alpha", 1000, resolver);
    int expectedCount = 0;
    for (int i = 0; i < numFiles; i++) {
        if (i % 3 == 0) expectedCount++;
    }
    check(static_cast<int>(results.size()) == expectedCount,
          "CI-Bench: 'alpha' matches correct count of files");

    // Benchmark: query with no matches (should short-circuit via trigram miss)
    t0 = std::chrono::steady_clock::now();
    for (int run = 0; run < iterations; run++) {
        auto res = ci.query("xyznonexistent", 100, resolver);
        (void)res;
    }
    t1 = std::chrono::steady_clock::now();
    double noMatchTime = std::chrono::duration<double>(t1 - t0).count() * 1000;

    std::cout << "  " << iterations << "x query no-match: " << std::fixed << std::setprecision(2)
              << noMatchTime << "ms (" << noMatchTime / iterations << "ms/query)\n";
    check(totalMatches > 0, "CI-Bench: query benchmark returned matches");

    // Test: posting lists are sorted after load from file
    std::string savePath = tmpDir + "/ci_bench.bin";
    ci.saveToFile(savePath);

    ContentIndex ci2;
    ci2.loadFromFile(savePath);
    auto results2 = ci2.query("alpha", 1000, resolver);
    check(results2.size() == results.size(),
          "CI-Bench: loaded index returns same results as original");

    fs::remove_all(tmpDir);
    std::cout << "\n";
}
