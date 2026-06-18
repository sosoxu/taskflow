#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include "worker/executor/task_executor.h"

namespace taskflow::worker::executor {

class ScriptExecutor : public TaskExecutorBase {
public:
    TaskResult execute(const std::string& task_instance_id,
                       const nlohmann::json& config,
                       int timeout,
                       const std::string& log_dir,
                       std::function<void(pid_t)> pid_callback = nullptr,
                       LogSink* log_sink = nullptr) override;
};

}  // namespace taskflow::worker::executor
