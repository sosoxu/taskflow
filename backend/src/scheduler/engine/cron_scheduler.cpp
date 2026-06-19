#include "scheduler/engine/cron_scheduler.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/util/uuid.h"
#include "scheduler/engine/cron_parser.h"

namespace taskflow::scheduler::engine {

namespace {

std::string formatCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    gmtime_r(&time_t_now, &tm_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string computeNextTriggerTime(const std::string& cron_expression) {
    auto result = CronParser::getNextTrigger(cron_expression, formatCurrentTime());
    if (result.ok()) {
        return result.value();
    }
    // Fallback: return current time + 60 seconds
    auto now = std::chrono::system_clock::now();
    auto next = now + std::chrono::seconds(60);
    auto time_t_next = std::chrono::system_clock::to_time_t(next);
    std::tm tm_next{};
    gmtime_r(&time_t_next, &tm_next);
    std::ostringstream oss;
    oss << std::put_time(&tm_next, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace

CronScheduler::CronScheduler(std::shared_ptr<grpc::LeaderElection> leader_election)
    : leader_election_(std::move(leader_election)) {}

void CronScheduler::start() {
    running_ = true;
    thread_ = std::thread(&CronScheduler::cronLoop, this);
    spdlog::info("CronScheduler started");
}

void CronScheduler::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    spdlog::info("CronScheduler stopped");
}

void CronScheduler::cronLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!running_) {
            break;
        }

        // 只有 leader 节点才执行定时调度
        if (leader_election_ && !leader_election_->isLeader()) {
            continue;
        }

        std::string current_time = formatCurrentTime();

        // Fix cron jobs with NULL next_trigger_time (legacy data)
        auto null_result = cron_job_dao_.listNullTriggerTime();
        if (null_result.ok()) {
            for (const auto& job : null_result.value()) {
                std::string next_time = computeNextTriggerTime(job.cron_expression);
                cron_job_dao_.updateNextTriggerTime(job.id, next_time);
                spdlog::info("CronScheduler: fixed NULL next_trigger_time for cron job {} to {}",
                             job.id, next_time);
            }
        }

        auto due_result = cron_job_dao_.listDue(current_time);
        if (!due_result.ok()) {
            spdlog::error("CronScheduler: failed to list due jobs: {}",
                          due_result.error());
            continue;
        }

        const auto& due_jobs = due_result.value();
        for (const auto& job : due_jobs) {
            try {
                triggerCronJob(job);
            } catch (const std::exception& e) {
                spdlog::error("CronScheduler: error triggering cron job {}: {}",
                              job.id, e.what());
            }
        }
    }
}

void CronScheduler::triggerCronJob(const common::models::CronJob& cron_job) {
    // Fix #185: Advance next_trigger_time BEFORE creating the instance. If this
    // were done at the end (as before), a failure in instance creation would
    // leave next_trigger_time unchanged, causing the job to fire again on the
    // next tick (duplicate triggers). Advancing first guarantees at-most-once
    // triggering even when subsequent steps fail.
    std::string next_time = computeNextTriggerTime(cron_job.cron_expression);
    auto update_result = cron_job_dao_.updateNextTriggerTime(cron_job.id, next_time);
    if (!update_result.ok()) {
        spdlog::error(
            "CronScheduler: failed to update next trigger time for cron job "
            "{}: {}",
            cron_job.id, update_result.error());
        return;
    }

    // Find the workflow
    auto workflow_result = workflow_dao_.findById(cron_job.workflow_id);
    if (!workflow_result.ok()) {
        spdlog::error("CronScheduler: workflow {} not found for cron job {}",
                      cron_job.workflow_id, cron_job.id);
        return;
    }

    const auto& workflow = workflow_result.value();

    // Create a WorkflowInstance
    auto instance_result = workflow_instance_dao_.create(
        workflow.id, workflow.version, "cron", workflow.creator_id);
    if (!instance_result.ok()) {
        spdlog::error("CronScheduler: failed to create workflow instance: {}",
                      instance_result.error());
        return;
    }

    const auto& instance_id = instance_result.value();
    spdlog::info("CronScheduler: created workflow instance {} for cron job {}",
                 instance_id, cron_job.id);

    // Parse dag_json and create TaskInstances for each node
    const auto& dag = workflow.dag_json;
    if (dag.contains("nodes") && dag["nodes"].is_array()) {
        std::vector<std::tuple<std::string, std::string, int, std::string>> tasks;

        for (const auto& node : dag["nodes"]) {
            std::string task_id = node.value("task_id", node.value("id", ""));
            std::string node_id = node.value("id", "");

            // Look up the task to get its current version and name
            auto task_result = task_dao_.findById(task_id);
            if (!task_result.ok()) {
                spdlog::warn(
                    "CronScheduler: task {} not found, skipping in instance "
                    "{}",
                    task_id, instance_id);
                continue;
            }

            const auto& task = task_result.value();
            tasks.emplace_back(task.id, task.name, task.version, node_id);
        }

        if (!tasks.empty()) {
            auto batch_result = task_instance_dao_.batchCreate(instance_id, tasks);
            if (!batch_result.ok()) {
                spdlog::error(
                    "CronScheduler: failed to batch create task instances: {}",
                    batch_result.error());
            }
        }
    }
}

}  // namespace taskflow::scheduler::engine
