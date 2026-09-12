// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace mm {

// Declared in pmm.hpp. Only a pointer to it appears below, so the
// definition is not needed here and the header stays independent of the
// physical allocator.
struct MemoryRegion;


// ============================================================
// Page-table constants
// ============================================================
inline constexpr size_t kPageTableEntries = 512;
inline constexpr size_t kPageSize = 4096;
inline constexpr size_t kLargePageSize = 2 * 1024 * 1024;

// Rounds up to the next multiple of alignment, which must be a power of
// two. Used to extend a range to a whole number of pages.
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


// ============================================================
// Address spaces
// ============================================================
//
// An address space is a PML4 held twice: as a pointer for the kernel to
// build through, and as a physical address for CR3. The two are the same
// frame. Keeping both in one value removes the opportunity to load a
// virtual address into CR3, which faults with no diagnostic.

struct AddressSpace {
    PageTable* pml4;
    uint64_t   pml4_physical;
};


// Allocates and zeroes a PML4. Returns false if no frame is available, in
// which case out is left untouched.
bool create_address_space(AddressSpace& out);


// Writes space.pml4_physical to CR3, replacing the active address space.
// Every address the next instruction touches -- code, stack, descriptor
// tables and the page tables themselves -- must already be mapped in it.
// The CR3 load flushes all non-global TLB entries.
void load_address_space(const AddressSpace& space);


// ============================================================
// Mapping
// ============================================================
//
// map_page and map_page_large take a raw PML4 pointer; the range forms
// take an AddressSpace. Every function refuses to replace an existing
// mapping, so a collision surfaces as a failure rather than as silently
// corrupted tables.
//
// Failure part-way through a range is not undone. Ranges are mapped
// during boot, where a failure is fatal, so no rollback path exists.

// Maps one 4 KiB page. Intermediate levels are allocated as needed and
// carry Present | Writable, plus User when the leaf is a user mapping;
// they never carry NoExecute, since permissions are the intersection of
// every level and the leaf is what decides.
bool map_page(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
);

// Maps one 2 MiB page. The walk terminates at the page directory with
// the page-size bit set, so no page table is allocated. Both addresses
// must be 2 MiB aligned.
bool map_page_large(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
);

// Maps size bytes of 4 KiB pages. All three of virt, phys and size must
// be page aligned.
bool map_range(
    const AddressSpace& space,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
);

// Maps size bytes of 2 MiB pages. All three arguments must be 2 MiB
// aligned.
bool map_range_large(
    const AddressSpace& space,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
);


// Walks the four levels and returns the physical address virt maps to, or
// 0 if any level is not present. Recognises 1 GiB and 2 MiB mappings and
// adds the offset within the larger page.
//
// Zero doubles as the failure value. Nothing maps physical frame 0, so
// the ambiguity is harmless here.
uint64_t translate(
    const AddressSpace& space,
    uint64_t virt
);


// ============================================================
// The kernel address space
// ============================================================
//
// Segment bounds come from the linker script. Each is page aligned except
// data_end, which is rounded up. The physical address of a kernel virtual
// address is virt - virtual_base + physical_base; the bases come from the
// boot protocol, since the load address is chosen at boot.

struct KernelLayout {
    uint64_t virtual_base;
    uint64_t physical_base;

    uint64_t image_start;    // __limine_requests_start
    uint64_t text_start;     // __text_start
    uint64_t rodata_start;   // __rodata_start
    uint64_t data_start;     // __data_start
    uint64_t data_end;       // __data_end, end of .bss
};


// Builds the address space the kernel runs in once it stops using the
// bootloader's tables.
//
// The direct map comes first and covers every region in the memory map,
// including the gaps between them, using 2 MiB pages: the page tables
// allocated for everything after it are reached through it, as is the
// boot stack. Spanning the whole range rather than each region
// individually costs a few megabytes of tables and removes the case
// where two regions share one 2 MiB page.
//
// The kernel image is then mapped a segment at a time with the
// permissions each needs: .text executable and read-only, .rodata read-
// only, .data and .bss writable. Enables EFER.NXE first, since without it
// bit 63 of an entry is reserved rather than NoExecute.
//
// Does not load CR3. Call load_address_space once the result is checked.
bool build_kernel_address_space(
    AddressSpace& out,
    const MemoryRegion* regions,
    size_t region_count,
    uint64_t hhdm_offset,
    const KernelLayout& layout
);

} // namespace mm

