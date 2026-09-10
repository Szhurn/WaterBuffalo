// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include <stdint.h>
#include <stddef.h>

#include "vmm.hpp"
#include "pmm.hpp"

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

    if (physical == 0) {
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


    // --------------------------------------------------------
    // PML4 -> PDPT
    // --------------------------------------------------------

    uint64_t& pml4_entry =
        pml4->entries[address.pml4_index];

    PageTable* pdpt = nullptr;

    if ((pml4_entry &
         static_cast<uint64_t>(PageFlags::Present)) == 0) {

        AllocatedTable table;

        if (!allocate_table(table)) {
            return false;
        }

        pml4_entry =
            make_page_entry(
                table.physical,
                middle_flags
            );

        pdpt = table.virtual_address;

    } else {

        pdpt =
            reinterpret_cast<PageTable*>(
                physical_to_virtual(
                    page_entry_address(pml4_entry)
                )
            );

        // PML4 entries point to the next table.
        // Do not accept malformed entries.
        if (
            pml4_entry &
            static_cast<uint64_t>(PageFlags::Huge)
        ) {
            return false;
        }

        pml4_entry |=
            static_cast<uint64_t>(middle_flags);
    }


    // --------------------------------------------------------
    // PDPT -> PD
    // --------------------------------------------------------

    uint64_t& pdpt_entry =
        pdpt->entries[address.pdpt_index];

    PageTable* pd = nullptr;

    if ((pdpt_entry &
         static_cast<uint64_t>(PageFlags::Present)) == 0) {

        AllocatedTable table;

        if (!allocate_table(table)) {
            return false;
        }

        pdpt_entry =
            make_page_entry(
                table.physical,
                middle_flags
            );

        pd = table.virtual_address;

    } else {

        if (
            pdpt_entry &
            static_cast<uint64_t>(PageFlags::Huge)
        ) {
            // This entry is already a 1 GiB mapping.
            // We cannot descend through it as a page table.
            return false;
        }

        pd =
            reinterpret_cast<PageTable*>(
                physical_to_virtual(
                    page_entry_address(pdpt_entry)
                )
            );

        pdpt_entry |=
            static_cast<uint64_t>(middle_flags);
    }


    // --------------------------------------------------------
    // PD -> PT
    // --------------------------------------------------------

    uint64_t& pd_entry =
        pd->entries[address.pd_index];

    PageTable* pt = nullptr;

    if ((pd_entry &
         static_cast<uint64_t>(PageFlags::Present)) == 0) {

        AllocatedTable table;

        if (!allocate_table(table)) {
            return false;
        }

        pd_entry =
            make_page_entry(
                table.physical,
                middle_flags
            );

        pt = table.virtual_address;

    } else {

        if (
            pd_entry &
            static_cast<uint64_t>(PageFlags::Huge)
        ) {
            // This entry is already a 2 MiB mapping.
            return false;
        }

        pt =
            reinterpret_cast<PageTable*>(
                physical_to_virtual(
                    page_entry_address(pd_entry)
                )
            );

        pd_entry |=
            static_cast<uint64_t>(middle_flags);
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

} // namespace mm