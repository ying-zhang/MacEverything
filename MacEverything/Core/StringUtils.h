#pragma once
#include <string>

namespace me {
/// Case-insensitive lowercasing: ASCII fast-path + CoreFoundation Unicode fallback.
std::string toLower(const std::string& s);

/// Unicode normalization helpers. ASCII strings are returned unchanged.
std::string normalizeNFC(const std::string& s);
std::string normalizeNFD(const std::string& s);

/// Build a compact search key for Chinese filenames: Han characters become
/// Mandarin pinyin initials, while existing ASCII alphanumeric runs are kept.
/// Example: "重要File" -> "zyfile". Returns empty for pure-ASCII input.
std::string mandarinInitialsKey(const std::string& s);
}
