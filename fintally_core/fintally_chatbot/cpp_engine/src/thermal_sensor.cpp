#include "thermal_sensor.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

namespace fin {

ThermalMonitor::ThermalMonitor() {
    discover_thermal_zones();
}

void ThermalMonitor::discover_thermal_zones() {
    thermal_zone_paths_.clear();

    // Standard Linux Sysfs Thermal Class
    const std::string sysfs_thermal_path = "/sys/class/thermal";
    if (fs::exists(sysfs_thermal_path)) {
        for (const auto& entry : fs::directory_iterator(sysfs_thermal_path)) {
            std::string path_str = entry.path().string();
            if (path_str.find("thermal_zone") != std::string::npos) {
                std::string temp_file = path_str + "/temp";
                if (fs::exists(temp_file)) {
                    thermal_zone_paths_.push_back(temp_file);
                }
            }
        }
    }
}

ThermalMetrics ThermalMonitor::read_metrics() {
    ThermalMetrics metrics;
    if (thermal_zone_paths_.empty()) {
        discover_thermal_zones();
    }

    if (thermal_zone_paths_.empty()) {
        // Fallback if running inside a restricted container without sysfs access
        metrics.status = ThermalStatus::NORMAL;
        return metrics;
    }

    float max_t = -273.15f;
    float sum_t = 0.0f;
    size_t valid_count = 0;

    for (const auto& path : thermal_zone_paths_) {
        std::ifstream file(path);
        if (file.is_open()) {
            long raw_temp = 0;
            if (file >> raw_temp) {
                // sysfs temps are usually reported in millidegrees Celsius
                float temp_c = (raw_temp > 1000) ? static_cast<float>(raw_temp) / 1000.0f : static_cast<float>(raw_temp);

                max_t = std::max(max_t, temp_c);
                sum_t += temp_c;
                valid_count++;
            }
        }
    }

    if (valid_count > 0) {
        metrics.max_temp_celsius = max_t;
        metrics.avg_temp_celsius = sum_t / static_cast<float>(valid_count);

        if (metrics.max_temp_celsius >= critical_threshold_c_) {
            metrics.status = ThermalStatus::CRITICAL_HOT;
        } else if (metrics.max_temp_celsius < 0.0f) {
            metrics.status = ThermalStatus::CRITICAL_COLD;
        } else if (metrics.max_temp_celsius >= warm_threshold_c_) {
            metrics.status = ThermalStatus::WARM;
        } else {
            metrics.status = ThermalStatus::NORMAL;
        }
    } else {
        metrics.status = ThermalStatus::NORMAL;
    }

    cached_status_.store(metrics.status, std::memory_order_relaxed);
    return metrics;
}

void ThermalMonitor::set_thresholds(float warm_limit_c, float critical_limit_c) {
    warm_threshold_c_ = warm_limit_c;
    critical_threshold_c_ = critical_limit_c;
}

} // namespace fin
