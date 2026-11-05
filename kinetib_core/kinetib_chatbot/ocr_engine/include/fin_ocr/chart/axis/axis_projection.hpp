#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::chart::axis {

// =============================================================================
// SPAN STATISTICS
// =============================================================================
//
// Represents the foreground extent of a one-dimensional image scan.
//
// `first` and `last` are pixel coordinates along the scanned dimension.
// `active` is the number of foreground pixels encountered.
//
// This is intentionally a small value type:
//   - no heap allocation
//   - trivially cheap to copy
//   - independent of axis detection policy
//
// =============================================================================

struct SpanStats {

    int first = -1;
    int last = -1;

    std::size_t active = 0;

    // -------------------------------------------------------------------------
    // EXTENT
    // -------------------------------------------------------------------------

    [[nodiscard]]
    int extent() const noexcept;

    // -------------------------------------------------------------------------
    // COVERAGE
    // -------------------------------------------------------------------------

    [[nodiscard]]
    double coverage(
        int total
    ) const noexcept;

    // -------------------------------------------------------------------------
    // DENSITY
    // -------------------------------------------------------------------------

    [[nodiscard]]
    double density() const noexcept;
};

// =============================================================================
// IMAGE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_image(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) noexcept;

// =============================================================================
// FOREGROUND ACCESS
// =============================================================================
//
// Reads the foreground channel produced by the chart preprocessing stage.
//
// For multi-channel chart representations:
//
//     channel 0 -> saturation
//     channel 1 -> OCR foreground strength
//     channel 2 -> chroma
//
// For one-channel masks:
//
//     channel 0 -> signal
//
// This function deliberately does not perform any image transformation.
// It is the lowest-level predicate consumed by axis projection/scanning.
//
// =============================================================================

[[nodiscard]]
bool is_foreground(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept;

// =============================================================================
// HORIZONTAL SPAN
// =============================================================================
//
// Scans one image row and returns the foreground extent.
//
// Complexity:
//     O(width)
//
// No allocation.
//
// =============================================================================

[[nodiscard]]
SpanStats horizontal_span(
    const std::uint8_t* image,
    int width,
    int y,
    int channels
) noexcept;

// =============================================================================
// VERTICAL SPAN
// =============================================================================
//
// Scans one image column and returns the foreground extent.
//
// Complexity:
//     O(height)
//
// No allocation.
//
// =============================================================================

[[nodiscard]]
SpanStats vertical_span(
    const std::uint8_t* image,
    int width,
    int height,
    int x,
    int channels
) noexcept;

// =============================================================================
// AXIS SCORE
// =============================================================================
//
// Combines foreground coverage and density into a normalized heuristic score.
//
// This is intentionally kept separate from axis-position priors.
// Candidate scanning owns those policy decisions.
//
// =============================================================================

[[nodiscard]]
double axis_score(
    const SpanStats& stats,
    int total_length
) noexcept;

} // namespace fin_ocr::chart::axis
