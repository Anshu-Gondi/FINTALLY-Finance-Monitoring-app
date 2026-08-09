#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include <new>
#include <iostream>
#include <exception>

// Internal context implementation struct
struct FinOcrEngineContext {
    size_t max_buffer_limit;
};

// Internal pipeline forward declaration
FinProcessedBuffer* execute_vision_pipeline(
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
);

extern "C" {

FinOcrEngineContext* fin_engine_create(void) {
    try {
        auto* ctx = new (std::nothrow) FinOcrEngineContext();
        if (ctx) {
            ctx->max_buffer_limit = 1024 * 1024 * 64; // 64MB working buffer limit
        }
        return ctx;
    } catch (...) {
        return nullptr;
    }
}

void fin_engine_destroy(FinOcrEngineContext* engine) {
    if (engine) {
        delete engine;
    }
}

FinProcessedBuffer* fin_process_document_bytes(
    FinOcrEngineContext* engine,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
) {
    // Basic argument safety validation
    if (!engine || !input_bytes || input_len == 0) return nullptr;

    try {
        return execute_vision_pipeline(
            input_bytes,
            input_len,
            input_type,
            target_width,
            target_height,
            target_channels
        );
    } catch (const std::exception& e) {
        std::cerr << "[FinOcr Engine Exception]: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "[FinOcr Engine Exception]: Unknown error occurred." << std::endl;
        return nullptr;
    }
}

void fin_free_processed_buffer(FinProcessedBuffer* buffer) {
    if (buffer) {
        if (buffer->data) {
            fin::ops::aligned_free(buffer->data);
            buffer->data = nullptr;
        }
        delete buffer;
    }
}

FinThermalMetrics fin_get_thermal_metrics(void) {
    fin::ThermalMetrics metrics = fin::ThermalMonitor::instance().read_metrics();
    FinThermalMetrics result;
    result.max_temp_celsius = metrics.max_temp_celsius;
    result.avg_temp_celsius = metrics.avg_temp_celsius;
    result.status = static_cast<FinThermalStatus>(metrics.status);
    return result;
}

void fin_set_thermal_thresholds(float warm_limit_c, float critical_limit_c) {
    fin::ThermalMonitor::instance().set_thresholds(warm_limit_c, critical_limit_c);
}

} // extern "C"
