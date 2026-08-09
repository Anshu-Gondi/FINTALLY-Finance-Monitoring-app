#ifndef FIN_THERMAL_SENSOR_HPP
#define FIN_THERMAL_SENSOR_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

namespace fin {

enum class ThermalStatus {
    NORMAL = 0,       // Temperature safe: run full AVX2/AVX-512 vector pipelines
    WARM = 1,         // Approaching limit: limit vector width or concurrency
    CRITICAL_HOT = 2, // Overheating: fallback to scalar execution to prevent crash/segfault
    CRITICAL_COLD = 3 // Sub-zero/unusual drop: hardware sensor anomaly
};

struct ThermalMetrics {
    float max_temp_celsius = 0.0f;
    float avg_temp_celsius = 0.0f;
    ThermalStatus status = ThermalStatus::NORMAL;
};

class ThermalMonitor {
public:
    static ThermalMonitor& instance() {
        static ThermalMonitor monitor;
        return monitor;
    }

    // Reads hardware thermal zones (/sys/class/thermal/ or hwmon)
    ThermalMetrics read_metrics();

    // Sets warning and critical limits in °C
    void set_thresholds(float warm_limit_c, float critical_limit_c);

    // Dynamic execution decision helper
    ThermalStatus current_status() const { return cached_status_.load(std::memory_order_relaxed); }

private:
    ThermalMonitor();

    std::vector<std::string> thermal_zone_paths_;
    float warm_threshold_c_ = 75.0f;
    float critical_threshold_c_ = 87.0f;
    mutable std::atomic<ThermalStatus> cached_status_{ThermalStatus::NORMAL};

    void discover_thermal_zones();
};

} // namespace fin

#endif // FIN_THERMAL_SENSOR_HPP
