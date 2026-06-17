#pragma once

#include <memory>
#include <vector>
#include "common/models/worker_info.h"
#include "common/result/result.h"
#include "scheduler/dao/worker_dao.h"

namespace taskflow::scheduler::service {

class WorkerService {
public:
    explicit WorkerService(std::shared_ptr<dao::WorkerDao> worker_dao);

    common::result::Result<std::vector<common::models::WorkerInfo>> listWorkers();

private:
    std::shared_ptr<dao::WorkerDao> worker_dao_;
};

}  // namespace taskflow::scheduler::service
