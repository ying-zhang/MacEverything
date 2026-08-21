#pragma once

static void runScannerConfigTests() {
    std::cout << "═══ Part 77: Scanner Config Overrides ═══\n\n";

    // Test 1: Explicit system roots remain searchable even when the broad
    // "include system files" switch is off.
    {
        const std::string utilitiesRoot = "/System/Applications/Utilities";
        const std::string terminalPath = utilitiesRoot + "/Terminal.app";
        if (fs::exists(terminalPath)) {
            DirectoryScanner scanner;
            ScanConfig config;
            config.includeSystem = false;
            config.includeAppBundleContents = false;
            scanner.scan(std::vector<std::string>{utilitiesRoot}, config);

            auto results = scanner.takeResults();
            bool foundTerminal = false;
            for (const auto& record : results) {
                if (record.path == utilitiesRoot && record.name == "Terminal.app") {
                    foundTerminal = true;
                    check(record.type == 5, "C77: Terminal.app is indexed as app bundle");
                    break;
                }
            }
            check(foundTerminal, "C77: explicit /System application root overrides system filter");
        } else {
            std::cout << "  Skipping Terminal.app scan override test; path not present\n";
        }
    }

    // Test 2: The broad system filter still applies when no explicit system root exists.
    {
        std::string tmpDir = "/tmp/maceverything_system_filter_test_" + std::to_string(getpid());
        fs::create_directories(tmpDir + "/Library/Caches/test");
        std::ofstream(tmpDir + "/Library/Caches/test/cache.txt") << "cache";

        DirectoryScanner scanner;
        ScanConfig config;
        config.includeSystem = false;
        scanner.scan(std::vector<std::string>{tmpDir}, config);

        auto results = scanner.takeResults();
        bool foundCacheFile = false;
        for (const auto& record : results) {
            if (record.name == "cache.txt") {
                foundCacheFile = true;
                break;
            }
        }
        check(!foundCacheFile, "C77: system-like cache paths remain filtered by default");

        fs::remove_all(tmpDir);
    }

    std::cout << "\n";
}
