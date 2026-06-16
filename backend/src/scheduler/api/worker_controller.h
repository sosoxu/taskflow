#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/dao/worker_dao.h"

namespace taskflow::scheduler::api {

class WorkerController : public drogon::HttpController<WorkerController, false> {
public:
    explicit WorkerController(std::shared_ptr<dao::WorkerDao> worker_dao);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkerController::listWorkers, "/api/v1/workers", drogon::Get);
    METHOD_LIST_END

    void listWorkers(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<dao::WorkerDao> worker_dao_;
};

}  // namespace taskflow::scheduler::api
