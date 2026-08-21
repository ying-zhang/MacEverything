#pragma once

// Logger.h is included by test_all.cpp
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

static void runLoggerTests() {
    std::cout << "\n══════════ Part 25: Logger Tests ══════════\n\n";

    namespace fs = std::filesystem;
    const std::string testLogDir = "/tmp/maceverything_logger_test_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    // Clean up from previous runs
    fs::remove_all(testLogDir);

    // ── Test 1: Init creates directory and log file ──
    {
        auto& lg = me::Logger::instance();
        lg.shutdown();  // Reset state from any prior test
        lg.init(testLogDir, me::LogLevel::Debug);

        check(fs::exists(testLogDir), "Logger creates log directory");
        check(fs::exists(testLogDir + "/maceverything.log"), "Logger creates log file");

        // Check session-start marker was written
        std::ifstream f(testLogDir + "/maceverything.log");
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        check(content.find("Log session started") != std::string::npos,
              "Log file contains session start marker");

        lg.shutdown();
    }

    // ── Test 2: Log level filtering ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Warn);

        lg.log(me::LogLevel::Debug, "Test", "debug msg");
        lg.log(me::LogLevel::Info,  "Test", "info msg");
        lg.log(me::LogLevel::Warn,  "Test", "warn msg");
        lg.log(me::LogLevel::Error, "Test", "error msg");

        lg.shutdown();

        std::ifstream f(testLogDir + "/maceverything.log");
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        check(content.find("debug msg") == std::string::npos,
              "DEBUG filtered out when level=WARN");
        check(content.find("info msg") == std::string::npos,
              "INFO filtered out when level=WARN");
        check(content.find("warn msg") != std::string::npos,
              "WARN passes through when level=WARN");
        check(content.find("error msg") != std::string::npos,
              "ERROR passes through when level=WARN");
    }

    // ── Test 3: setLevel changes filtering at runtime ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Error);

        lg.log(me::LogLevel::Info, "Test", "should-be-filtered");
        lg.setLevel(me::LogLevel::Debug);
        lg.log(me::LogLevel::Debug, "Test", "should-appear");

        lg.shutdown();

        std::ifstream f(testLogDir + "/maceverything.log");
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        check(content.find("should-be-filtered") == std::string::npos,
              "Message filtered before setLevel");
        check(content.find("should-appear") != std::string::npos,
              "Message passes after setLevel(Debug)");
    }

    // ── Test 4: Log format has timestamp, level, module ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Debug);

        lg.log(me::LogLevel::Info, "MyModule", "test format");

        lg.shutdown();

        std::ifstream f(testLogDir + "/maceverything.log");
        std::string line;
        std::string targetLine;
        while (std::getline(f, line)) {
            if (line.find("test format") != std::string::npos) {
                targetLine = line;
            }
        }

        // Expected: [2026-04-14 ...] [INFO ] [MyModule] test format
        check(!targetLine.empty(), "Log line with test message found");
        check(targetLine.find("[INFO ]") != std::string::npos,
              "Log line contains level tag");
        check(targetLine.find("[MyModule]") != std::string::npos,
              "Log line contains module tag");
        check(targetLine[0] == '[' && targetLine[5] == '-',
              "Log line starts with timestamp [YYYY-...");
    }

    // ── Test 5: LOG_* macros work ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Debug);

        LOG_DEBUG("Macro", "debug via macro");
        LOG_INFO("Macro", "info with number " << 42);
        LOG_WARN("Macro", "warn msg");
        LOG_ERROR("Macro", "error msg");

        lg.shutdown();

        std::ifstream f(testLogDir + "/maceverything.log");
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        check(content.find("debug via macro") != std::string::npos,
              "LOG_DEBUG macro works");
        check(content.find("info with number 42") != std::string::npos,
              "LOG_INFO macro works with stream operator");
        check(content.find("warn msg") != std::string::npos,
              "LOG_WARN macro works");
        check(content.find("error msg") != std::string::npos,
              "LOG_ERROR macro works");
    }

    // ── Test 6: LogTimer logs elapsed time ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Debug);

        {
            LOG_TIMER("Timer", "sleep operation");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        lg.shutdown();

        std::ifstream f(testLogDir + "/maceverything.log");
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        check(content.find("sleep operation completed in") != std::string::npos,
              "LogTimer logs elapsed time on scope exit");
        check(content.find("[Timer]") != std::string::npos,
              "LogTimer uses correct module name");
    }

    // ── Test 7: Thread safety — concurrent writes ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Debug);

        constexpr int kThreads = 8;
        constexpr int kMessagesPerThread = 100;
        std::vector<std::thread> threads;

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&lg, t]() {
                for (int i = 0; i < kMessagesPerThread; ++i) {
                    lg.log(me::LogLevel::Info, "Thread",
                           "t" + std::to_string(t) + "-m" + std::to_string(i));
                }
            });
        }
        for (auto& th : threads) th.join();

        lg.shutdown();

        // Count lines in log file (subtract 2 for session start/end markers)
        std::ifstream f(testLogDir + "/maceverything.log");
        int lineCount = 0;
        std::string line;
        while (std::getline(f, line)) ++lineCount;

        int expected = kThreads * kMessagesPerThread + 2;  // +2 for markers
        std::string msg = "All concurrent messages logged (" + std::to_string(lineCount) +
              " lines, expected " + std::to_string(expected) + ")";
        check(lineCount == expected, msg.c_str());
    }

    // ── Test 8: File rotation ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Debug);

        // Write enough to trigger rotation (>5MB)
        std::string bigMessage(1024, 'X');  // 1KB per message
        for (int i = 0; i < 5200; ++i) {  // ~5.2MB
            lg.log(me::LogLevel::Info, "Rot", bigMessage);
        }

        lg.shutdown();

        // After rotation, maceverything.log should exist (new file)
        // and maceverything.log.1 should exist (rotated old file)
        check(fs::exists(testLogDir + "/maceverything.log"),
              "Current log file exists after rotation");
        check(fs::exists(testLogDir + "/maceverything.log.1"),
              "Rotated log file .1 exists");

        // Current file should be small (post-rotation writes)
        auto currentSize = fs::file_size(testLogDir + "/maceverything.log");
        check(currentSize < 5 * 1024 * 1024,
              "Current log file is smaller than 5MB after rotation");
    }

    // ── Test 9: getLogFilePath ──
    {
        fs::remove_all(testLogDir);
        auto& lg = me::Logger::instance();
        lg.init(testLogDir, me::LogLevel::Info);

        std::string path = lg.getLogFilePath();
        check(path == testLogDir + "/maceverything.log",
              "getLogFilePath returns correct path");

        lg.shutdown();
    }

    // Clean up
    fs::remove_all(testLogDir);

    std::cout << "\n  Logger tests complete.\n";
}
