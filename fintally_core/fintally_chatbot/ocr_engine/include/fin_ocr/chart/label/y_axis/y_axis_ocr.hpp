#pragma once

#include <cstdint>
#include <string>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"

namespace fin_ocr {

class LineRecognizer;

namespace chart::label::y_axis {

// =============================================================================
// MULTI-PASS Y-AXIS OCR
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
);

} // namespace chart::label::y_axis

} // namespace fin_ocr
