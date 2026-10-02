#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::chart::label::y_axis {

// =============================================================================
// AXIS SIDE
// =============================================================================

enum class YAxisKind : std::uint8_t {

    NONE = 0,

    LEFT,

    RIGHT
};

// =============================================================================
// SEQUENCE STATISTICS
// =============================================================================

struct YAxisSequenceStats {

    double median_center_x = 0.0;

    double median_height = 0.0;

    double median_center_spacing = 0.0;

    std::size_t candidate_count = 0;

    std::size_t coherent_count = 0;
};

// =============================================================================
// VALUE FIT
// =============================================================================

struct YAxisValueFit {

    bool valid = false;

    double slope = 0.0;

    double intercept = 0.0;

    double tick_spacing = 0.0;

    double tick_delta = 0.0;

    std::size_t anchors = 0;
};

} // namespace fin_ocr::chart::label::y_axis
