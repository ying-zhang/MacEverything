#include "StringUtils.h"
#include "SIMDSearch.h"
#include <cctype>
#include <cstring>
#include <CoreFoundation/CoreFoundation.h>

namespace me {

static bool isAllAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 128) return false;
    }
    return true;
}

std::string toLower(const std::string& s) {
    // ASCII fast-path — skip CoreFoundation for pure-ASCII strings
    // (>95% of filenames on typical systems). Uses NEON SIMD on ARM64.
    if (isAllAscii(s)) {
        std::string result = s;
        simdToLowerAscii(result.data(), result.size());
        return result;
    }

    // Unicode-aware lowercasing via CoreFoundation (non-ASCII only)
    CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(s.data()), static_cast<CFIndex>(s.size()),
        kCFStringEncodingUTF8, false);
    if (!cfStr) {
        std::string result(s.size(), '\0');
        for (size_t i = 0; i < s.size(); i++)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        return result;
    }
    CFMutableStringRef mutable_ = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cfStr);
    CFRelease(cfStr);
    CFStringLowercase(mutable_, CFLocaleGetSystem());

    CFIndex len = CFStringGetLength(mutable_);
    CFIndex maxBuf = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(maxBuf), '\0');
    CFStringGetCString(mutable_, result.data(), maxBuf, kCFStringEncodingUTF8);
    CFRelease(mutable_);
    result.resize(std::strlen(result.c_str()));
    return result;
}

static std::string normalizeCF(const std::string& s, CFStringNormalizationForm form) {
    if (s.empty() || isAllAscii(s)) return s;

    CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(s.data()), static_cast<CFIndex>(s.size()),
        kCFStringEncodingUTF8, false);
    if (!cfStr) return s;

    CFMutableStringRef mutable_ = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cfStr);
    CFRelease(cfStr);
    if (!mutable_) return s;

    CFStringNormalize(mutable_, form);

    CFIndex len = CFStringGetLength(mutable_);
    CFIndex maxBuf = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(maxBuf), '\0');
    if (!CFStringGetCString(mutable_, result.data(), maxBuf, kCFStringEncodingUTF8)) {
        CFRelease(mutable_);
        return s;
    }
    CFRelease(mutable_);
    result.resize(std::strlen(result.c_str()));
    return result;
}

std::string normalizeNFC(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormC);
}

std::string normalizeNFD(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormD);
}

} // namespace me
