// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include "print.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

namespace print {

namespace {

PutChar output_sink = nullptr;

void emit(char c) {
    if (output_sink != nullptr)
        output_sink(c);
}

void emit_string(const char* str) {
    if (str == nullptr) {
        emit('(');
        emit('n');
        emit('u');
        emit('l');
        emit('l');
        emit(')');
        return;
    }

    while (*str)
        emit(*str++);
}

void emit_unsigned(uint64_t value, uint32_t base) {
    constexpr char digits[] = "0123456789abcdef";

    char buffer[24];
    size_t length = 0;

    if (value == 0) {
        emit('0');
        return;
    }

    while (value != 0) {
        buffer[length++] = digits[value % base];
        value /= base;
    }

    while (length > 0)
        emit(buffer[--length]);
}

void emit_hex(uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";

    char buffer[16];
    size_t length = 0;

    if (value == 0) {
        emit('0');
        return;
    }

    while (value != 0) {
        buffer[length++] = digits[value & 0xF];
        value >>= 4;
    }

    while (length > 0)
        emit(buffer[--length]);
}

void emit_signed(int32_t value) {
    if (value < 0) {
        emit('-');

        // Avoid overflowing when value == INT32_MIN.
        uint32_t magnitude =
            static_cast<uint32_t>(-(value + 1)) + 1;

        emit_unsigned(magnitude, 10);
        return;
    }

    emit_unsigned(static_cast<uint32_t>(value), 10);
}

} // namespace


void set_sink(PutChar sink) {
    output_sink = sink;
}


void putc(char c) {
    emit(c);
}


void write(const char* str) {
    emit_string(str);
}


void kprintf(const char* format, ...) {
    if (format == nullptr)
        return;

    va_list ap;
    va_start(ap, format);

    while (*format) {

        if (*format != '%') {
            emit(*format++);
            continue;
        }

        ++format;

        if (*format == '\0')
            break;

        // Length modifier.
        bool long_modifier = false;

        if (*format == 'l') {
            long_modifier = true;
            ++format;
        }

        switch (*format) {

            case '%':
                emit('%');
                break;

            case 'c': {
                int value = va_arg(ap, int);
                emit(static_cast<char>(value));
                break;
            }

            case 's': {
                const char* value = va_arg(ap, const char*);
                emit_string(value);
                break;
            }

            case 'd': {
                if (long_modifier) {
                    int64_t value = va_arg(ap, int64_t);

                    if (value < 0) {
                        emit('-');

                        uint64_t magnitude =
                            static_cast<uint64_t>(-(value + 1)) + 1;

                        emit_unsigned(magnitude, 10);
                    } else {
                        emit_unsigned(
                            static_cast<uint64_t>(value),
                            10
                        );
                    }
                } else {
                    emit_signed(va_arg(ap, int32_t));
                }

                break;
            }

            case 'u': {
                if (long_modifier) {
                    emit_unsigned(
                        va_arg(ap, uint64_t),
                        10
                    );
                } else {
                    emit_unsigned(
                        va_arg(ap, uint32_t),
                        10
                    );
                }

                break;
            }

            case 'x': {
                if (long_modifier) {
                    emit_hex(va_arg(ap, uint64_t));
                } else {
                    emit_hex(
                        static_cast<uint32_t>(
                            va_arg(ap, uint32_t)
                        )
                    );
                }

                break;
            }

            case 'p': {
                uint64_t value =
                    reinterpret_cast<uint64_t>(
                        va_arg(ap, void*)
                    );

                emit('0');
                emit('x');

                // Print pointers as full 64-bit values.
                for (int i = 15; i >= 0; --i) {
                    constexpr char digits[] =
                        "0123456789abcdef";

                    emit(digits[(value >> (i * 4)) & 0xF]);
                }

                break;
            }

            default:
                // Unknown format: print it literally.
                emit('%');
                if (long_modifier)
                    emit('l');
                emit(*format);
                break;
        }

        ++format;
    }

    va_end(ap);
}

} // namespace print