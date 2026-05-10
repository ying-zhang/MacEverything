#pragma once
#include <string>

namespace me {
/// Case-insensitive lowercasing: ASCII fast-path + CoreFoundation Unicode fallback.
std::string toLower(const std::string& s);

/// Unicode normalization helpers. ASCII strings are returned unchanged.
std::string normalizeNFC(const std::string& s);
std::string normalizeNFD(const std::string& s);
}
