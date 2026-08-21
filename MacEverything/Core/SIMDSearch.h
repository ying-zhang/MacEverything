#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ME_HAS_NEON 1
#else
#define ME_HAS_NEON 0
#endif

namespace me {

// ─── NEON helpers ──────────────────────────────────────────────────────────────

#if ME_HAS_NEON

/// Extract a 16-bit bitmask from a NEON comparison result (one bit per lane).
static inline uint16_t neon_movemask(uint8x16_t input) {
    static const uint8_t mask_bytes[16] = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t bit_mask = vld1q_u8(mask_bytes);
    uint8x16_t masked = vandq_u8(input, bit_mask);
    uint8_t lo = vaddv_u8(vget_low_u8(masked));
    uint8_t hi = vaddv_u8(vget_high_u8(masked));
    return lo | ((uint16_t)hi << 8);
}

#endif // ME_HAS_NEON

// ─── SIMD string search ───────────────────────────────────────────────────────

/// Find first occurrence of needle in haystack. Returns offset, or hayLen if not found.
/// Uses NEON first-last byte 2x unroll on ARM64, falls back to memmem otherwise.
inline size_t simdFind(const char* hay, size_t hayLen,
                       const char* ndl, size_t ndlLen) {
    if (ndlLen == 0) return 0;
    if (ndlLen > hayLen) return hayLen;

#if ME_HAS_NEON
    const auto* data = reinterpret_cast<const uint8_t*>(hay);
    const auto* needle = reinterpret_cast<const uint8_t*>(ndl);

    if (ndlLen == 1) {
        const void* p = memchr(hay, ndl[0], hayLen);
        return p ? static_cast<size_t>(static_cast<const char*>(p) - hay) : hayLen;
    }

    uint8x16_t v_first = vdupq_n_u8(needle[0]);
    uint8x16_t v_last  = vdupq_n_u8(needle[ndlLen - 1]);
    size_t end = hayLen - ndlLen + 1;
    size_t i = 0;

    // 2x unrolled NEON loop
    for (; i + 32 <= end; i += 32) {
        uint8x16_t a_match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i), v_first),
            vceqq_u8(vld1q_u8(data + i + ndlLen - 1), v_last));
        uint8x16_t b_match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i + 16), v_first),
            vceqq_u8(vld1q_u8(data + i + 16 + ndlLen - 1), v_last));
        if (vmaxvq_u8(vorrq_u8(a_match, b_match)) == 0) continue;

        if (vmaxvq_u8(a_match) != 0) {
            uint16_t mask = neon_movemask(a_match);
            while (mask) {
                int pos = __builtin_ctz(mask);
                if (memcmp(data + i + pos + 1, needle + 1, ndlLen - 2) == 0)
                    return i + pos;
                mask &= mask - 1;
            }
        }
        if (vmaxvq_u8(b_match) != 0) {
            uint16_t mask = neon_movemask(b_match);
            while (mask) {
                int pos = __builtin_ctz(mask);
                if (memcmp(data + i + 16 + pos + 1, needle + 1, ndlLen - 2) == 0)
                    return i + 16 + pos;
                mask &= mask - 1;
            }
        }
    }
    // 1x tail
    for (; i + 16 <= end; i += 16) {
        uint8x16_t match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i), v_first),
            vceqq_u8(vld1q_u8(data + i + ndlLen - 1), v_last));
        if (vmaxvq_u8(match) == 0) continue;
        uint16_t mask = neon_movemask(match);
        while (mask) {
            int pos = __builtin_ctz(mask);
            if (memcmp(data + i + pos + 1, needle + 1, ndlLen - 2) == 0)
                return i + pos;
            mask &= mask - 1;
        }
    }
    // scalar tail
    for (; i < end; i++) {
        if (memcmp(data + i, needle, ndlLen) == 0)
            return i;
    }
    return hayLen;
#else
    // Fallback: memmem
    const void* p = memmem(hay, hayLen, ndl, ndlLen);
    return p ? static_cast<size_t>(static_cast<const char*>(p) - hay) : hayLen;
#endif
}

/// Check if needle occurs anywhere in haystack.
inline bool simdContains(const char* hay, size_t hayLen,
                         const char* ndl, size_t ndlLen) {
    return simdFind(hay, hayLen, ndl, ndlLen) < hayLen;
}

/// Find all occurrences of needle in haystack. Pushes byte offsets into `out`.
inline void simdFindAll(const char* hay, size_t hayLen,
                        const char* ndl, size_t ndlLen,
                        std::vector<size_t>& out) {
    if (ndlLen == 0 || ndlLen > hayLen) return;

#if ME_HAS_NEON
    const auto* data = reinterpret_cast<const uint8_t*>(hay);
    const auto* needle = reinterpret_cast<const uint8_t*>(ndl);

    if (ndlLen == 1) {
        for (size_t i = 0; i < hayLen; i++)
            if (data[i] == needle[0]) out.push_back(i);
        return;
    }

    uint8x16_t v_first = vdupq_n_u8(needle[0]);
    uint8x16_t v_last  = vdupq_n_u8(needle[ndlLen - 1]);
    size_t end = hayLen - ndlLen + 1;
    size_t i = 0;

    // 2x unrolled
    for (; i + 32 <= end; i += 32) {
        uint8x16_t a_match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i), v_first),
            vceqq_u8(vld1q_u8(data + i + ndlLen - 1), v_last));
        uint8x16_t b_match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i + 16), v_first),
            vceqq_u8(vld1q_u8(data + i + 16 + ndlLen - 1), v_last));
        if (vmaxvq_u8(vorrq_u8(a_match, b_match)) == 0) continue;

        if (vmaxvq_u8(a_match) != 0) {
            uint16_t mask = neon_movemask(a_match);
            while (mask) {
                int pos = __builtin_ctz(mask);
                if (memcmp(data + i + pos + 1, needle + 1, ndlLen - 2) == 0)
                    out.push_back(i + pos);
                mask &= mask - 1;
            }
        }
        if (vmaxvq_u8(b_match) != 0) {
            uint16_t mask = neon_movemask(b_match);
            while (mask) {
                int pos = __builtin_ctz(mask);
                if (memcmp(data + i + 16 + pos + 1, needle + 1, ndlLen - 2) == 0)
                    out.push_back(i + 16 + pos);
                mask &= mask - 1;
            }
        }
    }
    // 1x tail
    for (; i + 16 <= end; i += 16) {
        uint8x16_t match = vandq_u8(
            vceqq_u8(vld1q_u8(data + i), v_first),
            vceqq_u8(vld1q_u8(data + i + ndlLen - 1), v_last));
        if (vmaxvq_u8(match) == 0) continue;
        uint16_t mask = neon_movemask(match);
        while (mask) {
            int pos = __builtin_ctz(mask);
            if (memcmp(data + i + pos + 1, needle + 1, ndlLen - 2) == 0)
                out.push_back(i + pos);
            mask &= mask - 1;
        }
    }
    // scalar tail
    for (; i < end; i++) {
        if (memcmp(data + i, needle, ndlLen) == 0)
            out.push_back(i);
    }
#else
    // Fallback
    const char* p = hay;
    size_t remaining = hayLen;
    while (remaining >= ndlLen) {
        const void* found = memmem(p, remaining, ndl, ndlLen);
        if (!found) break;
        size_t offset = static_cast<const char*>(found) - hay;
        out.push_back(offset);
        p = static_cast<const char*>(found) + 1;
        remaining = hayLen - (p - hay);
    }
#endif
}

/// In-place ASCII toLower using NEON. Only affects A-Z (0x41-0x5A).
inline void simdToLowerAscii(char* data, size_t len) {
#if ME_HAS_NEON
    auto* p = reinterpret_cast<uint8_t*>(data);
    size_t i = 0;
    uint8x16_t vA = vdupq_n_u8('A');
    uint8x16_t vZ = vdupq_n_u8('Z');
    uint8x16_t v32 = vdupq_n_u8(0x20);

    for (; i + 16 <= len; i += 16) {
        uint8x16_t v = vld1q_u8(p + i);
        uint8x16_t ge_A = vcgeq_u8(v, vA);
        uint8x16_t le_Z = vcleq_u8(v, vZ);
        uint8x16_t is_upper = vandq_u8(ge_A, le_Z);
        uint8x16_t lower_bits = vandq_u8(is_upper, v32);
        vst1q_u8(p + i, vorrq_u8(v, lower_bits));
    }
    for (; i < len; i++) {
        if (p[i] >= 'A' && p[i] <= 'Z') p[i] |= 0x20;
    }
#else
    for (size_t i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c >= 'A' && c <= 'Z') data[i] = c | 0x20;
    }
#endif
}

// ─── SIMD batch filter for SoA columnar arrays ──────────────────────────────

/// Batch-check 16 type bytes for liveness (type != 0). Returns 16-bit mask (1=live).
inline uint16_t simdTypeLive16(const uint8_t* types) {
#if ME_HAS_NEON
    uint8x16_t v = vld1q_u8(types);
    uint8x16_t zero = vdupq_n_u8(0);
    uint8x16_t neq = vmvnq_u8(vceqq_u8(v, zero));  // != 0
    return neon_movemask(neq);
#else
    uint16_t mask = 0;
    for (int i = 0; i < 16; i++) {
        if (types[i] != 0) mask |= (1u << i);
    }
    return mask;
#endif
}

} // namespace me
