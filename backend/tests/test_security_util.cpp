#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <thread>
#include <vector>

#include "common/util/password_util.h"
#include "common/util/crypto_util.h"
#include "common/util/jwt_util.h"
#include "common/util/uuid.h"

using namespace taskflow::common::util;

// ============================================================================
// §2.8 密码 bcrypt 加密
// 验收指标：
//   1. 相同密码每次哈希结果不同（随机盐值）
//   2. 验证正确密码返回 true
//   3. 验证错误密码返回 false
//   4. 哈希格式为 $2b$10$ 开头
//   5. cost ≥ 10
// ============================================================================

TEST_CASE("PasswordUtil: hash produces different results for same password", "[password]") {
    auto r1 = PasswordUtil::hashPassword("test123");
    auto r2 = PasswordUtil::hashPassword("test123");
    REQUIRE(r1.ok());
    REQUIRE(r2.ok());
    REQUIRE(r1.value() != r2.value());  // 不同盐值，结果不同
}

TEST_CASE("PasswordUtil: verify correct password returns true", "[password]") {
    auto hash_result = PasswordUtil::hashPassword("mypassword");
    REQUIRE(hash_result.ok());
    auto verify_result = PasswordUtil::verifyPassword("mypassword", hash_result.value());
    REQUIRE(verify_result.ok());
    REQUIRE(verify_result.value() == true);
}

TEST_CASE("PasswordUtil: verify wrong password returns false", "[password]") {
    auto hash_result = PasswordUtil::hashPassword("mypassword");
    REQUIRE(hash_result.ok());
    auto verify_result = PasswordUtil::verifyPassword("wrongpassword", hash_result.value());
    REQUIRE(verify_result.ok());
    REQUIRE(verify_result.value() == false);
}

TEST_CASE("PasswordUtil: hash format is $2b$10$", "[password]") {
    auto hash_result = PasswordUtil::hashPassword("test");
    REQUIRE(hash_result.ok());
    const auto& hash = hash_result.value();
    REQUIRE(hash.substr(0, 4) == "$2b$");
    REQUIRE(hash.substr(4, 2) == "10");  // cost = 10
}

TEST_CASE("PasswordUtil: empty hash returns failure", "[password]") {
    auto result = PasswordUtil::verifyPassword("test", "");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("PasswordUtil: unknown hash format returns failure", "[password]") {
    auto result = PasswordUtil::verifyPassword("test", "$unknown$hash$value");
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// §2.10 SQL 任务密码 AES-256-GCM 加密
// 验收指标：
//   1. 加密后可正确解密还原明文
//   2. 相同明文每次加密结果不同（随机 IV）
//   3. 密钥长度非 32 字节返回错误
//   4. 篡改密文后解密失败
// ============================================================================

TEST_CASE("CryptoUtil: encrypt then decrypt returns original plaintext", "[crypto]") {
    const std::string key(32, 'k');  // 32 字节密钥
    const std::string plaintext = "postgresql://user:pass@host:5432/db";

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
}

TEST_CASE("CryptoUtil: same plaintext produces different ciphertext", "[crypto]") {
    const std::string key(32, 'k');
    const std::string plaintext = "secret_password";

    auto r1 = CryptoUtil::encrypt(plaintext, key);
    auto r2 = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(r1.ok());
    REQUIRE(r2.ok());
    REQUIRE(r1.value() != r2.value());  // 随机 IV，结果不同
}

TEST_CASE("CryptoUtil: wrong key length returns error", "[crypto]") {
    const std::string short_key(16, 'k');
    auto result = CryptoUtil::encrypt("test", short_key);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CryptoUtil: tampered ciphertext fails to decrypt", "[crypto]") {
    const std::string key(32, 'k');
    auto encrypt_result = CryptoUtil::encrypt("secret", key);
    REQUIRE(encrypt_result.ok());

    // 篡改密文（修改 base64 字符串中间字符）
    std::string tampered = encrypt_result.value();
    if (tampered.size() > 10) {
        tampered[5] = (tampered[5] == 'A') ? 'B' : 'A';
    }

    auto decrypt_result = CryptoUtil::decrypt(tampered, key);
    REQUIRE_FALSE(decrypt_result.ok());
}

TEST_CASE("CryptoUtil: wrong key fails to decrypt", "[crypto]") {
    const std::string key1(32, 'a');
    const std::string key2(32, 'b');

    auto encrypt_result = CryptoUtil::encrypt("secret", key1);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key2);
    REQUIRE_FALSE(decrypt_result.ok());
}

TEST_CASE("CryptoUtil: base64 encode/decode roundtrip", "[crypto]") {
    const std::string key(32, 'k');
    const std::string plaintext = "hello world test";

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
}

TEST_CASE("CryptoUtil: short plaintext encrypts and decrypts", "[crypto]") {
    const std::string key(32, 'k');
    const std::string plaintext = "x";

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
}

TEST_CASE("CryptoUtil: empty plaintext encrypts and decrypts", "[crypto]") {
    const std::string key(32, 'k');
    auto encrypt_result = CryptoUtil::encrypt("", key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == "");
}

TEST_CASE("CryptoUtil: unicode plaintext encrypts and decrypts", "[crypto]") {
    const std::string key(32, 'k');
    const std::string plaintext = "数据库密码_中文测试🔑";

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
}

// ============================================================================
// §2.9 JWT Token 生成与验证
// 验收指标：
//   1. 生成 access_token 和 refresh_token 对
//   2. 验证有效 token 返回正确 payload
//   3. 验证过期 token 返回错误
//   4. 验证篡改 token 返回错误
//   5. Token 包含 jti 字段
//   6. TokenBlacklist 功能正常
// ============================================================================

TEST_CASE("JwtUtil: generate and verify token pair", "[jwt]") {
    const std::string secret(32, 's');

    auto token_result = JwtUtil::generateTokens("user-123", "admin", "admin", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    const auto& tokens = token_result.value();
    REQUIRE_FALSE(tokens.access_token.empty());
    REQUIRE_FALSE(tokens.refresh_token.empty());
    REQUIRE(tokens.expires_in == 3600);

    // 验证 access token
    auto access_result = JwtUtil::verifyToken(tokens.access_token, secret);
    REQUIRE(access_result.ok());
    REQUIRE(access_result.value().user_id == "user-123");
    REQUIRE(access_result.value().username == "admin");
    REQUIRE(access_result.value().role == "admin");
    REQUIRE(access_result.value().type == "access");
    REQUIRE_FALSE(access_result.value().jti.empty());

    // 验证 refresh token
    auto refresh_result = JwtUtil::verifyToken(tokens.refresh_token, secret);
    REQUIRE(refresh_result.ok());
    REQUIRE(refresh_result.value().type == "refresh");
}

TEST_CASE("JwtUtil: expired token returns error", "[jwt]") {
    const std::string secret(32, 's');

    // 生成已过期的 token（TTL = -1 秒）
    auto token_result = JwtUtil::generateTokens("user-123", "admin", "admin", secret, -1, -1);
    REQUIRE(token_result.ok());

    auto verify_result = JwtUtil::verifyToken(token_result.value().access_token, secret);
    REQUIRE_FALSE(verify_result.ok());
}

TEST_CASE("JwtUtil: wrong secret returns error", "[jwt]") {
    const std::string secret1(32, 'a');
    const std::string secret2(32, 'b');

    auto token_result = JwtUtil::generateTokens("user-123", "admin", "admin", secret1, 3600, 86400);
    REQUIRE(token_result.ok());

    auto verify_result = JwtUtil::verifyToken(token_result.value().access_token, secret2);
    REQUIRE_FALSE(verify_result.ok());
}

TEST_CASE("JwtUtil: tampered token returns error", "[jwt]") {
    const std::string secret(32, 's');

    auto token_result = JwtUtil::generateTokens("user-123", "admin", "admin", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    std::string tampered = token_result.value().access_token;
    // 修改 token 中间部分
    auto dot_pos = tampered.find('.');
    if (dot_pos != std::string::npos && dot_pos + 2 < tampered.size()) {
        tampered[dot_pos + 1] = (tampered[dot_pos + 1] == 'A') ? 'B' : 'A';
    }

    auto verify_result = JwtUtil::verifyToken(tampered, secret);
    REQUIRE_FALSE(verify_result.ok());
}

TEST_CASE("JwtUtil: token contains jti field", "[jwt]") {
    const std::string secret(32, 's');

    auto token_result = JwtUtil::generateTokens("user-123", "admin", "admin", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    auto payload = JwtUtil::verifyToken(token_result.value().access_token, secret);
    REQUIRE(payload.ok());
    REQUIRE_FALSE(payload.value().jti.empty());

    // 两个 token 的 jti 应该不同
    auto token_result2 = JwtUtil::generateTokens("user-123", "admin", "admin", secret, 3600, 86400);
    auto payload2 = JwtUtil::verifyToken(token_result2.value().access_token, secret);
    REQUIRE(payload2.ok());
    REQUIRE(payload.value().jti != payload2.value().jti);
}

TEST_CASE("TokenBlacklist: add and check jti", "[jwt]") {
    auto& blacklist = TokenBlacklist::instance();

    const std::string jti = "test-jti-" + generateUuid();
    REQUIRE_FALSE(blacklist.isBlacklisted(jti));

    blacklist.add(jti);
    REQUIRE(blacklist.isBlacklisted(jti));

    // 另一个 jti 不受影响
    const std::string jti2 = "test-jti2-" + generateUuid();
    REQUIRE_FALSE(blacklist.isBlacklisted(jti2));
}

TEST_CASE("TokenBlacklist: thread-safe concurrent access", "[jwt]") {
    auto& blacklist = TokenBlacklist::instance();

    std::vector<std::thread> threads;
    const int count = 100;

    for (int i = 0; i < count; ++i) {
        threads.emplace_back([&blacklist, i]() {
            std::string jti = "concurrent-jti-" + std::to_string(i);
            blacklist.add(jti);
            REQUIRE(blacklist.isBlacklisted(jti));
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

// ============================================================================
// UUID v4 生成
// 验收指标：
//   1. 格式为 8-4-4-4-12
//   2. 版本位为 4
//   3. 变体位为 RFC 4122
//   4. 每次生成不同
// ============================================================================

TEST_CASE("UUID: format is 8-4-4-4-12", "[uuid]") {
    auto uuid = generateUuid();
    REQUIRE(uuid.size() == 36);
    REQUIRE(uuid[8] == '-');
    REQUIRE(uuid[13] == '-');
    REQUIRE(uuid[18] == '-');
    REQUIRE(uuid[23] == '-');
}

TEST_CASE("UUID: version is 4", "[uuid]") {
    auto uuid = generateUuid();
    // 第 14 个字符（版本位）应为 '4'
    REQUIRE(uuid[14] == '4');
}

TEST_CASE("UUID: variant is RFC 4122", "[uuid]") {
    auto uuid = generateUuid();
    // 第 19 个字符（变体位）应为 '8', '9', 'a', 或 'b'
    char variant = uuid[19];
    REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b'));
}

TEST_CASE("UUID: generates unique values", "[uuid]") {
    std::set<std::string> uuids;
    for (int i = 0; i < 1000; ++i) {
        auto [it, inserted] = uuids.insert(generateUuid());
        REQUIRE(inserted);  // 所有 UUID 应该唯一
    }
}

// ============================================================================
// Fix #248: CryptoUtil 错误处理场景测试
// 原测试只覆盖正常加解密和篡改密文，缺少空密文/过短密文/无效base64/大文本等
// ============================================================================

TEST_CASE("CryptoUtil: decrypt empty ciphertext fails", "[crypto]") {
    // Fix #248: 空密文解密应失败
    const std::string key(32, 'k');
    auto result = CryptoUtil::decrypt("", key);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CryptoUtil: decrypt too-short ciphertext fails", "[crypto]") {
    // Fix #248: 密文短于 IV(12)+TAG(16)=28 字节应失败
    const std::string key(32, 'k');
    // "AAAA" base64 解码后只有 3 字节，远小于 28
    auto result = CryptoUtil::decrypt("AAAA", key);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CryptoUtil: decrypt invalid base64 fails", "[crypto]") {
    // Fix #248: 无效 base64 字符串解密应失败
    const std::string key(32, 'k');
    // 含非法字符 '!' 的 base64
    auto result = CryptoUtil::decrypt("!!!!invalid!base64!data!!!!", key);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CryptoUtil: decrypt with wrong key length fails", "[crypto]") {
    // Fix #248: decrypt 时密钥长度非 32 应失败（encrypt 已测但 decrypt 未测）
    const std::string short_key(16, 'k');
    auto result = CryptoUtil::decrypt("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", short_key);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CryptoUtil: large plaintext encrypts and decrypts", "[crypto]") {
    // Fix #248: 大文本加解密往返测试
    const std::string key(32, 'k');
    const std::string plaintext(10000, 'X');  // 10KB 文本

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
    REQUIRE(decrypt_result.value().size() == 10000);
}

TEST_CASE("CryptoUtil: binary data encrypts and decrypts", "[crypto]") {
    // Fix #248: 二进制数据（含 \0）加解密往返测试
    const std::string key(32, 'k');
    std::string plaintext;
    for (int i = 0; i < 256; ++i) {
        plaintext.push_back(static_cast<char>(i));
    }

    auto encrypt_result = CryptoUtil::encrypt(plaintext, key);
    REQUIRE(encrypt_result.ok());

    auto decrypt_result = CryptoUtil::decrypt(encrypt_result.value(), key);
    REQUIRE(decrypt_result.ok());
    REQUIRE(decrypt_result.value() == plaintext);
    REQUIRE(decrypt_result.value().size() == 256);
}

TEST_CASE("CryptoUtil: decrypt valid base64 but wrong structure fails", "[crypto]") {
    // Fix #248: 有效 base64 但长度恰好为 IV+TAG 但无密文内容
    const std::string key(32, 'k');
    // 构造一个恰好 28 字节的 base64 编码（IV+TAG，无密文）
    // 28 字节 base64 编码后约 40 字符（含 padding）
    std::string raw(28, '\0');
    // 手动 base64 编码
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned int n = static_cast<unsigned char>(raw[i]) << 16;
        if (i + 1 < raw.size()) n |= static_cast<unsigned char>(raw[i+1]) << 8;
        if (i + 2 < raw.size()) n |= static_cast<unsigned char>(raw[i+2]);
        b64 += tbl[(n >> 18) & 0x3F];
        b64 += tbl[(n >> 12) & 0x3F];
        b64 += (i + 1 < raw.size()) ? tbl[(n >> 6) & 0x3F] : '=';
        b64 += (i + 2 < raw.size()) ? tbl[n & 0x3F] : '=';
    }
    // 28 字节 = IV(12) + TAG(16)，密文长度为 0，解密应成功但返回空字符串
    auto result = CryptoUtil::decrypt(b64, key);
    // 密文长度为 0 时，GCM tag 验证应失败（因为没有密文）
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// Fix #249: PasswordUtil PBKDF2 fallback 路径测试
// 原测试只覆盖 bcrypt 路径，verifyPassword 代码明确支持 PBKDF2 格式但无测试
// ============================================================================

TEST_CASE("PasswordUtil: PBKDF2 hash verifies correctly", "[password_pbkdf2]") {
    // Fix #249: 生成 PBKDF2 哈希并验证
    // 由于 hashPassword 优先使用 bcrypt，我们手动构造一个 PBKDF2 格式哈希
    // 格式: $pbkdf2-sha256$i=10000$salt_b64$hash_b64
    // 使用 PasswordUtil 内部相同逻辑生成
    const std::string password = "test_pbkdf2_password";

    // 通过 verifyPassword 测试已知 PBKDF2 哈希格式
    // 先用 hashPassword 生成（可能是 bcrypt），然后用 PBKDF2 路径验证
    // 由于无法强制走 PBKDF2，我们测试 verifyPassword 对 PBKDF2 格式的解析

    // 构造一个有效的 PBKDF2 哈希
    #include <openssl/evp.h>
    unsigned char salt[16] = {0};
    unsigned char hash[32] = {0};
    PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                      salt, 16, 10000, EVP_sha256(), 32, hash);

    // base64 编码
    auto b64encode = [](const unsigned char* data, int len) -> std::string {
        static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string r;
        for (int i = 0; i < len; i += 3) {
            unsigned int n = static_cast<unsigned char>(data[i]) << 16;
            if (i+1 < len) n |= static_cast<unsigned char>(data[i+1]) << 8;
            if (i+2 < len) n |= static_cast<unsigned char>(data[i+2]);
            r += tbl[(n>>18)&0x3F];
            r += tbl[(n>>12)&0x3F];
            r += (i+1<len) ? tbl[(n>>6)&0x3F] : '=';
            r += (i+2<len) ? tbl[n&0x3F] : '=';
        }
        return r;
    };

    std::string salt_b64 = b64encode(salt, 16);
    std::string hash_b64 = b64encode(hash, 32);
    std::string pbkdf2_hash = "$pbkdf2-sha256$i=10000$" + salt_b64 + "$" + hash_b64;

    // 正确密码验证应返回 true
    auto verify_result = PasswordUtil::verifyPassword(password, pbkdf2_hash);
    REQUIRE(verify_result.ok());
    REQUIRE(verify_result.value() == true);

    // 错误密码验证应返回 false
    auto wrong_result = PasswordUtil::verifyPassword("wrong_password", pbkdf2_hash);
    REQUIRE(wrong_result.ok());
    REQUIRE(wrong_result.value() == false);
}

TEST_CASE("PasswordUtil: PBKDF2 with invalid format returns failure", "[password_pbkdf2]") {
    // Fix #249: 无效 PBKDF2 格式应返回失败
    auto result = PasswordUtil::verifyPassword("test", "$pbkdf2-sha256$invalid");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("PasswordUtil: PBKDF2 with bad iterations returns failure", "[password_pbkdf2]") {
    // Fix #249: 无效迭代次数应返回失败
    auto result = PasswordUtil::verifyPassword("test", "$pbkdf2-sha256$i=abc$salt$hash");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("PasswordUtil: $2a$ prefix bcrypt hash verifies", "[password]") {
    // Fix #249: $2a$ 前缀的 bcrypt 哈希也应能验证
    // 使用 $2b$ 生成的哈希，将前缀改为 $2a$ 测试兼容性
    auto hash_result = PasswordUtil::hashPassword("compat_test");
    REQUIRE(hash_result.ok());
    std::string hash_2b = hash_result.value();
    REQUIRE(hash_2b.substr(0, 4) == "$2b$");

    // 改为 $2a$ 前缀
    std::string hash_2a = "$2a$" + hash_2b.substr(4);
    auto verify_result = PasswordUtil::verifyPassword("compat_test", hash_2a);
    REQUIRE(verify_result.ok());
    REQUIRE(verify_result.value() == true);
}

TEST_CASE("PasswordUtil: $2y$ prefix bcrypt hash verifies", "[password]") {
    // Fix #249: $2y$ 前缀的 bcrypt 哈希也应能验证
    auto hash_result = PasswordUtil::hashPassword("compat_test_y");
    REQUIRE(hash_result.ok());
    std::string hash_2b = hash_result.value();

    // 改为 $2y$ 前缀
    std::string hash_2y = "$2y$" + hash_2b.substr(4);
    auto verify_result = PasswordUtil::verifyPassword("compat_test_y", hash_2y);
    REQUIRE(verify_result.ok());
    REQUIRE(verify_result.value() == true);
}

// ============================================================================
// Fix #250: JwtUtil parseTokenUnverified 测试
// 该公开方法用于不验证签名地解析 token（登出黑名单流程），零覆盖
// ============================================================================

TEST_CASE("JwtUtil: parseTokenUnverified extracts payload", "[jwt_unverified]") {
    // Fix #250: 有效 token 解析返回正确 payload
    const std::string secret(32, 's');
    auto token_result = JwtUtil::generateTokens("user-456", "testuser", "operator", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    auto payload = JwtUtil::parseTokenUnverified(token_result.value().access_token);
    REQUIRE(payload.ok());
    REQUIRE(payload.value().user_id == "user-456");
    REQUIRE(payload.value().username == "testuser");
    REQUIRE(payload.value().role == "operator");
    REQUIRE(payload.value().type == "access");
    REQUIRE_FALSE(payload.value().jti.empty());
    REQUIRE(payload.value().exp > 0);
}

TEST_CASE("JwtUtil: parseTokenUnverified on invalid token returns error", "[jwt_unverified]") {
    // Fix #250: 无效 token 解析返回错误
    const std::string secret(32, 's');
    auto payload = JwtUtil::parseTokenUnverified("not.a.valid.token");
    REQUIRE_FALSE(payload.ok());
}

TEST_CASE("JwtUtil: parseTokenUnverified does not check signature", "[jwt_unverified]") {
    // Fix #250: 篡改 token 仍能解析（因为不验证签名）
    const std::string secret(32, 's');
    auto token_result = JwtUtil::generateTokens("user-789", "tamperuser", "viewer", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    // 篡改 token 签名部分
    std::string tampered = token_result.value().access_token;
    auto last_dot = tampered.rfind('.');
    if (last_dot != std::string::npos && last_dot + 1 < tampered.size()) {
        tampered[last_dot + 1] = (tampered[last_dot + 1] == 'A') ? 'B' : 'A';
    }

    // verifyToken 应失败（签名不匹配）
    auto verify_result = JwtUtil::verifyToken(tampered, secret);
    REQUIRE_FALSE(verify_result.ok());

    // parseTokenUnverified 应成功（不验证签名）
    auto parse_result = JwtUtil::parseTokenUnverified(tampered);
    REQUIRE(parse_result.ok());
    REQUIRE(parse_result.value().user_id == "user-789");
    REQUIRE(parse_result.value().username == "tamperuser");
}

TEST_CASE("JwtUtil: parseTokenUnverified extracts refresh token", "[jwt_unverified]") {
    // Fix #250: 解析 refresh token
    const std::string secret(32, 's');
    auto token_result = JwtUtil::generateTokens("user-ref", "refuser", "admin", secret, 3600, 86400);
    REQUIRE(token_result.ok());

    auto payload = JwtUtil::parseTokenUnverified(token_result.value().refresh_token);
    REQUIRE(payload.ok());
    REQUIRE(payload.value().type == "refresh");
    REQUIRE(payload.value().user_id == "user-ref");
}

// ============================================================================
// Fix #251: TokenBlacklist 过期清理和 exp 参数测试
// 原测试只调用 add(jti) 不带 exp，未测试过期逻辑
// ============================================================================

TEST_CASE("TokenBlacklist: add with exp timestamp", "[jwt_blacklist_exp]") {
    // Fix #251: add(jti, exp) 带过期时间参数
    auto& blacklist = TokenBlacklist::instance();
    const std::string jti = "exp-test-" + generateUuid();

    // 设置过期时间为未来 1 小时
    int64_t future_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

    blacklist.add(jti, future_exp);
    REQUIRE(blacklist.isBlacklisted(jti));
}

TEST_CASE("TokenBlacklist: expired entry is not blacklisted", "[jwt_blacklist_exp]") {
    // Fix #251: 过期条目被 isBlacklisted 视为未黑名单（自动清理）
    auto& blacklist = TokenBlacklist::instance();
    const std::string jti = "expired-test-" + generateUuid();

    // 设置过期时间为过去 1 秒（已过期）
    int64_t past_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 1;

    blacklist.add(jti, past_exp);

    // 过期条目应被视为未黑名单
    REQUIRE_FALSE(blacklist.isBlacklisted(jti));
}

TEST_CASE("TokenBlacklist: add with default exp (24h) is blacklisted", "[jwt_blacklist_exp]") {
    // Fix #251: add(jti) 不带 exp 时默认 24h 过期
    auto& blacklist = TokenBlacklist::instance();
    const std::string jti = "default-exp-test-" + generateUuid();

    blacklist.add(jti);  // 不带 exp，默认 24h
    REQUIRE(blacklist.isBlacklisted(jti));
}

TEST_CASE("TokenBlacklist: add purges already-expired entries", "[jwt_blacklist_exp]") {
    // Fix #251: add 时自动清理已过期条目
    auto& blacklist = TokenBlacklist::instance();
    const std::string expired_jti = "purge-expired-" + generateUuid();
    const std::string active_jti = "purge-active-" + generateUuid();

    // 添加一个已过期的条目
    int64_t past_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 100;
    blacklist.add(expired_jti, past_exp);

    // 添加一个活跃条目（触发清理）
    int64_t future_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
    blacklist.add(active_jti, future_exp);

    // 活跃条目应在黑名单中
    REQUIRE(blacklist.isBlacklisted(active_jti));
    // 过期条目应已被清理（isBlacklisted 返回 false）
    REQUIRE_FALSE(blacklist.isBlacklisted(expired_jti));
}

TEST_CASE("TokenBlacklist: same jti re-added with new exp", "[jwt_blacklist_exp]") {
    // Fix #251: 重新添加同一 jti 时更新过期时间
    auto& blacklist = TokenBlacklist::instance();
    const std::string jti = "readd-test-" + generateUuid();

    // 先添加已过期
    int64_t past_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 1;
    blacklist.add(jti, past_exp);
    REQUIRE_FALSE(blacklist.isBlacklisted(jti));

    // 重新添加为未过期
    int64_t future_exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
    blacklist.add(jti, future_exp);
    REQUIRE(blacklist.isBlacklisted(jti));
}
