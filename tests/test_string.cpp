// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include "test.hpp"

#include <cstddef>
#include <cstdint>

// Declared rather than included: these are the kernel's own definitions from
// src/lib/string.cpp. The test target builds with -fno-builtin so the compiler
// emits real calls to them instead of substituting its own inline versions -
// otherwise these tests would silently exercise the compiler, not your code.
extern "C" {
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
}

TEST(memset_fills) {
    unsigned char b[8];
    memset(b, 0xAB, sizeof(b));
    for (unsigned char c : b) CHECK(c == 0xAB);

    // A zero-length fill must touch nothing.
    memset(b, 0, 0);
    CHECK(b[0] == 0xAB);
}

TEST(memcpy_copies) {
    const char src[] = "abcdef";
    char dst[7] = {};
    memcpy(dst, src, sizeof(src));
    CHECK_STREQ(dst, "abcdef");
}

TEST(memcmp_orders) {
    CHECK(memcmp("abc", "abc", 3) == 0);
    CHECK(memcmp("abc", "abd", 3) <  0);
    CHECK(memcmp("abd", "abc", 3) >  0);

    // Zero length compares equal regardless of contents.
    CHECK(memcmp("abc", "xyz", 0) == 0);
}

TEST(memmove_handles_overlap) {
    // The whole reason memmove exists: regions that overlap. Copying forward
    // when dest > src would read bytes already overwritten, so the direction
    // has to be chosen from the pointers.
    char forward[] = "abcdef";
    memmove(forward + 2, forward, 4);
    CHECK_STREQ(forward, "ababcd");

    char backward[] = "abcdef";
    memmove(backward, backward + 2, 4);
    CHECK_STREQ(backward, "cdefef");

    // Identical pointers must be a no-op, not a corrupted copy.
    char same[] = "abcdef";
    memmove(same, same, 6);
    CHECK_STREQ(same, "abcdef");
}
