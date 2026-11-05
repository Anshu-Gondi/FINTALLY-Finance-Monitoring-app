#include "fin_ocr/chart/label/candidate/candidate_segmenter.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::label::candidate {

namespace {

// =============================================================================
// RAW COMPONENT FILTER CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr std::size_t MIN_COMPONENT_PIXELS =
    2u;

constexpr int MIN_GLYPH_HEIGHT =
    4;

constexpr int MAX_GLYPH_HEIGHT =
    24;

constexpr int MAX_GLYPH_WIDTH =
    32;

constexpr double MAX_COMPONENT_DENSITY =
    0.90;

constexpr double MIN_COMPONENT_DENSITY =
    0.025;

// =============================================================================
// CHART-GEOMETRY REJECTION
// =============================================================================

constexpr int LARGE_HORIZONTAL_COMPONENT_WIDTH =
    8;

constexpr int LARGE_HORIZONTAL_COMPONENT_HEIGHT =
    3;

constexpr int LARGE_VERTICAL_COMPONENT_HEIGHT =
    8;

constexpr int LARGE_VERTICAL_COMPONENT_WIDTH =
    2;

constexpr int LARGE_GEOMETRY_WIDTH =
    32;

constexpr int LARGE_GEOMETRY_HEIGHT =
    20;

} // namespace

// =============================================================================
// FOREGROUND COMPONENT SEGMENTATION
// =============================================================================
//
// Produces raw 8-connected foreground components from a horizontal image band.
//
// This function intentionally does not know about:
//
//     - categories
//     - Y-axis labels
//     - OCR
//     - component grouping
//
// It is the low-level segmentation stage only.
//
// =============================================================================

[[nodiscard]]
std::vector<RawComponent> segment_foreground_components(
    const std::uint8_t* chart_buffer,
    int width,
    int y0,
    int y1,
    std::uint8_t minimum_foreground
)
{
    std::vector<RawComponent> components;

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        y0 < 0 ||
        y1 <= y0
    ) {
        return components;
    }

    const int region_height =
        y1 -
        y0;

    if (
        region_height <= 1
    ) {
        return components;
    }

    // =========================================================================
    // LOCAL BINARY FOREGROUND MASK
    // =========================================================================

    const std::size_t mask_size =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            region_height
        );

    if (
        mask_size == 0u
    ) {
        return components;
    }

    std::vector<std::uint8_t> mask(
        mask_size,
        std::uint8_t{0}
    );

    for (
        int local_y = 0;
        local_y < region_height;
        ++local_y
    ) {

        const int image_y =
            y0 +
            local_y;

        const std::size_t row_offset =
            static_cast<std::size_t>(
                local_y
            ) *
            static_cast<std::size_t>(
                width
            );

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            if (
                geometry::chart_foreground(
                    chart_buffer,
                    width,
                    x,
                    image_y
                ) >=
                minimum_foreground
            ) {

                mask[
                    row_offset +
                    static_cast<std::size_t>(
                        x
                    )
                ] =
                    1u;
            }
        }
    }

    // =========================================================================
    // VISITED STATE
    // =========================================================================

    std::vector<std::uint8_t> visited(
        mask_size,
        std::uint8_t{0}
    );

    const auto local_index =
        [width](
            int x,
            int y
        ) noexcept -> std::size_t {

        return
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                width
            ) +
            static_cast<std::size_t>(
                x
            );
    };

    // =========================================================================
    // BFS QUEUE
    // =========================================================================

    std::vector<int> queue;

    queue.reserve(
        256u
    );

    // =========================================================================
    // COMPONENT STORAGE
    // =========================================================================

    const std::size_t width_size =
        static_cast<std::size_t>(
            width
        );

    const std::size_t width_reserve =
        width_size <=
            static_cast<std::size_t>(
                std::numeric_limits<std::size_t>::max() / 4u
            )
            ? width_size * 4u
            : static_cast<std::size_t>(4096u);

    components.reserve(
        std::min(
            width_reserve,
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS * 4
            )
        )
    );

    // =========================================================================
    // 8-CONNECTED COMPONENT EXTRACTION
    // =========================================================================

    for (
        int local_y = 0;
        local_y < region_height;
        ++local_y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t start =
                local_index(
                    x,
                    local_y
                );

            if (
                visited[start] != 0u ||
                mask[start] == 0u
            ) {
                continue;
            }

            visited[start] =
                1u;

            RawComponent component{
                x,
                y0 + local_y,
                x,
                y0 + local_y,
                0u
            };

            queue.clear();

            queue.push_back(
                local_y *
                width +
                x
            );

            std::size_t head =
                0u;

            while (
                head <
                queue.size()
            ) {

                const int encoded =
                    queue[head++];

                const int cx =
                    encoded %
                    width;

                const int cy =
                    encoded /
                    width;

                const int image_y =
                    y0 +
                    cy;

                ++component.pixels;

                component.min_x =
                    std::min(
                        component.min_x,
                        cx
                    );

                component.max_x =
                    std::max(
                        component.max_x,
                        cx
                    );

                component.min_y =
                    std::min(
                        component.min_y,
                        image_y
                    );

                component.max_y =
                    std::max(
                        component.max_y,
                        image_y
                    );

                // =============================================================
                // 8-CONNECTED NEIGHBOURS
                // =============================================================

                for (
                    int dy = -1;
                    dy <= 1;
                    ++dy
                ) {

                    for (
                        int dx = -1;
                        dx <= 1;
                        ++dx
                    ) {

                        if (
                            dx == 0 &&
                            dy == 0
                        ) {
                            continue;
                        }

                        const int nx =
                            cx +
                            dx;

                        const int ny =
                            cy +
                            dy;

                        if (
                            nx < 0 ||
                            nx >= width ||
                            ny < 0 ||
                            ny >= region_height
                        ) {
                            continue;
                        }

                        const std::size_t neighbour =
                            local_index(
                                nx,
                                ny
                            );

                        if (
                            visited[neighbour] != 0u ||
                            mask[neighbour] == 0u
                        ) {
                            continue;
                        }

                        visited[neighbour] =
                            1u;

                        queue.push_back(
                            ny *
                            width +
                            nx
                        );
                    }
                }
            }

            // =========================================================================
            // RAW COMPONENT FILTER
            // =========================================================================

            const int component_width =
                component.width();

            const int component_height =
                component.height();

            // ---------------------------------------------------------------------
            // Minimum occupancy.
            // ---------------------------------------------------------------------

            if (
                component.pixels <
                MIN_COMPONENT_PIXELS
            ) {
                continue;
            }

            // ---------------------------------------------------------------------
            // Glyph dimension bounds.
            // ---------------------------------------------------------------------

            if (
                component_height >
                MAX_GLYPH_HEIGHT
            ) {
                continue;
            }

            if (
                component_width >
                MAX_GLYPH_WIDTH
            ) {
                continue;
            }

            // ---------------------------------------------------------------------
            // Horizontal chart line / tick / bar edge.
            // ---------------------------------------------------------------------

            if (
                component_width >=
                    LARGE_HORIZONTAL_COMPONENT_WIDTH &&
                component_height <=
                    LARGE_HORIZONTAL_COMPONENT_HEIGHT
            ) {
                continue;
            }

            // ---------------------------------------------------------------------
            // Vertical chart line / axis edge.
            // ---------------------------------------------------------------------

            if (
                component_height >=
                    LARGE_VERTICAL_COMPONENT_HEIGHT &&
                component_width <=
                    LARGE_VERTICAL_COMPONENT_WIDTH
            ) {
                continue;
            }

            // ---------------------------------------------------------------------
            // Large connected chart geometry.
            // ---------------------------------------------------------------------

            if (
                component_width >=
                    LARGE_GEOMETRY_WIDTH &&
                component_height >=
                    LARGE_GEOMETRY_HEIGHT
            ) {
                continue;
            }

            // ---------------------------------------------------------------------
            // Density.
            // ---------------------------------------------------------------------

            const std::size_t component_area =
                static_cast<std::size_t>(
                    component_width
                ) *
                static_cast<std::size_t>(
                    component_height
                );

            if (
                component_area == 0u
            ) {
                continue;
            }

            const double density =
                static_cast<double>(
                    component.pixels
                ) /
                static_cast<double>(
                    component_area
                );

            if (
                !std::isfinite(
                    density
                )
            ) {
                continue;
            }

            if (
                density <
                MIN_COMPONENT_DENSITY
            ) {
                continue;
            }

            if (
                density >
                    MAX_COMPONENT_DENSITY &&
                component_width > 24 &&
                component_height > 16
            ) {
                continue;
            }

            // =========================================================================
            // ACCEPT RAW COMPONENT
            // =========================================================================

            components.push_back(
                component
            );

            if (
                components.size() >=
                static_cast<std::size_t>(
                    MAX_CANDIDATE_COMPONENTS
                )
            ) {
                break;
            }
        }

        if (
            components.size() >=
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        ) {
            break;
        }
    }

    return components;
}

} // namespace fin_ocr::chart::label::candidate
