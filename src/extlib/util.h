#pragma once

#include <cctype>
#include <algorithm>
#include <chrono>

constexpr auto EPOCH = std::chrono::steady_clock::time_point{};

inline uint32_t read_u32_le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

inline void uppercase(std::string &s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
}

inline void ltrim(std::string &s, char ch = '\0') {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) {
        return c != ch;
    }));
}

inline void rtrim(std::string &s, char ch = '\0') {
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) {
        return c != ch;
    }).base(), s.end());
}

inline void trim(std::string &s, char ch = '\0') {
    rtrim(s, ch);
    ltrim(s, ch);
}
