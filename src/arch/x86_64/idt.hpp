// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#pragma once
#include <stdint.h>

namespace arch::idt {

    void init();

    struct Entry {
        uint16_t offset_low;   
        uint16_t selector; 
        uint8_t  ist;    
        uint8_t  type_attr; 
        uint16_t offset_mid;  
        uint32_t offset_high; 
        uint32_t zero;
} __attribute__((packed));

static_assert(sizeof(Entry) == 16);

constexpr Entry make_entry(
    uint64_t handler,
    uint16_t selector,
    uint8_t ist,
    uint8_t type_attr
) {
    return Entry{
        static_cast<uint16_t>(handler & 0xFFFF),
        selector,
        static_cast<uint8_t>(ist & 0x07),
        type_attr,
        static_cast<uint16_t>((handler >> 16) & 0xFFFF),
        static_cast<uint32_t>((handler >> 32) & 0xFFFFFFFF),
        0
    };
}

struct Pointer {
    uint16_t limit;
    uint64_t base;
}__attribute__((packed));

static_assert(sizeof(Pointer) == 10);

}// namespace arch::idt