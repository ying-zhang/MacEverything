#pragma once
#include <string>
#include <cstdint>
#include <ctime>

struct FileRecord {
    std::string name;       // filename only
    std::string path;       // parent directory path
    uint8_t     type;       // 1=file, 2=dir, 3=symlink, 4=other, 0=tombstone, 5=app bundle
    uint64_t    size;       // file data length in bytes (0 for dirs)
    time_t      modTime;    // last modification time (seconds since epoch)
    uint64_t    inode = 0;  // ATTR_CMN_FILEID (inode number)
    int32_t     devId = 0;  // ATTR_CMN_DEVID (device ID)
};
