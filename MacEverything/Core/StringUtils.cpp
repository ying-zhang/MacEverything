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

static bool containsCJKIdeograph(CFStringRef s) {
    CFIndex len = CFStringGetLength(s);
    for (CFIndex i = 0; i < len; i++) {
        UniChar ch = CFStringGetCharacterAtIndex(s, i);
        if ((ch >= 0x3400 && ch <= 0x4DBF) ||
            (ch >= 0x4E00 && ch <= 0x9FFF) ||
            (ch >= 0xF900 && ch <= 0xFAFF)) {
            return true;
        }
    }
    return false;
}

static bool isCJKIdeograph(UniChar ch) {
    return (ch >= 0x3400 && ch <= 0x4DBF) ||
           (ch >= 0x4E00 && ch <= 0x9FFF) ||
           (ch >= 0xF900 && ch <= 0xFAFF);
}

static bool appendMandarinInitial(UniChar ch, std::string& out) {
    UniChar chars[1] = {ch};
    CFStringRef single = CFStringCreateWithCharacters(kCFAllocatorDefault, chars, 1);
    if (!single) return false;

    CFMutableStringRef mutable_ = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, single);
    CFRelease(single);
    if (!mutable_) return false;

    if (!CFStringTransform(mutable_, nullptr, kCFStringTransformMandarinLatin, false)) {
        CFRelease(mutable_);
        return false;
    }
    CFStringTransform(mutable_, nullptr, kCFStringTransformStripDiacritics, false);

    CFIndex len = CFStringGetLength(mutable_);
    CFIndex maxBuf = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string latin(static_cast<size_t>(maxBuf), '\0');
    if (!CFStringGetCString(mutable_, latin.data(), maxBuf, kCFStringEncodingUTF8)) {
        CFRelease(mutable_);
        return false;
    }
    CFRelease(mutable_);
    latin.resize(std::strlen(latin.c_str()));

    for (unsigned char c : latin) {
        if (std::isalpha(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            return true;
        }
    }
    return false;
}

std::string normalizeNFC(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormC);
}

std::string normalizeNFD(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormD);
}

std::string mandarinInitialsKey(const std::string& s) {
    if (s.empty() || isAllAscii(s)) return {};

    CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(s.data()), static_cast<CFIndex>(s.size()),
        kCFStringEncodingUTF8, false);
    if (!cfStr) return {};
    if (!containsCJKIdeograph(cfStr)) {
        CFRelease(cfStr);
        return {};
    }

    std::string result;
    result.reserve(s.size());
    CFIndex len = CFStringGetLength(cfStr);
    for (CFIndex i = 0; i < len; i++) {
        UniChar ch = CFStringGetCharacterAtIndex(cfStr, i);
        if (isCJKIdeograph(ch)) {
            appendMandarinInitial(ch, result);
            continue;
        }
        if (ch < 128 && std::isalnum(static_cast<unsigned char>(ch))) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch))));
            continue;
        }
    }

    CFRelease(cfStr);
    return result;
}

} // namespace me
