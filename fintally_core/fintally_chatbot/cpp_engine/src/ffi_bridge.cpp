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

// -----------------------------------------------------------------------------
// Raster Image Path: Analyze OCR-relevant pixels to locate text regions,
// text lines, and spatial bounding coordinates.
//
// IMPORTANT:
//
// Do NOT inspect only buffer->data[pixel_idx] for RGB images.
//
// RGB layout:
//     [R][G][B]
//
// Financial chart layout:
//     [saturation][OCR foreground][chroma]
//
// Therefore each input type has its own foreground interpretation.
// -----------------------------------------------------------------------------

std::string extract_raster_layout_analysis(
    const FinProcessedBuffer* buffer,
    FinInputType input_type
) {
    if (!buffer ||
        !buffer->data ||
        buffer->data_len == 0 ||
        buffer->width == 0 ||
        buffer->height == 0 ||
        buffer->channels == 0) {

        return {};
    }

    // =========================================================================
    // Input type string
    // =========================================================================

    const char* type_str =
        "RAW_IMAGE_OR_RECEIPT";

    if (input_type == FIN_INPUT_FIN_CHART) {
        type_str = "FINANCIAL_CHART";
    } else if (input_type == FIN_INPUT_PDF_PAGE) {
        type_str = "SCANNED_PDF_PAGE";
    }

    // =========================================================================
    // Foreground predicate
    //
    // IMPORTANT:
    // This follows the same semantic representation used by the OCR pipeline.
    // =========================================================================

    const auto is_active_pixel =
        [buffer, input_type](size_t x, size_t y) noexcept -> bool {

            const size_t channels =
                buffer->channels;

            const size_t pixel_index =
                (
                    y * buffer->width +
                    x
                ) * channels;

            // -----------------------------------------------------------------
            // Grayscale / PDF OCR mask
            // -----------------------------------------------------------------

            if (channels == 1) {

                /*
                 * Native OCR mask:
                 *
                 *     foreground = 255
                 *     background = 0
                 */
                return
                    buffer->data[pixel_index] > 127;
            }

            // -----------------------------------------------------------------
            // Financial chart
            //
            //     channel 0 = saturation
            //     channel 1 = OCR foreground
            //     channel 2 = chroma
            //
            // MatrixMatcher also uses channel 1.
            // -----------------------------------------------------------------

            if (input_type == FIN_INPUT_FIN_CHART &&
                channels >= 3) {

                constexpr uint8_t
                    CHART_FOREGROUND_THRESHOLD = 35;

                return
                    buffer->data[pixel_index + 1] >=
                    CHART_FOREGROUND_THRESHOLD;
            }

            // -----------------------------------------------------------------
            // Ordinary RGB image / receipt
            //
            // Reconstruct the same dark-text -> foreground representation
            // used by rgb_to_ocr_mask().
            // -----------------------------------------------------------------

            if (channels >= 3) {

                const uint8_t r =
                    buffer->data[pixel_index + 0];

                const uint8_t g =
                    buffer->data[pixel_index + 1];

                const uint8_t b =
                    buffer->data[pixel_index + 2];

                // Same luminance approximation as rgb_to_ocr_mask().
                const uint8_t gray =
                    static_cast<uint8_t>(
                        (
                            77u *
                                static_cast<unsigned>(r) +
                            150u *
                                static_cast<unsigned>(g) +
                            29u *
                                static_cast<unsigned>(b)
                        ) >> 8
                    );

                // Dark pixels become foreground.
                const uint8_t inverted =
                    static_cast<uint8_t>(
                        255u -
                        static_cast<unsigned>(gray)
                    );

                // Same threshold as rgb_to_ocr_mask().
                constexpr uint8_t
                    LAYOUT_OCR_THRESHOLD = 100;

                return
                    inverted >
                    LAYOUT_OCR_THRESHOLD;
            }

            // -----------------------------------------------------------------
            // Unknown channel layout.
            // -----------------------------------------------------------------

            return false;
        };

    // =========================================================================
    // Output header
    // =========================================================================

    std::ostringstream ss;

    ss << "[FIN_ENGINE_OCR_OUTPUT]\n"
       << "Dimensions: "
       << buffer->width
       << "x"
       << buffer->height
       << "\n"
       << "Channels: "
       << buffer->channels
       << "\n"
       << "Binarized: "
       << (buffer->is_binarized ? "YES" : "NO")
       << "\n"
       << "Input Type: "
       << type_str
       << "\n";

    // =========================================================================
    // Scan parameters
    //
    // Charts contain much smaller and sparser text than receipts/PDFs.
    // Use finer horizontal sampling so small dashboard labels aren't merged
    // into a single large band.
    // =========================================================================

    const size_t line_height_scan =
        input_type == FIN_INPUT_FIN_CHART
            ? 4
            : std::max<size_t>(
                  8,
                  buffer->height / 64
              );

    // =========================================================================
    // Global statistics
    // =========================================================================

    size_t total_active_pixels = 0;
    size_t total_pixels = 0;
    size_t detected_text_lines = 0;

    struct LineRegion {

        size_t y0;
        size_t y1;

        size_t min_x;
        size_t max_x;

        size_t active_pixels;

        double density;
    };

    std::vector<LineRegion> regions;

    regions.reserve(
        buffer->height / line_height_scan + 1
    );

    // =========================================================================
    // Scan horizontal bands
    // =========================================================================

    for (
        size_t y = 0;
        y < buffer->height;
        y += line_height_scan
    ) {

        const size_t end_y =
            std::min(
                y + line_height_scan,
                buffer->height
            );

        const size_t band_height =
            end_y - y;

        if (band_height == 0) {
            continue;
        }

        size_t row_active_pixels = 0;

        size_t min_x =
            buffer->width;

        size_t max_x = 0;

        // ---------------------------------------------------------------------
        // Scan pixels
        // ---------------------------------------------------------------------

        for (
            size_t ry = y;
            ry < end_y;
            ++ry
        ) {

            for (
                size_t x = 0;
                x < buffer->width;
                ++x
            ) {

                ++total_pixels;

                if (
                    !is_active_pixel(
                        x,
                        ry
                    )
                ) {
                    continue;
                }

                ++row_active_pixels;
                ++total_active_pixels;

                min_x =
                    std::min(
                        min_x,
                        x
                    );

                max_x =
                    std::max(
                        max_x,
                        x
                    );
            }
        }

        // ---------------------------------------------------------------------
        // Band density
        // ---------------------------------------------------------------------

        const size_t band_pixels =
            buffer->width *
            band_height;

        const double band_density =
            band_pixels == 0
                ? 0.0
                : (
                    static_cast<double>(
                        row_active_pixels
                    ) /
                    static_cast<double>(
                        band_pixels
                    )
                ) * 100.0;

        // ---------------------------------------------------------------------
        // Text-line heuristic
        // ---------------------------------------------------------------------
        //
        // Normal images:
        //     preserve existing 0.75% density gate.
        //
        // Charts:
        //     text is much sparser, so use a very low density gate and
        //     an absolute-pixel guard to prevent completely empty bands.
        // ---------------------------------------------------------------------

        const bool has_horizontal_extent =
            max_x > min_x;

        const bool likely_text_line =
            input_type == FIN_INPUT_FIN_CHART
                ? (
                    row_active_pixels >= 8 &&
                    has_horizontal_extent &&
                    band_density >= 0.001
                )
                : (
                    row_active_pixels > 0 &&
                    has_horizontal_extent &&
                    band_density >= 0.75
                );

        if (
            !likely_text_line
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Chart-specific artifact rejection
        //
        // Full-width horizontal structures are much more likely to be:
        //
        //     - chart axes
        //     - borders
        //     - separators
        //     - grid structures
        //
        // than text.
        // ---------------------------------------------------------------------

        if (
            input_type == FIN_INPUT_FIN_CHART
        ) {

            const size_t region_width =
                max_x -
                min_x +
                1;

            const double width_ratio =
                buffer->width == 0
                    ? 0.0
                    : static_cast<double>(
                          region_width
                      ) /
                      static_cast<double>(
                          buffer->width
                      );

            /*
             * Very thin, almost full-width structures are unlikely
             * to be text lines.
             */
            if (
                width_ratio >= 0.90 &&
                band_height <= 16 &&
                band_density >= 0.05
            ) {
                continue;
            }

            /*
             * Extremely wide low-density structures are generally
             * chart geometry rather than actual text.
             */
            if (
                width_ratio >= 0.90 &&
                band_density < 0.50
            ) {
                continue;
            }
        }

        regions.push_back({
            y,
            end_y,
            min_x,
            max_x,
            row_active_pixels,
            band_density
        });
    }

    // =========================================================================
    // Merge neighboring detected bands belonging to one line
    // =========================================================================

    std::vector<LineRegion> merged_regions;

    merged_regions.reserve(
        regions.size()
    );

    for (
        const LineRegion& current :
        regions
    ) {

        if (
            merged_regions.empty()
        ) {

            merged_regions.push_back(
                current
            );

            continue;
        }

        LineRegion& previous =
            merged_regions.back();

        const size_t vertical_gap =
            current.y0 > previous.y1
                ? current.y0 -
                  previous.y1
                : 0;

        /*
         * Fine chart scanning means multiple 4px bands can belong
         * to the same text line.
         */
        const size_t allowed_vertical_gap =
            input_type == FIN_INPUT_FIN_CHART
                ? 4
                : 2;

        const bool close_vertically =
            vertical_gap <=
            allowed_vertical_gap;

        const bool x_overlap =
            !(
                current.max_x <
                    previous.min_x ||
                previous.max_x <
                    current.min_x
            );

        if (
            close_vertically &&
            x_overlap
        ) {

            previous.y1 =
                std::max(
                    previous.y1,
                    current.y1
                );

            previous.min_x =
                std::min(
                    previous.min_x,
                    current.min_x
                );

            previous.max_x =
                std::max(
                    previous.max_x,
                    current.max_x
                );

            previous.active_pixels +=
                current.active_pixels;

            // Recompute density over the merged region.
            const size_t merged_height =
                previous.y1 -
                previous.y0;

            const size_t merged_area =
                buffer->width *
                merged_height;

            previous.density =
                merged_area == 0
                    ? 0.0
                    : (
                        static_cast<double>(
                            previous.active_pixels
                        ) /
                        static_cast<double>(
                            merged_area
                        )
                    ) * 100.0;

        } else {

            merged_regions.push_back(
                current
            );
        }
    }

    // =========================================================================
    // Final region statistics
    // =========================================================================

    detected_text_lines =
        merged_regions.size();

    const double overall_density =
        total_pixels == 0
            ? 0.0
            : (
                static_cast<double>(
                    total_active_pixels
                ) /
                static_cast<double>(
                    total_pixels
                )
            ) * 100.0;

    ss << "Active Text Region Density: "
       << overall_density
       << "%\n";

    ss << "Detected Text Lines: "
       << detected_text_lines
       << "\n";

    // =========================================================================
    // Spatial regions
    // =========================================================================

    if (
        !merged_regions.empty()
    ) {

        ss << "Spatial Bounding Regions:\n";

        size_t line_number = 0;

        for (
            const LineRegion& region :
            merged_regions
        ) {

            ++line_number;

            const size_t region_width =
                region.max_x -
                region.min_x +
                1;

            const size_t region_height =
                region.y1 -
                region.y0;

            ss << "  - Text Line "
               << line_number
               << " [Y:"
               << region.y0
               << "-"
               << region.y1
               << ", X:"
               << region.min_x
               << "-"
               << region.max_x
               << "]: "
               << "Density "
               << region.density
               << "%, Width "
               << region_width
               << "px, Height "
               << region_height
               << "px\n";
        }
    }

    // =========================================================================
    // Payload contract
    // =========================================================================

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

// -----------------------------------------------------------------------------
// Final text extraction API
//
// Priority:
//
//   1. Text already extracted by native/model-free OCR pipeline.
//   2. Native PDF text stream for digital PDFs.
//   3. Raster/layout analysis as final diagnostic fallback.
//
// This function is intentionally model-free.
// No embedding model, OCR neural network, or LLM is required here.
// -----------------------------------------------------------------------------

char* fin_engine_recognize_text(
    FinOcrEngineContext* engine,
    const FinProcessedBuffer* buffer,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
) {
    if (!engine || !buffer) {
        return nullptr;
    }

    try {

        // ---------------------------------------------------------------------
        // ALWAYS begin with the structured buffer payload.
        //
        // This guarantees that PDF/image/chart callers receive:
        //
        //   [FIN_ENGINE_OCR_OUTPUT]
        //   Dimensions:
        //   Channels:
        //   Binarized:
        //   Input Type:
        //   ...
        //   Payload Status: VALID_PREPROCESSED_BUFFER
        //
        // before any literal OCR text.
        // ---------------------------------------------------------------------

        std::string final_payload =
            extract_raster_layout_analysis(
                buffer,
                input_type
            );

        // ---------------------------------------------------------------------
        // Digital PDF text stream.
        //
        // Preserve actual PDF text when available.
        // ---------------------------------------------------------------------

        if (input_type == FIN_INPUT_PDF_PAGE &&
            input_bytes != nullptr &&
            input_len > 0) {

            const std::string pdf_text =
                extract_pdf_native_text(
                    input_bytes,
                    input_len
                );

            if (!pdf_text.empty()) {

                final_payload +=
                    "\n[EXTRACTED_PDF_TEXT_STREAM]\n";

                final_payload +=
                    pdf_text;
            }
        }

        // ---------------------------------------------------------------------
        // Model-free literal OCR.
        //
        // For:
        //
        //   - charts
        //   - scanned PDFs
        //   - receipts
        //   - ordinary images
        //
        // MatrixMatcher output is preserved verbatim.
        // ---------------------------------------------------------------------

        if (buffer->extracted_text &&
            std::strlen(buffer->extracted_text) > 0) {

            // Financial charts already have their own structured header in
            // extracted_text. Preserve that path without wrapping it twice.

            if (input_type == FIN_INPUT_FIN_CHART) {

                final_payload +=
                    "\n[EXTRACTED_CHART_LABELS]\n";

                final_payload +=
                    buffer->extracted_text;

            } else {

                final_payload +=
                    "\n[EXTRACTED_MODEL_FREE_OCR]\n";

                final_payload +=
                    buffer->extracted_text;
            }
        }

        // ---------------------------------------------------------------------
        // Valid structured payload should never be empty for a valid buffer.
        // ---------------------------------------------------------------------

        if (final_payload.empty()) {
            return nullptr;
        }

        // ---------------------------------------------------------------------
        // Allocate C-ABI string.
        // ---------------------------------------------------------------------

        char* out_text =
            static_cast<char*>(
                std::malloc(
                    final_payload.size() + 1
                )
            );

        if (!out_text) {
            return nullptr;
        }

        std::memcpy(
            out_text,
            final_payload.c_str(),
            final_payload.size() + 1
        );

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
