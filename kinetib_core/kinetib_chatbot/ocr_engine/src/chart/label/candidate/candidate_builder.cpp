#include "fin_ocr/chart/label/candidate/candidate_builder.hpp"

#include "fin_ocr/chart/label/candidate/candidate_segmenter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::label::candidate {

namespace {

// =============================================================================
// CANDIDATE GROUPING CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr int MIN_GLYPH_HEIGHT =
    4;

constexpr int MAX_GLYPH_HEIGHT =
    24;

constexpr int MAX_GLYPH_WIDTH =
    32;

constexpr double MAX_GLYPH_HEIGHT_RATIO =
    2.0;

constexpr int MAX_GLYPH_HORIZONTAL_GAP =
    12;

constexpr int MAX_BASELINE_DELTA =
    5;

constexpr int MAX_CENTER_Y_DELTA =
    6;

// =============================================================================
// FINAL CANDIDATE LIMITS
// =============================================================================

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr int MAX_CANDIDATE_WIDTH =
    320;

constexpr int MAX_CANDIDATE_HEIGHT =
    32;

constexpr std::size_t MAX_CANDIDATE_COMPONENTS =
    4096u;

// =============================================================================
// FINAL CANDIDATE SHAPE LIMITS
// =============================================================================

constexpr double MAX_CANDIDATE_WIDTH_RATIO =
    0.35;

constexpr double MAX_CANDIDATE_HEIGHT_RATIO =
    0.20;

constexpr double MIN_LABEL_DENSITY =
    0.010;

constexpr double MAX_WIDE_DENSE_RATIO =
    0.25;

constexpr double MAX_WIDE_DENSE_DENSITY =
    0.08;

// =============================================================================
// GROUP SEARCH
// =============================================================================

constexpr std::size_t GROUP_SEARCH_LIMIT =
    32u;

} // namespace

// =============================================================================
// COMPLETE BAND CANDIDATE BUILD
// =============================================================================
//
// Pipeline:
//
//     foreground band
//         ↓
//     segment_foreground_components()
//         ↓
//     RawComponent[]
//         ↓
//     deterministic ordering
//         ↓
//     compatible-component grouping
//         ↓
//     candidate geometry/density gates
//         ↓
//     TextCandidate[]
//
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> build_band_candidates(
    const std::uint8_t* chart_buffer,
    int width,
    int y0,
    int y1,
    std::uint8_t minimum_foreground,
    int minimum_component_width
)
{
    std::vector<TextCandidate> candidates;

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        y0 < 0 ||
        y1 <= y0 ||
        minimum_component_width <= 0
    ) {
        return candidates;
    }

    const int region_height =
        y1 -
        y0;

    if (
        region_height <= 1
    ) {
        return candidates;
    }

    // =========================================================================
    // LOW-LEVEL SEGMENTATION
    // =========================================================================

    std::vector<RawComponent> components =
        segment_foreground_components(
            chart_buffer,
            width,
            y0,
            y1,
            minimum_foreground
        );

    if (
        components.empty()
    ) {
        return candidates;
    }

    // =========================================================================
    // SORT RAW COMPONENTS
    // =========================================================================

    std::sort(
        components.begin(),
        components.end(),
        [](
            const RawComponent& a,
            const RawComponent& b
        ) noexcept {

            if (
                a.min_y !=
                b.min_y
            ) {
                return
                    a.min_y <
                    b.min_y;
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
    // COMPONENT GROUP
    // =========================================================================

    struct ComponentGroup {

        int min_x = 0;
        int min_y = 0;

        int max_x = 0;
        int max_y = 0;

        std::size_t pixels = 0u;
        std::size_t count = 0u;

        int reference_height = 0;
        int reference_baseline = 0;

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

    std::vector<ComponentGroup> groups;

    groups.reserve(
        components.size()
    );

    // =========================================================================
    // GROUP COMPATIBLE COMPONENTS
    // =========================================================================

    for (
        const RawComponent& component :
        components
    ) {

        const int component_width =
            component.width();

        const int component_height =
            component.height();

        if (
            component_height <
                MIN_GLYPH_HEIGHT ||
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

        bool attached =
            false;

        const std::size_t search_begin =
            groups.size() >
                GROUP_SEARCH_LIMIT
                ? groups.size() -
                  GROUP_SEARCH_LIMIT
                : 0u;

        // Search recent groups first. This keeps grouping local and bounded.
        for (
            std::size_t i =
                groups.size();
            i-- > search_begin;
        ) {

            ComponentGroup& group =
                groups[i];

            // =================================================================
            // HEIGHT COMPATIBILITY
            // =================================================================

            const int reference_height =
                std::max(
                    1,
                    group.reference_height
                );

            const double height_ratio =
                component_height >
                    reference_height
                    ? static_cast<double>(
                          component_height
                      ) /
                      static_cast<double>(
                          reference_height
                      )
                    : static_cast<double>(
                          reference_height
                      ) /
                      static_cast<double>(
                          component_height
                      );

            if (
                height_ratio >
                MAX_GLYPH_HEIGHT_RATIO
            ) {
                continue;
            }

            // =================================================================
            // BASELINE COMPATIBILITY
            // =================================================================

            const int baseline_delta =
                std::abs(
                    component.max_y -
                    group.reference_baseline
                );

            if (
                baseline_delta >
                MAX_BASELINE_DELTA
            ) {
                continue;
            }

            // =================================================================
            // CENTER-Y COMPATIBILITY
            // =================================================================

            const int component_center_y =
                component.min_y +
                component_height /
                    2;

            const int group_center_y =
                group.min_y +
                group.height() /
                    2;

            if (
                std::abs(
                    component_center_y -
                    group_center_y
                ) >
                MAX_CENTER_Y_DELTA
            ) {
                continue;
            }

            // =================================================================
            // HORIZONTAL GAP
            // =================================================================

            const int horizontal_gap =
                component.min_x >
                    group.max_x
                    ? component.min_x -
                      group.max_x -
                      1
                    : group.min_x >
                          component.max_x
                        ? group.min_x -
                          component.max_x -
                          1
                        : 0;

            if (
                horizontal_gap >
                MAX_GLYPH_HORIZONTAL_GAP
            ) {
                continue;
            }

            // =================================================================
            // MERGED WIDTH
            // =================================================================

            const int merged_width =
                std::max(
                    group.max_x,
                    component.max_x
                ) -
                std::min(
                    group.min_x,
                    component.min_x
                ) +
                1;

            if (
                merged_width >
                MAX_CANDIDATE_WIDTH
            ) {
                continue;
            }

            // =================================================================
            // ATTACH COMPONENT
            // =================================================================

            group.min_x =
                std::min(
                    group.min_x,
                    component.min_x
                );

            group.min_y =
                std::min(
                    group.min_y,
                    component.min_y
                );

            group.max_x =
                std::max(
                    group.max_x,
                    component.max_x
                );

            group.max_y =
                std::max(
                    group.max_y,
                    component.max_y
                );

            group.pixels +=
                component.pixels;

            ++group.count;

            // -----------------------------------------------------------------
            // Running average component height.
            // -----------------------------------------------------------------

            group.reference_height =
                static_cast<int>(
                    (
                        static_cast<std::size_t>(
                            group.reference_height
                        ) *
                        (
                            group.count -
                            1u
                        )
                    +
                    static_cast<std::size_t>(
                        component_height
                    )
                    ) /
                    group.count
                );

            // -----------------------------------------------------------------
            // Running average baseline.
            // -----------------------------------------------------------------

            group.reference_baseline =
                static_cast<int>(
                    (
                        static_cast<std::size_t>(
                            group.reference_baseline
                        ) *
                        (
                            group.count -
                            1u
                        )
                    +
                    static_cast<std::size_t>(
                        component.max_y
                    )
                    ) /
                    group.count
                );

            attached =
                true;

            break;
        }

        // =========================================================================
        // START NEW GROUP
        // =========================================================================

        if (
            !attached
        ) {

            groups.push_back({
                component.min_x,
                component.min_y,
                component.max_x,
                component.max_y,
                component.pixels,
                1u,
                component_height,
                component.max_y
            });
        }
    }

    // =========================================================================
    // GROUP -> TEXT CANDIDATE
    // =========================================================================

    candidates.reserve(
        std::min(
            groups.size(),
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        )
    );

    for (
        const ComponentGroup& group :
        groups
    ) {

        if (
            candidates.size() >=
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        ) {
            break;
        }

        const int group_width =
            group.width();

        const int group_height =
            group.height();

        // =========================================================================
        // GROUP VALIDATION
        // =========================================================================

        if (
            group.count == 0u ||
            group_width <
                minimum_component_width ||
            group_height <
                MIN_LABEL_HEIGHT
        ) {
            continue;
        }

        if (
            group_width >
                MAX_CANDIDATE_WIDTH ||
            group_height >
                MAX_CANDIDATE_HEIGHT
        ) {
            continue;
        }

        // =========================================================================
        // GROUP AREA
        // =========================================================================

        const std::size_t group_area =
            static_cast<std::size_t>(
                group_width
            ) *
            static_cast<std::size_t>(
                group_height
            );

        if (
            group_area == 0u
        ) {
            continue;
        }

        // =========================================================================
        // GROUP DENSITY
        // =========================================================================

        const double density =
            static_cast<double>(
                group.pixels
            ) /
            static_cast<double>(
                group_area
            );

        if (
            !std::isfinite(
                density
            ) ||
            density <
                MIN_LABEL_DENSITY
        ) {
            continue;
        }

        // =========================================================================
        // WIDTH RATIO
        // =========================================================================

        const double width_ratio =
            static_cast<double>(
                group_width
            ) /
            static_cast<double>(
                width
            );

        if (
            width_ratio >
            MAX_CANDIDATE_WIDTH_RATIO
        ) {
            continue;
        }

        // =========================================================================
        // HEIGHT RATIO
        // =========================================================================

        const double height_ratio =
            static_cast<double>(
                group_height
            ) /
            static_cast<double>(
                region_height
            );

        if (
            height_ratio >
            MAX_CANDIDATE_HEIGHT_RATIO
        ) {
            continue;
        }

        // =========================================================================
        // WIDE / DENSE GEOMETRY REJECTION
        // =========================================================================

        if (
            group_width >= 48 &&
            group_height <= 4
        ) {
            continue;
        }

        if (
            group_width >= 96 &&
            group_height <= 8
        ) {
            continue;
        }

        if (
            width_ratio >=
                MAX_WIDE_DENSE_RATIO &&
            density >=
                MAX_WIDE_DENSE_DENSITY
        ) {
            continue;
        }

        // =========================================================================
        // BUILD CANDIDATE
        // =========================================================================

        candidates.push_back({
            group.min_x,
            group.min_y,
            group.max_x,
            group.max_y,
            group.pixels
        });
    }

    // =========================================================================
    // FINAL DETERMINISTIC ORDER
    // =========================================================================

    std::sort(
        candidates.begin(),
        candidates.end(),
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

            if (
                a.min_x !=
                b.min_x
            ) {
                return
                    a.min_x <
                    b.min_x;
            }

            const int a_width =
                a.max_x -
                a.min_x +
                1;

            const int b_width =
                b.max_x -
                b.min_x +
                1;

            return
                a_width <
                b_width;
        }
    );

    return candidates;
}

} // namespace fin_ocr::chart::label::candidate
