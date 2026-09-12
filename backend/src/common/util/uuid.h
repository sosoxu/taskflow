#pragma once

#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>
#include <openssl/rand.h>

namespace taskflow::common::util {

inline std::string generateUuid() {
    unsigned char bytes[16];
    // Fix #354: CSPRNG 失败必须硬失败——UUID 用作主键/JWT jti，可预测
    // 的 ID 会削弱不可猜测性。此前回退 mt19937 弱随机。
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed: cannot generate secure UUID");
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
