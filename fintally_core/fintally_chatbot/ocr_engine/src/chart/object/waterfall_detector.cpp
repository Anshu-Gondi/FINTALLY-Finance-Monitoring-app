#include "fin_ocr/chart/object/waterfall_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace fin_ocr::chart::object::waterfall {

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

constexpr double MIN_BAR_ASPECT =
    config::CHART_OBJECT_MIN_BAR_ASPECT;

constexpr double MAX_BAR_ASPECT =
    config::CHART_OBJECT_MAX_BAR_ASPECT;

// =============================================================================
// WATERFALL VALIDATION
// =============================================================================

constexpr std::size_t MIN_WATERFALL_STEPS = 3;

// =============================================================================
// WIDTH REGULARITY
// =============================================================================
//
// The original implementation accepts a candidate waterfall when the relative
// width regularity is at least 0.50.
//
// =============================================================================

constexpr double MIN_WIDTH_REGULARITY = 0.50;

// =============================================================================
// WIDTH COMPARISON TOLERANCE
// =============================================================================

constexpr double WIDTH_TOLERANCE = 1.0;

} // namespace

// =============================================================================
// WATERFALL
// =============================================================================
//
// Detection flow:
//
//   1. Require a valid Cartesian coordinate system.
//   2. Extract connected components.
//   3. Keep rectangular components with bar-like aspect ratios.
//   4. Keep components inside the plot.
//   5. Order them by X position.
//   6. Require at least three candidates.
//   7. Convert candidates into WaterfallStep objects.
//   8. Measure width regularity.
//   9. Reject irregular candidate groups.
//  10. Apply regularity to confidence.
//
// The current implementation intentionally does not infer delta or cumulative
// values because those values are not available from geometry alone.
//
// =============================================================================

void detect_waterfall_steps(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
)
{
    // =========================================================================
    // COORDINATE SYSTEM REQUIREMENT
    // =========================================================================

    if (
        !coordinates.valid ||
        !coordinates.x_axis.horizontal ||
        !coordinates.y_axis.vertical
    ) {
        return;
    }

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
    // CANDIDATE COMPONENTS
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
        // ---------------------------------------------------------------------
        // Minimum dimensions.
        // ---------------------------------------------------------------------

        if (
            component.width() <
                MIN_RECT_WIDTH ||
            component.height() <
                MIN_RECT_HEIGHT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Bar-like aspect ratio.
        // ---------------------------------------------------------------------

        const double aspect =
            component.aspect();

        if (
            aspect <
                MIN_BAR_ASPECT ||
            aspect >
                MAX_BAR_ASPECT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Component center.
        // ---------------------------------------------------------------------

        const int center_x =
            (
                component.min_x +
                component.max_x
            ) / 2;

        const int center_y =
            (
                component.min_y +
                component.max_y
            ) / 2;

        // ---------------------------------------------------------------------
        // Require the component to belong to the plot.
        // ---------------------------------------------------------------------

        if (
            !::fin_ocr::chart::object::inside_plot(
                coordinates,
                center_x,
                center_y
            )
        ) {
            continue;
        }

        candidates.push_back(
            component
        );
    }

    // =========================================================================
    // ORDER BY X POSITION
    // =========================================================================

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const ::fin_ocr::chart::object::Component& a,
            const ::fin_ocr::chart::object::Component& b
        ) noexcept {
            return
                a.min_x <
                b.min_x;
        }
    );

    // =========================================================================
    // MINIMUM CANDIDATE COUNT
    // =========================================================================

    if (
        candidates.size() <
        MIN_WATERFALL_STEPS
    ) {
        return;
    }

    // =========================================================================
    // BUILD WATERFALL STEPS
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::WaterfallStep
    > steps;

    steps.reserve(
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
            steps.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        ::fin_ocr::chart::object::WaterfallStep step{};

        step.bounds =
            ::fin_ocr::chart::object::to_rect(
                component
            );

        step.category_index =
            static_cast<int>(
                steps.size()
            );

        step.confidence =
            static_cast<float>(
                std::clamp(
                    component.density,
                    0.0,
                    1.0
                )
            );

        steps.push_back(
            step
        );
    }

    // =========================================================================
    // MINIMUM STEP COUNT AFTER LIMIT
    // =========================================================================

    if (
        steps.size() <
        MIN_WATERFALL_STEPS
    ) {
        return;
    }

    // =========================================================================
    // WIDTH MEAN
    // =========================================================================

    double width_mean =
        0.0;

    for (
        const ::fin_ocr::chart::object::WaterfallStep& step :
        steps
    ) {
        width_mean +=
            static_cast<double>(
                step.bounds.max_x -
                step.bounds.min_x +
                1
            );
    }

    width_mean /=
        static_cast<double>(
            steps.size()
        );

    if (
        width_mean <= 0.0
    ) {
        return;
    }

    // =========================================================================
    // WIDTH VARIANCE
    // =========================================================================

    double width_variance =
        0.0;

    for (
        const ::fin_ocr::chart::object::WaterfallStep& step :
        steps
    ) {
        const double width_value =
            static_cast<double>(
                step.bounds.max_x -
                step.bounds.min_x +
                1
            );

        const double difference =
            width_value -
            width_mean;

        width_variance +=
            difference *
            difference;
    }

    width_variance /=
        static_cast<double>(
            steps.size()
        );

    // =========================================================================
    // WIDTH STANDARD DEVIATION
    // =========================================================================

    const double width_stddev =
        std::sqrt(
            width_variance
        );

    // =========================================================================
    // REGULARITY
    // =========================================================================

    const double regularity =
        std::clamp(
            1.0 -
            width_stddev /
                std::max(
                    width_mean,
                    1.0
                ),
            0.0,
            1.0
        );

    if (
        regularity <
        MIN_WIDTH_REGULARITY
    ) {
        return;
    }

    // =========================================================================
    // COMMIT
    // =========================================================================

    result.waterfall_steps =
        std::move(
            steps
        );

    // =========================================================================
    // CONFIDENCE ADJUSTMENT
    // =========================================================================

    for (
        ::fin_ocr::chart::object::WaterfallStep& step :
        result.waterfall_steps
    ) {
        step.confidence =
            static_cast<float>(
                std::clamp(
                    static_cast<double>(
                        step.confidence
                    ) *
                    0.6 +
                    regularity *
                    0.4,
                    0.0,
                    1.0
                )
            );
    }
}

} // namespace fin_ocr::chart::object::waterfall
