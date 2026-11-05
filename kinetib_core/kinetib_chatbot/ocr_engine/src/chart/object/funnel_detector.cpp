#include "fin_ocr/chart/object/funnel_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace fin_ocr::chart::object::funnel {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int MIN_RECT_WIDTH =
    config::CHART_OBJECT_MIN_RECT_WIDTH;

constexpr int MIN_RECT_HEIGHT =
    config::CHART_OBJECT_MIN_RECT_HEIGHT;

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr int FUNNEL_MIN_STAGES =
    config::CHART_OBJECT_FUNNEL_MIN_STAGES;

} // namespace

// =============================================================================
// FUNNEL STAGES
// =============================================================================
//
// The original implementation:
//
//   1. extracts connected components
//   2. filters by minimum dimensions
//   3. orders components top-to-bottom / left-to-right
//   4. requires a minimum number of stages
//   5. converts components into FunnelStage objects
//   6. checks monotonically decreasing stage widths
//   7. normalizes width relative to the first stage
//   8. writes the result into ChartObjectSet
//
// This migration preserves that behavior.
// =============================================================================

void detect_funnel_stages(
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
    // CANDIDATE FILTERING
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::Component
    > candidates;

    candidates.reserve(
        components.size()
    );

    for (
        const ::fin_ocr::chart::object::Component& component :
        components
    ) {
        if (
            component.width() <
                MIN_RECT_WIDTH ||
            component.height() <
                MIN_RECT_HEIGHT
        ) {
            continue;
        }

        candidates.push_back(
            component
        );
    }

    // =========================================================================
    // STAGE ORDERING
    // =========================================================================
    //
    // Funnel stages are expected to progress vertically. When two components
    // occupy the same general row, their X position provides deterministic
    // ordering.
    // =========================================================================

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const ::fin_ocr::chart::object::Component& a,
            const ::fin_ocr::chart::object::Component& b
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

    // =========================================================================
    // MINIMUM STAGE COUNT
    // =========================================================================

    if (
        candidates.size() <
        static_cast<std::size_t>(
            FUNNEL_MIN_STAGES
        )
    ) {
        return;
    }

    // =========================================================================
    // BUILD FUNNEL STAGES
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::FunnelStage
    > stages;

    stages.reserve(
        std::min<std::size_t>(
            candidates.size(),
            MAX_OBJECTS
        )
    );

    for (
        const ::fin_ocr::chart::object::Component& component :
        candidates
    ) {
        if (
            stages.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        ::fin_ocr::chart::object::FunnelStage stage{};

        stage.bounds =
            ::fin_ocr::chart::object::to_rect(
                component
            );

        stage.stage_index =
            static_cast<int>(
                stages.size()
            );

        stage.relative_width =
            static_cast<double>(
                component.width()
            );

        stage.confidence =
            static_cast<float>(
                std::clamp(
                    component.density,
                    0.0,
                    1.0
                )
            );

        stages.push_back(
            stage
        );
    }

    // =========================================================================
    // STAGE COUNT AFTER LIMIT
    // =========================================================================

    if (
        stages.size() <
        static_cast<std::size_t>(
            FUNNEL_MIN_STAGES
        )
    ) {
        return;
    }

    // =========================================================================
    // MONOTONIC WIDTH CHECK
    // =========================================================================
    //
    // A funnel should narrow as the stages progress.
    //
    // The original implementation permits a tolerance of 2 pixels.
    // =========================================================================

    double previous_width =
        std::numeric_limits<double>::infinity();

    bool monotonically_decreasing =
        true;

    for (
        const ::fin_ocr::chart::object::FunnelStage& stage :
        stages
    ) {
        const double width_value =
            stage.relative_width;

        if (
            width_value >
            previous_width +
                2.0
        ) {
            monotonically_decreasing =
                false;

            break;
        }

        previous_width =
            width_value;
    }

    if (
        !monotonically_decreasing
    ) {
        return;
    }

    // =========================================================================
    // WIDTH NORMALIZATION
    // =========================================================================

    const double max_width =
        std::max(
            stages.front().relative_width,
            1.0
        );

    for (
        ::fin_ocr::chart::object::FunnelStage& stage :
        stages
    ) {
        stage.relative_width /=
            max_width;

        stage.confidence =
            static_cast<float>(
                std::clamp(
                    static_cast<double>(
                        stage.confidence
                    ) *
                    0.7 +
                    stage.relative_width *
                    0.3,
                    0.0,
                    1.0
                )
            );
    }

    // =========================================================================
    // COMMIT RESULT
    // =========================================================================

    result.funnel_stages =
        std::move(
            stages
        );
}

} // namespace fin_ocr::chart::object::funnel
