// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include <stdint.h>
#include <stddef.h>

#include "vmm.hpp"
#include "pmm.hpp"
#include "arch/x86_64/msr.hpp"

namespace mm {

// ============================================================
// Helpers
// ============================================================

struct AllocatedTable {
    uint64_t physical;
    PageTable* virtual_address;
};


static bool allocate_table(AllocatedTable& table)
{
    const uint64_t physical = alloc_frame();

    if (physical == UINT64_MAX) {
        return false;
    }

    auto* virtual_address =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(physical)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        virtual_address->entries[i] = 0;
    }

    table = {
        .physical = physical,
        .virtual_address = virtual_address
    };

    return true;
}


bool create_address_space(AddressSpace& out)
{
    AllocatedTable table;

    if (!allocate_table(table)) {
        return false;
    }

    out = {
        .pml4 = table.virtual_address,
        .pml4_physical = table.physical
    };

    return true;
}


constexpr PageFlags intermediate_flags(PageFlags leaf_flags)
{
    PageFlags result =
        PageFlags::Present |
        PageFlags::Writable;

    if (
        static_cast<uint64_t>(leaf_flags) &
        static_cast<uint64_t>(PageFlags::User)
    ) {
        result |= PageFlags::User;
    }

    return result;
}


static PageTable* ensure_table(
    uint64_t& entry,
    PageFlags middle_flags
)
{
    const uint64_t present =
        static_cast<uint64_t>(PageFlags::Present);

        const uint64_t huge =
        static_cast<uint64_t>(PageFlags::Huge);

    // Something already occupies this entry as a huge mapping.
    if (entry & huge) {
        return nullptr;
    }

    // No table exists yet.
    if ((entry & present) == 0) {
        AllocatedTable table;

        if (!allocate_table(table)) {
            return nullptr;
        }

        entry =
            make_page_entry(
                table.physical,
                middle_flags
            );

        return table.virtual_address;
    }

    // A table already exists. Add any permissions required
    // by the new mapping without removing existing ones.
    entry |=
        static_cast<uint64_t>(middle_flags);

    return reinterpret_cast<PageTable*>(
        physical_to_virtual(
            page_entry_address(entry)
        )
    );

}

bool map_range(
    const AddressSpace& space,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
)
{
    if (space.pml4 == nullptr) {
        return false;
    }

    if (size == 0) {
        return false;
    }

    if ((virt & (kPageSize - 1)) != 0) {
        return false;
    }

    if ((phys & (kPageSize - 1)) != 0) {
        return false;
    }

    if ((size & (kPageSize - 1)) != 0) {
        return false;
    }

    // Check that the virtual range does not wrap around
    // uint64_t before starting the mapping loop.
    if (virt > UINT64_MAX - size) {
        return false;
    }

    // Likewise, don't allow the physical range to overflow.
    if (phys > UINT64_MAX - size) {
        return false;
    }

    if (!is_canonical(virt)) return false;
    if (!is_canonical(virt + size - 1)) return false;

    // This operation is not atomic. If map_page() fails halfway
    // through, the pages already mapped remain mapped.
    for (uint64_t offset = 0;
         offset < size;
         offset += kPageSize) {

        if (!map_page(
                space.pml4,
                virt + offset,
                phys + offset,
                flags
            )) {
            return false;
        }
    }

    return true;
}


bool map_range_large(
    const AddressSpace& space,
    uint64_t virt,
    uint64_t phys,
    uint64_t size,
    PageFlags flags
)
{
    if (space.pml4 == nullptr) return false;
    if (size == 0) return false;

    constexpr uint64_t kLargePageMask =
        kLargePageSize - 1;

    if ((virt & kLargePageMask) != 0) return false;
    if ((phys & kLargePageMask) != 0) return false;
    if ((size & kLargePageMask) != 0) return false;

    if (virt > UINT64_MAX - size) return false;
    if (phys > UINT64_MAX - size) return false;

    if (!is_canonical(virt)) return false;
    if (!is_canonical(virt + size - 1)) return false;

    // This operation is not atomic. If map_page_large()
    // fails halfway through, earlier mappings remain mapped.
    for (
        uint64_t offset = 0;
        offset < size;
        offset += kLargePageSize
    ) {
        if (!map_page_large(
                space.pml4,
                virt + offset,
                phys + offset,
                flags
            )) {
            return false;
        }
    }

    return true;
}

// ============================================================
// Map one 4 KiB page
// ============================================================

bool map_page(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
)
{

    // --------------------------------------------------------
    // Validate arguments.
    // --------------------------------------------------------

    if (pml4 == nullptr) {
        return false;
    }

    if (!is_canonical(virt)) {
        return false;
    }

    if ((virt & (kPageSize - 1)) != 0) {
        return false;
    }

    if ((phys & (kPageSize - 1)) != 0) {
        return false;
    }

    if (phys & ~kPhysicalAddressMask) {
        return false;
    }

    // A 4 KiB mapping must not use the page-size bit.
    if (
        static_cast<uint64_t>(flags) &
        static_cast<uint64_t>(PageFlags::Huge)
    ) {
        return false;
    }

    const VirtualAddress address =
        decompose_address(virt);

    const PageFlags middle_flags =
        intermediate_flags(flags);

    PageTable* pdpt =
        ensure_table(
            pml4->entries[address.pml4_index],
            middle_flags
        );

    if (pdpt == nullptr) {
        return false;
    }

    PageTable* pd =
        ensure_table(
            pdpt->entries[address.pdpt_index],
            middle_flags
        );

    if (pd == nullptr) {
        return false;
    }

    PageTable* pt =
        ensure_table(
            pd->entries[address.pd_index],
            middle_flags
        );

    if (pt == nullptr) {
        return false;
    }



    // --------------------------------------------------------
    // PT -> physical page
    // --------------------------------------------------------

    uint64_t& pt_entry =
        pt->entries[address.pt_index];

    // Refuse to silently overwrite an existing mapping.
    if (
        pt_entry &
        static_cast<uint64_t>(PageFlags::Present)
    ) {
        return false;
    }

    pt_entry =
        make_page_entry(
            phys,
            flags | PageFlags::Present
        );

    return true;
}

bool map_page_large(
    PageTable* pml4,
    uint64_t virt,
    uint64_t phys,
    PageFlags flags
)
{
    if (pml4 == nullptr) return false;
    if (!is_canonical(virt)) return false;

    constexpr uint64_t kLargePageMask =
        kLargePageSize - 1;

    if ((virt & kLargePageMask) != 0) return false;
    if ((phys & kLargePageMask) != 0) return false;

    if (phys & ~kPhysicalAddressMask) return false;

    const VirtualAddress address =
        decompose_address(virt);

    const PageFlags middle_flags =
        intermediate_flags(flags);

    PageTable* pdpt =
        ensure_table(
            pml4->entries[address.pml4_index],
            middle_flags
        );

    if (pdpt == nullptr) return false;

    PageTable* pd =
        ensure_table(
            pdpt->entries[address.pdpt_index],
            middle_flags
        );

    if (pd == nullptr) return false;

    uint64_t& pd_entry =
        pd->entries[address.pd_index];

    if (
        pd_entry &
        static_cast<uint64_t>(PageFlags::Present)
    ) {
        return false;
    }

    pd_entry =
        make_page_entry(
            phys,
            flags |
            PageFlags::Present |
            PageFlags::Huge
        );

    return true;
}

uint64_t translate(
    const AddressSpace& space,
    uint64_t virt
)
{
    if (space.pml4 == nullptr) {
        return 0;
    }

    if (!is_canonical(virt)) {
        return 0;
    }

    const VirtualAddress address =
        decompose_address(virt);

    const uint64_t present =
        static_cast<uint64_t>(PageFlags::Present);

    const uint64_t huge =
        static_cast<uint64_t>(PageFlags::Huge);

    // --------------------------------------------------------
    // PML4 -> PDPT
    // --------------------------------------------------------

    const uint64_t pml4_entry =
        space.pml4->entries[address.pml4_index];

    if ((pml4_entry & present) == 0) {
        return 0;
    }

    auto* pdpt =
        reinterpret_cast<const PageTable*>(
            physical_to_virtual(
                page_entry_address(pml4_entry)
            )
        );

    // --------------------------------------------------------
    // PDPT -> PD
    // --------------------------------------------------------

    const uint64_t pdpt_entry =
        pdpt->entries[address.pdpt_index];

    if ((pdpt_entry & present) == 0) {
        return 0;
    }

    // A 1 GiB mapping isn't produced by our current mapper,
    // but recognize it here for completeness.
    if (pdpt_entry & huge) {
        return
            page_entry_address(pdpt_entry) |
            (virt & 0x3FFFFFFFull);
    }

    auto* pd =
        reinterpret_cast<const PageTable*>(
            physical_to_virtual(
                page_entry_address(pdpt_entry)
            )
        );

    // --------------------------------------------------------
    // PD -> PT
    // --------------------------------------------------------

    const uint64_t pd_entry =
        pd->entries[address.pd_index];

    if ((pd_entry & present) == 0) {
        return 0;
    }

    // 2 MiB page.
    if (pd_entry & huge) {
        return
            page_entry_address(pd_entry) |
            (virt & 0x1FFFFFull);
    }

    auto* pt =
        reinterpret_cast<const PageTable*>(
            physical_to_virtual(
                page_entry_address(pd_entry)
            )
        );

    // --------------------------------------------------------
    // PT -> physical page
    // --------------------------------------------------------

    const uint64_t pt_entry =
        pt->entries[address.pt_index];

    if ((pt_entry & present) == 0) {
        return 0;
    }

    return
        page_entry_address(pt_entry) |
        (virt & 0xFFFull);
}

void load_address_space(const AddressSpace& space)
{
    asm volatile("mov %0, %%cr3" :: "r"(space.pml4_physical) : "memory");
}

bool build_kernel_address_space(
    AddressSpace& out,
    const MemoryRegion* regions,
    size_t region_count,
    uint64_t hhdm_offset,
    const KernelLayout& layout
)
{
    if (regions == nullptr) return false;
    if (region_count == 0) return false;
    if ((hhdm_offset & (kLargePageSize - 1)) != 0) return false;

    arch::enable_nx();

    if (!create_address_space(out)) return false;

    uint64_t highest = 0;

    for (size_t i = 0; i < region_count; ++i) {
        const uint64_t end = regions[i].base + regions[i].length;

        if (end > highest)
            highest = end;
    }

    if (highest == 0)
        return false;

    highest = align_up(highest, kLargePageSize);

    if (!map_range_large(
            out,
            hhdm_offset,
            0,
            highest,
            PageFlags::Writable | PageFlags::NoExecute))
    {
        return false;
    }

    const uint64_t data_end_rounded =
        align_up(layout.data_end, kPageSize);

    auto map_segment =
        [&](uint64_t start, uint64_t end, PageFlags flags) -> bool
        {
            const uint64_t phys =
                start - layout.virtual_base + layout.physical_base;

            const uint64_t size = end - start;

            return map_range(out, start, phys, size, flags);
        };

    if (!map_segment(layout.image_start,  layout.text_start,
                    PageFlags::NoExecute))                        return false;

    if (!map_segment(layout.text_start,   layout.rodata_start,
                    PageFlags::None))                             return false;

    if (!map_segment(layout.rodata_start, layout.data_start,
                    PageFlags::NoExecute))                        return false;

    if (!map_segment(layout.data_start,   data_end_rounded,
                    PageFlags::Writable | PageFlags::NoExecute))  return false;

    return true;

}

} // namespace mm