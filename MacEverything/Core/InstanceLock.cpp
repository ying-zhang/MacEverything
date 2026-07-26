#include "InstanceLock.h"
#include "Logger.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
#include <cstring>

InstanceLock::~InstanceLock() {
    unlock();
}

bool InstanceLock::tryLock(const std::string& lockFilePath,
                           const std::string& appVersion,
                           InstanceLockError* lastError) {
    if (fd_ >= 0) return true; // already locked

    fd_ = open(lockFilePath.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        LOG_ERROR("InstanceLock", "Failed to open lock file: " << lockFilePath
                  << " (" << std::strerror(errno) << ")");
        if (lastError) *lastError = InstanceLockError::OpenFailed;
        return false;
    }

    struct stat lockStat{};
    if (fstat(fd_, &lockStat) != 0 || !S_ISREG(lockStat.st_mode) ||
        lockStat.st_uid != getuid() || lockStat.st_nlink != 1 ||
        fchmod(fd_, 0600) != 0) {
        LOG_ERROR("InstanceLock", "Unsafe lock file: " << lockFilePath);
        if (lastError) *lastError = InstanceLockError::OpenFailed;
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            LOG_WARN("InstanceLock", "Another instance holds the lock: " << lockFilePath);
            if (lastError) *lastError = InstanceLockError::AlreadyLocked;
        } else {
            LOG_ERROR("InstanceLock", "flock() failed on " << lockFilePath
                      << " (" << std::strerror(errno) << ")");
            if (lastError) *lastError = InstanceLockError::OpenFailed;
        }
        close(fd_);
        fd_ = -1;
        return false;
    }

    // ── Write diagnostic metadata into the lock file ──
    // Truncate and write PID, version, UTC startup timestamp.
    if (ftruncate(fd_, 0) == 0) {
        char buf[512];
        std::time_t now = std::time(nullptr);
        struct tm gmt;
        gmtime_r(&now, &gmt);
        char timeStr[32];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &gmt);

        int len = snprintf(buf, sizeof(buf),
                           "pid=%d\nversion=%s\nstarted=%s\n",
                           static_cast<int>(getpid()),
                           appVersion.empty() ? "unknown" : appVersion.c_str(),
                           timeStr);
        if (len > 0) {
            ssize_t written = write(fd_, buf, static_cast<size_t>(len));
            (void)written; // best-effort; lock is already held
        }
    }

    path_ = lockFilePath;
    LOG_INFO("InstanceLock", "Instance lock acquired: " << lockFilePath
             << " (pid=" << getpid() << ")");
    return true;
}

void InstanceLock::unlock() {
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
        fd_ = -1;
        LOG_INFO("InstanceLock", "Instance lock released: " << path_);
    }
}
