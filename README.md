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

Fetch the bootloader (once):

```bash
git clone https://github.com/limine-bootloader/limine.git \
    --branch=v9.x-binary --depth=1 limine
make -C limine
```

Build the kernel:

```bash
mkdir -p build

clang++ --target=x86_64-unknown-none-elf -Ithird_party/limine \
  -std=c++20 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
  -fno-PIC -fno-PIE -ffunction-sections -fdata-sections \
  -m64 -march=x86-64 -mabi=sysv \
  -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
  -Wall -Wextra -O2 -g -c src/main.cpp -o build/main.o

ld.lld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
  -z noexecstack --gc-sections -T linker.lds build/main.o \
  -o build/WaterBuffalo
```

Build the bootable ISO:

```bash
mkdir -p build/iso_root/boot/limine build/iso_root/EFI/BOOT
cp build/WaterBuffalo          build/iso_root/boot/
cp limine.conf                 build/iso_root/boot/limine/
cp limine/limine-bios.sys \
   limine/limine-bios-cd.bin \
   limine/limine-uefi-cd.bin   build/iso_root/boot/limine/
cp limine/BOOTX64.EFI          build/iso_root/EFI/BOOT/

xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
  -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  build/iso_root -o build/WaterBuffalo.iso

./limine/limine bios-install build/WaterBuffalo.iso
```

## Running

```bash
qemu-system-x86_64 -M q35 -m 512M -cdrom build/WaterBuffalo.iso \
  -serial stdio -no-reboot -d int,cpu_reset -D build/qemu.log
```

Debugging with gdb — start QEMU stopped and waiting:

```bash
qemu-system-x86_64 -M q35 -m 512M -cdrom build/WaterBuffalo.iso -s -S
gdb -ex 'target remote :1234' -ex 'symbol-file build/WaterBuffalo' -ex 'break kmain'
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
