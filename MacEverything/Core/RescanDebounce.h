#pragma once
// ═══════════════════════════════════════════════════════
//  RescanDebounce — pure-function helpers for coalescing
//  MustScanSubDirs rescan requests.
//  Header-only so tests can include directly.
// ═══════════════════════════════════════════════════════

#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>

/// Returns true if `child` is the same path as `parent` or a descendant of it.
/// Correctly handles the root path "/" and path boundaries
/// (e.g. "/Users" does NOT subsume "/Users2").
inline bool isPathSubsumedBy(const std::string& child, const std::string& parent) {
    if (child == parent) return true;
    if (parent == "/") return true;  // root subsumes all absolute paths
    return child.size() > parent.size()
        && child.compare(0, parent.size(), parent) == 0
        && child[parent.size()] == '/';
}

/// Merge `newPaths` into `pending`, applying path subsumption:
///  - If a new path is already subsumed by a pending path, skip it.
///  - If a new path subsumes existing pending paths, remove them.
///  - Deduplicates automatically (std::set).
inline std::set<std::string> mergeRescanPaths(
    const std::set<std::string>& pending,
    const std::vector<std::string>& newPaths)
{
    std::set<std::string> result = pending;

    for (const auto& np : newPaths) {
        // Check if np is already subsumed by something in result
        bool subsumed = false;
        for (const auto& existing : result) {
            if (isPathSubsumedBy(np, existing)) {
                subsumed = true;
                break;
            }
        }
        if (subsumed) continue;

        // Remove any existing paths that np subsumes
        for (auto it = result.begin(); it != result.end(); ) {
            if (isPathSubsumedBy(*it, np)) {
                it = result.erase(it);
            } else {
                ++it;
            }
        }

        result.insert(np);
    }

    return result;
}

/// Collapse a batch to the shallowest paths that cover all requested rescans.
/// This keeps bursty FSEvents batches from carrying thousands of redundant
/// descendant directories into the debounce queue.
inline std::vector<std::string> minimizeRescanPaths(std::vector<std::string> paths) {
    paths.erase(std::remove(paths.begin(), paths.end(), ""), paths.end());
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        const auto depthA = std::count(a.begin(), a.end(), '/');
        const auto depthB = std::count(b.begin(), b.end(), '/');
        if (depthA != depthB) return depthA < depthB;
        return a < b;
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    std::vector<std::string> result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        bool covered = false;
        for (const auto& parent : result) {
            if (isPathSubsumedBy(path, parent)) {
                covered = true;
                break;
            }
        }
        if (!covered) result.push_back(path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

/// Returns true if the given path was rescanned too recently
/// (within `throttleIntervalSec` seconds).
inline bool shouldThrottleRescan(
    const std::string& path,
    const std::unordered_map<std::string,
        std::chrono::steady_clock::time_point>& lastRescanTime,
    double throttleIntervalSec)
{
    auto it = lastRescanTime.find(path);
    if (it == lastRescanTime.end()) return false;

    auto elapsed = std::chrono::steady_clock::now() - it->second;
    return std::chrono::duration<double>(elapsed).count() < throttleIntervalSec;
}
