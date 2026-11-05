#include "thermal_sensor.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <chrono>

namespace fs = std::filesystem;

namespace fin {

ThermalMonitor::ThermalMonitor() {
    discover_thermal_zones();
}

void ThermalMonitor::discover_thermal_zones() {
    thermal_zone_paths_.clear();

    constexpr const char* sysfs_thermal_path = "/sys/class/thermal";

    if (fs::exists(sysfs_thermal_path)) {
        for (const auto& entry : fs::directory_iterator(sysfs_thermal_path)) {
            const auto& native_path_str = entry.path().native();
            std::string_view path_sv(native_path_str);

            if (path_sv.find("thermal_zone") != std::string_view::npos) {
                fs::path temp_file = entry.path() / "temp";

                if (fs::exists(temp_file)) {
                    thermal_zone_paths_.push_back(temp_file.string());
                }
            }
        }
    }
}

ThermalMetrics ThermalMonitor::read_metrics() {
    const auto now = std::chrono::steady_clock::now();
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    const uint64_t interval = cache_interval_ns_.load(std::memory_order_relaxed);
    int64_t last_time = last_read_time_ns_.load(std::memory_order_relaxed);

    // Fast Path: Return atomic cached values if under the refresh threshold
    if (last_time != 0 && (now_ns - last_time) < static_cast<int64_t>(interval)) {
        ThermalMetrics metrics;
        metrics.max_temp_celsius = cached_max_temp_.load(std::memory_order_relaxed);
        metrics.avg_temp_celsius = cached_avg_temp_.load(std::memory_order_relaxed);
        metrics.status = cached_status_.load(std::memory_order_relaxed);
        return metrics;
    }

    // Lock-Free Rate-Limiting Guard
    if (!last_read_time_ns_.compare_exchange_strong(last_time, now_ns, std::memory_order_acq_rel)) {
        ThermalMetrics metrics;
        metrics.max_temp_celsius = cached_max_temp_.load(std::memory_order_relaxed);
        metrics.avg_temp_celsius = cached_avg_temp_.load(std::memory_order_relaxed);
        metrics.status = cached_status_.load(std::memory_order_relaxed);
        return metrics;
    }

    // --- Sysfs Hardware Read Path ---
    if (thermal_zone_paths_.empty()) {
        discover_thermal_zones();
    }

    float max_t = 25.0f;
    float sum_t = 25.0f;
    size_t valid_count = 0;

    char read_buf[32];

    for (const auto& path : thermal_zone_paths_) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            ssize_t bytes_read = ::read(fd, read_buf, sizeof(read_buf) - 1);
            ::close(fd);

            if (bytes_read > 0) {
                read_buf[bytes_read] = '\0';
                long raw_temp = std::strtol(read_buf, nullptr, 10);

                float temp_c = (raw_temp > 1000) ? (raw_temp * 0.001f) : static_cast<float>(raw_temp);

                if (valid_count == 0) {
                    max_t = temp_c;
                    sum_t = temp_c;
                } else {
                    max_t = std::max(max_t, temp_c);
                    sum_t += temp_c;
                }
                valid_count++;
            }
        }
    }

    ThermalMetrics metrics;
    if (valid_count > 0) {
        metrics.max_temp_celsius = max_t;
        metrics.avg_temp_celsius = sum_t / static_cast<float>(valid_count);
    } else {
        metrics.max_temp_celsius = max_t;
        metrics.avg_temp_celsius = sum_t;
    }

    // Threshold evaluation logic
    if (metrics.max_temp_celsius >= critical_threshold_c_) {
        metrics.status = ThermalStatus::CRITICAL_HOT;
    } else if (metrics.max_temp_celsius < 0.0f) {
        metrics.status = ThermalStatus::CRITICAL_COLD;
    } else if (metrics.max_temp_celsius >= warm_threshold_c_) {
        metrics.status = ThermalStatus::WARM;
    } else {
        metrics.status = ThermalStatus::NORMAL;
    }

    // Update lock-free cache
    cached_max_temp_.store(metrics.max_temp_celsius, std::memory_order_relaxed);
    cached_avg_temp_.store(metrics.avg_temp_celsius, std::memory_order_relaxed);
    cached_status_.store(metrics.status, std::memory_order_release);

    return metrics;
}

void ThermalMonitor::set_thresholds(float warm_limit_c, float critical_limit_c) {
    warm_threshold_c_ = warm_limit_c;
    critical_threshold_c_ = critical_limit_c;
    // Reset cache timestamp to ensure next read re-evaluates thresholds immediately
    invalidate_cache();
}

} // namespace fin
