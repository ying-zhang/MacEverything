#pragma once
// ─────────── Helpers ───────────
#include <sys/resource.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

/// Run a shell command in a child process. FSEvents tests use this to create
/// files from a separate process, since FileSystemWatcher uses
/// kFSEventStreamCreateFlagIgnoreSelf (events from the same PID are dropped).
static void runShellCommand(const std::string& cmd) {
    pid_t pid;
    char* argv[] = {(char*)"/bin/sh", (char*)"-c", (char*)cmd.c_str(), nullptr};
    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ) == 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

static size_t getMemoryUsageMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size / (1024 * 1024);
    }
    return 0;
}

struct CpuTime {
    double userMs;
    double sysMs;
    double totalMs() const { return userMs + sysMs; }
};

static CpuTime getCpuTime() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double userMs = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0;
    double sysMs  = ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
    return {userMs, sysMs};
}

struct ResourceSnapshot {
    size_t memMB;
    CpuTime cpu;
    static ResourceSnapshot take() { return {getMemoryUsageMB(), getCpuTime()}; }
};

// Helper: derive paged persistence paths from a base path
static std::string pagesPathFor(const std::string& basePath) {
    auto dir = basePath.substr(0, basePath.rfind('/'));
    return dir + "/index.pages";
}
static std::string ptablePathFor(const std::string& basePath) {
    auto dir = basePath.substr(0, basePath.rfind('/'));
    return dir + "/index.ptable";
}

static int passed = 0, failed = 0;
static bool gSkipPerformanceTests = false;

#define CHECK(expr) check((expr), #expr)

static void check(bool cond, const char* msg) {
    if (cond) {
        std::cout << "    [PASS] " << msg << "\n";
        passed++;
    } else {
        std::cout << "    [FAIL] " << msg << "\n";
        failed++;
    }
}
