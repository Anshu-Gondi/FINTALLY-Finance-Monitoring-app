#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr {

class LineRecognizer;

namespace chart::label {

// =============================================================================
// LABEL ENGINE
// =============================================================================
//
// High-level recognition orchestration.
//
// This layer coordinates candidate generation, category processing, Y-axis
// processing, recovery, filtering, merging and final structured output.
//
// It does not own the underlying OCR engine.
//
// =============================================================================

class LabelEngine {
public:

    explicit LabelEngine(
        const ::fin_ocr::LineRecognizer& line_recognizer
    ) noexcept;

    [[nodiscard]]
    std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >
    recognize_labels(
        const std::uint8_t* chart_buffer,
        int width,
        int height
    ) const;

private:

    const ::fin_ocr::LineRecognizer& line_recognizer_;
};

} // namespace chart::label

} // namespace fin_ocr
