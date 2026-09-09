/* ===========================================================================
 * WaterBuffalo kernel linker script
 *
 * Read this top to bottom. Every construct is explained where it first appears.
 * ===========================================================================
 */

/* The output file format. Without this, ld guesses from the input objects,
 * which usually works but leaves you debugging a format mismatch the one time
 * it doesn't. Be explicit. */
OUTPUT_FORMAT(elf64-x86-64)

/* Sets e_entry in the ELF header: the address the loader jumps to. Limine
 * reads exactly this field. The name must match a symbol in your code, so
 * whatever you call your entry function in main.cpp, it goes here. Because
 * C++ mangles names, that function must be declared extern "C" or the symbol
 * will really be something like _Z5kmainv and this will not resolve. */
ENTRY(kmain)

/* ---------------------------------------------------------------------------
 * PHDRS - the program headers, i.e. the SEGMENTS.
 *
 * Sections are a link-time idea: named chunks of bytes the linker shuffles.
 * Segments are a run-time idea: "map these bytes at this address with these
 * permissions." An ELF loader such as Limine reads segments and ignores
 * sections entirely.
 *
 * PT_LOAD means "actually load this into memory." Four of them, so that code,
 * read-only data, and writable data each end up in their own pages and can
 * later be given their own permissions (execute / read-only / no-execute).
 * That separation is why they are split rather than lumped together.
 * ---------------------------------------------------------------------------
 */
PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD;
    rodata          PT_LOAD;
    data            PT_LOAD;
}

SECTIONS
{
    /* '.' is the location counter: the current output address. Assigning to it
     * moves it; each output section below consumes it as it grows.
     *
     * 0xffffffff80000000 is the top 2 GiB of the 64-bit address space. This is
     * the region -mcmodel=kernel assumes, which lets the compiler reach any
     * kernel symbol with a 32-bit signed offset instead of loading a full
     * 64-bit address into a register every time. */
    . = 0xffffffff80000000;

    /* -----------------------------------------------------------------------
     * Limine requests.
     *
     * Limine finds your feature requests by scanning the loaded image for magic
     * values, bracketed by a start marker and an end marker. So the order here
     * matters: start, then the requests themselves, then end.
     *
     * KEEP() is essential. --gc-sections deletes any section nothing refers to,
     * and nothing in your code refers to these - the bootloader finds them by
     * scanning, not by symbol reference. Without KEEP the linker throws them
     * away, the kernel links clean, boots, and every .response is null.
     *
     * The ':limine_requests' suffix assigns this output section to the segment
     * of that name declared above. Every output section needs one of these
     * tags, or ld invents a segment layout for you.
     * ----------------------------------------------------------------------- */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    /* Round the location counter up to a page boundary so the next segment
     * starts on a fresh page. CONSTANT(MAXPAGESIZE) is the target's maximum
     * page size - 0x1000 here, because you pass -z max-page-size=0x1000.
     *
     * This is what makes per-segment permissions possible at all: a page has
     * exactly one set of permissions, so two segments sharing a page cannot
     * have different ones. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* -----------------------------------------------------------------------
     * Code.
     *
     * '*' is a filename wildcard: every input object file. '(.text .text.*)'
     * lists the input section names to pull from those files.
     *
     * The '.text.*' half is not optional. With -ffunction-sections the compiler
     * puts each function in its own section named .text.kmain, .text.draw and
     * so on. Match only '.text' and nearly all of your code goes unplaced.
     * ----------------------------------------------------------------------- */
    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* Read-only data: string literals, const globals, jump tables. */
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    /* -----------------------------------------------------------------------
     * Global constructors.
     *
     * You do not need this for step 1, but leaving it out causes a genuinely
     * baffling bug later, so it is here now.
     *
     * When you write a namespace-scope object with a constructor - static Foo
     * g_foo; - the compiler cannot run that constructor at compile time, so it
     * emits a pointer to an initialiser function into .init_array. In a normal
     * program the C runtime walks that array before calling main(). You have no
     * C runtime. If you never walk it yourself, your constructors silently
     * never run and g_foo is all zeroes.
     *
     * These two symbols give your code the bounds of that array so it can walk
     * it. Symbol assignments like this are how a linker script hands addresses
     * to C++: declare them as extern arrays and the addresses match up.
     * ----------------------------------------------------------------------- */
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* Writable data with non-zero initial values. */
    .data : {
        *(.data .data.*)
    } :data

    /* -----------------------------------------------------------------------
     * Zero-initialised data.
     *
     * .bss MUST be its own output section, not folded into .data.
     *
     * ld derives an output section's type from the input sections in it. .bss
     * input is NOBITS - "reserve this much memory, store nothing in the file."
     * .data input is PROGBITS - "here are the actual bytes." Mix them and the
     * whole output section becomes PROGBITS, so every byte of .bss gets written
     * into the ELF as literal zeroes. One 4 MB static array and your kernel
     * image is 4 MB of nothing.
     *
     * Kept separate, .bss costs zero file bytes; the loader sees memsz > filesz
     * and zeroes the difference for you. Limine does this, so you do not need
     * to clear .bss yourself.
     *
     * COMMON is where tentative definitions land. Rare in C++, but if one
     * appears and you have not collected it, it goes unplaced.
     *
     * It shares the ':data' segment with .data - same permissions, no reason
     * to burn a separate one. This is the normal case: several sections per
     * segment.
     * ----------------------------------------------------------------------- */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* -----------------------------------------------------------------------
     * Anything matched here is dropped. Note this lives INSIDE the SECTIONS
     * block - /DISCARD/ is an output section specification, and outside
     * SECTIONS it is a syntax error.
     *
     * .eh_frame is exception-unwinding metadata, useless under -fno-exceptions.
     * .note and .comment are toolchain breadcrumbs. All dead weight in a kernel
     * image, and .eh_frame in particular is large.
     * ----------------------------------------------------------------------- */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
