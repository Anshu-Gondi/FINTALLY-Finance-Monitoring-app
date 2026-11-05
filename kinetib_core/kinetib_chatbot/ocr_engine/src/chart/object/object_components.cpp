#include "fin_ocr/chart/object/object_components.hpp"

#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace fin_ocr::chart::object {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_COMPONENTS =
    config::CHART_OBJECT_MAX_COMPONENTS;

constexpr int MIN_RECT_WIDTH =
    config::CHART_OBJECT_MIN_RECT_WIDTH;

constexpr int MIN_RECT_HEIGHT =
    config::CHART_OBJECT_MIN_RECT_HEIGHT;

constexpr double MIN_OBJECT_DENSITY =
    config::CHART_OBJECT_MIN_DENSITY;

// =============================================================================
// NEIGHBOR OFFSETS
// =============================================================================
//
// 8-connected traversal:
//
//     0 1 2
//     3   4
//     5 6 7
//
// Diagonal connectivity is intentional because rasterized chart geometry can
// contain diagonal contacts after antialiasing/compression.
// =============================================================================

constexpr int DX[8] = {
    -1, 0, 1,
    -1,    1,
    -1, 0, 1
};

constexpr int DY[8] = {
    -1, -1, -1,
     0,     0,
     1,  1,  1
};

} // namespace

// =============================================================================
// CONNECTED COMPONENT EXTRACTION
// =============================================================================
//
// Components are extracted from the broader object-geometry signal rather than
// the OCR foreground signal.
//
// This keeps component extraction reusable across bar, area, scatter,
// waterfall, funnel and treemap detectors.
//
// =============================================================================

std::vector<Component> find_components(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
)
{
    std::vector<Component> components;

    // =========================================================================
    // IMAGE VALIDATION
    // =========================================================================

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        )
    ) {
        return components;
    }

    // =========================================================================
    // PIXEL COUNT
    // =========================================================================

    const std::size_t pixel_count =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    if (
        pixel_count == 0
    ) {
        return components;
    }

    // =========================================================================
    // VISITED MAP
    // =========================================================================

    std::vector<std::uint8_t> visited(
        pixel_count,
        0
    );

    // =========================================================================
    // RESULT CAPACITY
    // =========================================================================

    components.reserve(
        std::min<std::size_t>(
            MAX_COMPONENTS,
            256
        )
    );

    // =========================================================================
    // BFS QUEUE
    // =========================================================================

    std::queue<std::pair<int, int>> queue;

    // =========================================================================
    // IMAGE SCAN
    // =========================================================================

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        for (
            int x = 0;
        x < width;
        ++x
        ) {

            const std::size_t root =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            if (
                visited[root] != 0 ||
                !is_object_pixel(
                    image,
                    x,
                    y,
                    width,
                    channels
                )
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Start new component.
            // -----------------------------------------------------------------

            visited[root] = 1;

            queue.push({
                x,
                y
            });

            Component component{};

            component.min_x = x;
            component.max_x = x;

            component.min_y = y;
            component.max_y = y;

            // -----------------------------------------------------------------
            // Breadth-first traversal.
            // -----------------------------------------------------------------

            while (
                !queue.empty()
            ) {

                const auto [cx, cy] =
                    queue.front();

                queue.pop();

                ++component.pixels;

                // -------------------------------------------------------------
                // Bounding box.
                // -------------------------------------------------------------

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
                        cy
                    );

                component.max_y =
                    std::max(
                        component.max_y,
                        cy
                    );

                // -------------------------------------------------------------
                // Explore 8 neighbors.
                // -------------------------------------------------------------

                for (
                    int direction = 0;
                    direction < 8;
                    ++direction
                ) {

                    const int nx =
                        cx +
                        DX[direction];

                    const int ny =
                        cy +
                        DY[direction];

                    if (
                        nx < 0 ||
                        ny < 0 ||
                        nx >= width ||
                        ny >= height
                    ) {
                        continue;
                    }

                    const std::size_t index =
                        static_cast<std::size_t>(ny) *
                        static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(nx);

                    if (
                        visited[index] != 0
                    ) {
                        continue;
                    }

                    if (
                        !is_object_pixel(
                            image,
                            nx,
                            ny,
                            width,
                            channels
                        )
                    ) {
                        continue;
                    }

                    visited[index] = 1;

                    queue.push({
                        nx,
                        ny
                    });
                }
            }

            // =========================================================================
            // COMPONENT METRICS
            // =========================================================================

            const int component_width =
                component.width();

            const int component_height =
                component.height();

            const std::size_t component_area =
                static_cast<std::size_t>(
                    component_width
                ) *
                static_cast<std::size_t>(
                    component_height
                );

            if (
                component_area > 0
            ) {
                component.density =
                    static_cast<double>(
                        component.pixels
                    ) /
                    static_cast<double>(
                        component_area
                    );
            }

            // =========================================================================
            // COMPONENT LIMIT
            // =========================================================================

            if (
                components.size() >=
                MAX_COMPONENTS
            ) {
                return components;
            }

            // =========================================================================
            // COMPONENT FILTER
            // =========================================================================

            if (
                component_width >=
                    MIN_RECT_WIDTH &&
                component_height >=
                    MIN_RECT_HEIGHT &&
                component.density >=
                    MIN_OBJECT_DENSITY
            ) {
                components.push_back(
                    component
                );
            }
        }
    }

    return components;
}

// =============================================================================
// COMPONENT -> PUBLIC RECTANGLE
// =============================================================================

ChartRect to_rect(
    const Component& component
) noexcept
{
    ChartRect rect{};

    rect.min_x =
        component.min_x;

    rect.min_y =
        component.min_y;

    rect.max_x =
        component.max_x;

    rect.max_y =
        component.max_y;

    rect.confidence =
        static_cast<float>(
            std::clamp(
                component.density,
                0.0,
                1.0
            )
        );

    return rect;
}

} // namespace fin_ocr::chart::object
