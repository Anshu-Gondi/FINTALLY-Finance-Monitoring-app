#ifndef FIN_THERMAL_SENSOR_HPP
#define FIN_THERMAL_SENSOR_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>

namespace fin {

enum class ThermalStatus {
    NORMAL = 0,       // Temperature safe: run full AVX2/AVX-512 vector pipelines
    WARM = 1,         // Approaching limit: limit vector width or concurrency
    CRITICAL_HOT = 2, // Overheating: fallback to scalar execution to prevent crash/segfault
    CRITICAL_COLD = 3 // Sub-zero/unusual drop: hardware sensor anomaly
};

struct ThermalMetrics {
    float max_temp_celsius = 25.0f;
    float avg_temp_celsius = 25.0f;
    ThermalStatus status = ThermalStatus::NORMAL;
};

class ThermalMonitor {
public:
    static ThermalMonitor& instance() {
        static ThermalMonitor monitor;
        return monitor;
    }

    // Reads hardware thermal zones (/sys/class/thermal/).
    // Rate-limited and cached lock-free to avoid sysfs I/O overhead on hot execution paths.
    ThermalMetrics read_metrics();

    // Sets warning and critical limits in °C and invalidates the cache timestamp
    void set_thresholds(float warm_limit_c, float critical_limit_c);

    // Forces immediate cache invalidation
    void invalidate_cache() {
        last_read_time_ns_.store(0, std::memory_order_relaxed);
    }

    // Configures cache refresh interval (default: 100ms)
    void set_cache_interval_ms(uint64_t interval_ms) {
        cache_interval_ns_.store(interval_ms * 1'000'000ULL, std::memory_order_relaxed);
        invalidate_cache();
    }

    // Dynamic execution decision helper
    ThermalStatus current_status() const { return cached_status_.load(std::memory_order_relaxed); }

private:
    ThermalMonitor();

    std::vector<std::string> thermal_zone_paths_;
    float warm_threshold_c_ = 75.0f;
    float critical_threshold_c_ = 87.0f;

    // Lock-free time and cache state tracking
    std::atomic<int64_t> last_read_time_ns_{0};
    std::atomic<uint64_t> cache_interval_ns_{100'000'000ULL}; // 100 ms default

    std::atomic<float> cached_max_temp_{25.0f};
    std::atomic<float> cached_avg_temp_{25.0f};
    mutable std::atomic<ThermalStatus> cached_status_{ThermalStatus::NORMAL};

    void discover_thermal_zones();
};

} // namespace fin

#endif // FIN_THERMAL_SENSOR_HPP
