#include "fin_ocr/pipeline/vision_pipeline.hpp"

#include "fin_ocr/chart/chart_color_isolator.hpp"
#include "fin_ocr/chart/chart_label_recognizer.hpp"
#include "fin_ocr/chart/chart_axis_detector.hpp"
#include "fin_ocr/chart/chart_object_detector.hpp"
#include "fin_ocr/chart/chart_associator.hpp"
#include "fin_ocr/chart/chart_interpreter.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/ocr_types.hpp"
#include "fin_ocr/core/safe_arithmetric.hpp"
#include "fin_ocr/core/buffer_utils.hpp"

#include "fin_ocr/image/grayscale.hpp"
#include "fin_ocr/image/luminance.hpp"
#include "fin_ocr/image/contrast_normalizer.hpp"
#include "fin_ocr/image/resize.hpp"
#include "fin_ocr/image/binary.hpp"

#include "fin_ocr/matrix/glyph_matcher.hpp"
#include "fin_ocr/matrix/glyph_template.hpp"
#include "fin_ocr/matrix/glyph_templates.hpp"

#include "fin_ocr/line/line_recognizer.hpp"

#include "fin_ocr/document/document_ocr.hpp"
#include "fin_ocr/tesseract/tesseract_recognizer.hpp"

#include "fin_ocr/pdf/pdf_page.hpp"
#include "fin_ocr/pdf/pdf_rasterizer.hpp"

#include "tensor_ops.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

namespace {

inline constexpr char CHART_FALLBACK_TEXT[] =
    "[CHART_TEXT_DATA]\n"
    "  - Line 1 [Y:0-0, X:0-0]: "
    "[NO_RECOGNIZED_CHART_LABELS] "
    "[confidence=0.000000]\n";

// =============================================================================
// VALIDATION
// =============================================================================

void validate_request(
    const uint8_t* input_bytes,
    std::size_t input_len,
    FinInputType input_type,
    std::size_t target_width,
    std::size_t target_height,
    std::size_t target_channels
) {

    if (input_bytes == nullptr) {

        throw std::invalid_argument(
            "[FIN_ERROR] Input buffer is null."
        );
    }

    if (input_len == 0) {

        throw std::invalid_argument(
            "[FIN_ERROR] Input buffer is empty."
        );
    }

    if (
        target_width == 0 ||
        target_height == 0
    ) {

        throw std::invalid_argument(
            "[FIN_ERROR] Target dimensions must be strictly positive."
        );
    }

    if (
        target_channels == 0
    ) {

        throw std::invalid_argument(
            "[FIN_ERROR] Target channel count must be strictly positive."
        );
    }

    if (
        target_width >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()
            ) ||
        target_height >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()
            )
    ) {

        throw std::invalid_argument(
            "[FIN_ERROR] Target dimensions exceed native int range."
        );
    }

    switch (input_type) {

        case FIN_INPUT_RAW_IMAGE:
        case FIN_INPUT_PDF_PAGE:
        case FIN_INPUT_FIN_CHART:
            break;

        default:

            throw std::invalid_argument(
                "[FIN_ERROR] Unsupported input type."
            );
    }

    if (
        input_type ==
            FIN_INPUT_PDF_PAGE &&
        target_channels != 1 &&
        target_channels != 4
    ) {

        throw std::invalid_argument(
            "[FIN_ERROR] PDF target_channels must be 1 or 4."
        );
    }
}

// =============================================================================
// RESULT ALLOCATION
// =============================================================================

FinProcessedBuffer* allocate_result(
    std::size_t width,
    std::size_t height,
    std::size_t channels
) noexcept {

    std::size_t num_pixels = 0;
    std::size_t data_len = 0;

    if (
        !core::safe_mul(
            width,
            height,
            num_pixels
        )
    ) {

        return nullptr;
    }

    if (
        !core::safe_mul(
            num_pixels,
            channels,
            data_len
        )
    ) {

        return nullptr;
    }

    auto* result =
        new (std::nothrow)
        FinProcessedBuffer{};

    if (
        result == nullptr
    ) {

        return nullptr;
    }

    result->data = nullptr;

    result->width = width;
    result->height = height;
    result->channels = channels;
    result->data_len = data_len;
    result->is_binarized = 0;
    result->extracted_text = nullptr;

    result->data =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                data_len
            )
        );

    if (
        result->data == nullptr
    ) {

        delete result;

        return nullptr;
    }

    std::memset(
        result->data,
        0,
        data_len
    );

    return result;
}

// =============================================================================
// EXTRACTED TEXT OWNERSHIP
// =============================================================================

bool set_extracted_text(
    FinProcessedBuffer& result,
    const std::string& text
) noexcept {

    if (
        text.empty()
    ) {

        return true;
    }

    char* buffer =
        static_cast<char*>(
            std::malloc(
                text.size() + 1
            )
        );

    if (
        buffer == nullptr
    ) {

        return false;
    }

    std::memcpy(
        buffer,
        text.c_str(),
        text.size() + 1
    );

    if (
        result.extracted_text != nullptr
    ) {

        std::free(
            result.extracted_text
        );
    }

    result.extracted_text = buffer;

    return true;
}

// =============================================================================
// INPUT SIZE -> INT
// =============================================================================

bool size_to_int(
    std::size_t value,
    int& output
) noexcept {

    if (
        value >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        )
    ) {

        return false;
    }

    output =
        static_cast<int>(
            value
        );

    return true;
}

// =============================================================================
// RGB IMAGE DECODING
// =============================================================================

unsigned char* decode_rgb_image(
    const uint8_t* input,
    std::size_t input_len,
    int& width,
    int& height
) noexcept {

    int source_channels = 0;

    if (
        input == nullptr ||
        input_len >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()
            )
    ) {

        return nullptr;
    }

    return stbi_load_from_memory(
        input,
        static_cast<int>(input_len),
        &width,
        &height,
        &source_channels,
        3
    );
}

// =============================================================================
// NORMAL IMAGE PROCESSING
// =============================================================================

bool process_normal_image(
    const uint8_t* input_bytes,
    std::size_t input_len,
    FinInputType input_type,
    FinProcessedBuffer& result,
    uint8_t*& ocr_grayscale,
    std::size_t& ocr_pixels
) noexcept {

    int decoded_width = 0;
    int decoded_height = 0;

    unsigned char* decoded =
        decode_rgb_image(
            input_bytes,
            input_len,
            decoded_width,
            decoded_height
        );

    if (
        decoded == nullptr
    ) {

        return false;
    }

    std::size_t num_pixels = 0;

    if (
        !core::safe_mul(
            result.width,
            result.height,
            num_pixels
        )
    ) {

        stbi_image_free(decoded);

        return false;
    }

    bool success = false;

    if (
        result.channels == 3
    ) {

        int target_width = 0;
        int target_height = 0;

        if (
            !size_to_int(
                result.width,
                target_width
            ) ||
            !size_to_int(
                result.height,
                target_height
            )
        ) {

            stbi_image_free(decoded);

            return false;
        }

        image::resize_rgb_nearest(
            decoded,
            decoded_width,
            decoded_height,
            result.data,
            target_width,
            target_height
        );

        if (
            input_type !=
            FIN_INPUT_FIN_CHART
        ) {

            ocr_grayscale =
                static_cast<uint8_t*>(
                    fin::ops::aligned_alloc(
                        64,
                        num_pixels
                    )
                );

            if (
                ocr_grayscale != nullptr
            ) {

                image::rgb_to_grayscale(
                    result.data,
                    ocr_grayscale,
                    num_pixels
                );

                uint8_t* normalized =
                    static_cast<uint8_t*>(
                        fin::ops::aligned_alloc(
                            64,
                            num_pixels
                        )
                    );

                if (
                    normalized != nullptr
                ) {

                    image::normalize_grayscale_for_ocr(
                        ocr_grayscale,
                        normalized,
                        num_pixels
                    );

                    std::memcpy(
                        ocr_grayscale,
                        normalized,
                        num_pixels
                    );

                    fin::ops::aligned_free(
                        normalized
                    );
                }

                ocr_pixels =
                    num_pixels;
            }
        }

        success = true;

    } else if (
        result.channels == 1
    ) {

        std::size_t resized_rgb_bytes = 0;

        if (
            !core::safe_mul(
                num_pixels,
                std::size_t{3},
                resized_rgb_bytes
            )
        ) {

            stbi_image_free(decoded);

            return false;
        }

        uint8_t* resized_rgb =
            static_cast<uint8_t*>(
                fin::ops::aligned_alloc(
                    64,
                    resized_rgb_bytes
                )
            );

        if (
            resized_rgb == nullptr
        ) {

            stbi_image_free(decoded);

            return false;
        }

        int target_width = 0;
        int target_height = 0;

        if (
            !size_to_int(
                result.width,
                target_width
            ) ||
            !size_to_int(
                result.height,
                target_height
            )
        ) {

            fin::ops::aligned_free(resized_rgb);
            stbi_image_free(decoded);

            return false;
        }

        image::resize_rgb_nearest(
            decoded,
            decoded_width,
            decoded_height,
            resized_rgb,
            target_width,
            target_height
        );

        if (
            input_type ==
            FIN_INPUT_FIN_CHART
        ) {

            image::rgb_to_ocr_mask(
                resized_rgb,
                result.data,
                num_pixels
            );

            result.is_binarized = 1;
            success = true;

        } else {

            ocr_grayscale =
                static_cast<uint8_t*>(
                    fin::ops::aligned_alloc(
                        64,
                        num_pixels
                    )
                );

            if (
                ocr_grayscale != nullptr
            ) {

                image::rgb_to_grayscale(
                    resized_rgb,
                    ocr_grayscale,
                    num_pixels
                );

                uint8_t* normalized =
                    static_cast<uint8_t*>(
                        fin::ops::aligned_alloc(
                            64,
                            num_pixels
                        )
                    );

                if (
                    normalized != nullptr
                ) {

                    image::normalize_grayscale_for_ocr(
                        ocr_grayscale,
                        normalized,
                        num_pixels
                    );

                    std::memcpy(
                        ocr_grayscale,
                        normalized,
                        num_pixels
                    );

                    fin::ops::aligned_free(
                        normalized
                    );
                }

                image::grayscale_to_binary(
                    ocr_grayscale,
                    result.data,
                    num_pixels
                );

                result.is_binarized = 1;
                ocr_pixels = num_pixels;
                success = true;
            }
        }

        fin::ops::aligned_free(
            resized_rgb
        );
    }

    stbi_image_free(decoded);

    return success;
}

// =============================================================================
// PDF PROCESSING
// =============================================================================

bool process_pdf(
    const uint8_t* input_bytes,
    std::size_t input_len,
    FinProcessedBuffer& result,
    uint8_t*& ocr_grayscale,
    std::size_t& ocr_pixels,
    int& ocr_width,
    int& ocr_height
) noexcept {

    if (
        result.channels != 1 &&
        result.channels != 4
    ) {

        return false;
    }

    int public_width = 0;
    int public_height = 0;

    if (
        !size_to_int(
            result.width,
            public_width
        ) ||
        !size_to_int(
            result.height,
            public_height
        )
    ) {

        return false;
    }

    PdfPageDimensions dimensions{};

    if (
        !PdfPage::get_dimensions(
            input_bytes,
            input_len,
            dimensions
        )
    ) {

        return false;
    }

    if (
        !PdfRasterizer::compute_ocr_size(
            dimensions.width_points,
            dimensions.height_points,
            public_width,
            public_height,
            ocr_width,
            ocr_height
        )
    ) {

        return false;
    }

    if (
        !core::safe_mul(
            static_cast<std::size_t>(ocr_width),
            static_cast<std::size_t>(ocr_height),
            ocr_pixels
        )
    ) {

        return false;
    }

    std::size_t ocr_bgra_bytes = 0;

    if (
        !core::safe_mul(
            ocr_pixels,
            std::size_t{4},
            ocr_bgra_bytes
        )
    ) {

        return false;
    }

    uint8_t* ocr_bgra =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                ocr_bgra_bytes
            )
        );

    if (
        ocr_bgra == nullptr
    ) {

        return false;
    }

    const bool rendered =
        PdfRasterizer::render_page_to_bgra(
            input_bytes,
            input_len,
            ocr_bgra,
            ocr_bgra_bytes,
            ocr_width,
            ocr_height
        );

    if (
        !rendered
    ) {

        fin::ops::aligned_free(ocr_bgra);

        return false;
    }

    ocr_grayscale =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                ocr_pixels
            )
        );

    if (
        ocr_grayscale == nullptr
    ) {

        fin::ops::aligned_free(ocr_bgra);

        return false;
    }

    image::bgra_to_grayscale(
        ocr_bgra,
        ocr_grayscale,
        ocr_pixels
    );

    uint8_t* normalized =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                ocr_pixels
            )
        );

    if (
        normalized != nullptr
    ) {

        image::normalize_grayscale_for_ocr(
            ocr_grayscale,
            normalized,
            ocr_pixels
        );

        std::memcpy(
            ocr_grayscale,
            normalized,
            ocr_pixels
        );

        fin::ops::aligned_free(
            normalized
        );
    }

    std::size_t public_pixels = 0;

    if (
        !core::safe_mul(
            result.width,
            result.height,
            public_pixels
        )
    ) {

        fin::ops::aligned_free(ocr_bgra);

        return false;
    }

    if (
        result.channels == 1
    ) {

        std::size_t public_bgra_bytes = 0;

        if (
            !core::safe_mul(
                public_pixels,
                std::size_t{4},
                public_bgra_bytes
            )
        ) {

            fin::ops::aligned_free(ocr_bgra);

            return false;
        }

        uint8_t* public_bgra =
            static_cast<uint8_t*>(
                fin::ops::aligned_alloc(
                    64,
                    public_bgra_bytes
                )
            );

        if (
            public_bgra == nullptr
        ) {

            fin::ops::aligned_free(ocr_bgra);

            return false;
        }

        const bool public_rendered =
            PdfRasterizer::render_page_to_bgra(
                input_bytes,
                input_len,
                public_bgra,
                public_bgra_bytes,
                public_width,
                public_height
            );

        if (
            public_rendered
        ) {

            image::bgra_to_ocr_mask(
                public_bgra,
                result.data,
                public_pixels
            );

            result.is_binarized = 1;
        }

        fin::ops::aligned_free(public_bgra);

        if (
            !public_rendered
        ) {

            fin::ops::aligned_free(ocr_bgra);

            return false;
        }

    } else {

        const bool public_rendered =
            PdfRasterizer::render_page_to_bgra(
                input_bytes,
                input_len,
                result.data,
                result.data_len,
                public_width,
                public_height
            );

        if (
            !public_rendered
        ) {

            fin::ops::aligned_free(ocr_bgra);

            return false;
        }

        result.is_binarized = 0;
    }

    fin::ops::aligned_free(ocr_bgra);

    return true;
}

// =============================================================================
// CHART OCR RECOGNITION CONTEXT
// =============================================================================

struct ChartRecognizerContext {

    GlyphMatcher glyph_matcher;

    TesseractRecognizer tesseract_recognizer;

    LineRecognizer line_recognizer;

    ChartLabelRecognizer chart_recognizer;

    ChartRecognizerContext()
        : glyph_matcher(
              default_glyph_templates()
          )
        , line_recognizer(
              glyph_matcher,
              tesseract_recognizer
          )
        , chart_recognizer(
              line_recognizer
          )
    {
    }
};

// =============================================================================
// CHART ANALYSIS CONTEXT
// =============================================================================

struct ChartAnalysisContext {

    chart::ChartAxisDetector axis_detector;

    chart::ChartObjectDetector object_detector;

    chart::ChartAssociator associator;

    chart::ChartInterpreter interpreter;
};

// =============================================================================
// CHART OCR + GEOMETRY + SEMANTIC ANALYSIS
// =============================================================================
//
// Pipeline:
//
//     RGB
//       |
//       v
//     ChartColorIsolator
//       |
//       +-------------------------------+
//       |                               |
//       v                               v
// ChartLabelRecognizer            ChartAxisDetector
//       |                               |
//       v                               v
// vector<ChartLabel>             ChartCoordinateSystem
//       |                               |
//       +---------------+---------------+
//                       |
//                       v
//               ChartObjectDetector
//                       |
//                       v
//                 ChartObjectSet
//                       |
//                       +----------------------------+
//                       |                            |
//                       v                            |
//                ChartAssociator                     |
//                       |                            |
//                       v                            |
//              ChartAssociationResult                |
//                       |                            |
//                       +-------------+--------------+
//                                     |
//                                     v
//                            ChartInterpreter
//                                     |
//                                     v
//                              ChartAnalysis
//
// IMPORTANT:
//
//     The formatted [CHART_TEXT_DATA] payload is presentation/transport data.
//
//     The semantic pipeline NEVER parses that formatted string.
//
//     Associator and Interpreter consume the structured objects directly.
//
// =============================================================================

bool process_chart(
    FinProcessedBuffer& result
) noexcept {

    // =========================================================================
    // DETERMINISTIC STRUCTURAL FALLBACK
    // =========================================================================
    //
    // Transport/compatibility fallback only.
    //
    // This does NOT fabricate a financial value or semantic chart type.
    //
    const auto attach_fallback =
        [&result]() noexcept -> bool {

            return set_extracted_text(
                result,
                std::string{
                    CHART_FALLBACK_TEXT
                }
            );
        };

    // =========================================================================
    // CHANNEL CONTRACT
    // =========================================================================

    if (
        result.channels != 3
    ) {

        result.is_binarized =
            0;

        (void)attach_fallback();

        return
            result.extracted_text != nullptr;
    }

    // =========================================================================
    // PIXEL COUNT
    // =========================================================================

    std::size_t num_pixels =
        0;

    if (
        !core::safe_mul(
            result.width,
            result.height,
            num_pixels
        )
    ) {

        (void)attach_fallback();

        return false;
    }

    // =========================================================================
    // INPUT BUFFER VALIDATION
    // =========================================================================

    if (
        result.data == nullptr ||
        result.data_len == 0
    ) {

        (void)attach_fallback();

        return false;
    }

    // =========================================================================
    // CHART OCR WORK BUFFER
    // =========================================================================

    uint8_t* chart_ocr =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                result.data_len
            )
        );

    if (
        chart_ocr == nullptr
    ) {

        (void)attach_fallback();

        return false;
    }

    // =========================================================================
    // CHART COLOR ISOLATION
    // =========================================================================

    const bool isolated =
        ChartColorIsolator::isolate(
            result.data,
            chart_ocr,
            num_pixels
        );

    if (
        !isolated
    ) {

        fin::ops::aligned_free(
            chart_ocr
        );

        (void)attach_fallback();

        return false;
    }

    bool attached =
        false;

    try {

        // =====================================================================
        // CONTEXT CONSTRUCTION
        // =====================================================================
        //
        // OCR context:
        //
        //     GlyphMatcher
        //         ↓
        //     TesseractRecognizer
        //         ↓
        //     LineRecognizer
        //         ↓
        //     ChartLabelRecognizer
        //
        // Analysis context:
        //
        //     AxisDetector
        //     ObjectDetector
        //     Associator
        //     Interpreter
        //
        // =====================================================================

        ChartRecognizerContext recognizer_context;

        ChartAnalysisContext analysis_context;

        const int width =
            static_cast<int>(
                result.width
            );

        const int height =
            static_cast<int>(
                result.height
            );

        // =====================================================================
        // STEP 1: STRUCTURED CHART LABEL RECOGNITION
        // =====================================================================
        //
        // IMPORTANT:
        //
        // Do NOT call recognize() here and then attempt to parse
        // [CHART_TEXT_DATA].
        //
        // recognize_labels() gives the semantic layer the actual structured
        // ChartLabel objects including:
        //
        //     text
        //     bounding box
        //     confidence
        //     initial semantic fields
        //
        // =====================================================================

        const std::vector<chart::ChartLabel> labels =
            recognizer_context.chart_recognizer.recognize_labels(
                chart_ocr,
                width,
                height
            );

        // =====================================================================
        // STEP 2: BUILD PRESENTATION OCR PAYLOAD
        // =====================================================================
        //
        // This payload remains compatible with the existing test/FFI contract.
        //
        // =====================================================================

        std::string text_payload;

        text_payload.reserve(
            256 +
            labels.size() * 96
        );

        text_payload +=
            "[CHART_TEXT_DATA]\n";

        if (
            labels.empty()
        ) {

            text_payload +=
                "  - Line 1 [Y:0-0, X:0-0]: "
                "[NO_RECOGNIZED_CHART_LABELS] "
                "[confidence=0.000000]\n";

        } else {

            for (
                std::size_t i = 0;
                i < labels.size();
                ++i
            ) {

                const chart::ChartLabel& label =
                    labels[i];

                text_payload +=
                    "  - Line " +
                    std::to_string(
                        i + 1
                    );

                text_payload +=
                    " [Y:" +
                    std::to_string(
                        label.min_y
                    );

                text_payload +=
                    "-" +
                    std::to_string(
                        label.max_y + 1
                    );

                text_payload +=
                    ", X:" +
                    std::to_string(
                        label.min_x
                    );

                text_payload +=
                    "-" +
                    std::to_string(
                        label.max_x
                    );

                text_payload +=
                    "]: " +
                    label.text;

                text_payload +=
                    " [confidence=" +
                    std::to_string(
                        label.confidence
                    ) +
                    "]\n";
            }
        }

        // =====================================================================
        // STEP 3: ATTACH OCR PAYLOAD
        // =====================================================================

        if (
            !text_payload.empty()
        ) {

            attached =
                set_extracted_text(
                    result,
                    text_payload
                );
        }

        if (
            !attached
        ) {

            attached =
                attach_fallback();
        }

        // =====================================================================
        // STEP 4: AXIS DETECTION
        // =====================================================================

        const chart::ChartCoordinateSystem coordinates =
            analysis_context.axis_detector.detect(
                chart_ocr,
                width,
                height,
                3
            );

        // =====================================================================
        // STEP 5: OBJECT DETECTION
        // =====================================================================

        const chart::ChartObjectSet objects =
            analysis_context.object_detector.detect(
                chart_ocr,
                width,
                height,
                3,
                coordinates
            );

        // =====================================================================
        // STEP 6: STRUCTURED LABEL ASSOCIATION
        // =====================================================================
        //
        // This is where the OCR labels become geometrically/semantically
        // associated with:
        //
        //     - X axis categories
        //     - Y axis labels
        //     - series / legend labels
        //     - bars
        //     - paths
        //
        // ChartAssociator owns an enriched copy of the labels and performs
        // classification and relationship construction. :contentReference[oaicite:0]{index=0}
        //
        // =====================================================================

        const chart::ChartAssociationResult associations =
            analysis_context.associator.associate(
                coordinates,
                objects,
                labels
            );

        // =====================================================================
        // STEP 7: FULL CHART INTERPRETATION
        // =====================================================================
        //
        // The interpreter now receives the structured output directly.
        //
        // It determines things such as:
        //
        //     - chart type
        //     - stacked / clustered / combo classification
        //     - data points
        //     - semantic validity
        //     - analysis confidence
        //
        // =====================================================================

        const chart::ChartAnalysis analysis =
            analysis_context.interpreter.interpret(
                coordinates,
                objects,
                associations
            );

        // =====================================================================
        // STEP 8: APPEND STRUCTURED ANALYSIS DIAGNOSTICS
        // =====================================================================
        //
        // Keep the existing OCR payload and expose the semantic stage as a
        // separate section.
        //
        // No financial values are invented here.
        //
        // =====================================================================

        text_payload +=
            "\n"
            "[CHART_ANALYSIS]\n";

        text_payload +=
            "Valid: " +
            std::string(
                analysis.valid
                    ? "YES"
                    : "NO"
            ) +
            "\n";

        text_payload +=
            "Confidence: " +
            std::to_string(
                analysis.confidence
            ) +
            "\n";

        text_payload +=
            "Labels: " +
            std::to_string(
                analysis.associations.labels.size()
            ) +
            "\n";

        text_payload +=
            "Categories: " +
            std::to_string(
                analysis.associations.categories.size()
            ) +
            "\n";

        text_payload +=
            "Series: " +
            std::to_string(
                analysis.associations.series.size()
            ) +
            "\n";

        text_payload +=
            "Associations: " +
            std::to_string(
                analysis.associations.associations.size()
            ) +
            "\n";

        text_payload +=
            "Data Points: " +
            std::to_string(
                analysis.data_points.size()
            ) +
            "\n";

        // =====================================================================
        // STEP 9: ANALYSIS STATUS
        // =====================================================================
        //
        // Keep the final payload attached even if the semantic interpreter
        // legitimately determines that the chart is not fully interpretable.
        //
        // OCR success and semantic-analysis success are independent concepts.
        //
        // =====================================================================

        attached =
            set_extracted_text(
                result,
                text_payload
            );

        if (
            !attached
        ) {

            attached =
                attach_fallback();
        }

        // =====================================================================
        // STEP 10: COPY FINAL CHART WORK BUFFER
        // =====================================================================

        std::memcpy(
            result.data,
            chart_ocr,
            result.data_len
        );

        fin::ops::aligned_free(
            chart_ocr
        );

        result.is_binarized =
            0;

        return attached;

    } catch (...) {

        // =====================================================================
        // EXCEPTION SAFETY
        // =====================================================================
        //
        // Never allow chart semantic-analysis failure to leak the temporary
        // chart OCR buffer.
        //
        // Preserve the OCR transport contract whenever possible.
        //
        // =====================================================================

        if (
            !attached
        ) {

            attached =
                attach_fallback();
        }

        fin::ops::aligned_free(
            chart_ocr
        );

        result.is_binarized =
            0;

        return attached;
    }
}

// =============================================================================
// BUILD BINARY MASK FOR DOCUMENT OCR
// =============================================================================

uint8_t* build_document_mask(
    const FinProcessedBuffer& result,
    const uint8_t* ocr_grayscale,
    std::size_t ocr_pixels,
    bool& owns_mask
) noexcept {

    owns_mask = false;

    if (
        ocr_grayscale == nullptr ||
        ocr_pixels == 0
    ) {

        return nullptr;
    }

    uint8_t* mask =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                ocr_pixels
            )
        );

    if (
        mask == nullptr
    ) {

        return nullptr;
    }

    image::grayscale_to_binary(
        ocr_grayscale,
        mask,
        ocr_pixels
    );

    owns_mask = true;

    (void)result;

    return mask;
}

// =============================================================================
// FALLBACK GRAYSCALE
// =============================================================================

bool build_fallback_grayscale(
    const FinProcessedBuffer& result,
    uint8_t*& grayscale,
    std::size_t pixels
) noexcept {

    if (
        grayscale != nullptr
    ) {

        return true;
    }

    if (
        result.data == nullptr ||
        pixels == 0
    ) {

        return false;
    }

    grayscale =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                pixels
            )
        );

    if (
        grayscale == nullptr
    ) {

        return false;
    }

    if (
        result.channels == 1
    ) {

        for (
            std::size_t i = 0;
            i < pixels;
            ++i
        ) {

            grayscale[i] =
                result.data[i] != 0
                    ? uint8_t{0}
                    : uint8_t{255};
        }

        return true;
    }

    if (
        result.channels == 3
    ) {

        image::rgb_to_grayscale(
            result.data,
            grayscale,
            pixels
        );

        return true;
    }

    std::memset(
        grayscale,
        255,
        pixels
    );

    return true;
}

// =============================================================================
// DOCUMENT OCR
// =============================================================================

void run_document_ocr(
    FinProcessedBuffer& result,
    uint8_t* ocr_grayscale,
    std::size_t ocr_pixels,
    int ocr_width,
    int ocr_height,
    FinInputType input_type
) noexcept {

    if (
        ocr_grayscale == nullptr ||
        ocr_pixels == 0
    ) {

        return;
    }

    bool owns_mask = false;

    uint8_t* ocr_mask =
        build_document_mask(
            result,
            ocr_grayscale,
            ocr_pixels,
            owns_mask
        );

    if (
        ocr_mask == nullptr
    ) {

        return;
    }

    DocumentOcr recognizer;

    const std::string text =
        recognizer.recognize(
            ocr_grayscale,
            ocr_mask,
            ocr_width,
            ocr_height,
            input_type
        );

    if (
        !text.empty()
    ) {

        set_extracted_text(
            result,
            text
        );
    }

    if (
        owns_mask &&
        ocr_mask != nullptr
    ) {

        fin::ops::aligned_free(
            ocr_mask
        );
    }

    if (
        result.channels == 1
    ) {

        result.is_binarized = 1;
    }
}

// =============================================================================
// FINAL FALLBACK
// =============================================================================

void apply_raw_fallback(
    const uint8_t* input_bytes,
    std::size_t input_len,
    FinProcessedBuffer& result,
    FinInputType input_type
) noexcept {

    core::copy_raw_fallback(
        input_bytes,
        input_len,
        result.data,
        result.data_len
    );

    if (
        input_type ==
            FIN_INPUT_PDF_PAGE &&
        result.channels == 1
    ) {

        image::force_binary_inplace(
            result.data,
            result.data_len
        );

        result.is_binarized = 1;

    } else {

        result.is_binarized = 0;
    }
}

} // namespace

// =============================================================================
// PUBLIC PIPELINE
// =============================================================================

FinProcessedBuffer*
VisionPipeline::execute(
    const uint8_t* input_bytes,
    std::size_t input_len,
    FinInputType input_type,
    std::size_t target_width,
    std::size_t target_height,
    std::size_t target_channels
) {

    validate_request(
        input_bytes,
        input_len,
        input_type,
        target_width,
        target_height,
        target_channels
    );

    FinProcessedBuffer* result =
        allocate_result(
            target_width,
            target_height,
            target_channels
        );

    if (
        result == nullptr
    ) {

        return nullptr;
    }

    uint8_t* ocr_grayscale = nullptr;

    std::size_t ocr_pixels = 0;

    int ocr_width =
        static_cast<int>(
            target_width
        );

    int ocr_height =
        static_cast<int>(
            target_height
        );

    bool decode_success = false;

    // =========================================================================
    // PDF
    // =========================================================================

    if (
        input_type ==
        FIN_INPUT_PDF_PAGE
    ) {

        decode_success =
            process_pdf(
                input_bytes,
                input_len,
                *result,
                ocr_grayscale,
                ocr_pixels,
                ocr_width,
                ocr_height
            );

    } else {

        // =====================================================================
        // IMAGE / CHART
        // =====================================================================

        decode_success =
            process_normal_image(
                input_bytes,
                input_len,
                input_type,
                *result,
                ocr_grayscale,
                ocr_pixels
            );
    }

    // =========================================================================
    // DECODE FALLBACK
    // =========================================================================

    if (
        !decode_success
    ) {

        apply_raw_fallback(
            input_bytes,
            input_len,
            *result,
            input_type
        );
    }

    // =========================================================================
    // CHART
    // =========================================================================

    if (
        input_type ==
        FIN_INPUT_FIN_CHART
    ) {

        // Chart processing owns the chart OCR attachment contract.
        // Always invoke it for a 3-channel chart, even when image decoding
        // reported failure; process_chart() will attach its deterministic
        // structural fallback when recognition cannot run.
        if (
            result->channels == 3
        ) {

            (void)process_chart(
                *result
            );
        }

        if (
            ocr_grayscale != nullptr
        ) {

            fin::ops::aligned_free(
                ocr_grayscale
            );

            ocr_grayscale = nullptr;
        }

        result->is_binarized = 0;

        return result;
    }

    // =========================================================================
    // NORMAL IMAGE OCR WORKSPACE
    // =========================================================================

    std::size_t result_pixels = 0;

    if (
        !core::safe_mul(
            result->width,
            result->height,
            result_pixels
        )
    ) {

        if (
            ocr_grayscale != nullptr
        ) {

            fin::ops::aligned_free(
                ocr_grayscale
            );

            ocr_grayscale = nullptr;
        }

        return result;
    }

    if (
        ocr_grayscale == nullptr
    ) {

        if (
            build_fallback_grayscale(
                *result,
                ocr_grayscale,
                result_pixels
            )
        ) {

            ocr_pixels = result_pixels;

            ocr_width =
                static_cast<int>(
                    result->width
                );

            ocr_height =
                static_cast<int>(
                    result->height
                );
        }
    }

    // =========================================================================
    // DOCUMENT OCR
    // =========================================================================

    if (
        ocr_grayscale != nullptr &&
        ocr_pixels > 0
    ) {

        run_document_ocr(
            *result,
            ocr_grayscale,
            ocr_pixels,
            ocr_width,
            ocr_height,
            input_type
        );
    }

    // =========================================================================
    // FREE OCR GRAYSCALE
    // =========================================================================

    if (
        ocr_grayscale != nullptr
    ) {

        fin::ops::aligned_free(
            ocr_grayscale
        );

        ocr_grayscale = nullptr;
    }

    // =========================================================================
    // FINAL PDF BINARY CONTRACT
    // =========================================================================

    if (
        input_type ==
            FIN_INPUT_PDF_PAGE &&
        result->channels == 1 &&
        result->data != nullptr
    ) {

        image::force_binary_inplace(
            result->data,
            result->data_len
        );

        result->is_binarized = 1;
    }

    return result;
}

} // namespace fin_ocr

// =============================================================================
// C ABI WRAPPER
// =============================================================================

extern "C"
FinProcessedBuffer*
execute_vision_pipeline(
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
) {

    try {

        return
            fin_ocr::VisionPipeline::execute(
                input_bytes,
                input_len,
                input_type,
                target_width,
                target_height,
                target_channels
            );

    } catch (
        const std::exception&
    ) {

        return nullptr;

    } catch (...) {

        return nullptr;
    }
}
