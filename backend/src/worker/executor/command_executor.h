#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include "worker/executor/task_executor.h"

namespace taskflow::worker::executor {

class CommandExecutor : public TaskExecutorBase {
public:
    TaskResult execute(const std::string& task_instance_id,
                       const nlohmann::json& config,
                       int timeout,
                       const std::string& log_dir) override;
};

}  // namespace taskflow::worker::executor
