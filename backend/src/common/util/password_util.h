#pragma once

#include <string>
#include <vector>
#include <random>
#include <limits>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unistd.h>  // for crypt_r
#include <crypt.h>   // for crypt_r

#include "common/result/result.h"

namespace taskflow::common::util {

class PasswordUtil {
public:
    PasswordUtil() = delete;

    static common::result::Result<std::string> hashPassword(const std::string& password) {
        // Generate bcrypt salt using crypt_r with $2b$ cost factor 10
        constexpr int cost = 10;

        // Generate 16 random bytes for the salt
        unsigned char salt_bytes[16];
        if (RAND_bytes(salt_bytes, sizeof(salt_bytes)) != 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, 255);
            for (int i = 0; i < 16; ++i) {
                salt_bytes[i] = static_cast<unsigned char>(dist(gen));
            }
        }

        // Encode salt in bcrypt base64 format (different from standard base64)
        std::string bcrypt_salt = "$2b$" + std::to_string(cost) + "$" +
                                  encodeBcryptBase64(salt_bytes, 16);

        struct crypt_data data;
        memset(&data, 0, sizeof(data));

        char* result = crypt_r(password.c_str(), bcrypt_salt.c_str(), &data);
        if (!result || result[0] == '*') {
            // Fallback: if bcrypt not available, use PBKDF2
            return hashPasswordPBKDF2(password);
        }

        return std::string(result);
    }

    static common::result::Result<bool> verifyPassword(const std::string& password,
                                                        const std::string& stored_hash) {
        if (stored_hash.empty()) {
            return common::result::Result<bool>::failure("Invalid hash format");
        }

        // Check if it's a bcrypt hash ($2a$, $2b$, or $2y$)
        if (stored_hash.substr(0, 4) == "$2a$" ||
            stored_hash.substr(0, 4) == "$2b$" ||
            stored_hash.substr(0, 4) == "$2y$") {
            struct crypt_data data;
            memset(&data, 0, sizeof(data));

            char* result = crypt_r(password.c_str(), stored_hash.c_str(), &data);
            if (!result) {
                return common::result::Result<bool>::failure("bcrypt verification failed");
            }

            // Constant-time comparison
            return constantTimeCompare(result, stored_hash);
        }

        // Legacy PBKDF2 format: $pbkdf2-sha256$i=...
        if (stored_hash.substr(0, 15) == "$pbkdf2-sha256$") {
            return verifyPasswordPBKDF2(password, stored_hash);
        }

        return common::result::Result<bool>::failure("Unknown hash format");
    }

private:
    // bcrypt base64 encoding (uses ./ instead of +/ and no padding)
    // Fix #120: Correct the output length for partial input blocks.
    // Standard bcrypt base64: 3 bytes -> 4 chars, 2 bytes -> 3 chars, 1 byte -> 2 chars.
    static std::string encodeBcryptBase64(const unsigned char* data, size_t len) {
        static const char kTable[] =
            "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

        std::string result;
        size_t i = 0;
        while (i < len) {
            unsigned int c0 = static_cast<unsigned int>(data[i++]);
            unsigned int c1 = 0;
            unsigned int c2 = 0;
            bool has_c1 = false;
            bool has_c2 = false;

            if (i < len) {
                c1 = static_cast<unsigned int>(data[i++]);
                has_c1 = true;
            }
            if (i < len) {
                c2 = static_cast<unsigned int>(data[i++]);
                has_c2 = true;
            }

            result += kTable[(c0 >> 2) & 0x3F];
            result += kTable[((c0 & 0x03) << 4) | ((c1 >> 4) & 0x0F)];
            // 3rd char only if c1 was real data (2+ bytes in this block)
            if (has_c1) {
                result += kTable[((c1 & 0x0F) << 2) | ((c2 >> 6) & 0x03)];
            }
            // 4th char only if c2 was real data (3 bytes in this block)
            if (has_c2) {
                result += kTable[c2 & 0x3F];
            }
        }
        return result;
    }

    static bool constantTimeCompare(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        }
        return diff == 0;
    }

    // PBKDF2 fallback methods (for backward compatibility)
    static common::result::Result<std::string> hashPasswordPBKDF2(const std::string& password) {
        constexpr int kSaltLen = 16;
        constexpr int kIterations = 10000;
        constexpr int kHashLen = 32;

        unsigned char salt[kSaltLen];
        if (RAND_bytes(salt, kSaltLen) != 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, 255);
            for (int i = 0; i < kSaltLen; ++i) {
                salt[i] = static_cast<unsigned char>(dist(gen));
            }
        }

        unsigned char hash[kHashLen];
        if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                               salt, kSaltLen,
                               kIterations, EVP_sha256(),
                               kHashLen, hash) != 1) {
            return common::result::Result<std::string>::failure(
                "PKCS5_PBKDF2_HMAC failed");
        }

        std::string salt_b64 = base64Encode(salt, kSaltLen);
        std::string hash_b64 = base64Encode(hash, kHashLen);

        std::ostringstream oss;
        oss << "$pbkdf2-sha256$i=" << kIterations
            << "$" << salt_b64
            << "$" << hash_b64;

        return oss.str();
    }

    static common::result::Result<bool> verifyPasswordPBKDF2(const std::string& password,
                                                              const std::string& stored_hash) {
        // Expected format: $pbkdf2-sha256$i=10000$salt_base64$hash_base64
        std::vector<std::string> parts;
        std::istringstream iss(stored_hash);
        std::string part;
        while (std::getline(iss, part, '$')) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }

        if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") {
            return common::result::Result<bool>::failure("Invalid hash format");
        }

        const std::string& iter_str = parts[1];
        if (iter_str.substr(0, 2) != "i=") {
            return common::result::Result<bool>::failure("Invalid hash format: missing iterations");
        }
        int iterations = 0;
        try {
            iterations = std::stoi(iter_str.substr(2));
        } catch (...) {
            return common::result::Result<bool>::failure("Invalid hash format: bad iterations");
        }

        if (iterations <= 0) {
            return common::result::Result<bool>::failure("Invalid hash format: non-positive iterations");
        }
        // Fix #286: 添加迭代次数上限，防止恶意构造的超大迭代次数导致 DoS
        constexpr int kMaxIterations = 1000000;
        if (iterations > kMaxIterations) {
            return common::result::Result<bool>::failure("Invalid hash format: iterations exceed maximum");
        }

        auto salt_result = base64Decode(parts[2]);
        if (!salt_result.ok()) {
            return common::result::Result<bool>::failure(salt_result.error());
        }
        const std::vector<unsigned char>& salt = salt_result.value();

        auto orig_hash_result = base64Decode(parts[3]);
        if (!orig_hash_result.ok()) {
            return common::result::Result<bool>::failure(orig_hash_result.error());
        }
        const std::vector<unsigned char>& orig_hash = orig_hash_result.value();

        int hash_len = static_cast<int>(orig_hash.size());
        if (hash_len == 0) {
            return common::result::Result<bool>::failure("Invalid hash format: empty hash");
        }

        std::vector<unsigned char> computed_hash(hash_len);
        if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                               salt.data(), static_cast<int>(salt.size()),
                               iterations, EVP_sha256(),
                               hash_len, computed_hash.data()) != 1) {
            return common::result::Result<bool>::failure("PKCS5_PBKDF2_HMAC failed");
        }

        unsigned char diff = 0;
        for (int i = 0; i < hash_len; ++i) {
            diff |= computed_hash[static_cast<size_t>(i)] ^ orig_hash[static_cast<size_t>(i)];
        }

        return diff == 0;
    }

    static std::string base64Encode(const unsigned char* data, int len) {
        static const char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string result;
        result.reserve(static_cast<size_t>(len) * 4 / 3 + 4);

        for (int i = 0; i < len; i += 3) {
            unsigned int n = static_cast<unsigned int>(data[i]) << 16;
            if (i + 1 < len) {
                n |= static_cast<unsigned int>(data[i + 1]) << 8;
            }
            if (i + 2 < len) {
                n |= static_cast<unsigned int>(data[i + 2]);
            }

            result += kTable[(n >> 18) & 0x3F];
            result += kTable[(n >> 12) & 0x3F];
            result += (i + 1 < len) ? kTable[(n >> 6) & 0x3F] : '=';
            result += (i + 2 < len) ? kTable[n & 0x3F] : '=';
        }

        return result;
    }

    static common::result::Result<std::vector<unsigned char>> base64Decode(const std::string& encoded) {
        static const int kTable[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        };

        if (encoded.empty()) {
            return std::vector<unsigned char>{};
        }

        // Fix #286: 校验 encoded.size() <= INT_MAX，防止 static_cast<int> 截断为负数导致越界读
        if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return common::result::Result<std::vector<unsigned char>>::failure(
                "Invalid base64: input too large");
        }

        auto len = static_cast<int>(encoded.size());
        if (len % 4 != 0) {
            return common::result::Result<std::vector<unsigned char>>::failure(
                "Invalid base64 length");
        }

        int padding = 0;
        if (encoded[static_cast<size_t>(len - 1)] == '=') padding++;
        if (encoded[static_cast<size_t>(len - 2)] == '=') padding++;

        auto out_len = static_cast<size_t>(len / 4 * 3 - padding);
        std::vector<unsigned char> result(out_len);

        int j = 0;
        for (int i = 0; i < len; i += 4) {
            int a = kTable[static_cast<unsigned char>(encoded[static_cast<size_t>(i)])];
            int b = kTable[static_cast<unsigned char>(encoded[static_cast<size_t>(i + 1)])];
            int c = kTable[static_cast<unsigned char>(encoded[static_cast<size_t>(i + 2)])];
            int d = kTable[static_cast<unsigned char>(encoded[static_cast<size_t>(i + 3)])];

            if (a < 0 || b < 0) {
                return common::result::Result<std::vector<unsigned char>>::failure(
                    "Invalid base64 character");
            }
            // c and d may be -1 for padding '=', treat as 0
            if (c < 0) c = 0;
            if (d < 0) d = 0;

            unsigned int n = (static_cast<unsigned int>(a) << 18) |
                             (static_cast<unsigned int>(b) << 12) |
                             (static_cast<unsigned int>(c) << 6) |
                             static_cast<unsigned int>(d);

            if (j < static_cast<int>(out_len))
                result[static_cast<size_t>(j++)] = static_cast<unsigned char>((n >> 16) & 0xFF);
            if (j < static_cast<int>(out_len))
                result[static_cast<size_t>(j++)] = static_cast<unsigned char>((n >> 8) & 0xFF);
            if (j < static_cast<int>(out_len))
                result[static_cast<size_t>(j++)] = static_cast<unsigned char>(n & 0xFF);
        }

        return result;
    }
};

}  // namespace taskflow::common::util
