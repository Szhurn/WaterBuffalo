#include "test.hpp"

#include "lib/print.hpp"

#include <cstdint>

namespace {

char   buffer[512];
size_t used;

void sink(char c) {
    if (used < sizeof(buffer) - 1) buffer[used++] = c;
}

// Formats into the capture buffer and returns it, so a check reads as one line.
const char* fmt(const char* format, auto... args) {
    used = 0;
    std::memset(buffer, 0, sizeof(buffer));
    print::set_sink(sink);
    print::kprintf(format, args...);
    return buffer;
}

}  // namespace

TEST(decimal) {
    CHECK_STREQ(fmt("%d", 0), "0");
    CHECK_STREQ(fmt("%d", 42), "42");
    CHECK_STREQ(fmt("%d", -42), "-42");

    // INT_MIN has no positive counterpart - negating it overflows.
    CHECK_STREQ(fmt("%d", -2147483647 - 1), "-2147483648");
    CHECK_STREQ(fmt("%ld", INT64_MIN), "-9223372036854775808");
}

TEST(unsigned_decimal) {
    CHECK_STREQ(fmt("%u", 0u), "0");
    CHECK_STREQ(fmt("%u", 4294967295u), "4294967295");

    // 20 digits - the case that overflows a 16-byte conversion buffer.
    CHECK_STREQ(fmt("%lu", UINT64_MAX), "18446744073709551615");
}

TEST(hexadecimal) {
    CHECK_STREQ(fmt("%x", 0u), "0");
    CHECK_STREQ(fmt("%x", 0xdeadu), "dead");
    CHECK_STREQ(fmt("%lx", static_cast<uint64_t>(0xffffffff80001000ull)),
                "ffffffff80001000");
}

TEST(pointers) {
    // Pointers print zero-padded to the full 64 bits, always.
    CHECK_STREQ(fmt("%p", reinterpret_cast<void*>(0xffffffff80001000ull)),
                "0xffffffff80001000");
    CHECK_STREQ(fmt("%p", static_cast<void*>(nullptr)),
                "0x0000000000000000");
}

TEST(strings_and_chars) {
    CHECK_STREQ(fmt("%s", "hello"), "hello");
    CHECK_STREQ(fmt("%s", static_cast<const char*>(nullptr)), "(null)");
    CHECK_STREQ(fmt("%c%c", 'h', 'i'), "hi");
}

TEST(literals_and_unknown) {
    CHECK_STREQ(fmt("no specifiers"), "no specifiers");
    CHECK_STREQ(fmt("100%%"), "100%");
    CHECK_STREQ(fmt("%q", 1), "%q");
    CHECK_STREQ(fmt("a %s b %d c", "X", 7), "a X b 7 c");
}
