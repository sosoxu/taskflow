#ifndef TASKFLOW_COMMON_UTIL_PASSWORD_UTIL_H_
#define TASKFLOW_COMMON_UTIL_PASSWORD_UTIL_H_

#include <string>
#include <vector>
#include <random>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>

#include "common/result/result.h"

namespace taskflow::common::util {

class PasswordUtil {
public:
    PasswordUtil() = delete;

    static common::result::Result<std::string> hashPassword(const std::string& password) {
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

    static common::result::Result<bool> verifyPassword(const std::string& password,
                                                        const std::string& stored_hash) {
        // Expected format: $pbkdf2-sha256$i=10000$salt_base64$hash_base64
        if (stored_hash.empty() || stored_hash[0] != '$') {
            return common::result::Result<bool>::failure("Invalid hash format");
        }

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

        // Parse iterations
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

        // Decode salt and original hash
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

        // Recompute hash
        std::vector<unsigned char> computed_hash(hash_len);
        if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                               salt.data(), static_cast<int>(salt.size()),
                               iterations, EVP_sha256(),
                               hash_len, computed_hash.data()) != 1) {
            return common::result::Result<bool>::failure("PKCS5_PBKDF2_HMAC failed");
        }

        // Constant-time comparison
        unsigned char diff = 0;
        for (int i = 0; i < hash_len; ++i) {
            diff |= computed_hash[static_cast<size_t>(i)] ^ orig_hash[static_cast<size_t>(i)];
        }

        return diff == 0;
    }

private:
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

            if (a < 0 || b < 0 || c < 0 || d < 0) {
                return common::result::Result<std::vector<unsigned char>>::failure(
                    "Invalid base64 character");
            }

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

#endif  // TASKFLOW_COMMON_UTIL_PASSWORD_UTIL_H_
