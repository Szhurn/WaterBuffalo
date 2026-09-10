// Copyright (c) 2026 Hunter Shurniak. All rights reserved

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "arch/x86_64/gdt.hpp"
#include "arch/x86_64/serial.hpp"
#include "lib/print.hpp"
#include "arch/x86_64/idt.hpp"
#include "mm/pmm.hpp"


namespace idt = arch::idt;
namespace gdt = arch::gdt;
namespace serial = arch::serial;


// ============================================================
// Limine requests
// ============================================================

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = nullptr
};


// ============================================================
// Halt and catch fire
// ============================================================

[[noreturn]]
static void hcf() {
    asm volatile ("cli");

    for (;;) {
        asm volatile ("hlt");
    }
}


// ============================================================
// Kernel entry point
// ============================================================

extern "C" void kmain() {

    gdt::init();

    idt::init();

    // Initialize serial first so even early boot failures are visible.
    serial::init();

    print::set_sink(serial::putc);

    serial::write("WaterBuffalo booting\n");


    // ========================================================
    // Limine base revision
    // ========================================================

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        print::kprintf(
            "fatal: bootloader rejected base revision\n"
        );

        hcf();
    }


    // ========================================================
    // Framebuffer
    // ========================================================

    if (framebuffer_request.response == nullptr ||
        framebuffer_request.response->framebuffer_count < 1) {

        print::kprintf(
            "fatal: no framebuffer from bootloader\n"
        );

        hcf();
    }

    serial::write("framebuffer acquired\n");


    // ========================================================
    // Memory map
    // ========================================================

    if (memmap_request.response == nullptr) {
        print::kprintf(
            "fatal: no memory map from bootloader\n"
        );

        hcf();
    }

    if (hhdm_request.response == nullptr) {
        print::kprintf(
            "fatal: no HHDM response from bootloader\n"
        );

        hcf();
    }


    // Translate the bootloader's memory map into the PMM's own types, so that
    // nothing under mm/ depends on the boot protocol.

    static mm::MemoryRegion regions[64];

    if (memmap_request.response->entry_count > 64) {
        print::kprintf(
            "fatal: memory map has too many entries\n"
        );

        hcf();
    }


    for (size_t i = 0;
         i < memmap_request.response->entry_count;
         ++i) {

        const auto* entry =
            memmap_request.response->entries[i];

        mm::RegionType type;

        switch (entry->type) {

            case LIMINE_MEMMAP_USABLE:
                type = mm::RegionType::Usable;
                break;

            case LIMINE_MEMMAP_RESERVED:
                type = mm::RegionType::Reserved;
                break;

            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
                type = mm::RegionType::AcpiReclaimable;
                break;

            case LIMINE_MEMMAP_ACPI_NVS:
                type = mm::RegionType::AcpiNvs;
                break;

            case LIMINE_MEMMAP_BAD_MEMORY:
                type = mm::RegionType::Bad;
                break;

            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
                type = mm::RegionType::BootloaderReclaimable;
                break;

            case LIMINE_MEMMAP_KERNEL_AND_MODULES:
                type = mm::RegionType::KernelAndModules;
                break;

            case LIMINE_MEMMAP_FRAMEBUFFER:
                type = mm::RegionType::Framebuffer;
                break;

            default:
                print::kprintf(
                    "fatal: unknown memory map type: %lu\n",
                    entry->type
                );

                hcf();
        }

        regions[i] = {
            .base = entry->base,
            .length = entry->length,
            .type = type
        };
    }


    // ========================================================
    // Physical memory manager
    // ========================================================

    mm::init(
        regions,
        memmap_request.response->entry_count,
        hhdm_request.response->offset
    );


    // ========================================================
    // Framebuffer drawing
    // ========================================================

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];

    // Assume a 32-bit RGB framebuffer.
    auto* fb_ptr =
        static_cast<volatile uint32_t*>(framebuffer->address);


    for (size_t y = 0; y < framebuffer->height; y++) {

        for (size_t x = 0; x < framebuffer->width; x++) {

            uint32_t nX =
                static_cast<uint32_t>(
                    x * 255 / framebuffer->width
                );

            uint32_t nY =
                static_cast<uint32_t>(
                    y * 255 / framebuffer->height
                );

            fb_ptr[
                y * (framebuffer->pitch / 4) + x
            ] = (nY << 8) | nX;
        }
    }


    serial::write("done\n");


    // ========================================================
    // Intentional page-fault test
    // ========================================================

    volatile int* p = nullptr;

    *p = 1;


    hcf();
}

