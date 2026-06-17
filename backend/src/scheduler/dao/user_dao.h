#pragma once
#include <string>
#include <vector>
#include "common/result/result.h"
#include "common/models/user.h"

namespace taskflow::scheduler::dao {

class UserDao {
public:
    // 创建用户，返回用户 ID
    common::result::Result<std::string> create(const std::string& username,
                                                const std::string& password_hash,
                                                const std::string& role);

    // 按 ID 查找
    common::result::Result<common::models::User> findById(const std::string& id);

    // 按用户名查找
    common::result::Result<common::models::User> findByUsername(const std::string& username);

    // 修改角色
    common::result::Result<void> updateRole(const std::string& id, const std::string& role);

    // 列表（分页）
    common::result::Result<std::vector<common::models::User>> list(int offset, int limit);

    // 删除
    common::result::Result<void> remove(const std::string& id);

    // 软删除
    common::result::Result<void> softDelete(const std::string& id);
};

}  // namespace taskflow::scheduler::dao
