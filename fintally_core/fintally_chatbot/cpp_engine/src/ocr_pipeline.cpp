#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include <new>
#include <cstring>
#include <limits>
#include <stdexcept>

// Production-grade error assertion guard macro (LibTorch/TensorFlow style)
#define FIN_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            throw std::invalid_argument( \
                std::string("[FIN_ERROR] ") + __FILE__ + ":" + \
                std::to_string(__LINE__) + " - " + (msg)); \
        } \
    } while (0)

FinProcessedBuffer* execute_vision_pipeline(
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
) {
    // Assert parameters are valid
    FIN_CHECK(target_width > 0 && target_height > 0 && target_channels > 0,
              "Target dimensions must be strictly positive.");

    // Check for overflow before calculating memory length
    size_t num_pixels = target_width * target_height;
    FIN_CHECK(num_pixels / target_width == target_height, "Integer overflow detected in width * height.");

    size_t data_len = num_pixels * target_channels;
    FIN_CHECK(data_len / target_channels == num_pixels, "Integer overflow detected in total buffer calculation.");

    auto* result = new (std::nothrow) FinProcessedBuffer();
    if (!result) return nullptr;

    result->width = target_width;
    result->height = target_height;
    result->channels = target_channels;
    result->data_len = data_len;
    result->is_binarized = 0; // Default metadata status

    // Allocate 64-byte aligned SIMD buffer
    result->data = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, result->data_len));
    if (!result->data) {
        delete result;
        return nullptr;
    }

    // Pass 1: Duplicate channel data dynamically for missing channels
    if (target_channels == 3) {
        for (size_t i = 0, j = 0; i < result->data_len && j < input_len; i += 3, ++j) {
            uint8_t val = input_bytes[j];
            result->data[i]     = val; // R
            result->data[i + 1] = val; // G
            result->data[i + 2] = val; // B
        }
    } else {
        size_t copy_size = (input_len < result->data_len) ? input_len : result->data_len;
        std::memcpy(result->data, input_bytes, copy_size);
    }

    // Read CPU core temperature before executing heavy vector instructions
    fin::ThermalMetrics thermal = fin::ThermalMonitor::instance().read_metrics();

    // Pass 2: Hardware-accelerated processing with Thermal Fallback Guard
    if (input_type == FIN_INPUT_PDF_PAGE) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            // Overheating protection: Fallback to scalar execution to prevent thermal crashes
            for (size_t i = 0; i < result->data_len; ++i) {
                result->data[i] = (result->data[i] > 128) ? 255 : 0;
            }
        } else {
            fin::ops::binarize_simd(result->data, result->data_len);
        }
        result->is_binarized = 1; // Update metadata tag
    } else if (input_type == FIN_INPUT_FIN_CHART) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            // Overheating protection: Fallback to scalar execution
            for (size_t i = 0; i < result->data_len; ++i) {
                uint16_t val = static_cast<uint16_t>(result->data[i]) * 2;
                result->data[i] = static_cast<uint8_t>((val > 255) ? 255 : val);
            }
        } else {
            fin::ops::contrast_boost_simd(result->data, result->data_len);
        }
        result->is_binarized = 0;
    }

    return result;
}
