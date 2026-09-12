#include "ffi_bridge.h"

#include "thermal_sensor.hpp"
#include "tensor_ops.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/image/luminance.hpp"
#include "fin_ocr/pipeline/vision_pipeline.hpp"

#include <fpdfview.h>
#include <fpdf_text.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

// =============================================================================
// INTERNAL ENGINE CONTEXT
// =============================================================================

struct FinOcrEngineContext {

    std::size_t max_buffer_limit = 64ULL * 1024ULL * 1024ULL;

    int notification_event_fd = -1;
};

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

namespace {

// =============================================================================
// UTF-16LE / UTF-8 CONVERSION
// =============================================================================
//
// PDFium exposes PDF text through UTF-16 code units.
//
// This conversion handles:
//     - ASCII
//     - BMP characters
//     - surrogate pairs
//
// Invalid isolated surrogate values are treated conservatively.
// =============================================================================

std::string utf16le_to_utf8(
    const std::vector<unsigned short>& utf16_buf,
    std::size_t char_count
) {

    if (
        char_count == 0 ||
        utf16_buf.empty()
    ) {
        return {};
    }

    const std::size_t safe_count =
        std::min(
            char_count,
            utf16_buf.size()
        );

    std::string utf8_out;

    utf8_out.reserve(
        safe_count * 2
    );

    for (
        std::size_t i = 0;
        i < safe_count;
        ++i
    ) {

        uint32_t codepoint =
            static_cast<uint32_t>(
                utf16_buf[i]
            );

        // ---------------------------------------------------------------------
        // Surrogate pair
        // ---------------------------------------------------------------------

        if (
            codepoint >= 0xD800 &&
            codepoint <= 0xDBFF &&
            i + 1 < safe_count
        ) {

            const uint32_t low =
                static_cast<uint32_t>(
                    utf16_buf[i + 1]
                );

            if (
                low >= 0xDC00 &&
                low <= 0xDFFF
            ) {

                codepoint =
                    0x10000u +
                    (
                        (
                            codepoint &
                            0x3FFu
                        ) << 10
                    ) +
                    (
                        low &
                        0x3FFu
                    );

                ++i;
            }
        }

        // ---------------------------------------------------------------------
        // Reject isolated surrogate code units.
        // ---------------------------------------------------------------------

        if (
            codepoint >= 0xD800 &&
            codepoint <= 0xDFFF
        ) {

            continue;
        }

        // ---------------------------------------------------------------------
        // UTF-8
        // ---------------------------------------------------------------------

        if (
            codepoint < 0x80
        ) {

            utf8_out.push_back(
                static_cast<char>(
                    codepoint
                )
            );

        } else if (
            codepoint < 0x800
        ) {

            utf8_out.push_back(
                static_cast<char>(
                    0xC0u |
                    (codepoint >> 6)
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (codepoint & 0x3Fu)
                )
            );

        } else if (
            codepoint < 0x10000
        ) {

            utf8_out.push_back(
                static_cast<char>(
                    0xE0u |
                    (codepoint >> 12)
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (
                        (codepoint >> 6) &
                        0x3Fu
                    )
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (codepoint & 0x3Fu)
                )
            );

        } else {

            utf8_out.push_back(
                static_cast<char>(
                    0xF0u |
                    (codepoint >> 18)
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (
                        (codepoint >> 12) &
                        0x3Fu
                    )
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (
                        (codepoint >> 6) &
                        0x3Fu
                    )
                )
            );

            utf8_out.push_back(
                static_cast<char>(
                    0x80u |
                    (codepoint & 0x3Fu)
                )
            );
        }
    }

    return utf8_out;
}

// =============================================================================
// PDF NATIVE TEXT EXTRACTION
// =============================================================================
//
// This is intentionally separate from raster OCR.
//
// Path:
//
//     digital PDF
//          |
//          +--> PDFium text stream
//          |
//          +--> UTF-16
//          |
//          +--> UTF-8
//
// If no native text exists, the caller can still use the OCR text already
// generated by VisionPipeline.
// =============================================================================

std::string extract_pdf_native_text(
    const uint8_t* pdf_bytes,
    std::size_t pdf_len
) {

    if (
        pdf_bytes == nullptr ||
        pdf_len == 0
    ) {
        return {};
    }

    if (
        pdf_len >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        )
    ) {
        return {};
    }

    FPDF_InitLibrary();

    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(
            pdf_bytes,
            static_cast<int>(
                pdf_len
            ),
            nullptr
        );

    if (
        document == nullptr
    ) {

        FPDF_DestroyLibrary();

        return {};
    }

    const int page_count =
        FPDF_GetPageCount(
            document
        );

    if (
        page_count <= 0
    ) {

        FPDF_CloseDocument(
            document
        );

        FPDF_DestroyLibrary();

        return {};
    }

    std::string text_output;

    for (
        int page_index = 0;
        page_index < page_count;
        ++page_index
    ) {

        FPDF_PAGE page =
            FPDF_LoadPage(
                document,
                page_index
            );

        if (
            page == nullptr
        ) {
            continue;
        }

        FPDF_TEXTPAGE text_page =
            FPDFText_LoadPage(
                page
            );

        if (
            text_page != nullptr
        ) {

            const int char_count =
                FPDFText_CountChars(
                    text_page
                );

            if (
                char_count > 0
            ) {

                std::vector<unsigned short>
                    buffer(
                        static_cast<std::size_t>(
                            char_count
                        ) + 1u,
                        0
                    );

                const int retrieved =
                    FPDFText_GetText(
                        text_page,
                        0,
                        char_count,
                        buffer.data()
                    );

                if (
                    retrieved > 0
                ) {

                    const std::size_t safe_retrieved =
                        std::min(
                            static_cast<std::size_t>(
                                retrieved
                            ),
                            buffer.size() - 1
                        );

                    std::string page_text =
                        utf16le_to_utf8(
                            buffer,
                            safe_retrieved
                        );

                    if (
                        !page_text.empty()
                    ) {

                        if (
                            page_count > 1
                        ) {

                            text_output +=
                                "--- [PAGE " +
                                std::to_string(
                                    page_index + 1
                                ) +
                                "] ---\n";
                        }

                        text_output +=
                            page_text;

                        text_output.push_back(
                            '\n'
                        );
                    }
                }
            }

            FPDFText_ClosePage(
                text_page
            );
        }

        FPDF_ClosePage(
            page
        );
    }

    FPDF_CloseDocument(
        document
    );

    FPDF_DestroyLibrary();

    return text_output;
}

// =============================================================================
// RASTER / BUFFER LAYOUT ANALYSIS
// =============================================================================
//
// This is diagnostic/metadata analysis.
//
// It does NOT perform OCR.
//
// Actual OCR is already performed by:
//     VisionPipeline
//         -> DocumentOcr
//         -> TesseractRecognizer
//         -> MatrixMatcher
//         -> ChartLabelRecognizer
//
// This function only describes the resulting buffer spatially.
// =============================================================================

std::string extract_raster_layout_analysis(
    const FinProcessedBuffer* buffer,
    FinInputType input_type
) {

    if (
        buffer == nullptr ||
        buffer->data == nullptr ||
        buffer->data_len == 0 ||
        buffer->width == 0 ||
        buffer->height == 0 ||
        buffer->channels == 0
    ) {
        return {};
    }

    // =========================================================================
    // Input type
    // =========================================================================

    const char* type_str =
        "RAW_IMAGE_OR_RECEIPT";

    if (
        input_type ==
        FIN_INPUT_FIN_CHART
    ) {

        type_str =
            "FINANCIAL_CHART";

    } else if (
        input_type ==
        FIN_INPUT_PDF_PAGE
    ) {

        type_str =
            "SCANNED_PDF_PAGE";
    }

    // =========================================================================
    // Foreground predicate
    // =========================================================================

    const auto is_active_pixel =
        [buffer, input_type](
            std::size_t x,
            std::size_t y
        ) noexcept -> bool {

            const std::size_t channels =
                buffer->channels;

            const std::size_t pixel_index =
                (
                    y *
                    buffer->width +
                    x
                ) *
                channels;

            // -----------------------------------------------------------------
            // 1-channel public OCR mask
            // -----------------------------------------------------------------

            if (
                channels == 1
            ) {

                /*
                 * Canonical public MatrixMatcher mask:
                 *
                 *     foreground = 255
                 *     background = 0
                 */
                return
                    buffer->data[
                        pixel_index
                    ] > 127;
            }

            // -----------------------------------------------------------------
            // Financial chart custom OCR representation
            // -----------------------------------------------------------------

            if (
                input_type ==
                    FIN_INPUT_FIN_CHART &&
                channels >= 3
            ) {

                return
                    buffer->data[
                        pixel_index + 1
                    ] >=
                    fin_ocr::config::CHART_FOREGROUND_THRESHOLD;
            }

            // -----------------------------------------------------------------
            // Ordinary RGB
            // -----------------------------------------------------------------

            if (
                channels >= 3
            ) {

                const uint8_t r =
                    buffer->data[
                        pixel_index + 0
                    ];

                const uint8_t g =
                    buffer->data[
                        pixel_index + 1
                    ];

                const uint8_t b =
                    buffer->data[
                        pixel_index + 2
                    ];

                const uint8_t gray =
                    fin_ocr::image::luminance_rgb(
                        r,
                        g,
                        b
                    );

                /*
                 * rgb_to_ocr_mask():
                 *
                 *     dark source pixel
                 *         -> foreground
                 *         -> 255
                 */
                const uint8_t inverted =
                    static_cast<uint8_t>(
                        255u -
                        static_cast<unsigned>(
                            gray
                        )
                    );

                return
                    inverted >
                    fin_ocr::config::DEFAULT_THRESHOLD;
            }

            return false;
        };

    // =========================================================================
    // Output header
    // =========================================================================

    std::ostringstream output;

    output
        << "[FIN_ENGINE_OCR_OUTPUT]\n"
        << "Dimensions: "
        << buffer->width
        << "x"
        << buffer->height
        << "\n"
        << "Channels: "
        << buffer->channels
        << "\n"
        << "Binarized: "
        << (
            buffer->is_binarized
                ? "YES"
                : "NO"
        )
        << "\n"
        << "Input Type: "
        << type_str
        << "\n";

    // =========================================================================
    // Scan granularity
    // =========================================================================

    const std::size_t line_height_scan =
        input_type ==
            FIN_INPUT_FIN_CHART

            ? 4u

            : std::max<std::size_t>(
                  8u,
                  buffer->height / 64u
              );

    // =========================================================================
    // Global statistics
    // =========================================================================

    std::size_t total_active_pixels =
        0;

    std::size_t total_pixels =
        0;

    struct LineRegion {

        std::size_t y0 = 0;
        std::size_t y1 = 0;

        std::size_t min_x = 0;
        std::size_t max_x = 0;

        std::size_t active_pixels = 0;

        double density = 0.0;
    };

    std::vector<LineRegion> regions;

    regions.reserve(
        buffer->height /
        std::max<std::size_t>(
            1u,
            line_height_scan
        ) +
        1u
    );

    // =========================================================================
    // Scan horizontal bands
    // =========================================================================

    for (
        std::size_t y = 0;
        y < buffer->height;
        y += line_height_scan
    ) {

        const std::size_t end_y =
            std::min(
                y + line_height_scan,
                buffer->height
            );

        const std::size_t band_height =
            end_y - y;

        if (
            band_height == 0
        ) {
            continue;
        }

        std::size_t row_active_pixels =
            0;

        std::size_t min_x =
            buffer->width;

        std::size_t max_x =
            0;

        // ---------------------------------------------------------------------
        // Scan pixels
        // ---------------------------------------------------------------------

        for (
            std::size_t ry = y;
            ry < end_y;
            ++ry
        ) {

            for (
                std::size_t x = 0;
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

        const std::size_t band_pixels =
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
        // Text line heuristic
        // ---------------------------------------------------------------------

        const bool has_horizontal_extent =
            max_x > min_x;

        const bool likely_text_line =
            input_type ==
                FIN_INPUT_FIN_CHART

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
        // Chart artifact rejection
        // ---------------------------------------------------------------------

        if (
            input_type ==
            FIN_INPUT_FIN_CHART
        ) {

            const std::size_t region_width =
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

            // Full-width high-density structures are normally axes/borders.
            if (
                width_ratio >= 0.90 &&
                band_height <= 16 &&
                band_density >= 0.05
            ) {
                continue;
            }

            // Full-width sparse structures are generally chart geometry.
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
    // Merge neighboring regions
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

        const std::size_t vertical_gap =
            current.y0 > previous.y1
                ? current.y0 -
                  previous.y1
                : 0;

        const std::size_t allowed_vertical_gap =
            input_type ==
                FIN_INPUT_FIN_CHART

                ? 4u

                : 2u;

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

            const std::size_t merged_height =
                previous.y1 -
                previous.y0;

            const std::size_t merged_area =
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
    // Final statistics
    // =========================================================================

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

    output
        << "Active Text Region Density: "
        << overall_density
        << "%\n";

    output
        << "Detected Text Lines: "
        << merged_regions.size()
        << "\n";

    // =========================================================================
    // Spatial regions
    // =========================================================================

    if (
        !merged_regions.empty()
    ) {

        output
            << "Spatial Bounding Regions:\n";

        std::size_t line_number =
            0;

        for (
            const LineRegion& region :
            merged_regions
        ) {

            ++line_number;

            const std::size_t region_width =
                region.max_x -
                region.min_x +
                1;

            const std::size_t region_height =
                region.y1 -
                region.y0;

            output
                << "  - Text Line "
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

    output
        << "Payload Status: "
        << "VALID_PREPROCESSED_BUFFER\n";

    return output.str();
}

} // namespace

// =============================================================================
// C ABI
// =============================================================================

extern "C" {

// =============================================================================
// ENGINE CREATE
// =============================================================================

FinOcrEngineContext*
fin_engine_create(void)
{
    try {

        auto* context =
            new (std::nothrow)
            FinOcrEngineContext{};

        if (
            context == nullptr
        ) {
            return nullptr;
        }

        context->max_buffer_limit =
            64ULL *
            1024ULL *
            1024ULL;

        context->notification_event_fd =
            -1;

        return context;

    } catch (...) {

        return nullptr;
    }
}

// =============================================================================
// ENGINE DESTROY
// =============================================================================

void
fin_engine_destroy(
    FinOcrEngineContext* engine
)
{
    if (
        engine == nullptr
    ) {
        return;
    }

    /*
     * Ownership of notification_event_fd remains with the Rust host.
     *
     * Therefore this function intentionally does NOT close it.
     */

    delete engine;
}

// =============================================================================
// EVENTFD CONFIGURATION
// =============================================================================

void
fin_engine_set_notification_fd(
    FinOcrEngineContext* engine,
    int event_fd
)
{
    if (
        engine == nullptr
    ) {
        return;
    }

    engine->notification_event_fd =
        event_fd;
}

// =============================================================================
// EVENTFD COMPLETION NOTIFICATION
// =============================================================================

int
fin_engine_notify_completion(
    FinOcrEngineContext* engine
)
{
    if (
        engine == nullptr ||
        engine->notification_event_fd < 0
    ) {
        return -1;
    }

    const std::uint64_t signal_value =
        1;

    const ssize_t written =
        write(
            engine->notification_event_fd,
            &signal_value,
            sizeof(signal_value)
        );

    if (
        written ==
        static_cast<ssize_t>(
            sizeof(signal_value)
        )
    ) {

        return 0;
    }

    return -2;
}

// =============================================================================
// HIGH-LEVEL DOCUMENT PROCESSING
// =============================================================================

FinProcessedBuffer*
fin_process_document_bytes(
    FinOcrEngineContext* engine,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
)
{
    if (
        engine == nullptr ||
        input_bytes == nullptr ||
        input_len == 0
    ) {

        return nullptr;
    }

    if (
        input_len >
        engine->max_buffer_limit
    ) {

        std::cerr
            << "[FinOcr Engine]: input exceeds configured buffer limit."
            << std::endl;

        return nullptr;
    }

    try {

        FinProcessedBuffer* result =
            fin_ocr::VisionPipeline::execute(
                input_bytes,
                input_len,
                input_type,
                target_width,
                target_height,
                target_channels
            );

        if (
            result != nullptr
        ) {

            /*
             * Notify the Rust host only after the pipeline has produced the
             * processed buffer successfully.
             */
            (void)fin_engine_notify_completion(
                engine
            );
        }

        return result;

    } catch (
        const std::exception& error
    ) {

        std::cerr
            << "[FinOcr Engine Exception]: "
            << error.what()
            << std::endl;

        return nullptr;

    } catch (...) {

        std::cerr
            << "[FinOcr Engine Exception]: "
            << "Unknown error occurred."
            << std::endl;

        return nullptr;
    }
}

// =============================================================================
// HIGH-LEVEL TEXT EXTRACTION
// =============================================================================
//
// Priority:
//
//     1. Raster/layout metadata.
//
//     2. Native PDF text stream, when the input is a digital PDF.
//
//     3. OCR text already attached to FinProcessedBuffer.
//
// The actual OCR work is NOT performed here anymore.
//
// That work has already happened inside:
//
//     VisionPipeline
//         |
//         +--> DocumentOcr
//         |      +--> DocumentTesseract
//         |      +--> MatrixMatcher fallback
//         |
//         +--> ChartLabelRecognizer
//                +--> LineRecognizer
//                       +--> TesseractRecognizer
//                       +--> GlyphMatcher
//
// =============================================================================

char*
fin_engine_recognize_text(
    FinOcrEngineContext* engine,
    const FinProcessedBuffer* buffer,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
)
{
    if (
        engine == nullptr ||
        buffer == nullptr
    ) {
        return nullptr;
    }

    try {

        // =========================================================================
        // STRUCTURED BUFFER ANALYSIS
        // =========================================================================

        std::string final_payload =
            extract_raster_layout_analysis(
                buffer,
                input_type
            );

        // =========================================================================
        // DIGITAL PDF NATIVE TEXT
        // =========================================================================

        if (
            input_type ==
                FIN_INPUT_PDF_PAGE &&
            input_bytes != nullptr &&
            input_len > 0
        ) {

            /*
             * Respect the engine's configured upper bound here as well.
             */
            if (
                input_len <=
                engine->max_buffer_limit
            ) {

                const std::string pdf_text =
                    extract_pdf_native_text(
                        input_bytes,
                        input_len
                    );

                if (
                    !pdf_text.empty()
                ) {

                    final_payload +=
                        "\n"
                        "[EXTRACTED_PDF_TEXT_STREAM]\n";

                    final_payload +=
                        pdf_text;
                }
            }
        }

        // =========================================================================
        // MODEL-FREE OCR RESULT FROM VISION PIPELINE
        // =========================================================================

        if (
            buffer->extracted_text !=
                nullptr &&
            buffer->extracted_text[0] !=
                '\0'
        ) {

            if (
                input_type ==
                FIN_INPUT_FIN_CHART
            ) {

                final_payload +=
                    "\n"
                    "[EXTRACTED_CHART_LABELS]\n";

            } else {

                final_payload +=
                    "\n"
                    "[EXTRACTED_MODEL_FREE_OCR]\n";
            }

            final_payload +=
                buffer->extracted_text;
        }

        // =========================================================================
        // VALIDATE FINAL PAYLOAD
        // =========================================================================

        if (
            final_payload.empty()
        ) {

            return nullptr;
        }

        // =========================================================================
        // C ABI ALLOCATION
        // =========================================================================

        char* output =
            static_cast<char*>(
                std::malloc(
                    final_payload.size() + 1
                )
            );

        if (
            output == nullptr
        ) {

            return nullptr;
        }

        std::memcpy(
            output,
            final_payload.c_str(),
            final_payload.size() + 1
        );

        return output;

    } catch (...) {

        return nullptr;
    }
}

// =============================================================================
// FREE C-ABI STRING
// =============================================================================

void
fin_free_string(
    char* str
)
{
    if (
        str != nullptr
    ) {

        std::free(
            str
        );
    }
}

// =============================================================================
// FREE PROCESSED BUFFER
// =============================================================================

void
fin_free_processed_buffer(
    FinProcessedBuffer* buffer
)
{
    if (
        buffer == nullptr
    ) {
        return;
    }

    if (
        buffer->data != nullptr
    ) {

        fin::ops::aligned_free(
            buffer->data
        );

        buffer->data =
            nullptr;
    }

    if (
        buffer->extracted_text != nullptr
    ) {

        std::free(
            buffer->extracted_text
        );

        buffer->extracted_text =
            nullptr;
    }

    delete buffer;
}

// =============================================================================
// THERMAL METRICS
// =============================================================================

FinThermalMetrics
fin_get_thermal_metrics(
    void
)
{
    const fin::ThermalMetrics metrics =
        fin::ThermalMonitor::instance()
            .read_metrics();

    FinThermalMetrics result{};

    result.max_temp_celsius =
        metrics.max_temp_celsius;

    result.avg_temp_celsius =
        metrics.avg_temp_celsius;

    result.status =
        static_cast<FinThermalStatus>(
            metrics.status
        );

    return result;
}

// =============================================================================
// THERMAL THRESHOLDS
// =============================================================================

void
fin_set_thermal_thresholds(
    float warm_limit_c,
    float critical_limit_c
)
{
    fin::ThermalMonitor::instance()
        .set_thresholds(
            warm_limit_c,
            critical_limit_c
        );
}

} // extern "C"
