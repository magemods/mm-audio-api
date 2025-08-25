#pragma once

#include <cctype>
#include <cstring>
#include <algorithm>
#include <chrono>

constexpr auto EPOCH = std::chrono::steady_clock::time_point{};

void print_bytes(const void* ptr, size_t size);

inline uint32_t read_u24_be(const uint8_t* p) {
    return (p[0] << 16) | (p[1] << 8) | p[2];
}

inline uint32_t read_u32_syncsafe(const uint8_t* p) {
    return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

inline uint32_t read_u32_be(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

inline uint32_t read_u32_le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

inline void swap_b(char* str, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        char tmp0 = str[i];
        char tmp1 = str[i+1];
        str[i]   = str[i+3];
        str[i+1] = str[i+2];
        str[i+2] = tmp1;
        str[i+3] = tmp0;
    }
}

inline std::string uppercase(std::string &s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return s;
}

inline std::string lowercase(std::string &s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

inline std::string alphanum(std::string &s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return !isalnum(c);
    }), s.end());
    return s;
}

inline std::string ltrim(std::string &s, char ch = '\0') {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) {
        return c != ch;
    }));
    return s;
}

inline std::string rtrim(std::string &s, char ch = '\0') {
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) {
        return c != ch;
    }).base(), s.end());
    return s;
}

inline std::string trim(std::string &s, char ch = '\0') {
    rtrim(s, ch);
    ltrim(s, ch);
    return s;
}
