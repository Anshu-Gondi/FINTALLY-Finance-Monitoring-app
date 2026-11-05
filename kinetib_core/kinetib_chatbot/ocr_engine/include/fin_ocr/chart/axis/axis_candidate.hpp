#pragma once

namespace fin_ocr::chart {

// =============================================================================
// AXIS CANDIDATE
// =============================================================================
//
// Represents a candidate line discovered during axis scanning.
//
// This is deliberately separate from ChartAxis.
//
// AxisCandidate:
//     intermediate detection result.
//
// ChartAxis:
//     validated domain-level result.
//
// =============================================================================

struct AxisCandidate {

    int position = -1;

    int start = 0;
    int end = 0;

    double coverage = 0.0;
    double density = 0.0;

    double score = 0.0;

    [[nodiscard]]
    bool valid() const noexcept
    {
        return
            position >= 0 &&
            end >= start;
    }
};

} // namespace fin_ocr::chart
