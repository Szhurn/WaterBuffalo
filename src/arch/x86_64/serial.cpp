// serial.cpp — bare-metal UART driver for COM1 (0x3F8)

#include "serial.hpp"
#include "io.hpp"

#include <stdint.h>

namespace arch::serial {

namespace {

constexpr uint16_t UART_BASE = 0x3F8;

enum class Reg : uint16_t {
    Data            = 0,
    InterruptEnable = 1,
    FifoControl     = 2,
    InterruptID     = 2,
    LineControl     = 3,
    ModemControl    = 4,
    LineStatus      = 5,
    ModemStatus     = 6,
    Scratch         = 7,

    // Offset 0/1 when DLAB is enabled.
    DivisorLow      = 0,
    DivisorHigh     = 1
};

constexpr uint8_t LCR_DLAB = 0x80;

constexpr uint8_t LSR_DR   = 0x01;
constexpr uint8_t LSR_THRE = 0x20;

constexpr uint8_t LOOPBACK_TEST_BYTE = 0xAE;

constexpr uint32_t LOOPBACK_TIMEOUT = 100000;
constexpr uint32_t TX_TIMEOUT       = 100000;
constexpr uint32_t RX_TIMEOUT       = 100000;

bool initialized = false;

Port<uint8_t> reg(Reg r) {
    return Port<uint8_t>(
        UART_BASE + static_cast<uint16_t>(r)
    );
}

bool wait_for(uint8_t mask, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; ++i) {
        if (reg(Reg::LineStatus).read() & mask)
            return true;
    }

    return false;
}

} // anonymous namespace


bool init(uint32_t baud) {
    if (baud == 0)
        return false;

    const uint32_t divisor = 115200 / baud;

    if (divisor == 0 || divisor > 0xFFFF)
        return false;

    initialized = false;

    // Disable interrupts.
    reg(Reg::InterruptEnable).write(0x00);

    // Enable DLAB.
    reg(Reg::LineControl).write(LCR_DLAB);

    // Program divisor.
    reg(Reg::DivisorLow).write(
        static_cast<uint8_t>(divisor & 0xFF)
    );

    reg(Reg::DivisorHigh).write(
        static_cast<uint8_t>((divisor >> 8) & 0xFF)
    );

    // Disable DLAB, configure 8N1.
    reg(Reg::LineControl).write(0x03);

    // Enable FIFO, clear FIFOs, 14-byte trigger.
    reg(Reg::FifoControl).write(0xC7);

    // Enable DTR + RTS.
    reg(Reg::ModemControl).write(0x03);

    // --------------------------------------------------------
    // Loopback self-test
    // --------------------------------------------------------

    reg(Reg::ModemControl).write(0x1E);

    reg(Reg::Data).write(LOOPBACK_TEST_BYTE);

    if (!wait_for(LSR_DR, LOOPBACK_TIMEOUT)) {
        // Restore normal state before reporting failure.
        reg(Reg::ModemControl).write(0x00);
        return false;
    }

    if (reg(Reg::Data).read() != LOOPBACK_TEST_BYTE) {
        // Restore normal state before reporting failure.
        reg(Reg::ModemControl).write(0x00);
        return false;
    }

    // Normal mode: DTR, RTS, OUT1, OUT2.
    reg(Reg::ModemControl).write(0x0F);

    initialized = true;
    return true;
}


void putc(char c) {
    if (!initialized)
        return;

    if (c == '\n')
        putc('\r');

    if (!wait_for(LSR_THRE, TX_TIMEOUT))
        return;

    reg(Reg::Data).write(static_cast<uint8_t>(c));
}


int getc() {
    if (!initialized)
        return -1;

    if (!wait_for(LSR_DR, RX_TIMEOUT))
        return -1;

    return reg(Reg::Data).read();
}

void write(const char* s) {
    if (!s)
        return;

    while (*s) {
        putc(*s++);
    }
}

} // namespace arch::serial