# WaterBuffalo

A small x86_64 kernel written from scratch in freestanding C++20, booted by
[Limine](https://github.com/limine-bootloader/limine) over UEFI or legacy BIOS.

No libc, no libstdc++, no runtime. Just a linker script, a boot protocol, and
whatever the CPU does when you hand it control.

## Status

Boots via Limine and draws a test gradient to the framebuffer.

- [x] **Boot** — Limine protocol requests, higher-half load, framebuffer output
- [ ] **Serial + console** — 16550 UART, bitmap font, formatted printing
- [ ] **CPU tables** — GDT with TSS, IDT, exception handlers
- [ ] **Memory** — physical frame allocator, 4-level paging, kernel heap
- [ ] **Scheduling** — timer interrupt, preemptive threads
- [ ] **Userspace** — ring 3, `syscall`/`sysret`, ELF loading

## Requirements

Linux (or WSL2). On Debian/Ubuntu:

```bash
sudo apt install clang lld llvm make xorriso qemu-system-x86
```

Clang is used as a cross-compiler via `--target=x86_64-unknown-none-elf`, so no
GCC cross-toolchain build is needed.

## Building

```bash
make          # kernel ELF
make iso      # bootable ISO
make run      # build and boot in QEMU
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

## Layout

```
linker.lds              Memory layout. Higher-half at 0xffffffff80000000,
                        four PT_LOAD segments, KEEPs the Limine requests.
limine.conf             Boot menu entry and kernel path.
src/                    Kernel source.
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
4. `kmain` verifies the base revision was accepted, takes the framebuffer from
   the response, and draws.

The kernel never returns. There is nothing to return to.

## Notes

Base revision **3**. Limine v9.6.7 supports up to 3; revision 6 exists only in
the protocol repository's `trunk` and is rejected by every released bootloader.
Requesting it fails silently — the check fails and the kernel halts with a black
screen and nothing in the logs.

## Credits

- [Limine](https://github.com/limine-bootloader/limine) — bootloader and boot
  protocol, 0BSD.
