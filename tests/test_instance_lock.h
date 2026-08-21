#pragma once
#include <unistd.h>
#include <iostream>
#include <fstream>

static void runInstanceLockTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 26 — Instance Lock Tests\n";
    std::cout << "========================================\n\n";

    std::string lockPath = "/tmp/test_instance_lock_" + std::to_string(getpid()) + ".lock";

    // Test 1: Basic lock/unlock
    {
        InstanceLock lock;
        check(lock.tryLock(lockPath, "1.0.0"), "First lock succeeds");
        check(lock.isLocked(), "Lock reports locked");
        lock.unlock();
        check(!lock.isLocked(), "Lock reports unlocked after unlock");
    }

    // Test 2: Second instance fails while first holds lock
    {
        InstanceLock lock1;
        check(lock1.tryLock(lockPath, "1.0.0"), "First lock succeeds");

        InstanceLock lock2;
        InstanceLockError err = InstanceLockError::None;
        check(!lock2.tryLock(lockPath, "1.0.0", &err), "Second lock fails (another instance)");
        check(err == InstanceLockError::AlreadyLocked, "Error reason is AlreadyLocked");

        lock1.unlock();
        check(lock2.tryLock(lockPath, "1.0.0"), "Second lock succeeds after first releases");
    }

    // Test 3: RAII cleanup — lock released when object goes out of scope
    {
        {
            InstanceLock lock;
            check(lock.tryLock(lockPath, "1.0.0"), "RAII lock succeeds");
        } // lock goes out of scope, should release

        InstanceLock lock2;
        check(lock2.tryLock(lockPath, "1.0.0"), "Lock available after RAII cleanup");
    }

    // Test 4: Double tryLock on same object returns true (idempotent)
    {
        InstanceLock lock;
        check(lock.tryLock(lockPath, "1.0.0"), "First tryLock succeeds");
        check(lock.tryLock(lockPath, "1.0.0"), "Second tryLock on same object returns true (already locked)");
        lock.unlock();
    }

    // Test 5: Unlock on never-locked object is a no-op
    {
        InstanceLock lock;
        check(!lock.isLocked(), "New lock is not locked");
        lock.unlock(); // should not crash
        check(!lock.isLocked(), "Still not locked after unlock on new lock");
    }

    // Test 6: Lock file contains diagnostic metadata
    {
        InstanceLock lock;
        check(lock.tryLock(lockPath, "2.0.0-test"), "Lock succeeds with version");
        // Read the lock file contents
        std::ifstream lf(lockPath);
        std::string content((std::istreambuf_iterator<char>(lf)),
                            std::istreambuf_iterator<char>());
        check(content.find("pid=") != std::string::npos, "Lock file contains PID");
        check(content.find("version=2.0.0-test") != std::string::npos, "Lock file contains version");
        check(content.find("started=") != std::string::npos, "Lock file contains startup timestamp");
        lock.unlock();
    }

    // Test 7: OpenFailed on invalid path
    {
        InstanceLock lock;
        InstanceLockError err = InstanceLockError::None;
        check(!lock.tryLock("/nonexistent_dir_xyzzy/sub/lock", "1.0", &err),
              "Lock fails on non-existent directory");
        check(err == InstanceLockError::OpenFailed,
              "Error reason is OpenFailed for invalid path");
    }

    // Test 8: Refuse a hard-linked lock file before truncating metadata.
    {
        unlink(lockPath.c_str());
        const std::string victimPath = lockPath + ".victim";
        {
            std::ofstream victim(victimPath, std::ios::trunc);
            victim << "preserve-me";
        }
        check(link(victimPath.c_str(), lockPath.c_str()) == 0,
              "Hard-linked lock test fixture is created");
        InstanceLock lock;
        InstanceLockError err = InstanceLockError::None;
        check(!lock.tryLock(lockPath, "1.0", &err),
              "Hard-linked lock file is rejected");
        std::ifstream victim(victimPath);
        std::string content((std::istreambuf_iterator<char>(victim)),
                            std::istreambuf_iterator<char>());
        check(content == "preserve-me", "Rejected lock file does not truncate hard-link target");
        unlink(lockPath.c_str());
        unlink(victimPath.c_str());
    }

    // Clean up
    unlink(lockPath.c_str());

    std::cout << "\n";
}
