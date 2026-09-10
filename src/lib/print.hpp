#pragma once

#include <stdint.h>

namespace print {

using PutChar = void (*)(char);

void set_sink(PutChar sink);

void putc(char c);
void write(const char* str);

void kprintf(const char* format, ...);

}