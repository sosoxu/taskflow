#include "worker/executor/log_sink.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <spdlog/spdlog.h>

namespace taskflow::worker::executor {

FileLogSink::FileLogSink(const std::string& base_dir) : base_dir_(base_dir) {
    std::filesystem::create_directories(base_dir_);
}

std::string FileLogSink::makeLogDir(const std::string& workflow_instance_id) {
    return base_dir_ + "/" + workflow_instance_id;
}

std::string FileLogSink::makeLogPath(const std::string& workflow_instance_id,
                                      const std::string& task_instance_id) {
    return makeLogDir(workflow_instance_id) + "/" + task_instance_id + ".log";
}

bool FileLogSink::write(const std::string& workflow_instance_id,
                         const std::string& task_instance_id,
                         const std::string& data) {
    auto dir = makeLogDir(workflow_instance_id);
    std::filesystem::create_directories(dir);

    auto path = makeLogPath(workflow_instance_id, task_instance_id);
    std::ofstream ofs(path, std::ios::app | std::ios::binary);
    if (!ofs.is_open()) {
        spdlog::error("FileLogSink: failed to open log file: {}", path);
        return false;
    }
    ofs << data;
    return true;
}

std::string FileLogSink::read(const std::string& workflow_instance_id,
                               const std::string& task_instance_id) {
    auto path = makeLogPath(workflow_instance_id, task_instance_id);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(ifs),
                        std::istreambuf_iterator<char>());
}

bool FileLogSink::exists(const std::string& workflow_instance_id,
                          const std::string& task_instance_id) {
    return std::filesystem::exists(makeLogPath(workflow_instance_id, task_instance_id));
}

void FileLogSink::cleanup(int retention_days) {
    if (!std::filesystem::exists(base_dir_)) return;

    auto now = std::filesystem::file_time_type::clock::now();
    auto retention = std::chrono::hours(24 * retention_days);

    for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
        if (!entry.is_directory()) continue;
        auto last_modified = std::filesystem::last_write_time(entry);
        if (now - last_modified > retention) {
            std::filesystem::remove_all(entry.path());
            spdlog::info("FileLogSink: cleaned up expired log directory {}", entry.path().string());
        }
    }
}

std::string FileLogSink::getLogPath(const std::string& workflow_instance_id,
                                     const std::string& task_instance_id) {
    return makeLogPath(workflow_instance_id, task_instance_id);
}

ElasticLogSink::ElasticLogSink(const std::string& base_dir,
                                 const std::string& es_url,
                                 const std::string& es_index)
    : file_sink_(base_dir), es_url_(es_url), es_index_(es_index) {
    spdlog::info("ElasticLogSink: initialized with ES URL={}, index={} (currently using file fallback)",
                 es_url_, es_index_);
}

bool ElasticLogSink::write(const std::string& workflow_instance_id,
                            const std::string& task_instance_id,
                            const std::string& data) {
    // Elasticsearch push not yet implemented - falling back to file storage
    return file_sink_.write(workflow_instance_id, task_instance_id, data);
}

std::string ElasticLogSink::read(const std::string& workflow_instance_id,
                                  const std::string& task_instance_id) {
    // Elasticsearch read not yet implemented - falling back to file storage
    return file_sink_.read(workflow_instance_id, task_instance_id);
}

bool ElasticLogSink::exists(const std::string& workflow_instance_id,
                             const std::string& task_instance_id) {
    return file_sink_.exists(workflow_instance_id, task_instance_id);
}

void ElasticLogSink::cleanup(int retention_days) {
    // Elasticsearch index cleanup not yet implemented - delegating to file cleanup
    file_sink_.cleanup(retention_days);
}

std::string ElasticLogSink::getLogPath(const std::string& workflow_instance_id,
                                        const std::string& task_instance_id) {
    return file_sink_.getLogPath(workflow_instance_id, task_instance_id);
}

std::unique_ptr<LogSink> createLogSink(const std::string& sink_type,
                                        const std::string& base_dir,
                                        const std::string& es_url,
                                        const std::string& es_index) {
    if (sink_type == "file" || sink_type.empty()) {
        return std::make_unique<FileLogSink>(base_dir);
    } else if (sink_type == "elasticsearch") {
        return std::make_unique<ElasticLogSink>(base_dir, es_url, es_index);
    }
    spdlog::warn("createLogSink: unknown sink type '{}', falling back to file", sink_type);
    return std::make_unique<FileLogSink>(base_dir);
}

}  // namespace taskflow::worker::executor
