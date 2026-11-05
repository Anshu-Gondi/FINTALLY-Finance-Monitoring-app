#include "fin_ocr/chart/label/y_axis/y_axis_fragment_merger.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// FRAGMENT CONSOLIDATION LIMITS
// =============================================================================
//
// These values are preserved from the legacy implementation.
//
// Fragment consolidation is intentionally bounded to a single horizontal
// Y-axis tick row.
//
// =============================================================================

constexpr double Y_AXIS_FRAGMENT_MAX_CENTER_Y_DELTA =
    10.0;

constexpr int Y_AXIS_FRAGMENT_MAX_HORIZONTAL_GAP =
    18;

constexpr double Y_AXIS_FRAGMENT_MIN_VERTICAL_OVERLAP_RATIO =
    0.25;

constexpr int MAX_Y_AXIS_LABEL_WIDTH =
    96;

constexpr int MAX_Y_AXIS_LABEL_HEIGHT =
    28;

} // namespace

// =============================================================================
// Y-AXIS FRAGMENT CONSOLIDATION
// =============================================================================
//
// The component extractor may split one numeric Y-axis label into several
// components:
//
//     "$5.00k" -> "$5" + ".00k"
//     "16.5%"  -> "16" + ".5%"
//
// Consolidation is geometry-only.
//
// No text is fabricated here.
//
// Foreground occupancy is recalculated after merging so overlapping fragments
// do not double-count active pixels.
//
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> consolidate_y_axis_fragments(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& fragments,
    std::uint8_t minimum_foreground
)
{
    std::vector<TextCandidate> consolidated;

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        fragments.empty()
    ) {
        return consolidated;
    }

    // =========================================================================
    // ORDER FRAGMENTS BY VERTICAL POSITION
    // =========================================================================

    std::vector<TextCandidate> ordered =
        fragments;

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            const double a_center_y =
                (
                    static_cast<double>(
                        a.min_y
                    ) +
                    static_cast<double>(
                        a.max_y
                    )
                ) *
                0.5;

            const double b_center_y =
                (
                    static_cast<double>(
                        b.min_y
                    ) +
                    static_cast<double>(
                        b.max_y
                    )
                ) *
                0.5;

            if (
                a_center_y !=
                b_center_y
            ) {
                return
                    a_center_y <
                    b_center_y;
            }

            if (
                a.min_x !=
                b.min_x
            ) {
                return
                    a.min_x <
                    b.min_x;
            }

            return
                a.max_x <
                b.max_x;
        }
    );

    // =========================================================================
    // MERGE GEOMETRICALLY COMPATIBLE FRAGMENTS
    // =========================================================================

    for (
        const TextCandidate& fragment :
        ordered
    ) {

        bool merged =
            false;

        const double fragment_center_y =
            (
                static_cast<double>(
                    fragment.min_y
                ) +
                static_cast<double>(
                    fragment.max_y
                )
            ) *
            0.5;

        for (
            TextCandidate& group :
            consolidated
        ) {

            const double group_center_y =
                (
                    static_cast<double>(
                        group.min_y
                    ) +
                    static_cast<double>(
                        group.max_y
                    )
                ) *
                0.5;

            // -----------------------------------------------------------------
            // Same horizontal tick row.
            // -----------------------------------------------------------------

            if (
                std::abs(
                    fragment_center_y -
                    group_center_y
                ) >
                Y_AXIS_FRAGMENT_MAX_CENTER_Y_DELTA
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Vertical overlap.
            // -----------------------------------------------------------------

            const int overlap_min_y =
                std::max(
                    fragment.min_y,
                    group.min_y
                );

            const int overlap_max_y =
                std::min(
                    fragment.max_y,
                    group.max_y
                );

            const int fragment_height =
                fragment.max_y -
                fragment.min_y +
                1;

            const int group_height =
                group.max_y -
                group.min_y +
                1;

            const int overlap_height =
                overlap_max_y >=
                    overlap_min_y
                    ? overlap_max_y -
                      overlap_min_y +
                      1
                    : 0;

            const double min_height =
                static_cast<double>(
                    std::min(
                        fragment_height,
                        group_height
                    )
                );

            const double overlap_ratio =
                min_height > 0.0
                    ? static_cast<double>(
                          overlap_height
                      ) /
                      min_height
                    : 0.0;

            // -----------------------------------------------------------------
            // Horizontal separation.
            // -----------------------------------------------------------------

            const int horizontal_gap =
                fragment.min_x >
                    group.max_x
                    ? fragment.min_x -
                      group.max_x -
                      1
                    : group.min_x >
                          fragment.max_x
                        ? group.min_x -
                          fragment.max_x -
                          1
                        : 0;

            // -----------------------------------------------------------------
            // Reject unrelated fragments.
            // -----------------------------------------------------------------

            if (
                overlap_ratio <
                    Y_AXIS_FRAGMENT_MIN_VERTICAL_OVERLAP_RATIO &&
                horizontal_gap >
                    Y_AXIS_FRAGMENT_MAX_HORIZONTAL_GAP
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Compute merged bounds.
            // -----------------------------------------------------------------

            const int merged_min_x =
                std::min(
                    group.min_x,
                    fragment.min_x
                );

            const int merged_max_x =
                std::max(
                    group.max_x,
                    fragment.max_x
                );

            const int merged_min_y =
                std::min(
                    group.min_y,
                    fragment.min_y
                );

            const int merged_max_y =
                std::max(
                    group.max_y,
                    fragment.max_y
                );

            const int merged_width =
                merged_max_x -
                merged_min_x +
                1;

            const int merged_height =
                merged_max_y -
                merged_min_y +
                1;

            // -----------------------------------------------------------------
            // Prevent oversized merged labels.
            // -----------------------------------------------------------------

            if (
                merged_width >
                    MAX_Y_AXIS_LABEL_WIDTH ||
                merged_height >
                    MAX_Y_AXIS_LABEL_HEIGHT
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Commit merge.
            // -----------------------------------------------------------------

            group.min_x =
                merged_min_x;

            group.min_y =
                merged_min_y;

            group.max_x =
                merged_max_x;

            group.max_y =
                merged_max_y;

            merged =
                true;

            break;
        }

        if (
            !merged
        ) {
            consolidated.push_back(
                fragment
            );
        }
    }

    // =========================================================================
    // RECALCULATE FOREGROUND OCCUPANCY
    // =========================================================================
    //
    // Re-measure directly from the source image after consolidation.
    //
    // This avoids double-counting when fragments overlap.
    // =========================================================================

    for (
        TextCandidate& candidate :
        consolidated
    ) {

        std::size_t active_pixels =
            0u;

        for (
            int y = candidate.min_y;
            y <= candidate.max_y;
            ++y
        ) {

            for (
                int x = candidate.min_x;
                x <= candidate.max_x;
                ++x
            ) {

                if (
                    geometry::chart_foreground(
                        chart_buffer,
                        image_width,
                        x,
                        y
                    ) >=
                    minimum_foreground
                ) {
                    ++active_pixels;
                }
            }
        }

        candidate.active_pixels =
            active_pixels;
    }

    // =========================================================================
    // FINAL DETERMINISTIC ORDER
    // =========================================================================

    std::sort(
        consolidated.begin(),
        consolidated.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            if (
                a.min_y !=
                b.min_y
            ) {
                return
                    a.min_y <
                    b.min_y;
            }

            return
                a.min_x <
                b.min_x;
        }
    );

    return consolidated;
}

} // namespace fin_ocr::chart::label::y_axis
