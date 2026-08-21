#pragma once
#include "test_helpers.h"
#include "../MacEverything/Core/SIMDSearch.h"
#include <iostream>
#include <cstring>
#include <vector>

inline void runSIMDBatchFilterTests() {
    std::cout << "\n── Part 63: SIMD Batch Filter Tests ──\n\n";

    // 63a: simdTypeLive16 — all live
    {
        uint8_t types[16];
        for (int i = 0; i < 16; i++) types[i] = 1;
        uint16_t mask = me::simdTypeLive16(types);
        CHECK(mask == 0xFFFF);
        std::cout << "  63a: typeLive16 all live         PASS\n";
    }

    // 63b: simdTypeLive16 — all dead (tombstones)
    {
        uint8_t types[16] = {};
        uint16_t mask = me::simdTypeLive16(types);
        CHECK(mask == 0);
        std::cout << "  63b: typeLive16 all dead         PASS\n";
    }

    // 63c: simdTypeLive16 — mixed
    {
        uint8_t types[16] = {};
        types[0] = 1;  // bit 0
        types[3] = 2;  // bit 3
        types[7] = 1;  // bit 7
        types[15] = 2; // bit 15
        uint16_t mask = me::simdTypeLive16(types);
        CHECK((mask & (1 << 0)) != 0);
        CHECK((mask & (1 << 3)) != 0);
        CHECK((mask & (1 << 7)) != 0);
        CHECK((mask & (1 << 15)) != 0);
        CHECK((mask & (1 << 1)) == 0);
        CHECK((mask & (1 << 4)) == 0);
        std::cout << "  63c: typeLive16 mixed            PASS\n";
    }

    // 63d: simdTypeLive16 — single live record among dead
    {
        uint8_t types[16] = {};
        types[8] = 1;
        uint16_t mask = me::simdTypeLive16(types);
        CHECK(mask == (1 << 8));
        std::cout << "  63o: typeLive16 single live      PASS\n";
    }
}
