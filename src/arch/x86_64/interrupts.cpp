#include <stdint.h>

#include "interrupts.hpp"
#include "serial.hpp"
#include "lib/print.hpp"

namespace arch::interrupts {

static const char* exception_name(uint64_t vector) {
    switch (vector) {
        case 0:  return "#DE Divide Error";
        case 1:  return "#DB Debug";
        case 2:  return "NMI";
        case 3:  return "#BP Breakpoint";
        case 4:  return "#OF Overflow";
        case 5:  return "#BR Bound Range";
        case 6:  return "#UD Invalid Opcode";
        case 7:  return "#NM Device Not Available";
        case 8:  return "#DF Double Fault";
        case 9:  return "Reserved";
        case 10: return "#TS Invalid TSS";
        case 11: return "#NP Segment Not Present";
        case 12: return "#SS Stack-Segment Fault";
        case 13: return "#GP General Protection";
        case 14: return "#PF Page Fault";
        case 15: return "Reserved";
        case 16: return "#MF x87 Floating-Point";
        case 17: return "#AC Alignment Check";
        case 18: return "#MC Machine Check";
        case 19: return "#XM SIMD Floating-Point";
        case 20: return "Reserved";
        case 21: return "#CP Control Protection";
        case 22: return "Reserved";
        case 23: return "Reserved";
        case 24: return "Reserved";
        case 25: return "Reserved";
        case 26: return "Reserved";
        case 27: return "Reserved";
        case 28: return "Reserved";
        case 29: return "#VC VMM Communication";
        case 30: return "#SX Security";
        case 31: return "Reserved";
        default: return "Unknown";
    }
}

extern "C" void isr_handler(InterruptFrame* frame) {
    serial::write("\n\n*** EXCEPTION ***\n");

    serial::write("Vector: ");
    print::kprintf("%lu\n", frame->vector);

    serial::write("Exception: ");
    serial::write(exception_name(frame->vector));
    serial::write("\n");

    print::kprintf("Error code: %lx\n", frame->error_code);

    uint64_t cr2 = 0;

    if (frame->vector == 14) {
        asm volatile(
            "mov %%cr2, %0"
            : "=r"(cr2)
        );

       
        print::kprintf("  Present: %u\n",
                        (frame->error_code >> 0) & 1);
        print::kprintf("  Write: %u\n",
                        (frame->error_code >> 1) & 1);
        print::kprintf("  User: %u\n",
                        (frame->error_code >> 2) & 1);
        print::kprintf("  Reserved-bit violation: %u\n",
                        (frame->error_code >> 3) & 1);
        print::kprintf("  Instruction fetch: %u\n",
                        (frame->error_code >> 4) & 1);
    }

    print::kprintf("CR2:    %p\n",
                   reinterpret_cast<void*>(cr2));
    print::kprintf("RIP:    %p\n",
                   reinterpret_cast<void*>(frame->rip));
    print::kprintf("CS:     %lx\n", frame->cs);
    print::kprintf("RFLAGS: %lx\n", frame->rflags);
    print::kprintf("RSP:    %p\n",
                   reinterpret_cast<void*>(frame->rsp));
    print::kprintf("SS:     %lx\n", frame->ss);

        //GPR Section

    print::kprintf("RAX: %p  RBX: %p  RCX: %p\n",
                   reinterpret_cast<void*>(frame->rax),
                   reinterpret_cast<void*>(frame->rbx),
                   reinterpret_cast<void*>(frame->rcx));

    print::kprintf("RDX: %p  RSI: %p  RDI: %p\n",
                   reinterpret_cast<void*>(frame->rdx),
                   reinterpret_cast<void*>(frame->rsi),
                   reinterpret_cast<void*>(frame->rdi));

    print::kprintf("RBP: %p  R8:  %p  R9:  %p\n",
                   reinterpret_cast<void*>(frame->rbp),
                   reinterpret_cast<void*>(frame->r8),
                   reinterpret_cast<void*>(frame->r9));

    print::kprintf("R10: %p  R11: %p  R12: %p\n",
                   reinterpret_cast<void*>(frame->r10),
                   reinterpret_cast<void*>(frame->r11),
                   reinterpret_cast<void*>(frame->r12));

    print::kprintf("R13: %p  R14: %p  R15: %p\n",
                   reinterpret_cast<void*>(frame->r13),
                   reinterpret_cast<void*>(frame->r14),
                   reinterpret_cast<void*>(frame->r15));

    serial::write("\nSystem halted.\n");

    for (;;) {
        asm volatile("cli; hlt");
    }
}

}