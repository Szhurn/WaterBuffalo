// Global Descriptor Table.
//
// Copyright (c) 2026 Hunter Shurniak. All rights reserved.
//
// Long mode retains segmentation only vestigially: base and limit are ignored
// for code and data segments, and the descriptors exist principally to carry
// a privilege level and the 64-bit code flag. A table is nonetheless required,
// because CS and SS must reference valid descriptors and ring 3 needs user
// descriptors to exist.

#pragma once

#include <stdint.h>

namespace arch::gdt {

// Represented as a plain 64-bit value rather than a bitfield struct, which
// avoids any dependence on the compiler's field packing.
struct Descriptor {
    uint64_t value;
};

static_assert(sizeof(Descriptor) == 8);

// Encodes the legacy descriptor layout:
//
//   bits  0-15   limit  15:0
//   bits 16-39   base   23:0
//   bits 40-47   access byte
//   bits 48-51   limit  19:16
//   bits 52-55   flags  (G, D/B, L, AVL)
//   bits 56-63   base   31:24
//
// Base and limit are ignored by the CPU in long mode but are populated
// conventionally.
constexpr Descriptor make_descriptor(
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t flags
) {
    uint64_t descriptor = 0;

    descriptor |= limit & 0xFFFFu;
    descriptor |= static_cast<uint64_t>(base & 0xFFFFFFu) << 16;
    descriptor |= static_cast<uint64_t>(access) << 40;
    descriptor |= static_cast<uint64_t>((limit >> 16) & 0xFu) << 48;
    descriptor |= static_cast<uint64_t>(flags & 0xFu) << 52;
    descriptor |= static_cast<uint64_t>((base >> 24) & 0xFFu) << 56;

    return { descriptor };
}

// Access byte: P | DPL | S | E | DC | RW | A.
// Flags nibble: G | D/B | L | AVL, where L marks a 64-bit code segment.
//
// The kernel and user variants differ only in DPL.
inline constexpr Descriptor null_descriptor = make_descriptor(0, 0, 0x00, 0x0);
inline constexpr Descriptor kernel_code     = make_descriptor(0, 0xFFFFF, 0x9A, 0xA);
inline constexpr Descriptor kernel_data     = make_descriptor(0, 0xFFFFF, 0x92, 0xC);
inline constexpr Descriptor user_data       = make_descriptor(0, 0xFFFFF, 0xF2, 0xC);
inline constexpr Descriptor user_code       = make_descriptor(0, 0xFFFFF, 0xFA, 0xA);

// A selector is a byte offset into the table, so entry N is at N * 8. The low
// two bits hold the requested privilege level; ring-3 selectors are these
// values ORed with 3.
//
// The ordering of the user entries is dictated by SYSRET, which derives
// SS from STAR[63:48] + 8 and CS from STAR[63:48] + 16.
inline constexpr uint16_t kNullSelector       = 0x00;
inline constexpr uint16_t kKernelCodeSelector = 0x08;
inline constexpr uint16_t kKernelDataSelector = 0x10;
inline constexpr uint16_t kUserDataSelector   = 0x18;
inline constexpr uint16_t kUserCodeSelector   = 0x20;

// Installs the table and reloads every segment register onto it.
void init();

}  // namespace arch::gdt
