#include "Logger.h"
#include <ctime>
#include <sys/stat.h>
#include <filesystem>
#include <iostream>

namespace me {

Logger::Logger() = default;

Logger::~Logger() {
    shutdown();
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& logDir, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load(std::memory_order_relaxed)) return;

    logDir_ = logDir;
    level_.store(level, std::memory_order_relaxed);

    // Create log directory if needed
    std::filesystem::create_directories(logDir_);

    logFilePath_ = logDir_ + "/maceverything.log";
    file_ = fopen(logFilePath_.c_str(), "a");
    if (!file_) {
        std::cerr << "[Logger] Failed to open log file: " << logFilePath_ << "\n";
        return;
    }

    initialized_.store(true, std::memory_order_release);

    // Write startup marker
    std::string startLine = formatTimestamp() + " [INFO] [Logger] === Log session started ===\n";
    writeRaw(startLine);
}

void Logger::log(LogLevel level, const char* module, const std::string& message) {
    // Fast path: level check is lock-free
    if (level < level_.load(std::memory_order_relaxed)) return;

    // Avoid building the log string if logger is not initialized
    if (!initialized_.load(std::memory_order_relaxed)) return;

    std::string line = formatTimestamp() + " [" + levelToString(level) + "] ["
                     + module + "] " + message + "\n";

    std::lock_guard<std::mutex> lock(mutex_);

    // Console output
    if (level >= LogLevel::Error) {
        std::cerr << line;
    } else {
        std::cout << line;
    }

    // File output
    if (file_) {
        rotateIfNeeded();
        writeRaw(line);
    }
}

void Logger::setLevel(LogLevel level) {
    level_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::getLevel() const {
    return level_.load(std::memory_order_relaxed);
}

std::string Logger::getLogFilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logFilePath_;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::string line = formatTimestamp() + " [INFO] [Logger] === Log session ended ===\n";
        writeRaw(line);
        fclose(file_);
        file_ = nullptr;
    }
    initialized_.store(false, std::memory_order_release);
}

void Logger::rotateIfNeeded() {
    // Caller holds mutex_
    if (!file_) return;

    long pos = ftell(file_);
    if (pos < 0 || static_cast<size_t>(pos) < kMaxFileSize) return;

    fclose(file_);

    // Rotate: remove oldest, shift others
    // maceverything.log.2 -> deleted
    // maceverything.log.1 -> maceverything.log.2
    // maceverything.log   -> maceverything.log.1
    for (int i = kMaxFiles - 1; i >= 1; --i) {
        std::string src = logDir_ + "/maceverything.log" +
                          (i == 1 ? "" : "." + std::to_string(i - 1));
        std::string dst = logDir_ + "/maceverything.log." + std::to_string(i);
        std::rename(src.c_str(), dst.c_str());
    }

    file_ = fopen(logFilePath_.c_str(), "w");
    if (!file_) {
        std::cerr << "[Logger] Failed to reopen log file after rotation\n";
    }
}

void Logger::writeRaw(const std::string& line) {
    // Caller holds mutex_
    if (!file_) return;
    fwrite(line.data(), 1, line.size(), file_);
    fflush(file_);
}

std::string Logger::formatTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

} // namespace me
