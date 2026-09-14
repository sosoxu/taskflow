#pragma once
#include <string>

namespace taskflow::common::util {

// Fix #342: isValidInstanceId 唯一定义（此前在 worker 侧 5 个文件逐字重复）。
// 校验任务实例 ID 不含路径分隔符或 ".."，防止以 ID 拼接日志/脚本路径时的
// 路径穿越攻击（Fix #298 引入）。
inline bool isValidInstanceId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (c == '/' || c == '\\' || c == '\0') return false;
    }
    if (id.find("..") != std::string::npos) return false;
    if (id == ".") return false;
    return true;
}

}  // namespace taskflow::common::util
