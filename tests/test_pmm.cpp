// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include <stdint.h>
#include <stddef.h>

#include "test.hpp"
#include "mm/pmm.hpp"

TEST(pmm_probe) {
    constexpr size_t kRegionCount = 4;
    constexpr uint64_t kPageSize = 4096;

    mm::MemoryRegion regions[kRegionCount] = {
        {
            .base = 0x00000000,
            .length = 0x00001000,
            .type = mm::RegionType::Reserved
        },
        {
            .base = 0x00001000,
            .length = 0x0001F000,
            .type = mm::RegionType::Usable
        },
        {
            .base = 0x00020000,
            .length = 0x00010000,
            .type = mm::RegionType::Reserved
        },
        {
            .base = 0x00030000,
            .length = 0x00008000,
            .type = mm::RegionType::Usable
        }
    };

    constexpr uint64_t kUsableFrames =
        (0x0001F000 / kPageSize) +
        (0x00008000 / kPageSize);

    constexpr uint64_t kBitmapBytes =
        ((kUsableFrames + 63) / 64) * sizeof(uint64_t);

    constexpr uint64_t kBitmapPages =
        (kBitmapBytes + kPageSize - 1) / kPageSize;

    constexpr uint64_t kExpectedAllocations =
        kUsableFrames - kBitmapPages;

    alignas(4096) static uint8_t fake_ram[64 * 1024];

    const uint64_t hhdm =
        reinterpret_cast<uint64_t>(fake_ram) - 0x1000;

    mm::init(
        regions,
        kRegionCount,
        hhdm
    );

    uint64_t frames[kExpectedAllocations];

    for (size_t i = 0; i < kExpectedAllocations; ++i) {
        frames[i] = mm::alloc_frame();

        CHECK(frames[i] != 0);
        CHECK((frames[i] % kPageSize) == 0);

        for (size_t j = 0; j < i; ++j) {
            CHECK(frames[i] != frames[j]);
        }
    }

    CHECK(mm::alloc_frame() == 0);

    mm::free_frame(frames[3]);
    mm::free_frame(frames[10]);
    mm::free_frame(frames[20]);

    const uint64_t a = mm::alloc_frame();
    const uint64_t b = mm::alloc_frame();
    const uint64_t c = mm::alloc_frame();

    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(c != 0);

    CHECK(a != b);
    CHECK(a != c);
    CHECK(b != c);

    CHECK(mm::alloc_frame() == 0);
}