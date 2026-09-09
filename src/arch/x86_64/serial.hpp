#pragma once

#include <stdint.h>

namespace arch::serial {

bool init(uint32_t baud = 115200);
void putc(char c);
int getc();
void write(const char* s);

}