// 预编译头文件 - 包含高频使用的系统库和第三方库头
// 通过 CMake target_precompile_headers 自动预编译，减少每个 .cpp 的编译时间
// 注意：只包含 scheduler 和 worker 共用的库，Drogon 仅 scheduler 使用，不放入 PCH

// C++ 标准库
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <utility>

// 第三方库（scheduler 和 worker 共用）
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
