add_executable(worker
    main.cpp
    grpc/worker_client.cpp
    executor/task_executor.cpp
    executor/command_executor.cpp
    executor/script_executor.cpp
    executor/sql_executor.cpp
)

target_link_libraries(worker PRIVATE
    taskflow_common
    taskflow_proto
    gRPC::grpc++
    PkgConfig::PQXX
)
