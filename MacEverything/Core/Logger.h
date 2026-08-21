#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <sstream>

namespace me {

enum class LogLevel : int {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

/// Thread-safe singleton logger with file rotation and console output.
class Logger {
public:
    static Logger& instance();

    /// Initialize the logger. Call once at startup.
    /// @param logDir  Directory for log files (created if needed)
    /// @param level   Minimum level to log
    void init(const std::string& logDir, LogLevel level = LogLevel::Info);

    /// Write a log message (thread-safe).
    void log(LogLevel level, const char* module, const std::string& message);

    /// Set minimum log level at runtime (lock-free).
    void setLevel(LogLevel level);

    /// Get current log level.
    LogLevel getLevel() const;

    /// Get current log file path.
    std::string getLogFilePath() const;

    /// Flush and close the log file (call at shutdown).
    void shutdown();

    // Non-copyable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    void rotateIfNeeded();
    void writeRaw(const std::string& line);
    std::string formatTimestamp() const;
    static const char* levelToString(LogLevel level);

    std::atomic<LogLevel> level_{LogLevel::Info};
    std::atomic<bool> initialized_{false};
    mutable std::mutex mutex_;
    std::string logDir_;
    std::string logFilePath_;
    FILE* file_ = nullptr;

    static constexpr size_t kMaxFileSize = 5 * 1024 * 1024;  // 5 MB
    static constexpr int kMaxFiles = 3;
};

/// RAII timer that logs elapsed time on scope exit.
class LogTimer {
public:
    LogTimer(const char* module, const char* operation)
        : module_(module), operation_(operation)
        , start_(std::chrono::steady_clock::now()) {}

    ~LogTimer() {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        std::ostringstream oss;
        if (ms >= 1000.0) {
            oss << operation_ << " completed in " << (ms / 1000.0) << "s";
        } else {
            oss << operation_ << " completed in " << ms << "ms";
        }
        Logger::instance().log(LogLevel::Info, module_, oss.str());
    }

    /// Get elapsed time in milliseconds (for use during the timed scope).
    double elapsedMs() const {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }

    // Non-copyable, non-movable
    LogTimer(const LogTimer&) = delete;
    LogTimer& operator=(const LogTimer&) = delete;

private:
    const char* module_;
    const char* operation_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace me

// ── Convenience macros ──────────────────────────────────────────

#define LOG_DEBUG(module, msg) \
    do { \
        auto& _lg = me::Logger::instance(); \
        if (_lg.getLevel() <= me::LogLevel::Debug) { \
            std::ostringstream _oss; _oss << msg; \
            _lg.log(me::LogLevel::Debug, module, _oss.str()); \
        } \
    } while (0)

#define LOG_INFO(module, msg) \
    do { \
        auto& _lg = me::Logger::instance(); \
        if (_lg.getLevel() <= me::LogLevel::Info) { \
            std::ostringstream _oss; _oss << msg; \
            _lg.log(me::LogLevel::Info, module, _oss.str()); \
        } \
    } while (0)

#define LOG_WARN(module, msg) \
    do { \
        auto& _lg = me::Logger::instance(); \
        if (_lg.getLevel() <= me::LogLevel::Warn) { \
            std::ostringstream _oss; _oss << msg; \
            _lg.log(me::LogLevel::Warn, module, _oss.str()); \
        } \
    } while (0)

#define LOG_ERROR(module, msg) \
    do { \
        auto& _lg = me::Logger::instance(); \
        if (_lg.getLevel() <= me::LogLevel::Error) { \
            std::ostringstream _oss; _oss << msg; \
            _lg.log(me::LogLevel::Error, module, _oss.str()); \
        } \
    } while (0)

/// Scoped timer — logs elapsed time when the enclosing scope exits.
/// Usage: LOG_TIMER("Module", "operation description");
#define LOG_TIMER_CONCAT_IMPL(a, b) a##b
#define LOG_TIMER_CONCAT(a, b) LOG_TIMER_CONCAT_IMPL(a, b)
#define LOG_TIMER(module, operation) \
    me::LogTimer LOG_TIMER_CONCAT(_logTimer_, __LINE__)(module, operation)
