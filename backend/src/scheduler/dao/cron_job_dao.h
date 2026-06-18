#pragma once
#include <string>
#include <vector>
#include "common/result/result.h"
#include "common/models/cron_job.h"

namespace taskflow::scheduler::dao {

class CronJobDao {
public:
    common::result::Result<std::string> create(
        const std::string& workflow_id,
        const std::string& cron_expression,
        const std::string& next_trigger_time = "");

    common::result::Result<common::models::CronJob> findById(const std::string& id);

    common::result::Result<common::models::CronJob> findByWorkflowId(const std::string& workflow_id);

    common::result::Result<void> update(
        const std::string& id,
        const std::string& cron_expression,
        bool enabled);

    common::result::Result<void> toggleEnabled(const std::string& id, bool enabled);

    common::result::Result<void> updateNextTriggerTime(
        const std::string& id,
        const std::string& next_trigger_time);

    // 查询当前时间前应触发的 CronJob
    common::result::Result<std::vector<common::models::CronJob>> listDue(
        const std::string& current_time);

    // 查询 next_trigger_time 为 NULL 的 CronJob（修复旧数据）
    common::result::Result<std::vector<common::models::CronJob>> listNullTriggerTime();

    // 禁用指定工作流的所有 CronJob
    common::result::Result<void> disableByWorkflowId(const std::string& workflow_id);
};

}  // namespace taskflow::scheduler::dao
