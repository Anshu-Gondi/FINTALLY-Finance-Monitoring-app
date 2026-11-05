#include "fin_ocr/chart/label/label_geometry.hpp"

#include <cstddef>
#include <cstdint>

namespace fin_ocr::chart::label::geometry {

// =============================================================================
// CANDIDATE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_candidate(
    const TextCandidate& candidate
) noexcept
{
    return
        candidate.min_x >= 0 &&
        candidate.min_y >= 0 &&
        candidate.max_x >= candidate.min_x &&
        candidate.max_y >= candidate.min_y &&
        candidate.active_pixels > 0;
}

// =============================================================================
// VERTICAL OVERLAP
// =============================================================================

[[nodiscard]]
bool vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept
{
    return
        a_min_y <= b_max_y &&
        b_min_y <= a_max_y;
}

// =============================================================================
// HORIZONTAL OVERLAP
// =============================================================================

[[nodiscard]]
bool horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept
{
    return
        a_min_x <= b_max_x &&
        b_min_x <= a_max_x;
}

// =============================================================================
// CHANNEL-3 FOREGROUND ACCESSOR
// =============================================================================
//
// The chart preprocessing contract uses:
//
//     channel 0 = supporting chart information
//     channel 1 = OCR foreground
//     channel 2 = supporting chart information
//
// This function intentionally reads channel 1.
//
// =============================================================================

[[nodiscard]]
std::uint8_t chart_foreground(
    const std::uint8_t* chart_buffer,
    int width,
    int x,
    int y
) noexcept
{
    const std::size_t index =
        (
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)
        ) *
        3u +
        1u;

    return chart_buffer[index];
}

} // namespace fin_ocr::chart::label::geometry
