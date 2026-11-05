#pragma once

#include <cstdint>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::recognition {

// =============================================================================
// GENERIC CANDIDATE BUFFER
// =============================================================================

[[nodiscard]]
OcrBuffer build_candidate_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
);

// =============================================================================
// Y-AXIS CANDIDATE BUFFER
// =============================================================================

[[nodiscard]]
OcrBuffer build_y_axis_candidate_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
);

// =============================================================================
// BUFFER TRANSFORMS
// =============================================================================

[[nodiscard]]
OcrBuffer invert_ocr_buffer(
    const OcrBuffer& source
);

[[nodiscard]]
OcrBuffer scale_ocr_buffer(
    const OcrBuffer& source,
    int scale
);

[[nodiscard]]
OcrBuffer build_y_axis_luminance_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
);

[[nodiscard]]
OcrBuffer threshold_ocr_buffer(
    const OcrBuffer& source,
    std::uint8_t threshold,
    bool dark_foreground
);

} // namespace fin_ocr::chart::label::recognition
