#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/category/category_sequence.hpp"

namespace fin_ocr {

class LineRecognizer;

namespace chart::label::recognition {

// =============================================================================
// CANDIDATE OCR
// =============================================================================
//
// Runs OCR and all generic candidate-level admission gates.
//
// =============================================================================

[[nodiscard]]
bool recognize_candidate(
    const ::fin_ocr::LineRecognizer& line_recognizer,
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate,
    int max_band_height,
    const std::vector<TextCandidate>& category_candidates,
    const category::CategorySequenceStats& sequence_stats,
    DetectedLabel& output,
    CandidateRejectReason& reject_reason
);

} // namespace chart::label::recognition

} // namespace fin_ocr
