#pragma once
// Part 12: Parallel Snippet Generation Tests

static void runParallelSnippetTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 12: Parallel Snippet Generation\n";
    std::cout << "========================================\n\n";

    // Create temp directory with test files
    std::string tmpDir = "/tmp/maceverything_p12_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    const int NUM_FILES = 20;
    std::string keyword = "SearchableKeyword";

    for (int i = 0; i < NUM_FILES; i++) {
        std::string filePath = tmpDir + "/file_" + std::to_string(i) + ".txt";
        std::ofstream ofs(filePath);
        // Write some padding, then the keyword, then more padding
        for (int j = 0; j < 10; j++) ofs << "Line " << j << " of file " << i << " with some padding text.\n";
        ofs << "Found the " << keyword << " here in file " << i << "!\n";
        for (int j = 0; j < 10; j++) ofs << "More padding line " << j << " after the keyword.\n";
    }

    // Test generateSnippet correctness
    for (int i = 0; i < NUM_FILES; i++) {
        std::string filePath = tmpDir + "/file_" + std::to_string(i) + ".txt";
        uint32_t offset = 0;
        auto snippet = ContentIndex::generateSnippet(filePath, keyword, offset);
        check(!snippet.empty(), "snippet generation: file has snippet");
        check(snippet.find(keyword) != std::string::npos || snippet.find("SearchableK") != std::string::npos,
              "snippet generation: file contains keyword");
    }

    // Benchmark: sequential vs parallel snippet generation
    auto seqStart = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 10; rep++) {
        for (int i = 0; i < NUM_FILES; i++) {
            std::string filePath = tmpDir + "/file_" + std::to_string(i) + ".txt";
            uint32_t offset = 0;
            auto snippet = ContentIndex::generateSnippet(filePath, keyword, offset);
            (void)snippet;
        }
    }
    auto seqEnd = std::chrono::high_resolution_clock::now();
    double seqMs = std::chrono::duration<double, std::milli>(seqEnd - seqStart).count();

    // Parallel version using dispatch_apply
    std::vector<std::string> filePaths(NUM_FILES);
    for (int i = 0; i < NUM_FILES; i++) {
        filePaths[i] = tmpDir + "/file_" + std::to_string(i) + ".txt";
    }

    auto parStart = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 10; rep++) {
        auto* snippetsBuf = new std::string[NUM_FILES];
        auto* offsetsBuf = new uint32_t[NUM_FILES]();
        dispatch_queue_t queue = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
        dispatch_apply(NUM_FILES, queue, ^(size_t i) {
            snippetsBuf[i] = ContentIndex::generateSnippet(filePaths[i], keyword, offsetsBuf[i]);
        });
        delete[] snippetsBuf;
        delete[] offsetsBuf;
    }
    auto parEnd = std::chrono::high_resolution_clock::now();
    double parMs = std::chrono::duration<double, std::milli>(parEnd - parStart).count();

    std::cout << "    Sequential: 10x " << NUM_FILES << " snippets: "
              << std::fixed << std::setprecision(2) << seqMs << "ms\n";
    std::cout << "    Parallel:   10x " << NUM_FILES << " snippets: "
              << std::fixed << std::setprecision(2) << parMs << "ms\n";
    if (parMs > 0) {
        std::cout << "    Speedup: " << std::fixed << std::setprecision(1) << seqMs / parMs << "x\n";
    }
    check(seqMs > 0 && parMs > 0, "parallel snippet generation: benchmark produced timing data");

    // Verify parallel results are correct
    {
        auto* snippetsBuf = new std::string[NUM_FILES];
        auto* offsetsBuf2 = new uint32_t[NUM_FILES]();
        dispatch_queue_t queue = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
        dispatch_apply(NUM_FILES, queue, ^(size_t i) {
            snippetsBuf[i] = ContentIndex::generateSnippet(filePaths[i], keyword, offsetsBuf2[i]);
        });
        int validCount = 0;
        for (int i = 0; i < NUM_FILES; i++) {
            if (!snippetsBuf[i].empty()) validCount++;
        }
        delete[] snippetsBuf;
        delete[] offsetsBuf2;
        check(validCount == NUM_FILES, "parallel snippet generation: all snippets valid");
    }

    // Cleanup
    fs::remove_all(tmpDir);
    std::cout << "\n";
}
