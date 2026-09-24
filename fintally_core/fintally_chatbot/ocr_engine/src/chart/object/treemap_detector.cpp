#include "fin_ocr/chart/object/treemap_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::object::treemap {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr double MIN_OBJECT_DENSITY =
    config::CHART_OBJECT_MIN_DENSITY;

constexpr int TREEMAP_MIN_RECT_SIZE =
    config::CHART_OBJECT_TREEMAP_MIN_RECT_SIZE;

} // namespace

// =============================================================================
// TREEMAP
// =============================================================================
//
// Detection flow:
//
//   1. Extract connected components.
//   2. Keep sufficiently large and dense rectangles.
//   3. Convert components to public ChartRect values.
//   4. For every rectangle, search for the smallest containing rectangle.
//   5. Use that rectangle as the immediate parent.
//   6. Emit TreemapNode objects.
//
// This preserves the current monolithic implementation.
//
// =============================================================================

void detect_treemap_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ::fin_ocr::chart::object::ChartObjectSet& result
)
{
    // =========================================================================
    // COMPONENT EXTRACTION
    // =========================================================================

    const std::vector<
        ::fin_ocr::chart::object::Component
    > components =
        ::fin_ocr::chart::object::find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    // =========================================================================
    // RECTANGLE CANDIDATES
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::ChartRect
    > rectangles;

    rectangles.reserve(
        components.size()
    );

    for (
        const ::fin_ocr::chart::object::Component& component :
        components
    ) {
        if (
            component.width() <
                TREEMAP_MIN_RECT_SIZE ||
            component.height() <
                TREEMAP_MIN_RECT_SIZE
        ) {
            continue;
        }

        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        rectangles.push_back(
            ::fin_ocr::chart::object::to_rect(
                component
            )
        );
    }

    // =========================================================================
    // MINIMUM RECTANGLE COUNT
    // =========================================================================

    if (
        rectangles.size() < 2
    ) {
        return;
    }

    // =========================================================================
    // BUILD TREEMAP NODES
    // =========================================================================

    for (
        std::size_t i = 0;
        i < rectangles.size() &&
            result.treemap_nodes.size() <
                MAX_OBJECTS;
        ++i
    ) {
        const ::fin_ocr::chart::object::ChartRect& rectangle =
            rectangles[i];

        ::fin_ocr::chart::object::TreemapNode node{};

        node.bounds =
            rectangle;

        node.hierarchy_level =
            0;

        node.parent_index =
            -1;

        node.confidence =
            rectangle.confidence;

        // =====================================================================
        // PARENT SEARCH
        // =====================================================================
        //
        // The smallest rectangle containing the center of the current
        // rectangle is treated as its immediate parent.
        //
        // =====================================================================

        double smallest_parent_area =
            std::numeric_limits<double>::infinity();

        int parent_index =
            -1;

        const int center_x =
            (
                rectangle.min_x +
                rectangle.max_x
            ) / 2;

        const int center_y =
            (
                rectangle.min_y +
                rectangle.max_y
            ) / 2;

        for (
            std::size_t j = 0;
            j < rectangles.size();
            ++j
        ) {
            if (
                i == j
            ) {
                continue;
            }

            const ::fin_ocr::chart::object::ChartRect& parent =
                rectangles[j];

            // -----------------------------------------------------------------
            // Center containment.
            // -----------------------------------------------------------------

            if (
                center_x < parent.min_x ||
                center_x > parent.max_x ||
                center_y < parent.min_y ||
                center_y > parent.max_y
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Parent area.
            // -----------------------------------------------------------------

            const double parent_area =
                static_cast<double>(
                    parent.max_x -
                    parent.min_x +
                    1
                ) *
                static_cast<double>(
                    parent.max_y -
                    parent.min_y +
                    1
                );

            // -----------------------------------------------------------------
            // Child area.
            // -----------------------------------------------------------------

            const double child_area =
                static_cast<double>(
                    rectangle.max_x -
                    rectangle.min_x +
                    1
                ) *
                static_cast<double>(
                    rectangle.max_y -
                    rectangle.min_y +
                    1
                );

            // -----------------------------------------------------------------
            // A parent must actually be larger than the child.
            // -----------------------------------------------------------------

            if (
                parent_area <=
                child_area
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Keep smallest valid containing rectangle.
            // -----------------------------------------------------------------

            if (
                parent_area <
                smallest_parent_area
            ) {
                smallest_parent_area =
                    parent_area;

                parent_index =
                    static_cast<int>(
                        j
                    );
            }
        }

        // =====================================================================
        // ASSIGN PARENT
        // =====================================================================

        if (
            parent_index >= 0
        ) {
            node.parent_index =
                parent_index;

            /*
             * The current implementation only distinguishes top-level
             * rectangles from directly contained rectangles.
             */
            node.hierarchy_level =
                1;
        }

        result.treemap_nodes.push_back(
            node
        );
    }
}

} // namespace fin_ocr::chart::object::treemap
