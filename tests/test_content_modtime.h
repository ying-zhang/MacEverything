#pragma once
// Tests for ContentIndex modTime-based incremental indexing (P3 optimization)

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <sys/stat.h>

static void runContentModTimeTests() {
    std::cout << "\n═══ Part 38: Content Index modTime Incremental Indexing ═══\n\n";

    namespace fs = std::filesystem;
    std::string tmpDir = "/tmp/test_content_modtime_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create a test file with known content
    std::string testFile = tmpDir + "/hello.txt";
    {
        std::ofstream ofs(testFile);
        ofs << "hello world test content for trigram indexing";
    }

    // Get the file's actual modTime
    struct stat st;
    stat(testFile.c_str(), &st);
    time_t fileModTime = st.st_mtime;

    // --- Test 1: Same modTime skips reindex ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // First index: should succeed (file is new)
        bool first = idx.indexFile(0, testFile, fileModTime);
        check(first, "First indexFile returns true (new file indexed)");
        check(idx.indexedFileCount() == 1, "File count is 1 after first index");

        // Second index with same modTime: should skip (return false)
        bool second = idx.indexFile(0, testFile, fileModTime);
        check(!second, "Same modTime → indexFile returns false (skipped)");
        check(idx.indexedFileCount() == 1, "File count still 1 after skip");
    }

    // --- Test 2: Changed modTime triggers reindex ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Index with original modTime
        bool first = idx.indexFile(0, testFile, fileModTime);
        check(first, "First indexFile returns true");

        // Index with different modTime (simulate file modification)
        time_t newModTime = fileModTime + 60; // 1 minute later

        // Modify the file content so hash differs
        {
            std::ofstream ofs(testFile);
            ofs << "modified content with different trigrams for testing";
        }

        bool second = idx.indexFile(0, testFile, newModTime);
        check(second, "Different modTime → indexFile returns true (reindexed)");
        check(idx.indexedFileCount() == 1, "File count still 1 (updated in place)");
    }

    // --- Test 3: modTime=0 does not skip (backward compat with v1 data) ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Write known content back
        {
            std::ofstream ofs(testFile);
            ofs << "hello world test content for trigram indexing";
        }

        // First index with modTime=0 (simulates v1 persisted data)
        bool first = idx.indexFile(0, testFile, 0);
        check(first, "modTime=0: first indexFile returns true");

        // Second index with modTime=0: should NOT skip (modTime=0 bypasses early exit)
        // Content hash is the same, so it returns false due to hash match, not modTime
        bool second = idx.indexFile(0, testFile, 0);
        check(!second, "modTime=0: second indexFile returns false (hash unchanged, but modTime check bypassed)");

        // Modify file and index again with modTime=0: should return true (hash changed)
        {
            std::ofstream ofs(testFile);
            ofs << "completely different content to change the hash value";
        }
        bool third = idx.indexFile(0, testFile, 0);
        check(third, "modTime=0: returns true when content actually changed");
    }

    // --- Test 4: insertFileInfo preserves lastModTime ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Directly insert file info with a specific modTime (simulates WAL replay)
        std::vector<Trigram> trigrams = {ContentIndex::makeTrigram('a', 'b', 'c')};
        time_t insertedModTime = 1700000000;
        idx.insertFileInfo(42, 12345, std::move(trigrams), insertedModTime);

        ContentFileInfo info;
        bool got = idx.getFileInfo(42, info);
        check(got, "getFileInfo returns true after insertFileInfo");
        check(info.lastModTime == insertedModTime, "insertFileInfo preserves lastModTime");
    }

    // --- Test 5: Hash-match updates lastModTime when stored value is 0 ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Write known content
        {
            std::ofstream ofs(testFile);
            ofs << "stable content for modtime upgrade test";
        }

        // First index with modTime=0 (simulates v1 persisted data)
        bool first = idx.indexFile(0, testFile, 0);
        check(first, "T5: first index with modTime=0 returns true");

        ContentFileInfo info;
        idx.getFileInfo(0, info);
        check(info.lastModTime == 0, "T5: stored lastModTime is 0 after modTime=0 index");

        // Second index with real modTime: hash matches but lastModTime differs
        // Should return true (to signal WAL update) and update stored lastModTime
        bool second = idx.indexFile(0, testFile, fileModTime);
        check(second, "T5: hash-match with new modTime returns true (triggers WAL persist)");

        idx.getFileInfo(0, info);
        check(info.lastModTime == fileModTime, "T5: stored lastModTime updated to real modTime");

        // Third index with same modTime: now should skip entirely (no I/O)
        bool third = idx.indexFile(0, testFile, fileModTime);
        check(!third, "T5: third call with same modTime skips (modTime early exit)");
    }

    // --- Test 6: Unreadable file removes the stale indexed entry ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Write and index a file normally
        std::string delFile = tmpDir + "/willdelete.txt";
        {
            std::ofstream ofs(delFile);
            ofs << "content that will be deleted soon";
        }

        struct stat dst;
        stat(delFile.c_str(), &dst);
        time_t delModTime = dst.st_mtime;

        bool first = idx.indexFile(0, delFile, delModTime);
        check(first, "T6: first indexFile returns true");
        check(idx.indexedFileCount() == 1, "T6: file count is 1");

        // Delete the file, then re-index with a new modTime
        fs::remove(delFile);
        time_t newMod = delModTime + 120;

        auto second = idx.indexFile(0, delFile, newMod);
        check(second == ContentIndexUpdate::Removed, "T6: deleted file removes stale content entry");

        ContentFileInfo info;
        check(!idx.getFileInfo(0, info), "T6: deleted file no longer has content metadata");

        // Third call stays unchanged because the stale entry is already gone.
        bool third = idx.indexFile(0, delFile, newMod);
        check(!third, "T6: third call remains unchanged");
    }

    // --- Test 7: pruneStaleEntries removes entries not in validFileIndices ---
    {
        ContentIndex idx;
        idx.setExtensions({"txt"});

        // Write test file back for indexing
        {
            std::ofstream ofs(testFile);
            ofs << "hello world test content for trigram indexing";
        }

        // Index file at indices 0, 5, 10
        idx.indexFile(0, testFile, fileModTime);
        // Insert synthetic entries for indices 5 and 10 (simulates WAL replay)
        std::vector<Trigram> tris = {ContentIndex::makeTrigram('x','y','z')};
        idx.insertFileInfo(5, 999, std::vector<Trigram>(tris), 100);
        idx.insertFileInfo(10, 888, std::vector<Trigram>(tris), 200);
        check(idx.indexedFileCount() == 3, "T7: 3 entries before prune");

        // Only index 0 and 10 are "valid regular files"
        std::unordered_set<uint32_t> valid = {0, 10};
        uint32_t pruned = idx.pruneStaleEntries(valid);
        check(pruned == 1, "T7: pruned 1 stale entry (index 5)");
        check(idx.indexedFileCount() == 2, "T7: 2 entries after prune");
        check(idx.isFileIndexed(0), "T7: index 0 still present");
        check(!idx.isFileIndexed(5), "T7: index 5 removed");
        check(idx.isFileIndexed(10), "T7: index 10 still present");
    }

    // --- Test 8: pruneStaleEntries with empty valid set removes all ---
    {
        ContentIndex idx;
        std::vector<Trigram> tris = {ContentIndex::makeTrigram('a','b','c')};
        idx.insertFileInfo(1, 111, std::vector<Trigram>(tris), 100);
        idx.insertFileInfo(2, 222, std::vector<Trigram>(tris), 200);
        check(idx.indexedFileCount() == 2, "T8: 2 entries before prune");

        std::unordered_set<uint32_t> empty;
        uint32_t pruned = idx.pruneStaleEntries(empty);
        check(pruned == 2, "T8: pruned all entries when valid set is empty");
        check(idx.indexedFileCount() == 0, "T8: 0 entries after prune");
    }

    // --- Test 9: getIndexedFileIndices returns correct key set ---
    {
        ContentIndex idx;
        std::vector<Trigram> tris = {ContentIndex::makeTrigram('a','b','c')};
        idx.insertFileInfo(3, 111, std::vector<Trigram>(tris), 100);
        idx.insertFileInfo(7, 222, std::vector<Trigram>(tris), 200);
        idx.insertFileInfo(15, 333, std::vector<Trigram>(tris), 300);

        auto indices = idx.getIndexedFileIndices();
        std::sort(indices.begin(), indices.end());
        check(indices.size() == 3, "T9: getIndexedFileIndices returns 3 entries");
        check(indices[0] == 3 && indices[1] == 7 && indices[2] == 15,
              "T9: getIndexedFileIndices returns correct indices {3,7,15}");
    }

    // --- Test 10: getIndexedFileIndices on empty index returns empty ---
    {
        ContentIndex idx;
        auto indices = idx.getIndexedFileIndices();
        check(indices.empty(), "T10: empty ContentIndex returns empty vector");
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "\n  Part 38 done.\n";
}
