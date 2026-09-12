// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace mm {
struct MemoryRegion;
// ============================================================
// Page-table constants
// ============================================================
inline constexpr size_t kPageTableEntries = 512;
inline constexpr size_t kPageSize = 4096;
inline constexpr size_t kLargePageSize = 2 * 1024 * 1024;

constexpr uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// ============================================================
// Page-table entry flags
// ============================================================
//
// These correspond directly to bits in an x86-64 page-table entry.
//
// Bit 0  = Present
// Bit 1  = Writable
// Bit 2  = User
// Bit 5  = Accessed
// Bit 6  = Dirty
// Bit 7  = Page Size
// Bit 8  = Global
// Bit 63 = No Execute
//

enum class PageFlags : uint64_t {
    None      = 0,
    Present   = 1ull << 0,
    Writable  = 1ull << 1,
    User      = 1ull << 2,
    Accessed  = 1ull << 5,
    Dirty     = 1ull << 6,
    Huge      = 1ull << 7,
    Global    = 1ull << 8,
    NoExecute = 1ull << 63
};


constexpr PageFlags operator|(
    PageFlags lhs,
    PageFlags rhs
)
{
    return static_cast<PageFlags>(
        static_cast<uint64_t>(lhs) |
        static_cast<uint64_t>(rhs)
    );
}


constexpr PageFlags operator&(
    PageFlags lhs,
    PageFlags rhs
)
{
    return static_cast<PageFlags>(
        static_cast<uint64_t>(lhs) &
        static_cast<uint64_t>(rhs)
    );
}


constexpr PageFlags& operator|=(
    PageFlags& lhs,
    PageFlags rhs
)
{
    lhs = lhs | rhs;
    return lhs;
}


// ============================================================
// Page table
// ============================================================
//
// A page table contains 512 entries.
//
// 512 entries * 8 bytes = 4096 bytes = one page.
//
// This exact size lets every page-table level occupy one physical
// 4 KiB frame.
//

struct PageTable {
    uint64_t entries[kPageTableEntries];
};

static_assert(
    sizeof(PageTable) == kPageSize,
    "PageTable must occupy exactly one 4 KiB page"
);


// ============================================================
// Page-table entry address helpers
// ============================================================

inline constexpr uint64_t kPhysicalAddressMask =
    0x000FFFFFFFFFF000ull;


constexpr uint64_t make_page_entry(
    uint64_t physical_address,
    PageFlags flags
)
{
    return
        (physical_address & kPhysicalAddressMask) |
        static_cast<uint64_t>(flags);
}


constexpr uint64_t page_entry_address(uint64_t entry)
{
    return entry & kPhysicalAddressMask;
}


constexpr PageFlags page_entry_flags(uint64_t entry)
{
    return static_cast<PageFlags>(
        entry & ~kPhysicalAddressMask
    );
}


// ============================================================
// Virtual-address decomposition
// ============================================================
//
// Canonical x86-64 virtual addresses currently use bits 0..47.
//
//              47        39 38        30 29        21 20        12 11       0
//             +------------+------------+------------+------------+----------+
//             |   PML4     |    PDPT    |     PD     |     PT     |  offset  |
//             |    9 bits  |   9 bits   |   9 bits   |   9 bits   | 12 bits  |
//             +------------+------------+------------+------------+----------+
//

struct VirtualAddress {
    uint16_t pml4_index;
    uint16_t pdpt_index;
    uint16_t pd_index;
    uint16_t pt_index;
    uint16_t offset;
};


// ============================================================
// Address decomposition
// ============================================================

constexpr VirtualAddress decompose_address(uint64_t address)
{
    return {
        .pml4_index = static_cast<uint16_t>(
            (address >> 39) & 0x1FF
        ),

        .pdpt_index = static_cast<uint16_t>(
            (address >> 30) & 0x1FF
        ),

        .pd_index = static_cast<uint16_t>(
            (address >> 21) & 0x1FF
        ),

        .pt_index = static_cast<uint16_t>(
            (address >> 12) & 0x1FF
        ),

        .offset = static_cast<uint16_t>(
            address & 0xFFF
        )
    };
}


// ============================================================
// Address reassembly
// ============================================================

constexpr uint64_t reassemble_address(
    const VirtualAddress& address
)
{
    uint64_t result = 
        (static_cast<uint64_t>(address.pml4_index) << 39) |
        (static_cast<uint64_t>(address.pdpt_index) << 30) |
        (static_cast<uint64_t>(address.pd_index) << 21) |
        (static_cast<uint64_t>(address.pt_index) << 12) |
        static_cast<uint64_t>(address.offset);

    // canonical x86_64 address sign-extended bit 47.
    if (result & (1ull << 47)) {
        result |= 0xFFFF000000000000ull;
    }

    return result;

}


// ============================================================
// Canonical-address check
// ============================================================
//
// In the current 48-bit virtual-address scheme:
//
//     bit 47 = 0
//         -> bits 63:48 must all be 0
//
//     bit 47 = 1
//         -> bits 63:48 must all be 1
//
// The upper 16 bits must therefore be either:
//
//     0x0000
// or
//     0xFFFF
//

constexpr bool is_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48;
    const uint64_t sign = (address >> 47) & 1;

    if (sign == 0) {
        return upper == 0;
    }

    return upper == 0xFFFF;
}


bool map_page(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
);

struct AddressSpace {
    PageTable* pml4;
    uint64_t pml4_physical;
};

bool create_address_space(AddressSpace& out);

bool map_range(
    const AddressSpace& space,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
);

uint64_t translate(
    const AddressSpace& space,
    uint64_t virt
);

bool map_page_large(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
);

bool map_range_large(
    const AddressSpace&,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
);

struct KernelLayout {
    uint64_t virtual_base;
    uint64_t physical_base;

    uint64_t image_start;
    uint64_t text_start;
    uint64_t rodata_start;
    uint64_t data_start;
    uint64_t data_end;
};

bool build_kernel_address_space(
    AddressSpace& out,
    const MemoryRegion* regions,
    size_t region_count,
    uint64_t hhdm_offset,
    const KernelLayout& layout
);


void load_address_space(const AddressSpace& space);

} // namespace mm

