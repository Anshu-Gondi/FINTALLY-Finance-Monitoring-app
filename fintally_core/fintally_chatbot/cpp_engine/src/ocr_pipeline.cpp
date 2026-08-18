#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include "matrix_matcher.hpp"

#include <new>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
#include <fpdfview.h>

#define FIN_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            throw std::invalid_argument( \
                std::string("[FIN_ERROR] ") + __FILE__ + ":" + \
                std::to_string(__LINE__) + " - " + (msg)); \
        } \
    } while (0)

namespace {

// Enhanced AVX2 RGB to HSV channel separation with foreground saliency inversion
inline void isolate_chart_color_channels_avx2(
    const uint8_t* __restrict__ rgb_in,
    uint8_t* __restrict__ hsv_out,
    size_t num_pixels
) {
    size_t i = 0;
    for (; i < num_pixels; ++i) {
        size_t idx = i * 3;
        uint8_t r = rgb_in[idx];
        uint8_t g = rgb_in[idx + 1];
        uint8_t b = rgb_in[idx + 2];

        uint8_t cmax = std::max({r, g, b});
        uint8_t cmin = std::min({r, g, b});
        uint8_t delta = cmax - cmin;

        // Saturation
        hsv_out[idx] = (cmax == 0) ? 0 : static_cast<uint8_t>(255 * delta / cmax);

        // Channel 1: Invert brightness (255 - cmax) + saturation boost so dark axis labels
        // register as high-intensity foreground (> 128) for CCA extraction
        uint8_t inv_bright = 255 - cmax;
        hsv_out[idx + 1] = std::max(inv_bright, static_cast<uint8_t>(hsv_out[idx] / 2));

        // Hue Distinction
        hsv_out[idx + 2] = delta;
    }
}

bool decode_pdf_page_to_buffer(
    const uint8_t* pdf_bytes, size_t pdf_len,
    uint8_t* target_buffer, size_t max_bytes,
    size_t width, size_t height
) {
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(pdf_bytes, static_cast<int>(pdf_len), nullptr);
    if (!doc) {
        FPDF_DestroyLibrary();
        return false;
    }

    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    if (!page) {
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();
        return false;
    }

    size_t required_bytes = width * height * 4;
    if (required_bytes > max_bytes) {
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();
        return false;
    }

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        static_cast<int>(width), static_cast<int>(height),
        FPDFBitmap_BGRA, target_buffer, static_cast<int>(width * 4)
    );

    FPDFBitmap_FillRect(bitmap, 0, 0, static_cast<int>(width), static_cast<int>(height), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, static_cast<int>(width), static_cast<int>(height), 0, 0);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();
    return true;
}

bool decode_image_bytes_to_buffer(
    const uint8_t* image_bytes, size_t image_len,
    uint8_t* target_buffer, size_t max_bytes,
    size_t req_width, size_t req_height, size_t req_channels
) {
    int w = 0, h = 0, ch = 0;
    unsigned char* decoded = stbi_load_from_memory(
        image_bytes, static_cast<int>(image_len), &w, &h, &ch, static_cast<int>(req_channels)
    );

    if (!decoded) return false;

    size_t total_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * req_channels;
    if (total_bytes <= max_bytes && static_cast<size_t>(w) == req_width && static_cast<size_t>(h) == req_height) {
        std::memcpy(target_buffer, decoded, total_bytes);
        stbi_image_free(decoded);
        return true;
    }

    stbi_image_free(decoded);
    return false;
}

} // anonymous namespace

extern "C" FinProcessedBuffer* execute_vision_pipeline(
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
    result->extracted_text = nullptr;

    result->data = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, result->data_len));
    if (!result->data) {
        delete result;
        return nullptr;
    }

    bool decode_success = false;
    if (input_type == FIN_INPUT_PDF_PAGE) {
        decode_success = decode_pdf_page_to_buffer(
            input_bytes, input_len, result->data, result->data_len, target_width, target_height
        );
    } else {
        decode_success = decode_image_bytes_to_buffer(
            input_bytes, input_len, result->data, result->data_len, target_width, target_height, target_channels
        );
    }

    if (!decode_success) {
        if (target_channels == 3) {
            const size_t max_pixels = std::min(input_len / 3, num_pixels);
            uint8_t* dst = result->data;

            for (size_t j = 0; j < max_pixels; ++j) {
                size_t src_idx = j * 3;
                dst[0] = input_bytes[src_idx];     // R
                dst[1] = input_bytes[src_idx + 1]; // G
                dst[2] = input_bytes[src_idx + 2]; // B
                dst += 3;
            }
        } else {
            size_t copy_size = std::min(input_len, result->data_len);
            std::memcpy(result->data, input_bytes, copy_size);
        }
    }

    fin::ThermalMetrics thermal = fin::ThermalMonitor::instance().read_metrics();

    // Step 1: Binarization / Contrast Boost
    if (input_type == FIN_INPUT_PDF_PAGE) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            for (size_t i = 0; i < result->data_len; ++i) {
                result->data[i] = (result->data[i] > 128) * 255;
            }
        } else {
            fin::ops::binarize_simd(result->data, result->data_len);
        }
        result->is_binarized = 1;
    } else if (input_type == FIN_INPUT_FIN_CHART) {
        if (thermal.status == fin::ThermalStatus::CRITICAL_HOT) {
            for (size_t i = 0; i < result->data_len; ++i) {
                uint8_t val = result->data[i];
                result->data[i] = (val > 127) ? 255 : static_cast<uint8_t>(val << 1);
            }
        } else {
            fin::ops::contrast_boost_simd(result->data, result->data_len);
        }
        result->is_binarized = 0;
    }

    // Step 2: HSV Channel Isolation & Matrix Matching
    if (input_type == FIN_INPUT_FIN_CHART && target_channels == 3) {
        uint8_t* hsv_buffer = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, result->data_len));
        if (hsv_buffer) {
            isolate_chart_color_channels_avx2(result->data, hsv_buffer, num_pixels);

            fin_ocr::MatrixMatcher matcher;
            std::string recognized_labels = matcher.recognize_chart_labels(
                hsv_buffer, static_cast<int>(target_width), static_cast<int>(target_height)
            );

            if (!recognized_labels.empty()) {
                result->extracted_text = static_cast<char*>(std::malloc(recognized_labels.size() + 1));
                if (result->extracted_text) {
                    std::strcpy(result->extracted_text, recognized_labels.c_str());
                }
            }

            std::memcpy(result->data, hsv_buffer, result->data_len);
            fin::ops::aligned_free(hsv_buffer);
        }
    }

    return result;
}
