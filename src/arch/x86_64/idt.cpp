#include <stdint.h>

#include "idt.hpp"
#include "gdt.hpp"

extern "C" const uint64_t isr_stub_table[];

namespace arch::idt {

static Entry idt[256];

static inline void lidt(const Pointer& pointer) {
    asm volatile(
        "lidt %0"
        :
        : "m"(pointer)
        : "memory"
    );
}

void init() {
    for (uint64_t i = 0; i < 256; ++i) {
        idt[i] = {};
    }

    for (uint64_t i = 0; i < 32; ++i) {
        idt[i] = make_entry(
            isr_stub_table[i],
            gdt::kKernelCodeSelector,
            0,
            0x8E
        );
    }

    Pointer pointer{
        .limit = static_cast<uint16_t>(sizeof(idt) - 1),
        .base  = reinterpret_cast<uint64_t>(&idt)
    };

    lidt(pointer);
}

}