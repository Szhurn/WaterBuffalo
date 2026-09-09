#pragma once

#include <stdint.h>
#include <stddef.h>
namespace arch {
template <typename> inline constexpr bool always_false = false;
template <typename T> 
class Port {
    static_assert( sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4, "Port width must be 1, 2, or 4 bytes");

private:
    uint16_t port_;

public:
    constexpr explicit Port(uint16_t port) : port_(port) {}

    void write(T value) const {
        if constexpr (sizeof(T) == 1) {
            asm volatile("outb %0, %1" : : "a"(value), "Nd"(port_) : "memory");
        } else if constexpr (sizeof(T) == 2) {
            asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port_) : "memory");
        } else if constexpr (sizeof(T) == 4) {
            asm volatile("outl %0, %1" : : "a"(value), "Nd"(port_) : "memory");
        } else {
            static_assert(always_false<T>, "unsupported port width");
        }
    }

    T read() const {
        T value{};

        if constexpr (sizeof(T) == 1) {
            asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port_) : "memory");
        } else if constexpr (sizeof(T) == 2) {
            asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port_) : "memory");
        } else if constexpr (sizeof(T) == 4) {
            asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port_) : "memory");
        } else {
            static_assert(always_false<T>, "unsupported port width");
        }
        

        return value;
    }
};
}