# WaterBuffalo — build system.
#
#   make          kernel ELF
#   make iso      bootable BIOS/UEFI ISO
#   make run      build and boot in QEMU
#   make debug    boot under QEMU, halted, gdb server on :1234
#   make gdb      attach gdb, load symbols, break on kmain
#   make test     build and run the host-side unit tests
#   make clean    remove build output
#
# Copyright (c) 2026 Hunter Shurniak. All rights reserved.

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
KERNEL   := WaterBuffalo
BUILD    := build
ELF      := $(BUILD)/$(KERNEL)
ISO      := $(BUILD)/$(KERNEL).iso
ISO_ROOT := $(BUILD)/iso_root

LIMINE_DIR    := limine
LIMINE_BRANCH := v9.x-binary

# ---------------------------------------------------------------------------
# Toolchain
#
# Clang cross-compiles via --target, so no GCC cross-toolchain is required.
# Assigned with ':=' rather than '?=' because make predefines CXX and LD;
# '?=' would leave the defaults in place. Command-line assignment still
# overrides these:  make CXX=x86_64-elf-g++
# ---------------------------------------------------------------------------
CXX := clang++
LD  := ld.lld

# Freestanding C++: no libc, no libstdc++, no unwinder.
#
#   -mno-red-zone    Required. Interrupt delivery pushes onto the stack
#                    without honouring the red zone, corrupting leaf-function
#                    locals.
#   -mno-sse et al.  CR4.OSFXSR is not set, so vector instructions fault.
#   -mcmodel=kernel  Matches the load address in linker.lds.
CXXFLAGS := --target=x86_64-unknown-none-elf \
            -std=c++20 \
            -Wall -Wextra \
            -O2 -g \
            -ffreestanding \
            -fno-exceptions -fno-rtti \
            -fno-stack-protector -fno-stack-check \
            -fno-PIC -fno-PIE \
            -ffunction-sections -fdata-sections \
            -m64 -march=x86-64 -mabi=sysv \
            -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
            -mno-red-zone -mcmodel=kernel

# -MMD -MP generate the header dependency files consumed at the end of this
# file.
CPPFLAGS := -I src -I third_party/limine -MMD -MP

ASFLAGS := --target=x86_64-unknown-none-elf -m64 -g

LDFLAGS := -m elf_x86_64 -nostdlib -static \
           -z max-page-size=0x1000 -z noexecstack \
           --gc-sections -T linker.lds

# ---------------------------------------------------------------------------
# Sources
#
# Discovered by glob, so new files under src/ require no edit here. Object
# paths mirror the source tree to keep same-named files in different
# directories from colliding.
# ---------------------------------------------------------------------------
CXX_SOURCES := $(shell find src -name '*.cpp' | sort)
ASM_SOURCES := $(shell find src -name '*.S'   | sort)

OBJECTS := $(CXX_SOURCES:%.cpp=$(BUILD)/%.cpp.o) \
           $(ASM_SOURCES:%.S=$(BUILD)/%.S.o)

DEPS := $(OBJECTS:.o=.d)

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------
.PHONY: all
all: $(ELF)

$(ELF): $(OBJECTS) linker.lds
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	@echo "==> $@"

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# Assembly is routed through the compiler driver so .S files are preprocessed,
# making #include and #define available to them.
$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Bootloader
#
# A file target: the clone runs once and is skipped thereafter. Remove the
# directory to force a refetch.
# ---------------------------------------------------------------------------
$(LIMINE_DIR)/limine:
	git clone https://github.com/limine-bootloader/limine.git \
		--branch=$(LIMINE_BRANCH) --depth=1 $(LIMINE_DIR)
	$(MAKE) -C $(LIMINE_DIR)

# ---------------------------------------------------------------------------
# ISO image
#
# Hybrid BIOS/UEFI. The xorriso invocation follows Limine's USAGE.md;
# `limine bios-install` patches the legacy BIOS boot record and is required
# for non-UEFI boot.
# ---------------------------------------------------------------------------
.PHONY: iso
iso: $(ISO)

$(ISO): $(ELF) limine.conf $(LIMINE_DIR)/limine
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(ELF) $(ISO_ROOT)/boot/$(KERNEL)
	cp limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios.sys \
	   $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@
	./$(LIMINE_DIR)/limine bios-install $@
	@echo "==> $@"

# ---------------------------------------------------------------------------
# Running and debugging
# ---------------------------------------------------------------------------
QEMU      := qemu-system-x86_64
QEMUFLAGS := -M q35 -m 512M -serial stdio -no-reboot -no-shutdown \
             -d guest_errors,int,cpu_reset -D $(BUILD)/qemu.log

.PHONY: run
run: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom $(ISO)

.PHONY: debug
debug: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom $(ISO) -s -S

.PHONY: gdb
gdb:
	gdb -ex 'target remote :1234' \
	    -ex 'symbol-file $(ELF)' \
	    -ex 'break kmain'

# ---------------------------------------------------------------------------
# Host-side unit tests
#
# TESTABLE_SRCS lists kernel modules with no hardware dependencies. Those are
# ordinary C++ and are compiled by the host toolchain and run as a normal
# program, giving a sub-second feedback loop for logic that would otherwise
# only be exercised by booting.
#
# A module belongs here only if it would still be meaningful in userspace.
# -fno-builtin prevents the compiler substituting its own implementations of
# the mem* functions, which would leave those tests exercising the compiler
# rather than src/lib/string.cpp.
# ---------------------------------------------------------------------------
HOST_CXX      ?= g++
TEST_SRCS     := $(wildcard tests/*.cpp)
TESTABLE_SRCS := src/lib/print.cpp src/lib/string.cpp src/mm/pmm.cpp
TEST_BIN      := $(BUILD)/tests/runner

.PHONY: test
test: $(TEST_BIN)
	@$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(TESTABLE_SRCS) tests/test.hpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++20 -fno-builtin -Wall -Wextra -g \
		-I src -I tests -o $@ $(TEST_SRCS) $(TESTABLE_SRCS)

# ---------------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: distclean
distclean: clean
	rm -rf $(LIMINE_DIR)

# ---------------------------------------------------------------------------
# Header dependency tracking.
#
# The compiler emits a .d file alongside each object listing the headers that
# translation unit included; those files are themselves make rules. Including
# them here is what makes a header edit rebuild its dependents. Without it,
# make sees only .cpp prerequisites, and a changed struct definition produces
# an image built from mismatched layouts.
#
# -MP emits phony targets for each header so a deleted or renamed header
# rebuilds cleanly instead of failing. The leading '-' tolerates the files not
# existing on a clean tree.
# ---------------------------------------------------------------------------
-include $(DEPS)
