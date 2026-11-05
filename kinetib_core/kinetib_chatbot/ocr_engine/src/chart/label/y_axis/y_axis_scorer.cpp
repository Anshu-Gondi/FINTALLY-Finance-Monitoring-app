#include "fin_ocr/chart/label/y_axis/y_axis_scorer.hpp"

#include "fin_ocr/chart/label/category/category_zone.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// Y-AXIS GEOMETRY LIMITS
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr int MIN_Y_AXIS_LABEL_WIDTH =
    4;

constexpr int MAX_Y_AXIS_LABEL_WIDTH =
    96;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr int MAX_Y_AXIS_LABEL_HEIGHT =
    28;

constexpr double MIN_Y_AXIS_HEIGHT_RATIO =
    0.008;

constexpr double MAX_Y_AXIS_HEIGHT_RATIO =
    0.050;

// =============================================================================
// Y-AXIS POSITION TARGETS
// =============================================================================

constexpr double Y_AXIS_MIN_Y_RATIO =
    0.055;

constexpr double Y_AXIS_MAX_Y_RATIO =
    0.82;

constexpr double Y_AXIS_TARGET_LEFT_X_RATIO =
    0.14;

constexpr double Y_AXIS_TARGET_RIGHT_X_RATIO =
    0.86;

constexpr double Y_AXIS_X_SCORE_TOLERANCE =
    0.10;

} // namespace

// =============================================================================
// Y-AXIS GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_y_axis_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept
{
    if (
        axis ==
        YAxisKind::NONE
    ) {
        return false;
    }

    const LabelZone expected_zone =
        axis == YAxisKind::LEFT
            ? LabelZone::LEFT_Y_AXIS
            : LabelZone::RIGHT_Y_AXIS;

    if (
        ::fin_ocr::chart::label::category::
            classify_label_zone(
                candidate,
                image_width,
                image_height
            ) !=
        expected_zone
    ) {
        return false;
    }

    const int candidate_width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int candidate_height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        candidate_width <
            MIN_Y_AXIS_LABEL_WIDTH ||
        candidate_width >
            MAX_Y_AXIS_LABEL_WIDTH ||
        candidate_height <
            MIN_LABEL_HEIGHT ||
        candidate_height >
            MAX_Y_AXIS_LABEL_HEIGHT
    ) {
        return false;
    }

    const double height_ratio =
        static_cast<double>(
            candidate_height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio <
            MIN_Y_AXIS_HEIGHT_RATIO ||
        height_ratio >
            MAX_Y_AXIS_HEIGHT_RATIO
    ) {
        return false;
    }

    const std::size_t area =
        static_cast<std::size_t>(
            candidate_width
        ) *
        static_cast<std::size_t>(
            candidate_height
        );

    if (
        area == 0u
    ) {
        return false;
    }

    const double density =
        static_cast<double>(
            candidate.active_pixels
        ) /
        static_cast<double>(
            area
        );

    if (
        !std::isfinite(
            density
        ) ||
        density < MIN_LABEL_DENSITY ||
        density > 0.95
    ) {
        return false;
    }

    return true;
}

// =============================================================================
// Y-AXIS SPATIAL SCORE
// =============================================================================

[[nodiscard]]
double y_axis_x_score(
    const TextCandidate& candidate,
    int image_width,
    YAxisKind axis
) noexcept
{
    if (
        image_width <= 0 ||
        axis == YAxisKind::NONE
    ) {
        return 0.0;
    }

    const double center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    const double x_ratio =
        center_x /
        static_cast<double>(
            image_width
        );

    const double target =
        axis == YAxisKind::LEFT
            ? Y_AXIS_TARGET_LEFT_X_RATIO
            : Y_AXIS_TARGET_RIGHT_X_RATIO;

    const double distance =
        std::abs(
            x_ratio -
            target
        );

    return std::clamp(
        1.0 -
            (
                distance /
                Y_AXIS_X_SCORE_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// Y-AXIS HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double y_axis_height_score(
    const TextCandidate& candidate,
    int image_height
) noexcept
{
    if (
        image_height <= 0
    ) {
        return 0.0;
    }

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    const double ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    const double target =
        (
            MIN_Y_AXIS_HEIGHT_RATIO +
            MAX_Y_AXIS_HEIGHT_RATIO
        ) *
        0.5;

    const double range =
        (
            MAX_Y_AXIS_HEIGHT_RATIO -
            MIN_Y_AXIS_HEIGHT_RATIO
        ) *
        0.5;

    if (
        range <= 0.0
    ) {
        return 0.0;
    }

    return std::clamp(
        1.0 -
            std::abs(
                ratio -
                target
            ) /
            range,
        0.0,
        1.0
    );
}

// =============================================================================
// Y-AXIS VERTICAL POSITION SCORE
// =============================================================================

[[nodiscard]]
double y_axis_vertical_position_score(
    const TextCandidate& candidate,
    int image_height
) noexcept
{
    if (
        image_height <= 0
    ) {
        return 0.0;
    }

    const double center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    const double ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    if (
        ratio < Y_AXIS_MIN_Y_RATIO ||
        ratio > Y_AXIS_MAX_Y_RATIO
    ) {
        return 0.0;
    }

    // Y-axis ticks naturally occupy most of the chart height. Do not strongly
    // prefer the center because top and bottom ticks are equally valid.
    return 1.0;
}

// =============================================================================
// COMPLETE Y-AXIS CANDIDATE SCORE
// =============================================================================

[[nodiscard]]
double y_axis_candidate_score(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept
{
    if (
        !acceptable_y_axis_geometry(
            candidate,
            image_width,
            image_height,
            axis
        )
    ) {
        return 0.0;
    }

    const double x_score =
        y_axis_x_score(
            candidate,
            image_width,
            axis
        );

    const double height_score =
        y_axis_height_score(
            candidate,
            image_height
        );

    const double vertical_score =
        y_axis_vertical_position_score(
            candidate,
            image_height
        );

    const double width =
        static_cast<double>(
            candidate.max_x -
            candidate.min_x +
            1
        );

    const double height =
        static_cast<double>(
            candidate.max_y -
            candidate.min_y +
            1
        );

    const double density =
        width > 0.0 &&
        height > 0.0
            ? static_cast<double>(
                  candidate.active_pixels
              ) /
              (
                  width *
                  height
              )
            : 0.0;

    const double density_score =
        std::clamp(
            (
                density -
                0.02
            ) /
            0.45,
            0.0,
            1.0
        );

    return
        x_score * 0.55 +
        height_score * 0.15 +
        vertical_score * 0.10 +
        density_score * 0.20;
}

} // namespace fin_ocr::chart::label::y_axis
