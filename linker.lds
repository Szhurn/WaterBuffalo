/* WaterBuffalo kernel linker script.
 *
 * Copyright (c) 2026 Hunter Shurniak. All rights reserved.
 *
 * Defines the kernel's memory layout and the ELF program headers that Limine
 * reads when loading the image.
 */

OUTPUT_FORMAT(elf64-x86-64)

/* Sets e_entry. Limine transfers control to this address; the symbol must be
 * declared extern "C" so its name is not mangled. */
ENTRY(kmain)

/* Program headers, i.e. loadable segments.
 *
 * Sections are a link-time grouping; segments are what an ELF loader maps.
 * Code, read-only data and writable data are given separate PT_LOAD segments
 * so each can later be assigned distinct page permissions. */
PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD;
    rodata          PT_LOAD;
    data            PT_LOAD;
}

SECTIONS
{
    /* Top 2 GiB of the address space, the region -mcmodel=kernel assumes.
     * Symbols are then reachable with 32-bit signed displacements rather than
     * full 64-bit immediates. */
    . = 0xffffffff80000000;

    /* Limine locates the boot protocol requests by scanning the image for a
     * magic value bracketed by start and end markers, so ordering is
     * significant.
     *
     * KEEP is required: no code references these objects, so --gc-sections
     * would otherwise discard them. The resulting image links and boots with
     * every response left null. */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    /* Page-align each segment boundary. A page carries a single set of
     * permissions, so segments sharing a page cannot be granted different
     * ones. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* The '.text.*' pattern is required by -ffunction-sections, which emits
     * each function into its own section. */
    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    /* Pointers to the initialisers of objects with static storage duration
     * and non-trivial constructors. There is no C runtime to walk this array,
     * so the kernel must call these itself before relying on any such object;
     * the bounds are exported for that purpose. */
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    /* .bss must remain a separate output section. An output section's type is
     * derived from its input sections, so merging NOBITS .bss into PROGBITS
     * .data would make the whole section PROGBITS and write every zero byte
     * into the image. Kept separate it occupies no file space, and the loader
     * zeroes the difference between memsz and filesz. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* Unwind tables are unused under -fno-exceptions; the rest is toolchain
     * metadata. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
