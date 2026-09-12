# WaterBuffalo

A small x86_64 kernel written from scratch in freestanding C++20, booted by
[Limine](https://github.com/limine-bootloader/limine) over UEFI or legacy BIOS.

No libc, no libstdc++, no runtime. Just a linker script, a boot protocol, and
whatever the CPU does when you hand it control.

## Status

Boots via Limine, installs its own descriptor tables, handles CPU exceptions
with a full register dump, and manages physical and virtual memory. It builds
its own four-level page tables and switches to them, enforcing W^X on its own
image: `.text` is executable and read-only, `.rodata` and `.data` are not
executable.

- [x] **Boot** — Limine protocol requests, higher-half load, framebuffer output
- [x] **Serial + console** — 16550 UART, formatted printing (`kprintf`)
- [x] **CPU tables** — GDT, IDT, 32 exception handlers with register diagnostics
- [x] **Physical memory** — bitmap frame allocator over the UEFI memory map
- [x] **Virtual memory** — 4-level paging, 4 KiB and 2 MiB mappings, own CR3
- [ ] **Kernel heap** — `kmalloc` / `kfree`
- [ ] **Scheduling** — timer interrupt, preemptive threads
- [ ] **Userspace** — TSS, ring 3, `syscall`/`sysret`, ELF loading

43 tests, 1446 assertions, run on the host in under a second.

## Memory model

The kernel is linked at `0xffffffff80000000`, the top 2 GiB of the address
space, which is what `-mcmodel=kernel` assumes: symbols are then reachable with
32-bit signed displacements rather than full 64-bit immediates. Limine loads it
at a physical address chosen at boot and reports both, so the physical address
of any kernel virtual address is

```
phys = virt - virtual_base + physical_base
```

All of physical memory is mapped a second time at `0xffff800000000000`, the
higher-half direct map. This exists because page tables store physical
addresses — the CPU is mid-translation and has nothing else to use — but the
kernel can only dereference virtual ones. Adding the offset gives a usable
pointer to any frame, which is how the allocator reaches its bitmap and how the
page-table code reaches the tables it is building.

Two consequences fall out of that. The direct map must be the first thing
mapped in a new address space, since everything mapped afterwards allocates
page tables that are reached through it. And the memory manager becomes
testable on the host: substitute an array for physical memory, compute an
offset that makes the arithmetic land inside it, and the same code runs as an
ordinary program.

```
0xffffffff80000000   kernel image      .text  r-x
                                       .rodata r--
                                       .data   rw-  (and .bss)

0xffff800000000000   direct map        rw-, 2 MiB pages
                                       spans every region the memory map
                                       describes, including the gaps
```

Mapping the whole span rather than each region individually costs a few
megabytes of page tables — the framebuffer and MMIO regions sit far above RAM —
and removes the case where two regions share one 2 MiB page.

Permissions are the intersection of every level of the walk, so intermediate
entries carry `Present | Writable` and never `NoExecute`; the leaf decides.
`EFER.NXE` is set before any `NoExecute` bit is written, because with it clear
bit 63 is a reserved bit and faults look nothing like permission errors.

## Requirements

Linux (or WSL2). On Debian/Ubuntu:

```bash
sudo apt install clang lld llvm make git g++ gdb xorriso qemu-system-x86
```

Clang is used as a cross-compiler via `--target=x86_64-unknown-none-elf`, so no
GCC cross-toolchain build is needed. `git` is required because the bootloader is
fetched on first build, and `g++` builds the host-side tests.

## Building

```bash
make          # kernel ELF
make iso      # bootable ISO
make clean    # remove build output
```

`make` fetches and builds the bootloader on first run, so a fresh clone needs
nothing but the packages above.

Override the toolchain from the command line if you want a GCC cross-compiler
instead of clang:

```bash
make CXX=x86_64-elf-g++ LD=x86_64-elf-ld
```

## Running

```bash
make run      # QEMU, serial on stdio, log to build/qemu.log
make debug    # QEMU stopped, waiting for gdb on :1234
make gdb      # attach, load symbols, break on kmain
```

## Testing

```bash
make test
```

Kernel modules with no hardware dependencies are compiled by the host toolchain
and run as an ordinary program, so logic is verified without building an image
or booting. This covers the formatted-output layer, the freestanding `mem*`
implementations, the frame allocator, and the whole paging layer — page-table
encoding, address decomposition, canonical-address rules, mapping, and
translation, including ranges that cross a page-table boundary.

`-fno-builtin` prevents the compiler substituting its own `mem*`, which would
leave those tests exercising the compiler rather than `src/lib/string.cpp`.

## Layout

```
linker.lds                  Memory layout. Higher-half at 0xffffffff80000000,
                            four PT_LOAD segments, KEEPs the Limine requests.
limine.conf                 Boot menu entry and kernel path.
src/main.cpp                Boot sequence.
src/arch/x86_64/            GDT, IDT, ISR stubs, serial, port and MSR access.
src/lib/                    Freestanding printing and mem* routines.
src/mm/                     Physical frame allocator and paging.
tests/                      Host-executed unit tests.
third_party/limine/         Vendored Limine protocol header (0BSD).
limine/                     Bootloader binaries. Fetched, not committed.
build/                      Output. Not committed.
```

## How it boots

1. Firmware loads Limine from the ISO.
2. Limine scans the kernel image for the request magic bracketed by the
   start/end markers in `.limine_requests`, and fills in the responses.
3. It maps the kernel at `0xffffffff80000000`, sets up a stack and long mode
   with paging already enabled, then jumps to `kmain`.
4. `kmain` brings up the serial port, verifies the base revision was accepted,
   and installs its own GDT and IDT.
5. The Limine memory map is translated into the allocator's own types and the
   frame allocator is initialised over it.
6. A new address space is built: direct map first, then the kernel image a
   segment at a time with the permissions each needs.
7. Every mapping the switch depends on is checked with `translate` and printed.
   A wrong value here is a diagnosable bug; the same wrong value after CR3 is a
   triple fault with no output.
8. CR3 is loaded. The kernel no longer depends on the bootloader's page tables.

The kernel never returns. There is nothing to return to.

## Diagnostics

All 32 CPU exception vectors are handled. A fault prints the vector and its
mnemonic, the decoded error code, and the complete register state before
halting.

Writing to a null pointer, after the switch to the kernel's own page tables:

```
loading cr3...
cr3 loaded

*** EXCEPTION ***
Vector: 14
Exception: #PF Page Fault
Error code: 0x2
  Present: 0
  Write: 1
  User: 0
  Reserved-bit violation: 0
  Instruction fetch: 0
CR2:    0x0000000000000000
RIP:    0xffffffff800023ef
CS:     0x8
RFLAGS: 0x46
RSP:    0xffff80001ff86f70
SS:     0x10
RAX: 0x000000000000000a  RBX: 0xffff8000fd3e6c04  RCX: 0x0000000000000000
RDX: 0x00000000000003f8  RSI: 0x000000000004fbff  RDI: 0xffffffff80004222
RBP: 0xffff80001ff86ff0  R8:  0xffff8000fd000000  R9:  0x0000000000000500
R10: 0x0000000000000500  R11: 0x0000000000000c80  R12: 0x0000000000000500
R13: 0x0000000000000500  R14: 0x0000000000000320  R15: 0xffff80001ff8b000

System halted.
```

`Error code: 0x2` decodes as a write to a non-present page, and `CR2` confirms
the address. That the dump appears at all is itself the check on the preceding
step: reaching it required `.text` executable, `.data` and `.bss` mapped for the
descriptor tables, the stack reachable through the direct map, and `.rodata`
readable for these strings — all under page tables the kernel built itself.

`R8` holds `0xffff8000fd000000`, the framebuffer seen through the direct map.

## Notes

Base revision **3**. Limine v9.6.7 supports up to 3; revision 6 exists only in
the protocol repository's `trunk` and is rejected by every released bootloader.
Requesting it fails silently — the check fails and the kernel halts with a black
screen and nothing in the logs.

The GDT is not `constexpr`. A `constexpr` table lands in `.rodata`, and the CPU
writes the Accessed bit on the first segment load, so the table has to be
writable. `ltr` sets the Busy bit in the TSS descriptor for the same reason.

## Credits

- [Limine](https://github.com/limine-bootloader/limine) — bootloader and boot
  protocol, 0BSD.

## Licence

Copyright (c) 2026 Hunter Shurniak. All rights reserved.

This source is published for review and evaluation only. No permission is
granted to use, copy, modify or distribute it. See [LICENSE](LICENSE).

`third_party/` contains components under their own licences; the Limine
protocol header is 0BSD.
