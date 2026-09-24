#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object {

// =============================================================================
// CONNECTED COMPONENT
// =============================================================================
//
// Shared geometric representation used by multiple chart-object detectors.
//
// Component extraction is implemented in object_components.cpp.
// The structure itself is exposed because bar, area, scatter, waterfall and
// treemap detectors all consume the same component representation.
//
// =============================================================================

struct Component {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    std::size_t pixels = 0;

    double density = 0.0;

    // =========================================================================
    // WIDTH
    // =========================================================================

    [[nodiscard]]
    int width() const noexcept
    {
        return
            max_x -
            min_x +
            1;
    }

    // =========================================================================
    // HEIGHT
    // =========================================================================

    [[nodiscard]]
    int height() const noexcept
    {
        return
            max_y -
            min_y +
            1;
    }

    // =========================================================================
    // ASPECT
    // =========================================================================

    [[nodiscard]]
    double aspect() const noexcept
    {
        const double h =
            static_cast<double>(
                std::max(
                    height(),
                    1
                )
            );

        return
            static_cast<double>(
                width()
            ) /
            h;
    }
};

// =============================================================================
// CONNECTED COMPONENT EXTRACTION
// =============================================================================
//
// Extracts 8-connected geometry components from the chart image.
//
// =============================================================================

[[nodiscard]]
std::vector<Component> find_components(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
);

// =============================================================================
// COMPONENT -> PUBLIC RECTANGLE
// =============================================================================

[[nodiscard]]
ChartRect to_rect(
    const Component& component
) noexcept;

} // namespace fin_ocr::chart::object
