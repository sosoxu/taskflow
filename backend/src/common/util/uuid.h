#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/rand.h>

namespace taskflow::common::util {

inline std::string generateUuid() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        // Fallback to random_device
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(0, 255);
        for (int i = 0; i < 16; ++i) {
            bytes[i] = static_cast<unsigned char>(dist(gen));
        }
    }

    // Set version to 4 (random)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // Set variant to RFC 4122
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    // Format: 8-4-4-4-12
    oss << std::setw(2) << static_cast<int>(bytes[0])
        << std::setw(2) << static_cast<int>(bytes[1])
        << std::setw(2) << static_cast<int>(bytes[2])
        << std::setw(2) << static_cast<int>(bytes[3])
        << "-"
        << std::setw(2) << static_cast<int>(bytes[4])
        << std::setw(2) << static_cast<int>(bytes[5])
        << "-"
        << std::setw(2) << static_cast<int>(bytes[6])
        << std::setw(2) << static_cast<int>(bytes[7])
        << "-"
        << std::setw(2) << static_cast<int>(bytes[8])
        << std::setw(2) << static_cast<int>(bytes[9])
        << "-"
        << std::setw(2) << static_cast<int>(bytes[10])
        << std::setw(2) << static_cast<int>(bytes[11])
        << std::setw(2) << static_cast<int>(bytes[12])
        << std::setw(2) << static_cast<int>(bytes[13])
        << std::setw(2) << static_cast<int>(bytes[14])
        << std::setw(2) << static_cast<int>(bytes[15]);

    return oss.str();
}

}  // namespace taskflow::common::util
