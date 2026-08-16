#include "StringUtils.h"
#include "SIMDSearch.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>
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
    CFLocaleRef stableLocale = CFLocaleCreate(kCFAllocatorDefault, CFSTR("en_US_POSIX"));
    CFStringLowercase(mutable_, stableLocale);
    if (stableLocale) CFRelease(stableLocale);

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

/// Decode UTF-8 into UTF-32 code points. Invalid sequences become U+FFFD.
static std::vector<uint32_t> utf8ToCodePoints(const std::string& s) {
    std::vector<uint32_t> cps;
    cps.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x06) {
            cp = c & 0x1F; extra = 1;
        } else if ((c >> 4) == 0x0E) {
            cp = c & 0x0F; extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07; extra = 3;
        } else {
            cps.push_back(0xFFFD); i++; continue;
        }
        if (i + static_cast<size_t>(extra) >= n) { cps.push_back(0xFFFD); break; }
        bool ok = true;
        for (int k = 1; k <= extra; k++) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { cps.push_back(0xFFFD); i++; continue; }
        cps.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return cps;
}

/// CJK ideograph test over full code points (includes Extension B–F).
static bool isCJKIdeograph32(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0x20000 && cp <= 0x2FFFF) ||
           (cp >= 0x30000 && cp <= 0x323AF);
}

/// Transliterate one code point to its Mandarin pinyin initial (first ASCII
/// letter of the latin reading). Returns false when no reading is available.
static bool appendMandarinInitial32(uint32_t cp, std::string& out) {
    UniChar utf16[2];
    CFIndex n;
    if (cp > 0xFFFF) {
        cp -= 0x10000;
        utf16[0] = static_cast<UniChar>(0xD800 + (cp >> 10));
        utf16[1] = static_cast<UniChar>(0xDC00 + (cp & 0x3FF));
        n = 2;
    } else {
        utf16[0] = static_cast<UniChar>(cp);
        n = 1;
    }
    CFStringRef single = CFStringCreateWithCharacters(kCFAllocatorDefault, utf16, n);
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

/// Common polyphonic words where a per-character transliteration picks the wrong
/// reading. Values are the correct pinyin initials for the whole word.
static const std::vector<std::pair<std::vector<uint32_t>, std::string>>&
polyphonicWords() {
    static const std::vector<std::pair<std::vector<uint32_t>, std::string>> words = [] {
        static const std::unordered_map<std::string, std::string> m = {
            {"重庆","cq"}, {"长城","cc"}, {"长江","cj"}, {"长期","cq"}, {"长征","cz"},
            {"长度","cd"}, {"重复","cf"}, {"重新","cx"}, {"重阳","cy"},
            {"音乐","yy"}, {"乐曲","yq"}, {"乐器","yq"}, {"乐队","yd"},
            {"银行","yh"}, {"行业","hy"}, {"行长","hz"},
            {"调整","tz"}, {"调节","tj"}, {"空调","kt"}, {"调查","dc"},
            {"睡觉","sj"}, {"校对","jd"},
            {"便宜","py"},
            {"西藏","xz"}, {"藏族","zz"}, {"宝藏","bz"},
            {"处理","cl"}, {"了解","lj"}, {"归还","gh"}, {"曾经","cj"},
        };
        std::vector<std::pair<std::vector<uint32_t>, std::string>> out;
        out.reserve(m.size());
        for (const auto& kv : m) out.emplace_back(utf8ToCodePoints(kv.first), kv.second);
        return out;
    }();
    return words;
}

std::string normalizeNFC(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormC);
}

std::string normalizeNFD(const std::string& s) {
    return normalizeCF(s, kCFStringNormalizationFormD);
}

std::string mandarinInitialsKey(const std::string& s) {
    if (s.empty() || isAllAscii(s)) return {};

    // NFKC folds compatibility ideographs (e.g. 﨑 → 崎) so CoreFoundation can
    // transliterate them, and folds full-width forms.
    std::string norm = normalizeCF(s, kCFStringNormalizationFormKC);
    std::vector<uint32_t> cps = utf8ToCodePoints(norm);

    bool hasCJK = false;
    for (uint32_t cp : cps) {
        if (isCJKIdeograph32(cp)) { hasCJK = true; break; }
    }
    if (!hasCJK) return {};

    std::string result;
    result.reserve(cps.size());
    const auto& words = polyphonicWords();

    size_t i = 0;
    while (i < cps.size()) {
        bool matched = false;
        for (const auto& w : words) {
            const auto& wcps = w.first;
            if (i + wcps.size() > cps.size()) continue;
            if (std::equal(wcps.begin(), wcps.end(),
                           cps.begin() + static_cast<std::ptrdiff_t>(i))) {
                result += w.second;
                i += wcps.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;

        uint32_t cp = cps[i];
        if (isCJKIdeograph32(cp)) {
            appendMandarinInitial32(cp, result);
        } else if (cp < 128 && std::isalnum(static_cast<int>(cp))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<int>(cp))));
        }
        i++;
    }
    return result;
}

} // namespace me
