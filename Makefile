# ===========================================================================
# WaterBuffalo kernel build
#
#   make          build the kernel ELF
#   make iso      build a bootable BIOS/UEFI ISO
#   make run      boot the ISO in QEMU
#   make debug    boot under QEMU, stopped, waiting for gdb on :1234
#   make gdb      attach gdb to a `make debug` session
#   make test     build and run the host-side unit tests
#   make clean    remove build output
# ===========================================================================

# ---------------------------------------------------------------------------
# Configuration
#
# ':=' expands NOW, once. '=' would re-expand every time the variable is used,
# which is slower and occasionally surprising. Default to ':=' unless you
# specifically need lazy evaluation.
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
# Use ':=' here, NOT '?='. Make PREDEFINES CXX as 'g++' and LD as 'ld', and
# '?=' only assigns when a variable is undefined - so '?=' would silently leave
# you compiling with g++, which does not understand --target=.
#
# ':=' still lets you override from the command line, because command-line
# assignments beat assignments in the file:   make CXX=x86_64-elf-g++
# ---------------------------------------------------------------------------
CXX := clang++
LD  := ld.lld

# Freestanding C++: no libc, no libstdc++, no unwinder.
#   -mno-red-zone   MANDATORY. Interrupts push onto the stack without honouring
#                   the red zone and would corrupt leaf-function locals.
#   -mno-sse etc.   CR4.OSFXSR is not set, so an SSE instruction is an
#                   immediate #UD.
#   -mcmodel=kernel matches the 0xffffffff80000000 load address in linker.lds.
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

# -MMD -MP is the important part: see the '-include $(DEPS)' note at the bottom.
CPPFLAGS := -I src -I third_party/limine -MMD -MP

ASFLAGS := --target=x86_64-unknown-none-elf -m64 -g

LDFLAGS := -m elf_x86_64 -nostdlib -static \
           -z max-page-size=0x1000 -z noexecstack \
           --gc-sections -T linker.lds

# ---------------------------------------------------------------------------
# Sources
#
# Found automatically, so adding a .cpp under src/ needs no edit here. The cost
# is that make cannot notice a NEW file appearing mid-build - but since 'find'
# runs when make starts, a plain re-run picks it up.
#
# The substitution turns  src/lib/string.cpp  into  build/src/lib/string.cpp.o
# so object files mirror the source tree instead of colliding in one flat dir
# (two files both named 'serial.cpp' in different folders would otherwise
# overwrite each other).
# ---------------------------------------------------------------------------
CXX_SOURCES := $(shell find src -name '*.cpp' | sort)
ASM_SOURCES := $(shell find src -name '*.S'   | sort)

OBJECTS := $(CXX_SOURCES:%.cpp=$(BUILD)/%.cpp.o) \
           $(ASM_SOURCES:%.S=$(BUILD)/%.S.o)

DEPS := $(OBJECTS:.o=.d)

# ---------------------------------------------------------------------------
# Build rules
#
# A rule is:   target: prerequisites
#              <TAB>recipe
#
# The recipe lines MUST start with a real tab, not spaces. This is make's most
# notorious wart; the error you get is 'missing separator'.
#
# make rebuilds a target when any prerequisite is NEWER than it. That is the
# whole model - it is timestamp comparison, nothing cleverer.
# ---------------------------------------------------------------------------

# The first target in the file is what a bare 'make' builds, so keep it first.
.PHONY: all
all: $(ELF)

$(ELF): $(OBJECTS) linker.lds
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	@echo "==> $@"

# A pattern rule. '%' matches any stem, so this one rule compiles every .cpp.
# Automatic variables:
#   $@  the target        (build/src/main.cpp.o)
#   $<  first prerequisite (src/main.cpp)
#   $^  all prerequisites, deduplicated
$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# Assembly goes through the compiler driver, not the assembler directly, so
# that .S files get run through the C preprocessor first - which lets you
# #include headers and use #define from assembly.
$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Bootloader
#
# A file target, so the clone happens once and never again - make sees the file
# exists and skips the recipe. Delete the directory to force a refetch.
# ---------------------------------------------------------------------------
$(LIMINE_DIR)/limine:
	git clone https://github.com/limine-bootloader/limine.git \
		--branch=$(LIMINE_BRANCH) --depth=1 $(LIMINE_DIR)
	$(MAKE) -C $(LIMINE_DIR)

# ---------------------------------------------------------------------------
# ISO image
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
# Running
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
# The modules listed in TESTABLE_SRCS have no hardware dependencies - no port
# I/O, no MMIO, no assembly - so they are ordinary C++ that the HOST compiler
# can build and run as a normal program. That turns a change-and-verify cycle
# from "rebuild the ISO and boot QEMU" into a few milliseconds.
#
# Only add a source here if it would still make sense running as a userspace
# program. serial.cpp would compile and then fault on its first `out`
# instruction; a physical frame allocator or an ELF header parser is pure logic
# over memory and belongs here.
#
# -fno-builtin matters for string.cpp: without it the compiler replaces calls
# to memcpy/memset with its own inline versions, and the tests would silently
# exercise the compiler instead of your implementations.
# ---------------------------------------------------------------------------
HOST_CXX      ?= g++
TEST_SRCS     := $(wildcard tests/*.cpp)
TESTABLE_SRCS := src/lib/print.cpp src/lib/string.cpp
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
#
# .PHONY marks targets that are commands, not files. Without it, creating a
# file called 'clean' would make 'make clean' silently do nothing, because make
# would see an up-to-date file and stop.
# ---------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: distclean
distclean: clean
	rm -rf $(LIMINE_DIR)

# ---------------------------------------------------------------------------
# Header dependency tracking. Do not remove this.
#
# -MMD makes the compiler emit a .d file beside each .o listing every header
# that .cpp included. Those .d files are themselves make rules. Including them
# here means editing serial.hpp rebuilds every .cpp that included it.
#
# Without this, make only knows about .cpp files. Change a header and nothing
# rebuilds - you get an executable built from a mix of old and new definitions,
# which in a kernel shows up as a struct layout mismatch and a triple fault
# with no explanation.
#
# -MP adds a dummy target for each header, so deleting or renaming one gives a
# clean rebuild instead of 'No rule to make target'.
#
# The leading '-' means "do not fail if these do not exist yet" - on a clean
# tree they have not been generated.
# ---------------------------------------------------------------------------
-include $(DEPS)
