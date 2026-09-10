// gdt.hpp
#pragma once

#include <stdint.h>

namespace arch::x86_64::gdt {

struct Descriptor {
    uint64_t value;
};

constexpr Descriptor make_descriptor(
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t flags
) {
    uint64_t descriptor = 0;

    descriptor |= (limit & 0xFFFFu);
    descriptor |= (base & 0xFFFFFFu) << 16;
    descriptor |= static_cast<uint64_t>(access) << 40;
    descriptor |= static_cast<uint64_t>((limit >> 16) & 0xFu) << 48;
    descriptor |= static_cast<uint64_t>(flags & 0xFu) << 52;
    descriptor |= static_cast<uint64_t>((base >> 24) & 0xFFu) << 56;

    return { descriptor };
}

inline constexpr Descriptor null_descriptor =
    make_descriptor(0, 0, 0, 0);

inline constexpr Descriptor kernel_code =
    make_descriptor(
        0,
        0xFFFFF,
        0x9A,
        0xA
    );

inline constexpr Descriptor kernel_data =
    make_descriptor(
        0,
        0xFFFFF,
        0x92,
        0xC
    );

} // namespace arch::x86_64::gdt