#pragma once
#include <string>

/// Error reason returned by tryLock on failure.
enum class InstanceLockError {
    None = 0,
    /// The lock file path cannot be opened (permission, non-existent parent, I/O).
    OpenFailed,
    /// Another process already holds the lock (flock returned EWOULDBLOCK).
    AlreadyLocked,
};

/// RAII file lock for single-instance enforcement.
/// Uses POSIX flock() for advisory locking to prevent multiple
/// app instances from corrupting shared WAL and index files.
///
/// On successful acquisition, writes the holder's PID, application
/// version, and UTC startup time into the lock file for diagnostics.
/// Mutual exclusion is still enforced by flock — the file contents
/// are purely informational.
class InstanceLock {
public:
    InstanceLock() = default;
    ~InstanceLock();

    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    /// Try to acquire the lock. Returns true if this is the only instance.
    /// @param lockFilePath  Full path to the lock file (will be created if absent).
    /// @param appVersion    Human-readable version string written into the file.
    /// @param lastError     On failure, set to the reason (OpenFailed or AlreadyLocked).
    bool tryLock(const std::string& lockFilePath,
                 const std::string& appVersion = "",
                 InstanceLockError* lastError = nullptr);

    /// Release the lock.
    void unlock();

    /// Whether the lock is currently held.
    bool isLocked() const { return fd_ >= 0; }

    /// Path of the current lock file (empty if not locked).
    const std::string& path() const { return path_; }

private:
    int fd_ = -1;
    std::string path_;
};
