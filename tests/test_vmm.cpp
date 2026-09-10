#include "test.hpp"
#include "mm/vmm.hpp"

using namespace mm;


// ============================================================
// Page table layout
// ============================================================

TEST(page_table_is_one_page)
{
    CHECK(sizeof(PageTable) == 4096);
    CHECK(kPageTableEntries == 512);
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

