#include <stdint.h>
#include <stddef.h>
#include <limine.h>

// ============================================================
// Limine requests
// ============================================================


__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests_start")))
LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
LIMINE_REQUESTS_END_MARKER;


// ============================================================
// Halt and catch fire
// ============================================================

[[noreturn]]
static void hcf() {
    asm volatile ("cli");

    for (;;) {
        asm volatile ("hlt");
    }
}


// ============================================================
// Kernel entry point
// ============================================================

extern "C" void kmain() {

    // Ensure the bootloader understands our base revision.
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        hcf();
    }

    // Ensure we received a framebuffer.
    if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];

    // Assume a 32-bit RGB framebuffer.
    auto *fb_ptr =
        static_cast<volatile uint32_t *>(framebuffer->address);

    for (size_t y = 0; y < framebuffer->height; y++) {
        for (size_t x = 0; x < framebuffer->width; x++) {

            uint32_t nX =
                static_cast<uint32_t>(
                    x * 255 / framebuffer->width
                );

            uint32_t nY =
                static_cast<uint32_t>(
                    y * 255 / framebuffer->height
                );

            fb_ptr[
                y * (framebuffer->pitch / 4) + x
            ] = (nY << 8) | nX;
        }
    }

    hcf();
}