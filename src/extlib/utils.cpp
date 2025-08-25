#include <extlib/utils.hpp>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

void print_bytes(const void* ptr, size_t size) {
    auto p = static_cast<const uint8_t*>(ptr);

    std::cout << std::setfill('0') << std::hex;

    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            std::cout << std::setw(8) << reinterpret_cast<uintptr_t>(p + i) << ": ";
        }

        std::cout << std::uppercase << std::setw(2) << static_cast<int>(p[i]);

        if (i % 16 == 15) {
            std::cout << std::endl;
        } else if (i % 2 == 1) {
            std::cout << " ";
        }

        std::cout << std::nouppercase;
    }

    std::cout << std::dec << std::endl;
}
