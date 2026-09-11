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

} // namespace mm