// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

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
// Linker symbols
// ============================================================

extern "C" char __text_start[];
extern "C" char __text_end[];

extern "C" char __rodata_start[];
extern "C" char __rodata_end[];

extern "C" char __data_start[];
extern "C" char __data_end[];


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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request
    kernel_address_request = {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };


// ============================================================
// Halt and catch fire
// ============================================================

[[noreturn]]
static void hcf()
{
    asm volatile ("cli");

    for (;;) {
        asm volatile ("hlt");
    }
}


// ============================================================
// Kernel entry point
// ============================================================

extern "C" void kmain()
{
    gdt::init();

    idt::init();

    // Initialize serial so early boot failures are visible.
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
    // Memory map / HHDM / kernel address
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

    if (kernel_address_request.response == nullptr) {
        print::kprintf(
            "fatal: no kernel address response from bootloader\n"
        );

        hcf();
    }


    // ========================================================
    // Translate Limine memory map into PMM types
    // ========================================================

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
    // Kernel virtual / physical address information
    // ========================================================

    const uint64_t kernel_virtual_base =
        kernel_address_request.response->virtual_base;

    const uint64_t kernel_physical_base =
        kernel_address_request.response->physical_base;


    print::kprintf(
        "kernel virtual base:  %p\n",
        kernel_virtual_base
    );

    print::kprintf(
        "kernel physical base: %p\n",
        kernel_physical_base
    );


    // ========================================================
    // Verify kernel physical base belongs to kernel memory
    // ========================================================

    bool kernel_physical_base_valid = false;

    for (size_t i = 0;
         i < memmap_request.response->entry_count;
         ++i) {

        const mm::MemoryRegion& region = regions[i];

        if (region.type != mm::RegionType::KernelAndModules) {
            continue;
        }

        const uint64_t region_end =
            region.base + region.length;

        if (kernel_physical_base >= region.base &&
            kernel_physical_base < region_end) {

            kernel_physical_base_valid = true;
            break;
        }
    }

    if (!kernel_physical_base_valid) {
        print::kprintf(
            "fatal: kernel physical base is not inside "
            "KERNEL_AND_MODULES region\n"
        );

        hcf();
    }

    print::kprintf(
        "kernel physical base verified\n"
    );


    // ========================================================
    // Kernel virtual -> physical translation
    // ========================================================

    const auto virtual_to_physical =
        [kernel_virtual_base, kernel_physical_base](
            uint64_t address
        ) -> uint64_t {

            return address
                - kernel_virtual_base
                + kernel_physical_base;
        };


    // ========================================================
    // Kernel segment ranges
    // ========================================================

    const uint64_t text_start =
        reinterpret_cast<uint64_t>(__text_start);

    const uint64_t text_end =
        reinterpret_cast<uint64_t>(__text_end);

    const uint64_t rodata_start =
        reinterpret_cast<uint64_t>(__rodata_start);

    const uint64_t rodata_end =
        reinterpret_cast<uint64_t>(__rodata_end);

    const uint64_t data_start =
        reinterpret_cast<uint64_t>(__data_start);

    const uint64_t data_end =
        reinterpret_cast<uint64_t>(__data_end);


    // ========================================================
    // Print kernel segment information
    // ========================================================

    print::kprintf(
        ".text:   virt=%p-%p size=%lu "
        "phys=%p-%p\n",
        text_start,
        text_end,
        text_end - text_start,
        virtual_to_physical(text_start),
        virtual_to_physical(text_end)
    );

    print::kprintf(
        ".rodata: virt=%p-%p size=%lu "
        "phys=%p-%p\n",
        rodata_start,
        rodata_end,
        rodata_end - rodata_start,
        virtual_to_physical(rodata_start),
        virtual_to_physical(rodata_end)
    );

    print::kprintf(
        ".data:   virt=%p-%p size=%lu "
        "phys=%p-%p\n",
        data_start,
        data_end,
        data_end - data_start,
        virtual_to_physical(data_start),
        virtual_to_physical(data_end)
    );


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
    struct limine_framebuffer* framebuffer =
        framebuffer_request.response->framebuffers[0];

    // Assume a 32-bit RGB framebuffer.
    auto* fb_ptr =
        static_cast<volatile uint32_t*>(framebuffer->address);


    for (size_t y = 0;
         y < framebuffer->height;
         y++) {

        for (size_t x = 0;
             x < framebuffer->width;
             x++) {

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