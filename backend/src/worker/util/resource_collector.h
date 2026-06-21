#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>

namespace taskflow::worker::util {

struct ResourceUsage {
    double cpu_usage;     // percentage 0-100
    double memory_usage;  // percentage 0-100
};

class ResourceCollector {
public:
    ResourceCollector() = delete;

    static ResourceUsage collect() {
        ResourceUsage usage;
        usage.cpu_usage = getCpuUsage();
        usage.memory_usage = getMemoryUsage();
        return usage;
    }

private:
    static double getCpuUsage() {
        auto readCpuTimes = []() -> std::pair<unsigned long long, unsigned long long> {
            std::ifstream ifs("/proc/stat");
            if (!ifs.is_open()) {
                return {0, 0};
            }

            std::string line;
            if (!std::getline(ifs, line)) {
                return {0, 0};
            }

            std::istringstream iss(line);
            std::string cpu_label;
            iss >> cpu_label;  // skip "cpu"

            unsigned long long user = 0, nice = 0, system = 0, idle = 0;
            unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;

            if (!(iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) {
                return {0, 0};
            }

            unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            unsigned long long idle_total = idle + iowait;

            return {total, idle_total};
        };

        auto [total1, idle1] = readCpuTimes();
        if (total1 == 0) {
            return 0.0;
        }

        usleep(100000);  // 100ms

        auto [total2, idle2] = readCpuTimes();
        if (total2 == 0) {
            return 0.0;
        }

        unsigned long long delta_total = total2 - total1;
        unsigned long long delta_idle = idle2 - idle1;

        if (delta_total == 0) {
            return 0.0;
        }

        return (1.0 - static_cast<double>(delta_idle) / static_cast<double>(delta_total)) * 100.0;
    }

    static double getMemoryUsage() {
        std::ifstream ifs("/proc/meminfo");
        if (!ifs.is_open()) {
            return 0.0;
        }

        unsigned long long mem_total = 0;
        unsigned long long mem_available = 0;
        unsigned long long mem_free = 0;
        bool has_mem_available = false;

        std::string line;
        while (std::getline(ifs, line)) {
            if (line.compare(0, 9, "MemTotal:") == 0) {
                std::istringstream iss(line.substr(9));
                iss >> mem_total;
            } else if (line.compare(0, 13, "MemAvailable:") == 0) {
                std::istringstream iss(line.substr(13));
                iss >> mem_available;
                has_mem_available = true;
            } else if (line.compare(0, 8, "MemFree:") == 0) {
                std::istringstream iss(line.substr(8));
                iss >> mem_free;
            }

            if (mem_total != 0 && has_mem_available) {
                break;
            }
        }

        if (mem_total == 0) {
            return 0.0;
        }

        // Fix #292: 若系统不支持 MemAvailable（内核 < 3.14），回退使用 MemFree
        // 原实现 mem_available=0 时返回 (mem_total-0)/mem_total*100 = 100%，错误报告内存已满
        unsigned long long mem_used;
        if (has_mem_available) {
            mem_used = mem_total - mem_available;
        } else if (mem_free > 0) {
            mem_used = mem_total - mem_free;
        } else {
            // 无法采集可用内存信息，返回 0 表示无法确定
            return 0.0;
        }

        return (static_cast<double>(mem_used) / static_cast<double>(mem_total)) * 100.0;
    }
};

}  // namespace taskflow::worker::util
