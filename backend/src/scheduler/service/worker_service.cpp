#include "scheduler/service/worker_service.h"

#include <spdlog/spdlog.h>

namespace taskflow::scheduler::service {

WorkerService::WorkerService(std::shared_ptr<dao::WorkerDao> worker_dao)
    : worker_dao_(std::move(worker_dao)) {}

common::result::Result<std::vector<common::models::WorkerInfo>> WorkerService::listWorkers() {
    return worker_dao_->listAll();
}

}  // namespace taskflow::scheduler::service
