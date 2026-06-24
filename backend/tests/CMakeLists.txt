# Core logic unit tests (no database dependency)
add_executable(taskflow_tests
    test_dag_engine.cpp
    test_dag_validator.cpp
    test_dispatcher.cpp
    test_cron_parser.cpp
    test_security_util.cpp
    test_executor.cpp
    test_result.cpp
    test_models.cpp
    test_executor_extended.cpp
    test_dag_cron_extended.cpp
    test_cron_long_task.cpp
    test_config.cpp
    test_api_response.cpp
    test_resource_collector.cpp
    test_dag_driver.cpp
    test_regression_116.cpp
    test_round8_fixes.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/engine/dag_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/crypto_util.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/task_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/command_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/script_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/sql_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/log_sink.cpp
)

target_include_directories(taskflow_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(taskflow_tests PRIVATE
    taskflow_common
    Catch2::Catch2WithMain
    PkgConfig::PQXX
)
if(LINUX)
    target_link_libraries(taskflow_tests PRIVATE crypt)
endif()

# Register tests
include(CTest)
include(Catch)
catch_discover_tests(taskflow_tests)

# Performance benchmarks (microbenchmarks for core components)
add_executable(perf_tests
    test_perf.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/engine/dag_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/crypto_util.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/task_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/command_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/script_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/sql_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/executor/log_sink.cpp
)

target_include_directories(perf_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(perf_tests PRIVATE
    taskflow_common
    Catch2::Catch2WithMain
    PkgConfig::PQXX
    OpenSSL::Crypto
    OpenSSL::SSL
)
if(LINUX)
    target_link_libraries(perf_tests PRIVATE crypt)
endif()

catch_discover_tests(perf_tests
    TEST_PREFIX "perf:"
    REPORTER compact
)
