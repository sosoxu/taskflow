#pragma once
#include <string>

namespace taskflow::common::util {

// Escape a string so it can be safely embedded inside single quotes in a
// POSIX shell command. Each single quote becomes '\'' (close quote, escaped
// quote, reopen quote).
// Fix #328: 拼接远程 shell 命令时必须经由此函数包裹用户可控输入
// （remote_dir / worker_binary_path / config_path 等），否则可注入命令。
inline std::string shellSingleQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return "'" + out + "'";
}

}  // namespace taskflow::common::util
