#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chart_associator.hpp"

enum class CandidateRejectReason {
    NONE,
    GEOMETRY,
    ZONE,
    ZONE_SCORE,
    SEQUENCE,
    EMPTY_CROP,
    OCR_EMPTY,
    REPEATED_NOISE,
    TEXT_QUALITY,
    GEOMETRY_TEXT,
    DENSITY,
    CONFIDENCE,
    COMPOSITION,
    SINGLE_GLYPH,
    TWO_GLYPH,
    THREE_GLYPH,
    PUNCTUATION,
    OTHER_CHARS,
    NUMERIC_ONLY,
    LONG_GEOMETRY,
    OCCUPANCY
};

namespace fin_ocr {

class LineRecognizer;

class ChartLabelRecognizer {
public:

    explicit ChartLabelRecognizer(
        const LineRecognizer& line_recognizer
    ) noexcept;

    // Structured label output consumed by ChartAssociator.
    [[nodiscard]]
    std::vector<chart::ChartLabel> recognize_labels(
        const std::uint8_t* chart_buffer,
        int width,
        int height
    ) const;

    // Existing textual diagnostic/compatibility API.
    [[nodiscard]]
    std::string recognize(
        const std::uint8_t* chart_buffer,
        int width,
        int height
    ) const;

private:

    const LineRecognizer& line_recognizer_;
};

} // namespace fin_ocr
