#pragma once

#include <string>
#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"

namespace fin_ocr::chart::label::y_axis {

[[nodiscard]]
bool recover_right_y_axis_decimal(
    std::string& text,
    const std::vector<DetectedLabel>& accepted_labels,
    int candidate_min_y,
    int candidate_max_y,
    double& original_value,
    double& recovered_value
) noexcept;

[[nodiscard]]
std::vector<bool> y_axis_sequence_inlier_mask(
    const std::vector<DetectedLabel>& labels,
    YAxisKind axis
) noexcept;

[[nodiscard]]
YAxisValueFit estimate_y_axis_value_fit(
    const std::vector<TextCandidate>& axis_candidates,
    const std::vector<DetectedLabel>& axis_results,
    YAxisKind axis
) noexcept;

[[nodiscard]]
std::string format_recovered_y_axis_value(
    double value,
    YAxisKind axis,
    const std::vector<DetectedLabel>& existing_results
);

void reconcile_y_axis_values(
    const std::vector<TextCandidate>& axis_candidates,
    YAxisKind axis,
    std::vector<DetectedLabel>& axis_results
);

} // namespace fin_ocr::chart::label::y_axis
