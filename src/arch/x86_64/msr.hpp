// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#pragma once

#include <stdint.h>

namespace arch {

inline constexpr uint32_t kEferMsr = 0xC0000080;
inline constexpr uint64_t kEferNxe = 1ull << 11;

inline uint64_t read_msr(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;

    asm volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );

    return
        (static_cast<uint64_t>(hi) << 32) |
        static_cast<uint64_t>(lo);
}

inline void write_msr(uint32_t msr, uint64_t value)
{
    const uint32_t lo =
        static_cast<uint32_t>(value);

    const uint32_t hi =
        static_cast<uint32_t>(value >> 32);

    asm volatile(
        "wrmsr"
        :
        : "c"(msr),
          "a"(lo),
          "d"(hi)
        : "memory"
    );
}

inline void enable_nx()
{
    const uint64_t efer =
        read_msr(kEferMsr);

        if ((efer & kEferNxe) == 0) {
        write_msr(
            kEferMsr,
            efer | kEferNxe
        );
    }
}
} // namespace arch