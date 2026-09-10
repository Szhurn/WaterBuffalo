# WaterBuffalo

A small x86_64 kernel written from scratch in freestanding C++20, booted by
[Limine](https://github.com/limine-bootloader/limine) over UEFI or legacy BIOS.

No libc, no libstdc++, no runtime. Just a linker script, a boot protocol, and
whatever the CPU does when you hand it control.

## Status

Boots via Limine to a framebuffer and serial console, installs its own
descriptor tables, and handles CPU exceptions with a full register dump.

- [x] **Boot** — Limine protocol requests, higher-half load, framebuffer output
- [x] **Serial + console** — 16550 UART, formatted printing (`kprintf`)
- [x] **CPU tables** — GDT, IDT, 32 exception handlers with register diagnostics
- [ ] **Memory** — physical frame allocator, 4-level paging, kernel heap
- [ ] **Scheduling** — timer interrupt, preemptive threads
- [ ] **Userspace** — TSS, ring 3, `syscall`/`sysret`, ELF loading

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

Kernel modules without hardware dependencies are compiled by the host
toolchain and run as an ordinary program, so logic can be verified without
building an image or booting. Currently covers the formatted-output layer and
the freestanding `mem*` implementations.

## Diagnostics

All 32 CPU exception vectors are handled. A fault prints the vector and its
mnemonic, the decoded error code, and the complete register state before
halting. Writing to a null pointer produces:

```
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
RIP:    0xffffffff80001fdf
CS:     0x8
RFLAGS: 0x46
RSP:    0xffff80001ff87fc0
SS:     0x10
RAX: 0x000000000000000a  RBX: 0x0000000000000c80  RCX: 0x0000000000000000
RDX: 0x00000000000003f8  RSI: 0x000000000004fbff  RDI: 0xffffffff8000314a
RBP: 0xffff80001ff87ff0  R8:  0x0000000000000320  R9:  0xffff8000fd000000
R10: 0x0000000000000500  R11: 0x0000000000000500  R12: 0x0000000000000500
R13: 0x0000000000000500  R14: 0xffff8000fd3e6c04  R15: 0x0000000000000320

System halted.
```

`Error code: 0x2` decodes as a write to a non-present page, and `CR2` confirms
the address. `CS` and `SS` are the kernel selectors from the GDT installed at
boot, and the cleared interrupt flag in `RFLAGS` reflects entry through an
interrupt gate rather than a trap gate.

## Layout

```
linker.lds              Memory layout. Higher-half at 0xffffffff80000000,
                        four PT_LOAD segments, KEEPs the Limine requests.
limine.conf             Boot menu entry and kernel path.
src/                    Kernel source.
tests/                  Host-executed unit tests.
third_party/limine/     Vendored Limine protocol header (0BSD).
limine/                 Bootloader binaries. Fetched, not committed.
build/                  Output. Not committed.
```

## How it boots

1. Firmware loads Limine from the ISO.
2. Limine scans the kernel image for the request magic bracketed by the
   start/end markers in `.limine_requests`, and fills in the responses.
3. It maps the kernel at `0xffffffff80000000`, sets up a stack and long mode
   with paging already enabled, then jumps to `kmain`.
4. `kmain` brings up the serial port, verifies the base revision was accepted,
   installs its own GDT and IDT, then takes the framebuffer from the response
   and draws.

The kernel never returns. There is nothing to return to.

## Notes

Base revision **3**. Limine v9.6.7 supports up to 3; revision 6 exists only in
the protocol repository's `trunk` and is rejected by every released bootloader.
Requesting it fails silently — the check fails and the kernel halts with a black
screen and nothing in the logs.

## Credits

- [Limine](https://github.com/limine-bootloader/limine) — bootloader and boot
  protocol, 0BSD.

## Licence

Copyright (c) 2026 Hunter Shurniak. All rights reserved.

This source is published for review and evaluation only. No permission is
granted to use, copy, modify or distribute it. See [LICENSE](LICENSE).

`third_party/` contains components under their own licences; the Limine
protocol header is 0BSD.
