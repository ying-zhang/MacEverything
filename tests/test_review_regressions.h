#pragma once
#include "PathUtils.h"
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <iterator>
#include <limits>
#include <sys/mount.h>

static bool waitForMainFlag(const std::atomic<bool>& flag, double seconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!flag.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    }
    return true;
}

static bool engineHasFileName(ServiceEngine& service, const std::string& query,
                              const std::string& name) {
    auto engine = service.safeEngine();
    if (!engine) return false;
    for (uint32_t idx : engine->query(query, 20)) {
        if (engine->getRecord(idx).name == name) return true;
    }
    return false;
}

static void writeVolumeKindFile(const fs::path& file, const std::string& kind,
                                const std::string& uuid, const std::string& indexedUuid,
                                const std::string& volumePath) {
    std::ofstream out(file);
    out << "v2\n" << kind
        << ' ' << uuid.size() << ' ' << uuid
        << ' ' << indexedUuid.size() << ' ' << indexedUuid
        << ' ' << volumePath.size() << ' ' << volumePath << '\n';
}

static void runReviewRegressionTests() {
    std::cout << "\n=== Part 78: Review Regression Tests ===\n";

    // A disappeared scan root must never look like a successful empty scan.
    {
        DirectoryScanner scanner;
        const auto missing = fs::temp_directory_path() /
            ("maceverything_missing_root_" + std::to_string(getpid()));
        fs::remove_all(missing);
        scanner.scan(missing.string());
        check(!scanner.isComplete(), "Review: inaccessible scan root is incomplete");
    }

    // An unplugged external volume is deferred only after it was classified
    // while mounted. An unknown or network /Volumes path is a hard failure.
    // A missing folder on a mounted volume remains a hard failure too.
    PathUtils::setVolumeKindStorePath("");
    const std::string missingVolume = "/Volumes/maceverything_unmounted_" +
        std::to_string(getpid());
    const std::string missingNetwork = "/Volumes/maceverything_nas_" +
        std::to_string(getpid());
    {
        DirectoryScanner scanner;
        scanner.scan(missingVolume);
        check(!scanner.isComplete(), "Review: unknown /Volumes root is not deferred");
        check(scanner.deferredVolumeCount() == 0, "Review: unknown /Volumes root is not counted as deferred");
    }
    {
        PathUtils::rememberVolumeKind(missingNetwork, PathUtils::VolumeKind::Network, "nas-uuid");
        DirectoryScanner scanner;
        scanner.scan(missingNetwork);
        check(!scanner.isComplete(), "Review: offline network volume is not deferred");
        check(scanner.deferredVolumeCount() == 0, "Review: offline network volume is not counted as deferred");
        check(!PathUtils::isUnpluggedRemovableVolume(missingNetwork),
              "Review: offline network volume is not an unplugged external disk");
    }
    {
        PathUtils::rememberVolumeKind(missingVolume, PathUtils::VolumeKind::External, "usb-uuid");
        PathUtils::noteIndexedVolumeIdentity(missingVolume, "usb-uuid");
        DirectoryScanner scanner;
        scanner.scan(missingVolume);
        check(scanner.isComplete(), "Review: unplugged external volume root is deferred");
        check(scanner.deferredVolumeCount() == 1, "Review: unplugged external volume is counted");
        check(scanner.reachableRootCount() == 0, "Review: unplugged external volume is not reachable");
        check(scanner.takeResults().empty(), "Review: deferred volume adds no records");
    }
    if (PathUtils::isReachableDirectory("/Volumes/Macintosh HD")) {
        check(!PathUtils::isUnpluggedRemovableVolume("/Volumes/Macintosh HD"),
              "Review: boot-volume symlink is not an unplugged disk");
    }
    {
        const std::string reused = "/Volumes/maceverything_reused_" + std::to_string(getpid());
        PathUtils::rememberVolumeKind(reused, PathUtils::VolumeKind::External, "usb-uuid");
        PathUtils::noteIndexedVolumeIdentity(reused, "nas-uuid");
        check(!PathUtils::isUnpluggedRemovableVolume(reused),
              "Review: external name reused by another volume is not pruned");
        const std::string unverified = "/Volumes/maceverything_unverified_" + std::to_string(getpid());
        PathUtils::rememberVolumeKind(unverified, PathUtils::VolumeKind::External, "usb-uuid");
        check(!PathUtils::isUnpluggedRemovableVolume(unverified),
              "Review: external volume without indexed identity is not pruned");
        check(PathUtils::volumeKindFromTraits(true, false, true, true) == PathUtils::VolumeKind::Network,
              "Review: non-local volume trait is network");
        check(PathUtils::volumeKindFromTraits(true, true, true, false) == PathUtils::VolumeKind::External,
              "Review: local non-internal volume trait is external");
        check(PathUtils::volumeKindFromTraits(true, true, true, true) == PathUtils::VolumeKind::Fixed,
              "Review: internal volume trait is fixed");
        check(PathUtils::volumeKindFromTraits(false, false, false, false) == PathUtils::VolumeKind::Unknown,
              "Review: missing volume traits stay unknown");
        PathUtils::setMountedVolumeInspectorForTests(+[](const std::string& path) {
            PathUtils::MountedVolumeTraits traits;
            if (path.find("Share") != std::string::npos) {
                traits.kind = PathUtils::VolumeKind::Network;
                traits.uuid = "nas-uuid";
            } else {
                traits.kind = PathUtils::VolumeKind::External;
                traits.uuid = "usb-uuid";
            }
            return traits;
        });
        PathUtils::observeVolumeKind("/Volumes/maceverything_Share");
        check(PathUtils::rememberedVolumeKind("/Volumes/maceverything_Share") == PathUtils::VolumeKind::Network,
              "Review: injected network traits are stored as network");
        PathUtils::observeVolumeKind("/Volumes/maceverything_Disk");
        check(PathUtils::rememberedVolumeClassification("/Volumes/maceverything_Disk").uuid == "usb-uuid",
              "Review: injected external traits store the volume UUID");
        PathUtils::setMountedVolumeInspectorForTests(nullptr);
    }
    {
        const auto present = fs::temp_directory_path() /
            ("maceverything_present_root_" + std::to_string(getpid()));
        fs::remove_all(present);
        fs::create_directories(present);
        std::ofstream(present / "keep.txt") << "keep";
        DirectoryScanner scanner;
        scanner.scan(std::vector<std::string>{
            present.string(),
            missingVolume
        });
        check(scanner.isComplete(), "Review: present roots complete while a volume is absent");
        bool found = false;
        for (const auto& record : scanner.takeResults()) {
            if (record.name == "keep.txt") found = true;
        }
        check(found, "Review: present root is indexed while a volume is absent");
        fs::remove_all(present);
    }
    {
        struct statfs* mounts = nullptr;
        const int mountCount = getmntinfo_r_np(&mounts, MNT_NOWAIT);
        std::string mounted;
        if (mountCount > 0 && mounts) {
            for (int i = 0; i < mountCount; ++i) {
                const char* mountPoint = mounts[i].f_mntonname;
                if (!mountPoint || mountPoint[0] == '\0') continue;
                const std::string point = mountPoint;
                if (point.rfind("/Volumes/", 0) == 0 && point.size() > std::string("/Volumes/").size()) {
                    mounted = point;
                    break;
                }
            }
        }
        std::free(mounts);
        if (!mounted.empty()) {
            DirectoryScanner scanner;
            scanner.scan(mounted + "/maceverything_missing_child_" + std::to_string(getpid()));
            check(!scanner.isComplete(),
                  "Review: missing folder on a mounted volume is still incomplete");
        }
    }

    // Startup must index the reachable roots when one configured root is an
    // unplugged external volume, and must not ask the user to remove it.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_unmounted_startup_" + std::to_string(getpid()));
        const auto present = cache / "present";
        fs::remove_all(cache);
        fs::create_directories(present);
        std::ofstream(present / "keep.txt") << "keep";

        ServiceConfig config;
        config.scanRoot = present.string();
        config.scanRoots = {
            present.string(),
            missingVolume
        };
        writeVolumeKindFile(cache / "volume-kinds.tsv", "external", "usb-uuid", "usb-uuid", missingVolume);
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        std::atomic<bool> startupFailed{false};
        std::atomic<bool> completionCalled{false};
        std::atomic<uint32_t> completionCount{0};
        std::atomic<bool> didScan{false};
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.onStartupFailed = [&](const std::string&) {
            startupFailed.store(true, std::memory_order_release);
        };
        service.startIncremental([&](uint32_t count, bool didFullScan) {
            completionCalled.store(true, std::memory_order_release);
            completionCount.store(count, std::memory_order_release);
            didScan.store(didFullScan, std::memory_order_release);
            dispatch_semaphore_signal(done);
        });
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);

        check(completed && completionCalled.load(std::memory_order_acquire) &&
              !startupFailed.load(std::memory_order_acquire) &&
              didScan.load(std::memory_order_acquire) &&
              completionCount.load(std::memory_order_acquire) > 0,
              "Review: unmounted volume does not block startup of other roots");
        service.shutdown();
        fs::remove_all(cache);
    }

    // A saved external classification must survive a reload, and an offline
    // network root must block rebuild before any index file is removed.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_volume_kinds_" + std::to_string(getpid()));
        fs::remove_all(cache);
        fs::create_directories(cache);
        const auto store = cache / "volume-kinds.tsv";
        const std::string persisted = "/Volumes/maceverything_persisted_" +
            std::to_string(getpid());
        {
            std::ofstream out(store);
            out << "v1\nexternal " << persisted.size() << ' ' << persisted << '\n';
        }
        const std::string poison = "/Volumes/maceverything_poison_" + std::to_string(getpid());
        PathUtils::setVolumeKindStorePath("");
        PathUtils::rememberVolumeKind(poison, PathUtils::VolumeKind::External, "poison-uuid");
        PathUtils::noteIndexedVolumeIdentity(poison, "poison-uuid");
        PathUtils::setVolumeKindStorePath(store.string());
        PathUtils::loadVolumeKinds();
        check(!PathUtils::isUnpluggedRemovableVolume(poison),
              "Review: switching the classification file drops the previous cache");
        check(!PathUtils::isUnpluggedRemovableVolume(persisted),
              "Review: v1 classification without a UUID is not pruned");
        writeVolumeKindFile(store, "external", "usb-uuid", "usb-uuid", persisted);
        PathUtils::loadVolumeKinds();
        check(PathUtils::isUnpluggedRemovableVolume(persisted),
              "Review: matching external UUID is unplugged when missing");
        PathUtils::setVolumeKindStorePath("");
        fs::remove_all(cache);
    }
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_nas_rebuild_" + std::to_string(getpid()));
        fs::remove_all(cache);
        fs::create_directories(cache);
        const auto sentinel = cache / "index.v6";
        {
            std::ofstream out(sentinel);
            out << "keep";
        }
        const std::string nas = "/Volumes/maceverything_rebuild_nas_" +
            std::to_string(getpid());
        writeVolumeKindFile(cache / "volume-kinds.tsv", "network", "nas-uuid", "nas-uuid", nas);
        const std::string poison = "/Volumes/maceverything_rebuild_poison_" + std::to_string(getpid());
        PathUtils::setVolumeKindStorePath("");
        PathUtils::rememberVolumeKind(poison, PathUtils::VolumeKind::External, "poison-uuid");
        PathUtils::noteIndexedVolumeIdentity(poison, "poison-uuid");

        ServiceConfig config;
        config.scanRoot = nas;
        config.scanRoots = {nas};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        std::atomic<bool> startupFailed{false};
        std::atomic<bool> completionCalled{false};
        service.onStartupFailed = [&](const std::string&) {
            startupFailed.store(true, std::memory_order_release);
        };
        service.rebuildIndex([&](uint32_t, bool) {
            completionCalled.store(true, std::memory_order_release);
        });
        check(startupFailed.load(std::memory_order_acquire) &&
              completionCalled.load(std::memory_order_acquire),
              "Review: offline network volume blocks index rebuild");
        std::ifstream saved(sentinel);
        std::string body;
        std::getline(saved, body);
        check(body == "keep", "Review: offline network volume keeps the old index file");
        check(PathUtils::isUnpluggedRemovableVolume(poison),
              "Review: a service keeps its volume classifications out of the process-global store");
        {
            std::ifstream kinds(cache / "volume-kinds.tsv");
            std::string kindsBody((std::istreambuf_iterator<char>(kinds)),
                                  std::istreambuf_iterator<char>());
            check(kindsBody.find("nas-uuid") != std::string::npos &&
                  kindsBody.find("poison-uuid") == std::string::npos,
                  "Review: constructing a service does not rewrite another cache's volume file");
        }
        service.shutdown();
        PathUtils::setVolumeKindStorePath("");
        fs::remove_all(cache);
    }

    // A corrupt length must be rejected. It must not wrap and throw during startup.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_volume_kinds_corrupt_" + std::to_string(getpid()));
        fs::remove_all(cache);
        fs::create_directories(cache);
        const std::string bogus = "/Volumes/maceverything_corrupt_" + std::to_string(getpid());
        {
            std::ofstream out(cache / "volume-kinds.tsv");
            out << "v2\nexternal " << std::numeric_limits<unsigned long>::max()
                << " x 1 a 0  " << bogus.size() << ' ' << bogus << '\n';
        }
        PathUtils::setVolumeKindStorePath((cache / "volume-kinds.tsv").string());
        PathUtils::loadVolumeKinds();
        check(!PathUtils::isUnpluggedRemovableVolume(bogus),
              "Review: an overflowing volume-kinds length is ignored");
        PathUtils::setVolumeKindStorePath("");
        fs::remove_all(cache);
    }

    // Mount lookup errors stay distinct from "not mounted", and each service
    // writes only its own classification file.
    {
        const std::string absent = "/Volumes/maceverything_mount_" + std::to_string(getpid());
        PathUtils::rememberVolumeKind(absent, PathUtils::VolumeKind::External, "usb-uuid");
        PathUtils::noteIndexedVolumeIdentity(absent, "usb-uuid");
        PathUtils::setMountTableOverrideForTests(true, true);
        check(!PathUtils::isUnpluggedRemovableVolume(absent),
              "Review: a mount-table failure does not prune an external volume");
        check(!PathUtils::isNetworkFilesystem(absent + "/child"),
              "Review: a mount-table failure does not invent a network filesystem");
        PathUtils::setMountTableOverrideForTests(true, false, {});
        check(PathUtils::isUnpluggedRemovableVolume(absent),
              "Review: an empty mount table leaves a missing external volume unplugged");
        PathUtils::setMountTableOverrideForTests(
            true, false, {PathUtils::MountTableEntry{absent, MNT_LOCAL}});
        check(!PathUtils::isUnpluggedRemovableVolume(absent),
              "Review: a mounted volume name is not treated as unplugged");
        PathUtils::setMountTableOverrideForTests(
            true, false, {PathUtils::MountTableEntry{"/Volumes/maceverything_share", 0}});
        check(PathUtils::isNetworkFilesystem("/Volumes/maceverything_share/pending"),
              "Review: a non-local mount table entry keeps the network scan policy");
        PathUtils::setMountTableOverrideForTests(false, false);
        PathUtils::setVolumeKindStorePath("/tmp/maceverything-volume-reset");
        PathUtils::setVolumeKindStorePath("");

        const auto cacheA = fs::temp_directory_path() /
            ("maceverything_volume_store_a_" + std::to_string(getpid()));
        const auto cacheB = fs::temp_directory_path() /
            ("maceverything_volume_store_b_" + std::to_string(getpid()));
        fs::remove_all(cacheA);
        fs::remove_all(cacheB);
        fs::create_directories(cacheA);
        fs::create_directories(cacheB);
        const std::string volA = "/Volumes/maceverything_store_a_" + std::to_string(getpid());
        const std::string volB = "/Volumes/maceverything_store_b_" + std::to_string(getpid());
        PathUtils::VolumeKindStore storeA;
        PathUtils::VolumeKindStore storeB;
        PathUtils::setVolumeKindStorePath(storeA, (cacheA / "volume-kinds.tsv").string());
        PathUtils::setVolumeKindStorePath(storeB, (cacheB / "volume-kinds.tsv").string());
        PathUtils::rememberVolumeKind(storeA, volA, PathUtils::VolumeKind::External, "uuid-a");
        PathUtils::noteIndexedVolumeIdentity(storeA, volA, "uuid-a");
        PathUtils::rememberVolumeKind(storeB, volB, PathUtils::VolumeKind::Network, "uuid-b");
        PathUtils::noteIndexedVolumeIdentity(storeB, volB, "uuid-b");
        check(PathUtils::isUnpluggedRemovableVolume(storeA, volA),
              "Review: one volume store can prune its own external disk");
        check(!PathUtils::isUnpluggedRemovableVolume(storeB, volA),
              "Review: another volume store does not see the first store's classification");
        std::ifstream fileA(cacheA / "volume-kinds.tsv");
        std::string bodyA((std::istreambuf_iterator<char>(fileA)), std::istreambuf_iterator<char>());
        std::ifstream fileB(cacheB / "volume-kinds.tsv");
        std::string bodyB((std::istreambuf_iterator<char>(fileB)), std::istreambuf_iterator<char>());
        check(bodyA.find("uuid-a") != std::string::npos && bodyA.find("uuid-b") == std::string::npos,
              "Review: the first volume store file does not receive the second store's UUID");
        check(bodyB.find("uuid-b") != std::string::npos && bodyB.find("uuid-a") == std::string::npos,
              "Review: the second volume store file does not receive the first store's UUID");
        fs::remove_all(cacheA);
        fs::remove_all(cacheB);

        check(PathUtils::scannedPathCoversConfiguredVolumeRoots(
                  "/Volumes/Disk", {"/Volumes/Disk/Photos", "/tmp/other"}),
              "Review: rescanning a volume root covers that volume's configured roots");
        check(!PathUtils::scannedPathCoversConfiguredVolumeRoots(
                  "/Volumes/Disk/Photos", {"/Volumes/Disk/Photos", "/Volumes/Disk/Docs"}),
              "Review: a partial subtree does not cover every root on the volume");
        check(PathUtils::scannedPathCoversConfiguredVolumeRoots(
                  "/Volumes/Disk/Photos", {"/Volumes/Disk/Photos", "/Users/username"}),
              "Review: the only configured root on a volume can record its UUID");
        check(!PathUtils::scannedPathCoversConfiguredVolumeRoots(
                  "/Users/username/Desktop", {"/Users/username"}),
              "Review: a path outside /Volumes does not update a volume identity");
    }

    // A removal queued during a scan must not delete a path that is still
    // present when the scan publishes. A later rename must update that engine.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_rename_lifecycle_" + std::to_string(getpid()));
        const auto root = cache / "files";
        fs::remove_all(cache);
        fs::create_directories(root);
        {
            std::ofstream out(root / "old-name.txt");
            out << "keep";
        }
        for (int i = 0; i < 4000; ++i) {
            std::ofstream out(root / ("pad-" + std::to_string(i) + ".txt"));
            out << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(done);
        });
        service.removeSubtree(root.string(), [](uint32_t) {});
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);

        bool kept = false;
        for (int attempt = 0; attempt < 40; ++attempt) {
            kept = false;
            auto engine = service.safeEngine();
            if (engine) {
                for (uint32_t idx : engine->query("old-name", 10)) {
                    if (engine->getRecord(idx).name == "old-name.txt") kept = true;
                }
            }
            dispatch_semaphore_t pause = dispatch_semaphore_create(0);
            dispatch_semaphore_wait(pause, dispatch_time(DISPATCH_TIME_NOW, 25 * NSEC_PER_MSEC));
            dispatch_release(pause);
        }
        check(completed && kept,
              "Review: deferred removal does not delete a path mounted again after the scan");

        fs::rename(root / "old-name.txt", root / "new-name.txt");
        service.refreshRenamedPath((root / "old-name.txt").string(),
                                   (root / "new-name.txt").string());
        bool renamed = false;
        for (int attempt = 0; attempt < 50 && !renamed; ++attempt) {
            auto engine = service.safeEngine();
            if (engine) {
                for (uint32_t idx : engine->query("new-name", 10)) {
                    if (engine->getRecord(idx).name == "new-name.txt") renamed = true;
                }
            }
            if (!renamed) {
                dispatch_semaphore_t pause = dispatch_semaphore_create(0);
                dispatch_semaphore_wait(pause, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC));
                dispatch_release(pause);
            }
        }
        check(renamed, "Review: rename refresh updates the published engine");
        service.shutdown();
        fs::remove_all(cache);
    }

    // A rename that arrives while the full scan flag is set must land on the
    // engine that scan publishes, not on the engine it replaces.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_rename_during_scan_" + std::to_string(getpid()));
        const auto root = cache / "files";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "old-name.txt") << "keep";
        for (int i = 0; i < 3000; ++i) {
            std::ofstream(root / ("pad-" + std::to_string(i) + ".txt")) << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(done);
        });
        check(service.isScanning(), "Review: full scan owns the index before it returns");
        fs::rename(root / "old-name.txt", root / "new-name.txt");
        std::ofstream(root / "arrived-during-scan.txt") << "new";
        service.refreshRenamedPath((root / "old-name.txt").string(),
                                   (root / "new-name.txt").string());
        std::atomic<bool> rescanDone{false};
        service.rescanSubtree(root.string(), [&] { rescanDone.store(true, std::memory_order_release); });
        std::atomic<bool> removeDone{false};
        std::atomic<uint32_t> removedCount{999};
        // The root is still mounted. A removal queued during the scan must not
        // erase it after the scan publishes.
        service.removeSubtree(root.string(), [&](uint32_t removed) {
            removedCount.store(removed, std::memory_order_release);
            removeDone.store(true, std::memory_order_release);
        });
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);
        const bool callbacks = waitForMainFlag(rescanDone, 20) && waitForMainFlag(removeDone, 5);

        bool renamed = false;
        bool arrived = false;
        for (int attempt = 0; attempt < 50 && !(renamed && arrived); ++attempt) {
            renamed = engineHasFileName(service, "new-name", "new-name.txt");
            arrived = engineHasFileName(service, "arrived-during-scan", "arrived-during-scan.txt");
            if (!(renamed && arrived)) {
                dispatch_semaphore_t pause = dispatch_semaphore_create(0);
                dispatch_semaphore_wait(pause, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC));
                dispatch_release(pause);
            }
        }
        check(completed && callbacks && renamed && arrived &&
              removedCount.load(std::memory_order_acquire) == 0,
              "Review: rename, rescan, and removal during a full scan land on the published engine");
        service.shutdown();
        fs::remove_all(cache);
    }

    // The first scan can fail after volume work was queued. Those completions
    // still have to run.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_failed_scan_deferred_" + std::to_string(getpid()));
        const auto root = cache / "files";
        const auto missing = cache / "missing-root";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "keep.txt") << "keep";
        for (int i = 0; i < 2000; ++i) {
            std::ofstream(root / ("pad-" + std::to_string(i) + ".txt")) << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string(), missing.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        std::atomic<bool> startupFailed{false};
        service.onStartupFailed = [&](const std::string&) {
            startupFailed.store(true, std::memory_order_release);
        };
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool ok) {
            if (!ok) dispatch_semaphore_signal(done);
        });
        check(service.isScanning(), "Review: failed scan still owns the index while it runs");
        std::atomic<bool> removeDone{false};
        std::atomic<bool> rescanDone{false};
        service.removeSubtree(root.string(), [&](uint32_t) {
            removeDone.store(true, std::memory_order_release);
        });
        service.rescanSubtree(root.string(), [&] {
            rescanDone.store(true, std::memory_order_release);
        });
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);
        const bool callbacks = waitForMainFlag(removeDone, 20) && waitForMainFlag(rescanDone, 20);
        bool kept = false;
        for (int attempt = 0; attempt < 40 && !kept; ++attempt) {
            kept = engineHasFileName(service, "keep", "keep.txt");
            if (!kept) {
                dispatch_semaphore_t pause = dispatch_semaphore_create(0);
                dispatch_semaphore_wait(pause, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC));
                dispatch_release(pause);
            }
        }
        check(completed && startupFailed.load(std::memory_order_acquire) &&
              callbacks && kept,
              "Review: a failed first scan still finishes deferred removal and rescan");
        service.shutdown();
        fs::remove_all(cache);
    }

    // Rename is queued before a removal event. Both must run in that order,
    // including when the drain is already on the mutation queue. The missing
    // root keeps this scan from publishing a snapshot, which would drop the
    // removal event instead of ordering it after the rename.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_deferred_order_" + std::to_string(getpid()));
        const auto root = cache / "files";
        const auto missing = cache / "missing-root";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "old-name.txt") << "keep";
        for (int i = 0; i < 2500; ++i) {
            std::ofstream(root / ("pad-" + std::to_string(i) + ".txt")) << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string(), missing.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(done);
        });
        check(service.isScanning(), "Review: deferred-order scan owns the index");
        fs::rename(root / "old-name.txt", root / "new-name.txt");
        service.refreshRenamedPath((root / "old-name.txt").string(),
                                   (root / "new-name.txt").string());
        service.queueWatcherEventsForTests({
            {(root / "new-name.txt").string(), kFSEventStreamEventFlagItemRemoved}
        });
        std::atomic<bool> drained{false};
        service.removeSubtree((root / "zz-barrier-not-a-file").string(), [&](uint32_t) {
            drained.store(true, std::memory_order_release);
        });
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);
        const bool barrier = waitForMainFlag(drained, 20);
        const bool oldGone = !engineHasFileName(service, "old-name", "old-name.txt");
        const bool newGone = !engineHasFileName(service, "new-name", "new-name.txt");
        check(completed && barrier && oldGone && newGone,
              "Review: deferred rename runs before a later removal event");
        service.shutdown();
        fs::remove_all(cache);
    }

    // A removal queued for the generation being scanned must not delete that
    // path from the index a rebuild publishes.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_stale_events_rebuild_" + std::to_string(getpid()));
        const auto root = cache / "files";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "keep.txt") << "keep";
        for (int i = 0; i < 2500; ++i) {
            std::ofstream(root / ("pad-" + std::to_string(i) + ".txt")) << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        std::atomic<bool> armed{false};
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.onScanProgress = [&](uint64_t, uint64_t) {
            bool expected = false;
            if (!armed.compare_exchange_strong(expected, true)) return;
            service.queueWatcherEventsForTests({
                {(root / "keep.txt").string(), kFSEventStreamEventFlagItemRemoved}
            });
            service.rebuildIndex([&](uint32_t, bool) {
                dispatch_semaphore_signal(done);
            });
        };
        service.startFullScan([&](uint32_t, bool) {});
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);
        bool kept = false;
        for (int attempt = 0; attempt < 40 && !kept; ++attempt) {
            kept = engineHasFileName(service, "keep", "keep.txt");
            if (!kept) {
                dispatch_semaphore_t pause = dispatch_semaphore_create(0);
                dispatch_semaphore_wait(pause, dispatch_time(DISPATCH_TIME_NOW, 25 * NSEC_PER_MSEC));
                dispatch_release(pause);
            }
        }
        check(completed && armed.load(std::memory_order_acquire) && kept &&
              fs::exists(root / "keep.txt"),
              "Review: events queued before rebuild are not applied to the new index");
        service.shutdown();
        fs::remove_all(cache);
    }

    // Events already removed from the shared deferred queue must not delete
    // files a later background full replacement just loaded. Rebuild bumps
    // the lifecycle generation; this path keeps that generation.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_stale_events_replace_" + std::to_string(getpid()));
        const auto root = cache / "files";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "keep.txt") << "keep";

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        dispatch_semaphore_t started = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(started);
        });
        const bool scanDone = dispatch_semaphore_wait(
            started, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        dispatch_release(started);
        const auto startupDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < startupDeadline &&
               (!service.startupCompletedForTests() ||
                service.deferredDrainCountForTests() == 0)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
        }
        const bool startupDone = service.startupCompletedForTests() &&
            service.deferredDrainCountForTests() > 0;
        check(scanDone && startupDone,
              "Review: replacement race starts from a published index");

        service.publishFreshScanOnNextDeferredDrainForTests();
        service.queueWatcherEventsForTests({
            {(root / "keep.txt").string(), kFSEventStreamEventFlagItemRemoved}
        });
        std::atomic<bool> staleDrained{false};
        service.removeSubtree((root / "zz-barrier-not-a-file").string(), [&](uint32_t) {
            staleDrained.store(true, std::memory_order_release);
        });
        const bool staleBarrier = waitForMainFlag(staleDrained, 20);
        const bool kept = engineHasFileName(service, "keep", "keep.txt");
        check(staleBarrier && kept && fs::exists(root / "keep.txt"),
              "Review: events taken off the deferred queue do not delete a background replacement");

        fs::remove(root / "keep.txt");
        service.queueWatcherEventsForTests({
            {(root / "keep.txt").string(), kFSEventStreamEventFlagItemRemoved}
        });
        std::atomic<bool> freshDrained{false};
        service.removeSubtree((root / "zz-barrier-not-a-file").string(), [&](uint32_t) {
            freshDrained.store(true, std::memory_order_release);
        });
        const bool freshBarrier = waitForMainFlag(freshDrained, 20);
        check(freshBarrier && !engineHasFileName(service, "keep", "keep.txt"),
              "Review: an event queued after replacement still updates the new index");
        service.shutdown();
        fs::remove_all(cache);
    }

    // A full scan publishes a new engine without retiring the lifecycle
    // generation. Events queued while that scan owns the index must not
    // delete files the scan just recorded, and a watcher started from "now"
    // must not suppress replay from the scan's event id.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_scan_publish_events_" + std::to_string(getpid()));
        const auto root = cache / "files";
        fs::remove_all(cache);
        fs::create_directories(root);
        std::ofstream(root / "keep.txt") << "keep";
        for (int i = 0; i < 2500; ++i) {
            std::ofstream(root / ("pad-" + std::to_string(i) + ".txt")) << "p";
        }

        ServiceConfig config;
        config.scanRoot = root.string();
        config.scanRoots = {root.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = true;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        auto waitUntilReady = [&](uint64_t previousDrains) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (std::chrono::steady_clock::now() < deadline) {
                if (service.startupCompletedForTests() &&
                    service.deferredDrainCountForTests() > previousDrains) {
                    return true;
                }
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
            }
            return service.startupCompletedForTests() &&
                service.deferredDrainCountForTests() > previousDrains;
        };

        dispatch_semaphore_t firstDone = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(firstDone);
        });
        const bool firstScan = dispatch_semaphore_wait(
            firstDone, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0;
        dispatch_release(firstDone);
        const bool firstReady = waitUntilReady(0);
        check(firstScan && firstReady && service.isMonitoring(),
              "Review: full scan starts live monitoring");

        std::atomic<bool> attempted{false};
        std::atomic<bool> monitoringDuringScan{true};
        service.onScanProgress = [&](uint64_t, uint64_t) {
            bool expected = false;
            if (!attempted.compare_exchange_strong(expected, true)) return;
            service.attemptVolumeMonitoringForTests();
            monitoringDuringScan.store(service.isMonitoring(), std::memory_order_release);
            service.queueWatcherEventsForTests({
                {(root / "keep.txt").string(), kFSEventStreamEventFlagItemRemoved}
            });
        };
        service.armVolumeMonitoringBeforeReplayForTests();
        const uint64_t drainsBeforeSecond = service.deferredDrainCountForTests();
        dispatch_semaphore_t secondDone = dispatch_semaphore_create(0);
        service.startFullScan([&](uint32_t, bool) {
            dispatch_semaphore_signal(secondDone);
        });
        const bool secondScan = dispatch_semaphore_wait(
            secondDone, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0;
        dispatch_release(secondDone);
        const bool secondReady = waitUntilReady(drainsBeforeSecond);
        std::atomic<bool> drained{false};
        service.removeSubtree((root / "zz-barrier-not-a-file").string(), [&](uint32_t) {
            drained.store(true, std::memory_order_release);
        });
        const bool barrier = waitForMainFlag(drained, 20);
        const bool kept = engineHasFileName(service, "keep", "keep.txt");
        const uint64_t anchor = service.monitoringAnchorForTests();
        check(secondScan && secondReady && attempted.load(std::memory_order_acquire) &&
              !monitoringDuringScan.load(std::memory_order_acquire),
              "Review: a volume notification does not start monitoring during a full scan");
        check(barrier && kept && fs::exists(root / "keep.txt"),
              "Review: events queued during a full scan do not delete the published scan");
        check(service.isMonitoring() && anchor != 0 &&
              anchor != kFSEventStreamEventIdSinceNow,
              "Review: full scan replay replaces a watcher that started from now");
        service.shutdown();
        fs::remove_all(cache);
    }

    // An incomplete first scan must report failure without constructing a
    // persistence layer around a missing engine or writing an empty index.
    {
        const auto cache = fs::temp_directory_path() /
            ("maceverything_incomplete_startup_" + std::to_string(getpid()));
        const auto missing = cache / "unmounted-root";
        fs::remove_all(cache);
        fs::create_directories(cache);

        ServiceConfig config;
        config.scanRoot = missing.string();
        config.scanRoots = {missing.string()};
        config.cachePath = cache.string();
        config.realtimeMonitoring = false;
        config.contentIndexingEnabled = false;
        config.automaticMaintenanceEnabled = false;

        ServiceEngine service(config);
        auto initialEngine = service.safeEngine();
        std::atomic<bool> startupFailed{false};
        std::atomic<bool> completionCalled{false};
        std::atomic<bool> reportedSuccess{false};
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        service.onStartupFailed = [&](const std::string&) {
            startupFailed.store(true, std::memory_order_release);
        };
        service.startIncremental([&](uint32_t count, bool didFullScan) {
            completionCalled.store(true, std::memory_order_release);
            reportedSuccess.store(count > 0 || didFullScan, std::memory_order_release);
            dispatch_semaphore_signal(done);
        });
        const bool completed = dispatch_semaphore_wait(
            done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);

        check(completed && completionCalled.load(std::memory_order_acquire),
              "Review: incomplete first scan reaches completion callback");
        check(startupFailed.load(std::memory_order_acquire) &&
              !reportedSuccess.load(std::memory_order_acquire) &&
              service.safeEngine() == initialEngine,
              "Review: incomplete first scan reports failure without replacing the engine");
        check(!fs::exists(cache / "index.v6"),
              "Review: incomplete first scan does not write an empty v6 index");
        service.shutdown();
        fs::remove_all(cache);
    }

    // Filename trigrams must remain usable when path acceleration is disabled.
    {
        SearchEngineOptions options;
        options.enablePathTrigramIndex = false;
        SearchEngine engine(options);
        std::vector<FileRecord> records;
        for (int i = 0; i < 500; ++i) {
            records.push_back({"generic_" + std::to_string(i) + ".dat", "/test", 1, 10, 100});
        }
        for (int i = 0; i < 8; ++i) {
            records.push_back({"xqztarget_" + std::to_string(i) + ".txt", "/test", 1, 10, 100});
        }
        engine.loadRecords(std::move(records));
        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("xqztarget", 100, true, timing);
        check(results.size() == 8, "Review: name query works without path trigram index");
        check(timing.usedTrigram, "Review: name trigram is used without path acceleration");
    }

    // A completed rewrite may acknowledge only requests that existed before it
    // started. A later WAL failure must remain pending for the next flush.
    {
        SearchEngine engine;
        engine.loadRecords({{"base.txt", "/tmp", 1, 10, 100}});
        engine.attachWAL(std::make_shared<IndexWAL>()); // unopened: append fails deterministically
        engine.addRecord({"first.txt", "/tmp", 1, 10, 101});
        uint64_t firstGeneration = engine.fullRewriteGeneration();
        engine.addRecord({"second.txt", "/tmp", 1, 10, 102});
        uint64_t secondGeneration = engine.fullRewriteGeneration();
        engine.acknowledgeFullRewrite(firstGeneration);
        check(secondGeneration > firstGeneration && engine.needsFullRewrite(),
              "Review: newer WAL failure survives an older rewrite acknowledgement");
        engine.acknowledgeFullRewrite(secondGeneration);
        check(!engine.needsFullRewrite(),
              "Review: latest rewrite generation can be acknowledged");
    }

    // Each AND term may be satisfied by either the filename or its directory path.
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 1000; ++i) {
            records.push_back({"generic_" + std::to_string(i) + ".dat", "/noise", 1, 10, 100});
        }
        records.push_back({"report_2024_archive.txt", "/archive", 1, 10, 100});
        records.push_back({"report.pdf", "/Users/test/2024", 1, 10, 100});
        engine.loadRecords(std::move(records));
        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("report 2024", 100, true, timing);
        bool foundCrossField = false;
        for (uint32_t idx : results) {
            auto record = engine.getRecord(idx);
            if (record.name == "report.pdf" && record.path == "/Users/test/2024") {
                foundCrossField = true;
            }
        }
        check(foundCrossField, "Review: multi-term AND unions filename and path candidates per term");
    }

    // An oversized posting list must invalidate the whole multi-term accelerator.
    // A pinyin posting for only the best term is not a complete AND candidate set.
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 200; ++i) {
            records.push_back({"log_noise_" + std::to_string(i) + ".txt", "/tmp", 1, 10, 100});
        }
        for (int i = 0; i < 800; ++i) {
            records.push_back({"other_" + std::to_string(i) + ".txt", "/tmp", 1, 10, 100});
        }
        records.push_back({"zywj-log-target.txt", "/tmp", 1, 10, 100});
        records.push_back({"重要文件.txt", "/tmp", 1, 10, 100});
        engine.loadRecords(std::move(records));

        QueryTimingInfo timing;
        auto results = engine.queryAdvanced("zywj log", 100, true, timing);
        bool foundLiteralMatch = false;
        for (uint32_t idx : results) {
            if (engine.getRecord(idx).name == "zywj-log-target.txt") {
                foundLiteralMatch = true;
            }
        }
        check(foundLiteralMatch,
              "Review: oversized multi-term candidates fall back without pinyin takeover");
    }

    // A wildcard gap belongs to the segment before it when matching right-to-left.
    {
        SearchEngine engine;
        engine.loadRecords({{"bin-tool", "/Users/x/usr/y/local", 1, 10, 100}});
        auto results = engine.query("/usr/*/local/bin");
        check(results.size() == 1, "Review: structured wildcard permits non-adjacent path components");
    }

    // Mutation lookups and loaded path dictionaries must share Unicode lowering.
    {
        const std::string upperDir = "/Users/\xC3\x89mile";
        const std::string lowerFile = "/Users/\xC3\xA9mile/report.pdf";
        SearchEngine engine;
        engine.loadRecords({{"report.pdf", upperDir, 1, 10, 100}});
        FileRecord updated{"report.pdf", upperDir, 1, 99, 200};
        engine.updateByPath(lowerFile, std::move(updated));
        check(engine.liveRecordCount() == 1, "Review: Unicode path update replaces existing record");
        check(engine.removeByPath(lowerFile), "Review: Unicode-lowercase path removes existing record");
        check(engine.liveRecordCount() == 0, "Review: Unicode path removal leaves no live duplicate");
    }

    // A compaction generation mismatch must prevent stale indices from being resolved.
    {
        SearchEngine engine;
        engine.loadRecords({{"old.txt", "/tmp", 1, 10, 100},
                            {"keep.txt", "/tmp", 1, 10, 100}});
        std::vector<uint32_t> stale{1};
        uint64_t generation = engine.compactionGeneration();
        engine.removeByPath("/tmp/old.txt");
        engine.compactRecords();
        bool visited = false;
        bool stable = engine.forEachRecordWithPathIfGeneration(
            stale, generation, [&](uint32_t, const FileRecord&, const std::string&) { visited = true; });
        check(!stable && !visited, "Review: stale result indices are rejected after compaction");
    }

    // Content readers can identify the entire cross-index remapping interval.
    {
        ContentIndex index;
        uint64_t before = index.mappingGeneration();
        index.beginFileIndexRemap();
        uint64_t during = index.mappingGeneration();
        index.endFileIndexRemap();
        uint64_t after = index.mappingGeneration();
        check((before & 1U) == 0 && (during & 1U) == 1 && after == before + 2,
              "Review: content mapping generation brackets remapping");

        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_remap_guard_" + std::to_string(getpid()));
        fs::create_directories(tmpDir);
        auto path = (tmpDir / "guard.txt").string();
        { std::ofstream out(path); out << "generation guard content"; }
        index.setExtensions({"txt"});
        index.beginFileIndexRemap();
        auto update = index.indexFile(7, path);
        index.endFileIndexRemap();
        check(update == ContentIndexUpdate::Unchanged && index.indexedFileCount() == 0,
              "Review: content mutation is rejected while file indices are remapping");
        fs::remove_all(tmpDir);
    }

    // File-index mutations hold a lease that prevents compaction/remapping from
    // changing the numeric identity between path lookup and content mutation.
    {
        ContentIndex index;
        auto lease = index.acquireFileIndexMappingLease();
        bool remapEntered = true;
        std::thread remapper([&] {
            remapEntered = index.tryBeginFileIndexRemap();
            if (remapEntered) index.endFileIndexRemap();
        });
        remapper.join();
        check(!remapEntered,
              "Review: mapping lease blocks cross-index remapping");
        lease.unlock();
        check(index.tryBeginFileIndexRemap(),
              "Review: remapping resumes after mapping lease release");
        index.endFileIndexRemap();
    }

    // V6 Phase 1 has no extension index yet, so ext: must use the linear fallback.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_v6_review_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto v6Path = (tmpDir / "index.v6").string();

        auto source = std::make_shared<SearchEngine>();
        std::vector<FileRecord> records;
        for (int i = 0; i < 50; ++i) {
            records.push_back({"report_" + std::to_string(i) + ".csv", "/new", 1, 10, 100});
        }
        source->loadRecords(std::move(records));
        FlatIndexWriter writer(v6Path);
        IndexMetadata metadata;
        metadata.lastEventId = 1;
        check(writer.fullRewrite(*source, metadata), "Review: wrote V6 fixture");

        auto loaded = std::make_shared<SearchEngine>();
        std::vector<FileRecord> oldRecords;
        for (int i = 0; i < 30; ++i) {
            oldRecords.push_back({"old_" + std::to_string(i) + ".dat", "/old", 1, 10, 100});
        }
        loaded->loadRecords(std::move(oldRecords));
        FlatIndexWriter reader(v6Path);
        IndexMetadata loadedMetadata;
        check(reader.load(*loaded, &loadedMetadata), "Review: loaded V6 fixture into reused engine");
        check(loaded->isPhase2Pending(), "Review: V6 secondary indexes are pending");

        QueryTimingInfo timing;
        auto pendingResults = loaded->queryAdvanced("ext:csv", 100, true, timing);
        check(pendingResults.size() == 50,
              "Review: ext filter falls back correctly while Phase 2 is pending");
        fs::remove_all(tmpDir);
    }

    // A false-positive trigram candidate before a real match must not consume maxResults.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_verified_content_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::vector<std::string> paths = {
            (tmpDir / "false.txt").string(),
            (tmpDir / "real.txt").string(),
        };
        { std::ofstream out(paths[0]); out << "abaXbab"; }
        { std::ofstream out(paths[1]); out << "contains abab here"; }

        ContentIndex index;
        index.setExtensions({"txt"});
        index.indexFile(0, paths[0]);
        index.indexFile(1, paths[1]);
        std::atomic<bool> receivedStablePath{false};
        auto resolver = [&](uint32_t idx, std::string& path) {
            if (idx >= paths.size()) return false;
            if (path == paths[idx]) {
                receivedStablePath.store(true, std::memory_order_relaxed);
            }
            path = paths[idx];
            return true;
        };
        auto matches = index.query("abab", 1, resolver);
        check(matches.size() == 1 && matches[0].fileIndex == 1,
              "Review: content limit counts verified matches only");
        check(!matches.empty() && !matches[0].snippet.empty(),
              "Review: verified content match includes snippet");
        check(receivedStablePath.load(std::memory_order_relaxed),
              "Review: content verification receives a path snapshot, not a remapped index lookup");
        fs::remove_all(tmpDir);
    }

    // Verification must honor the configured index size, not stop at the 1 MiB default.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_large_content_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto path = (tmpDir / "large.txt").string();
        constexpr size_t kMatchOffset = 1024 * 1024 + 128 * 1024;
        {
            std::ofstream out(path, std::ios::binary);
            std::string padding(kMatchOffset, 'a');
            out.write(padding.data(), static_cast<std::streamsize>(padding.size()));
            out << "needle_beyond_one_mib";
        }

        ContentIndex index;
        index.setExtensions({"txt"});
        index.setMaxFileSize(2 * 1024 * 1024);
        check(index.indexFile(0, path) == ContentIndexUpdate::Upserted,
              "Review: large content fixture is indexed");
        auto matches = index.query("needle_beyond_one_mib", 1,
            [&](uint32_t idx, std::string& resolved) {
                if (idx != 0) return false;
                resolved = path;
                return true;
            });
        check(matches.size() == 1 && matches[0].matchOffset == kMatchOffset,
              "Review: content verification honors configured max file size");
        fs::remove_all(tmpDir);
    }

    std::cout << "\n";

    // A directory with no indexed children must not dereference a missing
    // lowerPathLookup_ entry in DIR_LIST mode.
    {
        SearchEngine engine;
        engine.loadRecords({{"Dir", "/review_parent", 2, 0, 100}});
        auto results = engine.query("/review_parent/Dir/*", 100, false);
        check(results.empty(), "Review: DIR_LIST missing child path returns empty safely");
    }

    // Content persistence resolves stable paths to the current runtime index.
    {
        auto tmpDir = fs::temp_directory_path() /
            ("maceverything_content_identity_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto filePath = (tmpDir / "identity.txt").string();
        { std::ofstream out(filePath); out << "stable path identity"; }
        auto basePath = (tmpDir / "content.bin").string();

        ContentIndex saved;
        saved.setExtensions({"txt"});
        check(saved.indexFile(5, filePath) == ContentIndexUpdate::Upserted,
              "Review: content identity fixture indexed");
        check(saved.saveToFile(basePath), "Review: path-keyed content base saved");

        ContentIndex loaded;
        check(loaded.loadFromFile(basePath, [&](const std::string& path) {
                  return path == filePath ? uint32_t{42} : UINT32_MAX;
              }), "Review: path-keyed content base loaded");
        ContentFileInfo info;
        check(loaded.getFileInfo(42, info) && info.fullPath == filePath,
              "Review: content path resolves to remapped runtime index");
        fs::remove_all(tmpDir);
    }
}
