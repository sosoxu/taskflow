# Core logic unit tests (no database dependency)
add_executable(taskflow_tests
    test_dag_engine.cpp
    test_dag_validator.cpp
    test_dispatcher.cpp
    test_cron_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/engine/dag_engine.cpp
)

target_include_directories(taskflow_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(taskflow_tests PRIVATE
    taskflow_common
    Catch2::Catch2WithMain
)

# Register tests
include(CTest)
include(Catch)
catch_discover_tests(taskflow_tests)
