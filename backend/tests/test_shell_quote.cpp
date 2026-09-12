// Fix #328: shellSingleQuote 单元测试——远程部署命令拼接的安全转义
#include <catch2/catch_test_macros.hpp>

#include "common/util/shell_quote.h"

using taskflow::common::util::shellSingleQuote;

TEST_CASE("shellSingleQuote: 普通路径原样包裹", "[shell_quote]") {
    REQUIRE(shellSingleQuote("abc") == "'abc'");
    REQUIRE(shellSingleQuote("/opt/taskflow") == "'/opt/taskflow'");
    REQUIRE(shellSingleQuote("/opt/task flow/bin") == "'/opt/task flow/bin'");
    REQUIRE(shellSingleQuote("") == "''");
}

TEST_CASE("shellSingleQuote: 单引号被正确转义", "[shell_quote]") {
    REQUIRE(shellSingleQuote("it's") == "'it'\\''s'");
    REQUIRE(shellSingleQuote("a'b'c") == "'a'\\''b'\\''c'");
    // 全部为单引号：期望值由转义规则组合构造，避免手写字面量出错
    const std::string escaped_quote = "'\\''";  // 每个 ' 变成 '\''（4 字符）
    REQUIRE(shellSingleQuote("''") == "'" + escaped_quote + escaped_quote + "'");
}

TEST_CASE("shellSingleQuote: 命令注入向量被当作字面量", "[shell_quote]") {
    // 分号分隔的第二条命令
    REQUIRE(shellSingleQuote("/tmp/x; touch /tmp/pwned") == "'/tmp/x; touch /tmp/pwned'");
    // 反引号与命令替换
    REQUIRE(shellSingleQuote("$(id)") == "'$(id)'");
    REQUIRE(shellSingleQuote("`id`") == "'`id`'");
    // 管道与重定向
    REQUIRE(shellSingleQuote("a | b > /tmp/c") == "'a | b > /tmp/c'");
    // 组合攻击：引号 + 命令替换
    REQUIRE(shellSingleQuote("a'b$(rm -rf /)'c") == "'a'\\''b$(rm -rf /)'\\''c'");
    // 换行引入新命令
    REQUIRE(shellSingleQuote("x\ny") == "'x\ny'");
}
