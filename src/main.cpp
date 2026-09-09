#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
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
// Freestanding C library functions
// ============================================================

extern "C" void *memcpy( void *__restrict dest, const void *__restrict src, size_t n) {
    auto *pdest = static_cast<uint8_t *>(dest);
    auto *psrc  = static_cast<const uint8_t *>(src);

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}


extern "C" void *memset(
    void *s,
    int c,
    size_t n
) {
    auto *p = static_cast<uint8_t *>(s);

    for (size_t i = 0; i < n; i++) {
        p[i] = static_cast<uint8_t>(c);
    }

    return s;
}


extern "C" void *memmove(void *dest, const void *src, size_t n) {
    auto *pdest = static_cast<uint8_t *>(dest);
    auto *psrc  = static_cast<const uint8_t *>(src);

    if (pdest < psrc) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    }
    else if (pdest > psrc) {
        for (size_t i = n; i > 0; i--) {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}


extern "C" int memcmp(const void *s1, const void *s2, size_t n) {
    auto *p1 = static_cast<const uint8_t *>(s1);
    auto *p2 = static_cast<const uint8_t *>(s2);

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}


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