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
