#pragma once
#include "PathUtils.h"
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/// Cross-cutting utility for the local HTTP API token.
///
///   - ServiceEngine ensures the token exists before starting HttpServer.
///   - HttpServer validates the Authorization: Bearer header.
///   - mace CLI and MCP read the token from the same file.
///   - GUI normal search bypasses HTTP entirely — no token needed.
///
/// Token location: ~/Library/Application Support/com.maceverything.app/.http_token
/// Permissions: 0600 (owner read/write only).
namespace HttpToken {

inline bool isValidToken(const std::string& token) {
    return token.size() == 64 &&
           token.find_first_not_of("0123456789abcdef") == std::string::npos;
}

inline bool ensureParentDirectory(const std::string& parentDir) {
    if (parentDir.empty() || parentDir.front() != '/') return false;
    std::error_code directoryError;
    std::filesystem::create_directories(parentDir, directoryError);
    if (directoryError) return false;
    int parentFd = open(parentDir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parentFd < 0) return false;
    struct stat parentStat{};
    const bool valid = fstat(parentFd, &parentStat) == 0 &&
                       S_ISDIR(parentStat.st_mode) && parentStat.st_uid == getuid() &&
                       fchmod(parentFd, 0700) == 0;
    close(parentFd);
    return valid;
}

/// Generate a cryptographically random 64-character hex token (256 bits).
inline std::string generateToken() {
    unsigned char bytes[32];
    arc4random_buf(bytes, sizeof(bytes));
    std::string token;
    token.reserve(sizeof(bytes) * 2);
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned char byte : bytes) {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0xF]);
    }
    return token;
}

/// Replace the token file with a caller-provided 256-bit lowercase hex token.
inline bool writeToken(
    const std::string& token,
    const std::string& tokenPath = PathUtils::getHttpTokenPath(),
    const std::string& parentDir = PathUtils::getDefaultAppSupportPath()) {
    if (!isValidToken(token)) return false;
    if (!ensureParentDirectory(parentDir)) return false;

    int fd = open(tokenPath.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX) != 0) { close(fd); return false; }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || fchmod(fd, 0600) != 0 ||
        ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
        flock(fd, LOCK_UN); close(fd); return false;
    }
    size_t totalWritten = 0;
    while (totalWritten < token.size()) {
        ssize_t written = write(fd, token.data() + totalWritten,
                                token.size() - totalWritten);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        totalWritten += static_cast<size_t>(written);
    }
    const bool persisted = totalWritten == token.size() && fsync(fd) == 0;
    flock(fd, LOCK_UN);
    close(fd);
    if (!persisted) unlink(tokenPath.c_str());
    return persisted;
}

inline std::string regenerateTokenFile() {
    const std::string token = generateToken();
    return writeToken(token) ? token : std::string();
}

/// Ensure a token file exists at the standard location.
/// Creates parent directories and the file with mode 0600 if absent.
/// Returns the token string, or empty string on failure.
inline std::string ensureTokenFile(
    const std::string& tokenPath = PathUtils::getHttpTokenPath(),
    const std::string& parentDir = PathUtils::getDefaultAppSupportPath()) {

    // Application Support may not exist in stripped-down test environments.
    if (!ensureParentDirectory(parentDir)) return "";

    // Open without following symlinks, then serialize read/repair under the
    // file lock. This avoids both symlink attacks and concurrent regeneration.
    int fd = open(tokenPath.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return "";
    if (flock(fd, LOCK_EX) != 0) { close(fd); return ""; }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || fchmod(fd, 0600) != 0) {
        flock(fd, LOCK_UN); close(fd); return "";
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        flock(fd, LOCK_UN); close(fd); return "";
    }
    std::string existing;
    char buffer[65];
    while (existing.size() < sizeof(buffer)) {
        ssize_t count = read(fd, buffer, sizeof(buffer) - existing.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        existing.append(buffer, static_cast<size_t>(count));
    }
    if (isValidToken(existing)) {
        flock(fd, LOCK_UN); close(fd); return existing;
    }
    const std::string token = generateToken();
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
        flock(fd, LOCK_UN); close(fd); return "";
    }
    size_t totalWritten = 0;
    while (totalWritten < token.size()) {
        ssize_t written = write(fd, token.data() + totalWritten,
                                token.size() - totalWritten);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        totalWritten += static_cast<size_t>(written);
    }
    const bool persisted = totalWritten == token.size() && fsync(fd) == 0;
    flock(fd, LOCK_UN);
    close(fd);
    if (!persisted) {
        unlink(tokenPath.c_str());
        return "";
    }
    return token;
}

/// Read the current token from the standard location.
/// Returns empty string if the file cannot be read or is malformed.
inline std::string readToken(
    const std::string& tokenPath = PathUtils::getHttpTokenPath()) {
    int fd = open(tokenPath.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return "";
    if (flock(fd, LOCK_SH) != 0) { close(fd); return ""; }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || (st.st_mode & 0077) != 0) {
        flock(fd, LOCK_UN); close(fd); return "";
    }
    std::string token;
    char buffer[65];
    while (token.size() < sizeof(buffer)) {
        ssize_t count = read(fd, buffer, sizeof(buffer) - token.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        token.append(buffer, static_cast<size_t>(count));
    }
    flock(fd, LOCK_UN);
    close(fd);
    if (isValidToken(token))
        return token;
    return "";
}

/// Return the token file path for display to the user (e.g. for curl usage).
inline std::string tokenFilePath() {
    return PathUtils::getHttpTokenPath();
}

} // namespace HttpToken
