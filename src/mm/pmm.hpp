// Copyright (c) 2026 Hunter Shurniak. All rights reserved

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace mm {

enum class RegionType {
    Usable,
    Reserved,
    AcpiReclaimable,
    AcpiNvs,
    Bad,
    BootloaderReclaimable,
    KernelAndModules,
    Framebuffer
};

struct MemoryRegion {
    uint64_t base;
    uint64_t length;
    RegionType type;
};

void init(
    const MemoryRegion* regions,
    size_t count,
    uint64_t hhdm_offset
);

uint64_t alloc_frame();

void free_frame(uint64_t address);

}