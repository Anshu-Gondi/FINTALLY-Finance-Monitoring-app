#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"

// PDFium C-API headers for vector page rasterization & native text stream extraction
#include <fpdfview.h>
#include <fpdf_text.h>

// Linux eventfd for zero-CPU IPC notification interrupts
#include <sys/eventfd.h>
#include <unistd.h>

#include <new>
#include <iostream>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

// Internal context implementation struct
struct FinOcrEngineContext {
    size_t max_buffer_limit;
    int notification_event_fd{-1}; // File descriptor passed from Rust host
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

namespace {

// Converts UTF-16LE characters returned by PDFium into a standard UTF-8 string
std::string utf16le_to_utf8(const std::vector<unsigned short>& utf16_buf, size_t char_count) {
    std::string utf8_out;
    utf8_out.reserve(char_count * 2);

    for (size_t i = 0; i < char_count; ++i) {
        uint32_t cp = utf16_buf[i];

        // Handle surrogate pairs
        if (cp >= 0xD800 && cp <= 0xDBFF && (i + 1) < char_count) {
            uint32_t low = utf16_buf[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + (((cp & 0x3FF) << 10) | (low & 0x3FF));
                i++;
            }
        }

        if (cp < 0x80) {
            utf8_out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            utf8_out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            utf8_out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            utf8_out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            utf8_out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            utf8_out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            utf8_out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            utf8_out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            utf8_out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            utf8_out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return utf8_out;
}

// PDF Path: Extract native text characters directly using PDFium
std::string extract_pdf_native_text(const uint8_t* pdf_bytes, size_t pdf_len) {
    if (!pdf_bytes || pdf_len == 0) return "";

    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(pdf_bytes, static_cast<int>(pdf_len), nullptr);
    if (!doc) {
        FPDF_DestroyLibrary();
        return "";
    }

    int page_count = FPDF_GetPageCount(doc);
    std::string text_output;

    for (int page_idx = 0; page_idx < page_count; ++page_idx) {
        FPDF_PAGE page = FPDF_LoadPage(doc, page_idx);
        if (!page) continue;

        FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
        if (text_page) {
            int char_count = FPDFText_CountChars(text_page);
            if (char_count > 0) {
                std::vector<unsigned short> buffer(static_cast<size_t>(char_count) + 1, 0);
                int retrieved = FPDFText_GetText(text_page, 0, char_count, buffer.data());
                if (retrieved > 0) {
                    std::string page_text = utf16le_to_utf8(buffer, static_cast<size_t>(retrieved));
                    if (!page_text.empty()) {
                        if (page_count > 1) {
                            text_output += "--- [PAGE " + std::to_string(page_idx + 1) + "] ---\n";
                        }
                        text_output += page_text;
                        text_output += "\n";
                    }
                }
            }
            FPDFText_ClosePage(text_page);
        }
        FPDF_ClosePage(page);
    }

    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();

    return text_output;
}

// Raster Image Path: Analyzes the pixel buffer to locate text regions, lines, and bounding coordinates
std::string extract_raster_layout_analysis(const FinProcessedBuffer* buffer, FinInputType input_type) {
    if (!buffer || !buffer->data || buffer->data_len == 0) return "";

    std::string type_str = "RAW_IMAGE_OR_RECEIPT";
    if (input_type == FIN_INPUT_FIN_CHART) {
        type_str = "FINANCIAL_CHART";
    } else if (input_type == FIN_INPUT_PDF_PAGE) {
        type_str = "SCANNED_PDF_PAGE";
    }

    std::ostringstream ss;
    ss << "[FIN_ENGINE_OCR_OUTPUT]\n"
       << "Dimensions: " << buffer->width << "x" << buffer->height << "\n"
       << "Channels: " << buffer->channels << "\n"
       << "Binarized: " << (buffer->is_binarized ? "YES" : "NO") << "\n"
       << "Input Type: " << type_str << "\n";

    size_t total_active_pixels = 0;
    size_t line_height_scan = std::max<size_t>(8, buffer->height / 64);
    size_t detected_text_lines = 0;

    std::vector<std::string> line_details;

    for (size_t y = 0; y < buffer->height; y += line_height_scan) {
        size_t row_active_pixels = 0;
        size_t min_x = buffer->width;
        size_t max_x = 0;

        size_t end_y = std::min(y + line_height_scan, buffer->height);
        for (size_t ry = y; ry < end_y; ++ry) {
            for (size_t x = 0; x < buffer->width; ++x) {
                size_t pixel_idx = (ry * buffer->width + x) * buffer->channels;
                if (buffer->data[pixel_idx] > 128) {
                    row_active_pixels++;
                    total_active_pixels++;
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                }
            }
        }

        double band_density = (static_cast<double>(row_active_pixels) / (buffer->width * (end_y - y))) * 100.0;
        if (band_density > 1.5 && max_x > min_x) {
            detected_text_lines++;
            std::ostringstream line_ss;
            line_ss << "  - Text Line " << detected_text_lines
                    << " [Y:" << y << "-" << end_y
                    << ", X:" << min_x << "-" << max_x << "]: "
                    << "Density " << band_density << "%, Width " << (max_x - min_x + 1) << "px";
            line_details.push_back(line_ss.str());
        }
    }

    double overall_density = (static_cast<double>(total_active_pixels) / buffer->data_len) * 100.0;
    ss << "Active Text Region Density: " << overall_density << "%\n"
       << "Detected Text Lines: " << detected_text_lines << "\n";

    if (!line_details.empty()) {
        ss << "Spatial Bounding Regions:\n";
        for (const auto& detail : line_details) {
            ss << detail << "\n";
        }
    }

    ss << "Payload Status: VALID_PREPROCESSED_BUFFER\n";
    return ss.str();
}

} // anonymous namespace

extern "C" {

FinOcrEngineContext* fin_engine_create(void) {
    try {
        auto* ctx = new (std::nothrow) FinOcrEngineContext();
        if (ctx) {
            ctx->max_buffer_limit = 1024 * 1024 * 64; // 64MB working buffer limit
            ctx->notification_event_fd = -1;
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

void fin_engine_set_notification_fd(FinOcrEngineContext* engine, int event_fd) {
    if (engine) {
        engine->notification_event_fd = event_fd;
    }
}

int fin_engine_notify_completion(FinOcrEngineContext* engine) {
    if (!engine || engine->notification_event_fd < 0) {
        return -1;
    }

    uint64_t signal_val = 1;
    ssize_t bytes_written = write(engine->notification_event_fd, &signal_val, sizeof(signal_val));
    if (bytes_written == sizeof(signal_val)) {
        return 0; // Trigger sent successfully
    }
    return -2; // Write error
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
    if (!engine || !input_bytes || input_len == 0) return nullptr;

    try {
        FinProcessedBuffer* res = execute_vision_pipeline(
            input_bytes,
            input_len,
            input_type,
            target_width,
            target_height,
            target_channels
        );

        // Send 1-byte interrupt signal to wake up Rust thread blocked on read/epoll
        if (res != nullptr) {
            fin_engine_notify_completion(engine);
        }

        return res;
    } catch (const std::exception& e) {
        std::cerr << "[FinOcr Engine Exception]: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "[FinOcr Engine Exception]: Unknown error occurred." << std::endl;
        return nullptr;
    }
}

char* fin_engine_recognize_text(
    FinOcrEngineContext* engine,
    const FinProcessedBuffer* buffer,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
) {
    if (!engine || !buffer) return nullptr;

    try {
        std::string final_payload;

        // 1. Check if direct matrix-matched chart OCR text exists on the buffer
        if (buffer->extracted_text && std::strlen(buffer->extracted_text) > 0) {
            final_payload = "[EXTRACTED_CHART_LABELS]\n" + std::string(buffer->extracted_text);
        }

        // 2. Digital PDF path: Extract actual text characters from the document stream
        if (final_payload.empty() && input_type == FIN_INPUT_PDF_PAGE && input_bytes && input_len > 0) {
            std::string pdf_text = extract_pdf_native_text(input_bytes, input_len);
            if (!pdf_text.empty()) {
                final_payload = "[EXTRACTED_PDF_TEXT_STREAM]\n" + pdf_text;
            }
        }

        // 3. Image/Receipt/Chart/Scanned PDF path fallback: Compute bounding boxes & spatial regions
        if (final_payload.empty()) {
            final_payload = extract_raster_layout_analysis(buffer, input_type);
        }

        if (final_payload.empty()) {
            return nullptr;
        }

        // Allocate heap memory for FFI caller return
        char* out_text = static_cast<char*>(std::malloc(final_payload.size() + 1));
        if (out_text) {
            std::memcpy(out_text, final_payload.c_str(), final_payload.size() + 1);
        }
        return out_text;
    } catch (...) {
        return nullptr;
    }
}

void fin_free_string(char* str) {
    if (str) {
        std::free(str);
    }
}

void fin_free_processed_buffer(FinProcessedBuffer* buffer) {
    if (buffer) {
        if (buffer->data) {
            fin::ops::aligned_free(buffer->data);
            buffer->data = nullptr;
        }

        // Free the dynamically allocated OCR text buffer
        if (buffer->extracted_text) {
            std::free(buffer->extracted_text);
            buffer->extracted_text = nullptr;
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
