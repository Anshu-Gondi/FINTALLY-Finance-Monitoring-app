#include "fin_ocr/chart/label/y_axis/y_axis_ocr.hpp"

#include "fin_ocr/line/line_recognizer.hpp"

#include "fin_ocr/chart/label/recognition/ocr_buffer.hpp"
#include "fin_ocr/chart/label/text/text_utils.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_parser.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// Y-AXIS OCR RETRY CONFIGURATION
// =============================================================================
//
// Preserved from the legacy recognizer.
//
// The retry path uses a bounded 2x upscale rather than repeatedly increasing
// the image size.
//
// =============================================================================

constexpr int Y_AXIS_OCR_SCALE_FACTOR =
    2;

} // namespace

// =============================================================================
// MULTI-PASS Y-AXIS OCR
// =============================================================================
//
// Recognition order is intentionally preserved:
//
//     1. adaptive/current representation
//     2. adaptive scaled
//     3. adaptive inverted
//     4. luminance scaled
//     5. luminance
//     6. luminance inverted
//     7. luminance thresholded + scaled
//     8. legacy candidate buffer
//
// Every successful OCR result passes through Y-axis normalization before it is
// accepted.
//
// =============================================================================

[[nodiscard]]
bool recognize_y_axis_text(
    const ::fin_ocr::LineRecognizer& line_recognizer,
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate,
    YAxisKind axis,
    std::string& raw_text,
    std::string& normalized_text
)
{
    raw_text.clear();
    normalized_text.clear();

    // =========================================================================
    // PRIMARY ADAPTIVE REPRESENTATION
    // =========================================================================

    const OcrBuffer adaptive =
        ::fin_ocr::chart::label::recognition::
            build_y_axis_candidate_buffer(
                chart_buffer,
                image_width,
                image_height,
                candidate
            );

    // =========================================================================
    // LUMINANCE REPRESENTATION
    // =========================================================================

    const OcrBuffer luminance =
        ::fin_ocr::chart::label::recognition::
            build_y_axis_luminance_buffer(
                chart_buffer,
                image_width,
                image_height,
                candidate
            );

    // =========================================================================
    // SINGLE BUFFER OCR ATTEMPT
    // =========================================================================

    const auto try_buffer =
        [&](
            const OcrBuffer& buffer
        ) -> bool {

        if (
            buffer.data.empty() ||
            buffer.width <= 0 ||
            buffer.height <= 1
        ) {
            return false;
        }

        std::string raw =
            line_recognizer.recognize(
                buffer.data.data(),
                buffer.width,
                0,
                buffer.height,
                1
            );

        ::fin_ocr::chart::label::text::
            trim_text(
                raw
            );

        if (
            raw.empty()
        ) {
            return false;
        }

        std::string normalized =
            raw;

        if (
            !normalize_y_axis_text(
                normalized,
                axis
            )
        ) {
            return false;
        }

        raw_text =
            std::move(
                raw
            );

        normalized_text =
            std::move(
                normalized
            );

        return true;
    };

    // =========================================================================
    // PASS 1: ADAPTIVE
    // =========================================================================

    if (
        try_buffer(
            adaptive
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 2: ADAPTIVE + SCALE
    // =========================================================================

    const OcrBuffer adaptive_scaled =
        ::fin_ocr::chart::label::recognition::
            scale_ocr_buffer(
                adaptive,
                Y_AXIS_OCR_SCALE_FACTOR
            );

    if (
        try_buffer(
            adaptive_scaled
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 3: ADAPTIVE + INVERT
    // =========================================================================

    const OcrBuffer adaptive_inverted =
        ::fin_ocr::chart::label::recognition::
            invert_ocr_buffer(
                adaptive
            );

    if (
        try_buffer(
            adaptive_inverted
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 4: LUMINANCE + SCALE
    // =========================================================================

    const OcrBuffer luminance_scaled =
        ::fin_ocr::chart::label::recognition::
            scale_ocr_buffer(
                luminance,
                Y_AXIS_OCR_SCALE_FACTOR
            );

    if (
        try_buffer(
            luminance_scaled
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 5: LUMINANCE
    // =========================================================================

    if (
        try_buffer(
            luminance
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 6: LUMINANCE + INVERT
    // =========================================================================

    const OcrBuffer luminance_inverted =
        ::fin_ocr::chart::label::recognition::
            invert_ocr_buffer(
                luminance
            );

    if (
        try_buffer(
            luminance_inverted
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 7: LUMINANCE + THRESHOLD + SCALE
    // =========================================================================

    const OcrBuffer luminance_thresholded =
        ::fin_ocr::chart::label::recognition::
            threshold_ocr_buffer(
                luminance,
                220u,
                true
            );

    const OcrBuffer luminance_thresholded_scaled =
        ::fin_ocr::chart::label::recognition::
            scale_ocr_buffer(
                luminance_thresholded,
                Y_AXIS_OCR_SCALE_FACTOR
            );

    if (
        try_buffer(
            luminance_thresholded_scaled
        )
    ) {
        return true;
    }

    // =========================================================================
    // PASS 8: LEGACY / GENERIC CANDIDATE BUFFER
    // =========================================================================

    const OcrBuffer legacy =
        ::fin_ocr::chart::label::recognition::
            build_candidate_buffer(
                chart_buffer,
                image_width,
                image_height,
                candidate
            );

    if (
        try_buffer(
            legacy
        )
    ) {
        return true;
    }

    return false;
}

} // namespace fin_ocr::chart::label::y_axis
