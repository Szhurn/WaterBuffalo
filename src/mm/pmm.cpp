// Copyright (c) 2026 Hunter Shurniak. All rights reserved

#include <stdint.h>
#include <stddef.h>

#include "pmm.hpp"
#include "lib/print.hpp"

namespace mm {

static constexpr uint64_t kPageSize = 4096;
static constexpr uint64_t kWordBits = 64;

static uint64_t* bitmap = nullptr;

static uint64_t bitmap_phys = 0;
static uint64_t bitmap_words = 0;
static uint64_t bitmap_bytes = 0;

static uint64_t frame_count = 0;
static uint64_t free_frame_count = 0;


// ============================================================
// Helpers
// ============================================================

static const char* region_type_name(RegionType type) {
    switch (type) {
        case RegionType::Usable:
            return "USABLE";

        case RegionType::Reserved:
            return "RESERVED";

        case RegionType::AcpiReclaimable:
            return "ACPI_RECLAIMABLE";

        case RegionType::AcpiNvs:
            return "ACPI_NVS";

        case RegionType::Bad:
            return "BAD_MEMORY";

        case RegionType::BootloaderReclaimable:
            return "BOOTLOADER_RECLAIMABLE";

        case RegionType::KernelAndModules:
            return "KERNEL_AND_MODULES";

        case RegionType::Framebuffer:
            return "FRAMEBUFFER";
    }

    return "UNKNOWN";
}


static bool frame_valid(uint64_t frame) {
    return frame < frame_count;
}


static void set_frame_used(uint64_t frame) {
    if (!frame_valid(frame)) {
        return;
    }

    const uint64_t word = frame / kWordBits;
    const uint64_t bit = frame % kWordBits;

    bitmap[word] |= uint64_t{1} << bit;
}


static void set_frame_free(uint64_t frame) {
    if (!frame_valid(frame)) {
        return;
    }

    const uint64_t word = frame / kWordBits;
    const uint64_t bit = frame % kWordBits;

    bitmap[word] &= ~(uint64_t{1} << bit);
}


// ============================================================
// Initialization
// ============================================================

void init(
    const MemoryRegion* regions,
    size_t count,
    uint64_t hhdm_offset
) {
    uint64_t highest_usable_end = 0;
    uint64_t usable_bytes = 0;

    print::kprintf("\nMemory map:\n");

    // --------------------------------------------------------
    // Print map and find highest usable address.
    // --------------------------------------------------------

    for (size_t i = 0; i < count; ++i) {
        const MemoryRegion& region = regions[i];

        print::kprintf(
            "  %p - %p  length=%lu  type=%s\n",
            reinterpret_cast<void*>(region.base),
            reinterpret_cast<void*>(
                region.base + region.length
            ),
            region.length,
            region_type_name(region.type)
        );

        if (region.type == RegionType::Usable) {
            usable_bytes += region.length;

            const uint64_t end =
                region.base + region.length;

            if (end > highest_usable_end) {
                highest_usable_end = end;
            }
        }
    }

    print::kprintf(
        "Total usable memory: %lu bytes\n",
        usable_bytes
    );

    if (highest_usable_end == 0) {
        print::kprintf(
            "fatal: no usable memory\n"
        );

        for (;;) {
            asm volatile ("cli; hlt");
        }
    }

    // --------------------------------------------------------
    // Calculate frame count.
    // --------------------------------------------------------

    frame_count =
        (highest_usable_end + kPageSize - 1) / kPageSize;

    // --------------------------------------------------------
    // Calculate bitmap size.
    //
    // One bit per physical frame.
    //
    // Calculate WORDS first so bitmap accesses and storage
    // always agree.
    // --------------------------------------------------------

    bitmap_words =
        (frame_count + kWordBits - 1) / kWordBits;

    bitmap_bytes =
        bitmap_words * sizeof(uint64_t);

    const uint64_t bitmap_pages =
        (bitmap_bytes + kPageSize - 1) / kPageSize;

    // --------------------------------------------------------
    // Find a usable region large enough for the bitmap.
    // --------------------------------------------------------

    bool found_bitmap = false;

    for (size_t i = 0; i < count; ++i) {
        const MemoryRegion& region = regions[i];

        if (region.type != RegionType::Usable) {
            continue;
        }

        const uint64_t bitmap_storage_bytes =
            bitmap_pages * kPageSize;

        if (region.length >= bitmap_storage_bytes) {
            bitmap_phys = region.base;
            found_bitmap = true;
            break;
        }
    }

    if (!found_bitmap) {
        print::kprintf(
            "fatal: no usable region large enough for bitmap\n"
        );

        for (;;) {
            asm volatile ("cli; hlt");
        }
    }

    // --------------------------------------------------------
    // Map bitmap through the HHDM.
    // --------------------------------------------------------

    bitmap = reinterpret_cast<uint64_t*>(
        hhdm_offset + bitmap_phys
    );

    print::kprintf(
        "PMM:\n"
        "  highest usable: %p\n"
        "  frame count:    %lu\n"
        "  bitmap phys:    %p\n"
        "  bitmap virt:    %p\n"
        "  bitmap bytes:   %lu\n"
        "  bitmap words:   %lu\n"
        "  bitmap pages:   %lu\n",
        reinterpret_cast<void*>(highest_usable_end),
        frame_count,
        reinterpret_cast<void*>(bitmap_phys),
        reinterpret_cast<void*>(hhdm_offset + bitmap_phys),
        bitmap_bytes,
        bitmap_words,
        bitmap_pages
    );

    // --------------------------------------------------------
    // Phase 1:
    // Everything is USED.
    // --------------------------------------------------------

    for (uint64_t i = 0; i < bitmap_words; ++i) {
        bitmap[i] = UINT64_MAX;
    }

    // --------------------------------------------------------
    // Phase 2:
    // USABLE physical frames become FREE.
    // --------------------------------------------------------

    free_frame_count = 0;

    for (size_t i = 0; i < count; ++i) {
        const MemoryRegion& region = regions[i];

        if (region.type != RegionType::Usable) {
            continue;
        }

        // Round the beginning UP to the next complete frame.
        const uint64_t first_frame =
            (region.base + kPageSize - 1) / kPageSize;

        // Round the end DOWN.
        const uint64_t frame_end =
            (region.base + region.length) / kPageSize;

        for (uint64_t frame = first_frame;
             frame < frame_end;
             ++frame) {

            if (!frame_valid(frame)) {
                continue;
            }

            set_frame_free(frame);
            ++free_frame_count;
        }
    }

    // --------------------------------------------------------
    // Phase 3:
    // The bitmap's own physical frames are USED.
    // --------------------------------------------------------

    const uint64_t bitmap_first_frame =
        bitmap_phys / kPageSize;

    const uint64_t bitmap_last_frame =
        bitmap_first_frame + bitmap_pages;

    for (uint64_t frame = bitmap_first_frame;
         frame < bitmap_last_frame;
         ++frame) {

        if (!frame_valid(frame)) {
            continue;
        }

        // Only decrement if it was actually free.
        const uint64_t word = frame / kWordBits;
        const uint64_t bit = frame % kWordBits;

        if ((bitmap[word] & (uint64_t{1} << bit)) == 0) {
            --free_frame_count;
        }

        set_frame_used(frame);
    }

    print::kprintf(
        "  free frames:    %lu\n",
        free_frame_count
    );

    print::kprintf(
        "  free memory:    %lu bytes\n",
        free_frame_count * kPageSize
    );

    print::kprintf(
        "  bitmap initialized\n"
    );
}


// ============================================================
// Allocate one physical frame
// ============================================================

uint64_t alloc_frame() {

    for (uint64_t word_index = 0;
         word_index < bitmap_words;
         ++word_index) {

        const uint64_t word = bitmap[word_index];

        // Every bit is already used.
        if (word == UINT64_MAX) {
            continue;
        }

        // Invert so FREE bits become 1.
        const uint64_t free_bits = ~word;

        // Find the first free bit.
        const uint64_t bit =
            static_cast<uint64_t>(
                __builtin_ctzll(free_bits)
            );

        const uint64_t frame =
            word_index * kWordBits + bit;

        // The final bitmap word may contain padding bits.
        if (!frame_valid(frame)) {
            continue;
        }

        set_frame_used(frame);
        --free_frame_count;

        return frame * kPageSize;
    }

    // No physical frames available.
    return 0;
}


// ============================================================
// Free one physical frame
// ============================================================

void free_frame(uint64_t address) {

        if ((address % kPageSize) != 0) {
        print::kprintf(
            "fatal: pmm::free_frame: misaligned address %p\n",
            address
        );
        return;
    }

    const uint64_t frame = address / kPageSize;

    if (!frame_valid(frame)) {
        print::kprintf(
            "fatal: pmm::free_frame: frame out of range: %lu\n",
            frame
        );
        return;
    }

    const uint64_t word = frame / kWordBits;
    const uint64_t bit = frame % kWordBits;
    const uint64_t mask = uint64_t{1} << bit;

    if ((bitmap[word] & mask) == 0) {
        print::kprintf(
            "fatal: pmm::free_frame: double free: %p\n",
            address
        );
        return;
    }

    bitmap[word] &= ~mask;
    ++free_frame_count;
}

}

