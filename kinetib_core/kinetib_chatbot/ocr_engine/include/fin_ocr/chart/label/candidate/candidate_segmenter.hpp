#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::label::candidate {

// =============================================================================
// RAW COMPONENT
// =============================================================================
//
// Internal connected-component representation.
//
// =============================================================================

struct RawComponent {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    std::size_t pixels = 0;

    [[nodiscard]]
    int width() const noexcept
    {
        return
            max_x -
            min_x +
            1;
    }

    [[nodiscard]]
    int height() const noexcept
    {
        return
            max_y -
            min_y +
            1;
    }
};

// =============================================================================
// FOREGROUND COMPONENT SEGMENTATION
// =============================================================================

[[nodiscard]]
std::vector<RawComponent> segment_foreground_components(
    const std::uint8_t* chart_buffer,
    int width,
    int y0,
    int y1,
    std::uint8_t minimum_foreground
);

} // namespace fin_ocr::chart::label::candidate
