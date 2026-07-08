#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#elif __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif
#include <spdlog/spdlog.h>

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

        // Fix #323: 用跨调用 delta 计算替代阻塞 sleep(100ms)。
        // 旧实现每次调用 usleep(100000) 阻塞心跳线程 100ms，且 100ms 窗口在
        // HZ=100 的系统上只有 ~10 ticks，低负载 (<5%) 时 delta_idle ≈ delta_total，
        // 结果被舍入为 0，导致前端"始终显示 0"。
        // 新实现用静态变量保存上次采样，每次调用（间隔 ~10s 心跳）的 delta
        // 约 1000 ticks，能稳定检测到 0.1% 级别的 CPU 活动，且不阻塞心跳线程。
        static unsigned long long last_total = 0;
        static unsigned long long last_idle = 0;
        static bool initialized = false;
        static bool proc_stat_unavailable = false;

        auto [total, idle] = readCpuTimes();
        if (total == 0) {
            if (!proc_stat_unavailable) {
                spdlog::warn("ResourceCollector: failed to read /proc/stat (unavailable in this environment, CPU usage will report 0)");
                proc_stat_unavailable = true;
            }
            return 0.0;
        }

        if (!initialized) {
            // 第一次调用：sleep 100ms 后再采样一次，建立基线
            usleep(100000);
            auto [total2, idle2] = readCpuTimes();
            if (total2 == 0) {
                if (!proc_stat_unavailable) {
                    spdlog::warn("ResourceCollector: failed to read /proc/stat on second attempt");
                    proc_stat_unavailable = true;
                }
                return 0.0;
            }
            last_total = total2;
            last_idle = idle2;
            initialized = true;
            unsigned long long dt = total2 - total;
            unsigned long long di = idle2 - idle;
            if (dt == 0) return 0.0;
            return (1.0 - static_cast<double>(di) / static_cast<double>(dt)) * 100.0;
        }

        unsigned long long delta_total = total - last_total;
        unsigned long long delta_idle = idle - last_idle;
        last_total = total;
        last_idle = idle;

        if (delta_total == 0) {
            return 0.0;
        }

        // 防御：计数器回绕（极罕见）
        if (delta_total > 1000000000ULL) {
            spdlog::warn("ResourceCollector: suspicious delta_total={}, resetting baseline", delta_total);
            return 0.0;
        }

        return (1.0 - static_cast<double>(delta_idle) / static_cast<double>(delta_total)) * 100.0;
    }

    static double getMemoryUsage() {
        // 首选 /proc/meminfo（更精确，包含 MemAvailable）
        auto result = getMemoryUsageFromProc();
        if (result >= 0.0) {
            return result;
        }

        // 回退到 sysinfo() 系统调用（不依赖 /proc 文件系统）
        static bool proc_fallback_logged = false;
        if (!proc_fallback_logged) {
            spdlog::info("ResourceCollector: /proc/meminfo unavailable, falling back to sysinfo()");
            proc_fallback_logged = true;
        }
        return getMemoryUsageFromSysinfo();
    }

    // 返回 >= 0 表示成功，< 0 表示失败
    static double getMemoryUsageFromProc() {
        std::ifstream ifs("/proc/meminfo");
        if (!ifs.is_open()) {
            return -1.0;
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
            return -1.0;
        }

        // Fix #292: 若系统不支持 MemAvailable（内核 < 3.14），回退使用 MemFree
        // 原实现 mem_available=0 时返回 (mem_total-0)/mem_total*100 = 100%，错误报告内存已满
        unsigned long long mem_used;
        if (has_mem_available) {
            mem_used = mem_total - mem_available;
        } else if (mem_free > 0) {
            mem_used = mem_total - mem_free;
        } else {
            return -1.0;
        }

        return (static_cast<double>(mem_used) / static_cast<double>(mem_total)) * 100.0;
    }

    // 使用 sysinfo() 系统调用回退，不依赖 /proc 文件系统
    static double getMemoryUsageFromSysinfo() {
#ifdef __linux__
        struct sysinfo si;
        if (sysinfo(&si) != 0) {
            return 0.0;
        }

        unsigned long long total_ram = static_cast<unsigned long long>(si.totalram) * si.mem_unit;
        unsigned long long free_ram = static_cast<unsigned long long>(si.freeram) * si.mem_unit;
        unsigned long long buffer_ram = static_cast<unsigned long long>(si.bufferram) * si.mem_unit;

        if (total_ram == 0) {
            return 0.0;
        }

        // sysinfo 只有 freeram（含 buffer/cache），Linux 空闲内存 = free + buffer + cache
        // sharedram 在新版内核中为 Shmem（共享内存，属于已用内存）
        // 实际已用 = total - free - buffer（buffer/cache 可回收，类似 MemAvailable 的近似）
        unsigned long long used_ram = total_ram - free_ram - buffer_ram;
        if (used_ram > total_ram) {
            used_ram = total_ram - free_ram;  // buffer 为 0 或计算异常时回退
        }

        return (static_cast<double>(used_ram) / static_cast<double>(total_ram)) * 100.0;
#elif __APPLE__
        // macOS: use sysctl to get memory info
        int64_t total_ram = 0;
        size_t len = sizeof(total_ram);
        if (sysctlbyname("hw.memsize", &total_ram, &len, nullptr, 0) != 0 || total_ram == 0) {
            return 0.0;
        }

        // Get vm statistics via host_statistics
        vm_size_t page_size = 0;
        len = sizeof(page_size);
        sysctlbyname("vm.pagesize", &page_size, &len, nullptr, 0);

        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stats), &count) != KERN_SUCCESS) {
            return 0.0;
        }

        int64_t free_ram = static_cast<int64_t>(vm_stats.free_count) * page_size;
        int64_t used_ram = total_ram - free_ram;
        if (used_ram < 0) used_ram = 0;

        return (static_cast<double>(used_ram) / static_cast<double>(total_ram)) * 100.0;
#else
        // Other platforms: not supported
        return 0.0;
#endif
    }
};

}  // namespace taskflow::worker::util
