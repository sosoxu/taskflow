#pragma once

#include <string>
#include <vector>

#include "common/result/result.h"

namespace taskflow::common::util {

class CryptoUtil {
public:
    CryptoUtil() = delete;

    static common::result::Result<std::string> encrypt(const std::string& plaintext,
                                                        const std::string& key);

    static common::result::Result<std::string> decrypt(const std::string& ciphertext_b64,
                                                        const std::string& key);
};

}  // namespace taskflow::common::util
