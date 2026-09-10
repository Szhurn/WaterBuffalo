// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include "arch/x86_64/gdt.hpp"

namespace {

// The lgdt operand: a 16-bit limit followed by a 64-bit base.
//
// Packing is required; the natural alignment of the base member would
// otherwise introduce six bytes of padding and the instruction would read a
// malformed address.
struct GdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static_assert(sizeof(GdtPointer) == 10);

// Entry order is fixed by the selector constants in gdt.hpp and cannot be
// changed independently of them.
//
// The table must be writable. Loading a selector causes the processor to set
// the Accessed bit in the corresponding descriptor, and ltr sets the Busy bit
// in the TSS descriptor; placing the table in .rodata faults on the first
// segment load. The initialisers are still compile-time constants, so this
// costs an entry in .data rather than any runtime construction.
arch::gdt::Descriptor gdt[] = {
    arch::gdt::null_descriptor,
    arch::gdt::kernel_code,
    arch::gdt::kernel_data,
    arch::gdt::user_data,
    arch::gdt::user_code,
};

// limit is the offset of the last valid byte, hence size - 1.
const GdtPointer gdt_pointer = {
    sizeof(gdt) - 1,
    reinterpret_cast<uint64_t>(&gdt),
};

}  // namespace

// Replaces the table installed by the bootloader.
//
// Limine's descriptor tables reside in memory reported as
// bootloader-reclaimable; once that region is handed to the physical
// allocator, continuing to rely on them would leave the CPU reading structures
// that may be reallocated.
void arch::gdt::init() {
    // lgdt records the table's location only. Segment registers continue to
    // use the descriptors cached in their hidden registers at the time they
    // were last loaded, so each must be reloaded for the new table to take
    // effect.
    asm volatile("lgdt %0" : : "m"(gdt_pointer) : "memory");

    // CS cannot be written directly; changing it requires a far transfer so
    // that CS and RIP are updated together. A far return pops RIP and then CS,
    // so pushing the pair and executing lretq resumes at the following label
    // under the new code segment.
    //
    // The data segments have no such restriction. SS is included because
    // interrupt delivery requires a valid selector there. Loading FS or GS
    // clears the corresponding base MSR, which will need revisiting when
    // per-CPU data is introduced.
    asm volatile(
        "pushq %[code]              \n\t"
        "leaq  1f(%%rip), %%rax     \n\t"
        "pushq %%rax                \n\t"
        "lretq                      \n\t"
        "1:                         \n\t"
        "movw  %[data], %%ax        \n\t"
        "movw  %%ax, %%ds           \n\t"
        "movw  %%ax, %%es           \n\t"
        "movw  %%ax, %%fs           \n\t"
        "movw  %%ax, %%gs           \n\t"
        "movw  %%ax, %%ss           \n\t"
        :
        : [code] "i"(static_cast<uint64_t>(kKernelCodeSelector)),
          [data] "i"(static_cast<uint16_t>(kKernelDataSelector))
        : "rax", "memory");
}
