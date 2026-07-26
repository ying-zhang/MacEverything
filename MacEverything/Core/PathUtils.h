#pragma once
#include <string>
#include <cstdlib>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>

/// Pure C++ path/OS utilities — no ObjC/Foundation dependency.
namespace PathUtils {

inline std::string getHomeDirectory() {
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') return home;

    struct passwd pwd{};
    struct passwd* result = nullptr;
    long bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufferSize < 1024) bufferSize = 16 * 1024;
    std::string buffer(static_cast<size_t>(bufferSize), '\0');
    if (getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result) == 0 &&
        result && result->pw_dir && result->pw_dir[0] == '/') {
        return result->pw_dir;
    }
    return {};
}

/// Get "~/Library/Caches/com.maceverything.app" expanded.
inline std::string getDefaultCachePath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Caches/com.maceverything.app";
}

/// Get "~/Library/Logs/MacEverything" expanded.
inline std::string getDefaultLogPath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Logs/MacEverything";
}

/// Get "~/Library/Application Support/com.maceverything.app" expanded.
inline std::string getDefaultAppSupportPath() {
    const std::string home = getHomeDirectory();
    return home.empty() ? std::string{} : home + "/Library/Application Support/com.maceverything.app";
}

/// Get "~/Library/Application Support/com.maceverything.app/.http_token" expanded.
/// This file stores the random local HTTP API token with 0600 permissions.
/// GUI normal search never uses HTTP; only mace, MCP, and external curl need it.
inline std::string getHttpTokenPath() {
    const std::string support = getDefaultAppSupportPath();
    return support.empty() ? std::string{} : support + "/.http_token";
}
/// Uses ProductVersion from kern.osproductversion (macOS 10.13.4+).
inline std::string getOSVersionString() {
    // Try kern.osproductversion first (available macOS 10.13.4+)
    char buf[64] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0) == 0) {
        return std::string(buf);
    }
    // Fallback to uname
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.release);
    }
    return "unknown";
}

} // namespace PathUtils
