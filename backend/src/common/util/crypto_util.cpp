#include "common/util/crypto_util.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits>

namespace taskflow::common::util {

namespace {

std::string base64Encode(const unsigned char* data, int len) {
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

common::result::Result<std::vector<unsigned char>> base64Decode(const std::string& encoded) {
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

    // Fix #284: 校验 encoded.size() <= INT_MAX，防止 static_cast<int> 截断为负数导致越界读
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

}  // anonymous namespace

common::result::Result<std::string> CryptoUtil::encrypt(const std::string& plaintext,
                                                         const std::string& key) {
    constexpr int kKeyLen = 32;
    constexpr int kIvLen = 12;
    constexpr int kTagLen = 16;

    if (static_cast<int>(key.size()) != kKeyLen) {
        return common::result::Result<std::string>::failure(
            "Key must be exactly 32 bytes");
    }

    unsigned char iv[kIvLen];
    if (RAND_bytes(iv, kIvLen) != 1) {
        return common::result::Result<std::string>::failure(
            "Failed to generate random IV");
    }

    std::vector<unsigned char> ciphertext(plaintext.size());
    std::vector<unsigned char> tag(kTagLen);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return common::result::Result<std::string>::failure(
            "Failed to create cipher context");
    }

    int len = 0;
    int ciphertext_len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "EncryptInit_ex failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Setting IV length failed");
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Setting key/IV failed");
    }

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "EncryptUpdate failed");
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "EncryptFinal_ex failed");
    }
    ciphertext_len += len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Getting GCM tag failed");
    }

    EVP_CIPHER_CTX_free(ctx);

    // Output: iv + ciphertext + tag, then base64 encode
    std::vector<unsigned char> output;
    output.reserve(static_cast<size_t>(kIvLen + ciphertext_len + kTagLen));
    output.insert(output.end(), iv, iv + kIvLen);
    output.insert(output.end(), ciphertext.begin(),
                  ciphertext.begin() + ciphertext_len);
    output.insert(output.end(), tag.begin(), tag.end());

    return base64Encode(output.data(), static_cast<int>(output.size()));
}

common::result::Result<std::string> CryptoUtil::decrypt(const std::string& ciphertext_b64,
                                                         const std::string& key) {
    constexpr int kKeyLen = 32;
    constexpr int kIvLen = 12;
    constexpr int kTagLen = 16;

    if (static_cast<int>(key.size()) != kKeyLen) {
        return common::result::Result<std::string>::failure(
            "Key must be exactly 32 bytes");
    }

    auto decoded_result = base64Decode(ciphertext_b64);
    if (!decoded_result.ok()) {
        return common::result::Result<std::string>::failure(decoded_result.error());
    }
    const std::vector<unsigned char>& decoded = decoded_result.value();

    if (static_cast<int>(decoded.size()) < kIvLen + kTagLen) {
        return common::result::Result<std::string>::failure(
            "Ciphertext too short");
    }

    int ciphertext_len = static_cast<int>(decoded.size()) - kIvLen - kTagLen;
    if (ciphertext_len < 0) {
        return common::result::Result<std::string>::failure(
            "Ciphertext too short");
    }

    const unsigned char* iv = decoded.data();
    const unsigned char* ciphertext = decoded.data() + kIvLen;
    const unsigned char* tag = decoded.data() + kIvLen + ciphertext_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return common::result::Result<std::string>::failure(
            "Failed to create cipher context");
    }

    std::vector<unsigned char> plaintext(static_cast<size_t>(ciphertext_len));
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "DecryptInit_ex failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Setting IV length failed");
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Setting key/IV failed");
    }

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "DecryptUpdate failed");
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                            const_cast<unsigned char*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Setting GCM tag failed");
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return common::result::Result<std::string>::failure(
            "Decryption failed: tag verification failed");
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return std::string(reinterpret_cast<const char*>(plaintext.data()),
                       static_cast<size_t>(plaintext_len));
}

}  // namespace taskflow::common::util
