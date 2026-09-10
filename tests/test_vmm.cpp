// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include "test.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"

#include <stdint.h>
#include <stddef.h>

using namespace mm;


// ============================================================
// Host-test fake physical memory
// ============================================================
//
// The PMM thinks this is physical RAM:
//
//     0x0000 --------------------
//            fake RAM
//     0x10000 -------------------
//
// physical_to_virtual() translates those physical addresses
// into addresses inside fake_ram.
//

namespace {

alignas(4096)
static uint8_t fake_ram[64 * 1024];

static const uint64_t hhdm =
    reinterpret_cast<uint64_t>(fake_ram) - 0x1000;

static MemoryRegion regions[] = {
    {
        .base = 0x0000,
        .length = 0x10000,
        .type = RegionType::Usable
    }
};

}


// ============================================================
// Page table layout
// ============================================================

TEST(page_table_is_one_page)
{
    CHECK(sizeof(PageTable) == 4096);
    CHECK(kPageTableEntries == 512);
}


// ============================================================
// Page-entry encoding
// ============================================================

TEST(page_entry_encoding)
{
    constexpr uint64_t phys =
        0x0000000012345000ull;

    constexpr PageFlags flags =
        PageFlags::Present |
        PageFlags::Writable |
        PageFlags::NoExecute;

    constexpr uint64_t entry =
        make_page_entry(phys, flags);

    CHECK(page_entry_address(entry) == phys);

    CHECK(
        static_cast<uint64_t>(page_entry_flags(entry)) ==
        static_cast<uint64_t>(flags)
    );
}


// ============================================================
// Virtual-address decomposition
// ============================================================

TEST(decompose_kernel_address)
{
    constexpr uint64_t address =
        0xffffffff80001000ull;

    constexpr VirtualAddress parts =
        decompose_address(address);

    CHECK(parts.pml4_index == 511);
    CHECK(parts.pdpt_index == 510);
    CHECK(parts.pd_index == 0);
    CHECK(parts.pt_index == 1);
    CHECK(parts.offset == 0);
}


TEST(decompose_HHDM_address)
{
    constexpr uint64_t address =
        0xffff800000052000ull;

    constexpr VirtualAddress parts =
        decompose_address(address);

    CHECK(parts.pml4_index == 256);
    CHECK(parts.pdpt_index == 0);
    CHECK(parts.pd_index == 0);
    CHECK(parts.pt_index == 82);
    CHECK(parts.offset == 0);
}


// ============================================================
// Address round-trip
// ============================================================

TEST(kernel_address_round_trips)
{
    constexpr uint64_t original =
        0xffffffff80001000ull;

    constexpr VirtualAddress parts =
        decompose_address(original);

    constexpr uint64_t rebuilt =
        reassemble_address(parts);

    CHECK(rebuilt == original);
}


TEST(HHDM_address_round_trips)
{
    constexpr uint64_t original =
        0xffff800000052000ull;

    constexpr VirtualAddress parts =
        decompose_address(original);

    constexpr uint64_t rebuilt =
        reassemble_address(parts);

    CHECK(rebuilt == original);
}


// ============================================================
// Canonical addresses
// ============================================================

TEST(canonical_addresses)
{
    CHECK(is_canonical(0x0000000000000000ull));
    CHECK(is_canonical(0x00007ffffffff000ull));

    CHECK(is_canonical(0xffff800000000000ull));
    CHECK(is_canonical(0xffffffff80000000ull));
}


TEST(non_canonical_addresses)
{
    CHECK(!is_canonical(0x0000800000000000ull));
    CHECK(!is_canonical(0xffff7ffffffff000ull));
}


// ============================================================
// map_page()
// ============================================================

TEST(map_page_creates_page_table_hierarchy)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    // Allocate the root PML4 from fake physical memory.
    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    // The PMM gives us a physical frame, but the host test needs
    // to explicitly treat it as a page table.
    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    const uint64_t virt =
        0x0000000040000000ull;

    const uint64_t phys =
        0x0000000000005000ull;

    const bool mapped =
        map_page(
            pml4,
            virt,
            phys,
            PageFlags::Writable
        );

    CHECK(mapped);

    const VirtualAddress parts =
        decompose_address(virt);


    // --------------------------------------------------------
    // PML4
    // --------------------------------------------------------

    const uint64_t pml4_entry =
        pml4->entries[parts.pml4_index];

    CHECK(
        pml4_entry &
        static_cast<uint64_t>(PageFlags::Present)
    );

    CHECK(
        pml4_entry &
        static_cast<uint64_t>(PageFlags::Writable)
    );

    auto* pdpt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pml4_entry)
            )
        );


    // --------------------------------------------------------
    // PDPT
    // --------------------------------------------------------

    const uint64_t pdpt_entry =
        pdpt->entries[parts.pdpt_index];

    CHECK(
        pdpt_entry &
        static_cast<uint64_t>(PageFlags::Present)
    );

    CHECK(
        pdpt_entry &
        static_cast<uint64_t>(PageFlags::Writable)
    );

    auto* pd =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pdpt_entry)
            )
        );


    // --------------------------------------------------------
    // PD
    // --------------------------------------------------------

    const uint64_t pd_entry =
        pd->entries[parts.pd_index];

    CHECK(
        pd_entry &
        static_cast<uint64_t>(PageFlags::Present)
    );

    CHECK(
        pd_entry &
        static_cast<uint64_t>(PageFlags::Writable)
    );

    auto* pt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pd_entry)
            )
        );


    // --------------------------------------------------------
    // PT / leaf
    // --------------------------------------------------------

    const uint64_t pt_entry =
        pt->entries[parts.pt_index];

    CHECK(
        pt_entry &
        static_cast<uint64_t>(PageFlags::Present)
    );

    CHECK(
        pt_entry &
        static_cast<uint64_t>(PageFlags::Writable)
    );

    CHECK(
        page_entry_address(pt_entry) == phys
    );
}


// ============================================================
// map_page() reuses existing hierarchy
// ============================================================

TEST(map_page_reuses_existing_page_tables)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }


    // --------------------------------------------------------
    // First mapping
    // --------------------------------------------------------

    CHECK(
        map_page(
            pml4,
            0x40000000ull,
            0x5000ull,
            PageFlags::Writable
        )
    );

    const VirtualAddress first =
        decompose_address(0x40000000ull);


    const uint64_t first_pdpt_phys =
        page_entry_address(
            pml4->entries[first.pml4_index]
        );

    auto* pdpt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(first_pdpt_phys)
        );


    const uint64_t first_pd_phys =
        page_entry_address(
            pdpt->entries[first.pdpt_index]
        );

    auto* pd =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(first_pd_phys)
        );


    const uint64_t first_pt_phys =
        page_entry_address(
            pd->entries[first.pd_index]
        );


    // --------------------------------------------------------
    // Second mapping
    //
    // 0x40000000 and 0x40001000 differ only in PT index.
    // Therefore they must use the same PT.
    // --------------------------------------------------------

    CHECK(
        map_page(
            pml4,
            0x40001000ull,
            0x6000ull,
            PageFlags::Writable
        )
    );

    const VirtualAddress second =
        decompose_address(0x40001000ull);


    const uint64_t second_pt_phys =
        page_entry_address(
            pd->entries[second.pd_index]
        );

    CHECK(second_pt_phys == first_pt_phys);


    // --------------------------------------------------------
    // Verify both leaf entries
    // --------------------------------------------------------

    auto* pt =
        reinterpret_cast<const PageTable*>(
            physical_to_virtual(first_pt_phys)
        );

    CHECK(
        page_entry_address(
            pt->entries[first.pt_index]
        ) == 0x5000
    );

    CHECK(
        page_entry_address(
            pt->entries[second.pt_index]
        ) == 0x6000
    );
}


// ============================================================
// map_page() validation
// ============================================================

TEST(map_page_rejects_null_pml4)
{
    CHECK(
        !map_page(
            nullptr,
            0x40000000ull,
            0x5000ull,
            PageFlags::Writable
        )
    );
}


TEST(map_page_rejects_non_canonical_virtual_address)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    CHECK(
        !map_page(
            pml4,
            0x0000800000000000ull,
            0x5000ull,
            PageFlags::Writable
        )
    );
}


TEST(map_page_rejects_unaligned_virtual_address)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    CHECK(
        !map_page(
            pml4,
            0x40000001ull,
            0x5000ull,
            PageFlags::Writable
        )
    );
}


TEST(map_page_rejects_unaligned_physical_address)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    CHECK(
        !map_page(
            pml4,
            0x40000000ull,
            0x5001ull,
            PageFlags::Writable
        )
    );
}


TEST(map_page_rejects_huge_flag)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    CHECK(
        !map_page(
            pml4,
            0x40000000ull,
            0x5000ull,
            PageFlags::Huge
        )
    );
}


// ============================================================
// map_page() permission propagation
// ============================================================

TEST(map_page_propagates_user_permission)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    const uint64_t virt =
        0x40000000ull;

    const uint64_t phys =
        0x5000ull;

    CHECK(
        map_page(
            pml4,
            virt,
            phys,
            PageFlags::Writable |
            PageFlags::User
        )
    );

    const VirtualAddress parts =
        decompose_address(virt);


    // PML4 must have User.
    const uint64_t pml4_entry =
        pml4->entries[parts.pml4_index];

    CHECK(
        pml4_entry &
        static_cast<uint64_t>(PageFlags::User)
    );


    // PDPT must have User.
    auto* pdpt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pml4_entry)
            )
        );

    const uint64_t pdpt_entry =
        pdpt->entries[parts.pdpt_index];

    CHECK(
        pdpt_entry &
        static_cast<uint64_t>(PageFlags::User)
    );


    // PD must have User.
    auto* pd =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pdpt_entry)
            )
        );

    const uint64_t pd_entry =
        pd->entries[parts.pd_index];

    CHECK(
        pd_entry &
        static_cast<uint64_t>(PageFlags::User)
    );


    // Leaf must have User.
    auto* pt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pd_entry)
            )
        );

    const uint64_t pt_entry =
        pt->entries[parts.pt_index];

    CHECK(
        pt_entry &
        static_cast<uint64_t>(PageFlags::User)
    );
}


TEST(map_page_does_not_propagate_nx)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    const uint64_t virt =
        0x40000000ull;

    const uint64_t phys =
        0x5000ull;

    CHECK(
        map_page(
            pml4,
            virt,
            phys,
            PageFlags::Writable |
            PageFlags::NoExecute
        )
    );

    const VirtualAddress parts =
        decompose_address(virt);


    // --------------------------------------------------------
    // PML4 must NOT have NX.
    // --------------------------------------------------------

    const uint64_t pml4_entry =
        pml4->entries[parts.pml4_index];

    CHECK(
        (pml4_entry &
         static_cast<uint64_t>(PageFlags::NoExecute)) == 0
    );


    // --------------------------------------------------------
    // PDPT must NOT have NX.
    // --------------------------------------------------------

    auto* pdpt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pml4_entry)
            )
        );

    const uint64_t pdpt_entry =
        pdpt->entries[parts.pdpt_index];

    CHECK(
        (pdpt_entry &
         static_cast<uint64_t>(PageFlags::NoExecute)) == 0
    );


    // --------------------------------------------------------
    // PD must NOT have NX.
    // --------------------------------------------------------

    auto* pd =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pdpt_entry)
            )
        );

    const uint64_t pd_entry =
        pd->entries[parts.pd_index];

    CHECK(
        (pd_entry &
         static_cast<uint64_t>(PageFlags::NoExecute)) == 0
    );


    // --------------------------------------------------------
    // Leaf MUST have NX.
    // --------------------------------------------------------

    auto* pt =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(
                page_entry_address(pd_entry)
            )
        );

    const uint64_t pt_entry =
        pt->entries[parts.pt_index];

    CHECK(
        pt_entry &
        static_cast<uint64_t>(PageFlags::NoExecute)
    );
}


// ============================================================
// map_page() duplicate mapping
// ============================================================

TEST(map_page_rejects_existing_mapping)
{
    init(
        regions,
        sizeof(regions) / sizeof(regions[0]),
        hhdm
    );

    const uint64_t pml4_phys =
        alloc_frame();

    CHECK(pml4_phys != UINT64_MAX);

    auto* pml4 =
        reinterpret_cast<PageTable*>(
            physical_to_virtual(pml4_phys)
        );

    for (size_t i = 0; i < kPageTableEntries; ++i) {
        pml4->entries[i] = 0;
    }

    const uint64_t virt =
        0x40000000ull;

    CHECK(
        map_page(
            pml4,
            virt,
            0x5000ull,
            PageFlags::Writable
        )
    );

    CHECK(
        !map_page(
            pml4,
            virt,
            0x6000ull,
            PageFlags::Writable
        )
    );
}

