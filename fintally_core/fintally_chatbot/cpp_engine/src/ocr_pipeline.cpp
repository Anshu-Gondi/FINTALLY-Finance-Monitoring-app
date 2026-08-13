#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include <new>
#include <cstring>
#include <algorithm>
#include <stdexcept>

// Production-grade error assertion guard macro
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
    FIN_CHECK(target_width > 0 && target_height > 0 && target_channels > 0,
              "Target dimensions must be strictly positive.");

    size_t num_pixels = 0;
    size_t data_len = 0;

    // Fast intrinsic overflow checks bypassing scalar division
#if defined(__GNUC__) || defined(__clang__)
    FIN_CHECK(!__builtin_mul_overflow(target_width, target_height, &num_pixels),
              "Integer overflow detected in width * height.");
    FIN_CHECK(!__builtin_mul_overflow(num_pixels, target_channels, &data_len),
              "Integer overflow detected in total buffer calculation.");
#else
    num_pixels = target_width * target_height;
    FIN_CHECK(num_pixels / target_width == target_height, "Integer overflow detected in width * height.");
    data_len = num_pixels * target_channels;
    FIN_CHECK(data_len / target_channels == num_pixels, "Integer overflow detected in total buffer calculation.");
#endif

    auto* result = new (std::nothrow) FinProcessedBuffer();
    if (!result) return nullptr;

    result->width = target_width;
    result->height = target_height;
    result->channels = target_channels;
    result->data_len = data_len;
    result->is_binarized = 0;

    // Allocate 64-byte aligned SIMD buffer
    result->data = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, result->data_len));
    if (!result->data) {
        delete result;
        return nullptr;
    }

    // Pass 1: Duplicate channel data for missing channels without per-iteration bounds check
    if (target_channels == 3) {
        const size_t max_pixels = std::min(input_len, result->data_len / 3);
        uint8_t* dst = result->data;

        for (size_t j = 0; j < max_pixels; ++j) {
            uint8_t val = input_bytes[j];
            dst[0] = val; // R
            dst[1] = val; // G
            dst[2] = val; // B
            dst += 3;
        }
    } else {
        size_t copy_size = std::min(input_len, result->data_len);
        std::memcpy(result->data, input_bytes, copy_size);
    }

    // Read CPU thermal status before vector processing
    fin::ThermalMetrics thermal = fin::ThermalMonitor::instance().read_metrics();

    // Pass 2: Hardware-accelerated processing with Thermal Fallback Guard
    if (input_type == FIN_INPUT_PDF_PAGE) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            // Branchless binarization scalar fallback (zero casts)
            for (size_t i = 0; i < result->data_len; ++i) {
                result->data[i] = (result->data[i] > 128) * 255;
            }
        } else {
            fin::ops::binarize_simd(result->data, result->data_len);
        }
        result->is_binarized = 1;
    } else if (input_type == FIN_INPUT_FIN_CHART) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            // Branchless saturated doubling scalar fallback (zero static_casts / uint16_t promotions)
            for (size_t i = 0; i < result->data_len; ++i) {
                uint8_t val = result->data[i];
                result->data[i] = (val > 127) ? 255 : (val << 1);
            }
        } else {
            fin::ops::contrast_boost_simd(result->data, result->data_len);
        }
        result->is_binarized = 0;
    }

    return result;
}
