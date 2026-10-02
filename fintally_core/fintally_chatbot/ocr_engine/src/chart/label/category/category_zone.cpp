#include "fin_ocr/chart/label/category/category_zone.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fin_ocr::chart::label::category {

namespace {

// =============================================================================
// ZONE ESTIMATION CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double LEFT_Y_AXIS_MAX_X_RATIO =
    0.18;

constexpr double RIGHT_Y_AXIS_MIN_X_RATIO =
    0.82;

constexpr double Y_AXIS_MIN_Y_RATIO =
    0.055;

constexpr double Y_AXIS_MAX_Y_RATIO =
    0.82;

constexpr double CATEGORY_ZONE_MIN_Y_RATIO =
    0.68;

constexpr double CATEGORY_ZONE_MAX_Y_RATIO =
    0.87;

constexpr double TITLE_ZONE_MIN_Y_RATIO =
    0.87;

constexpr double BOTTOM_EXCLUSION_RATIO =
    0.985;

// =============================================================================
// CATEGORY GEOMETRY LIMITS
// =============================================================================

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr double MIN_CATEGORY_HEIGHT_RATIO =
    0.010;

constexpr double MAX_CATEGORY_HEIGHT_RATIO =
    0.045;

constexpr double MAX_CANDIDATE_WIDTH_RATIO =
    0.35;

constexpr double MIN_LABEL_DENSITY =
    0.010;

} // namespace

// =============================================================================
// ZONE CLASSIFICATION
// =============================================================================
//
// Classifies a candidate according to its normalized position in the image.
//
// Priority is deliberate:
//
//     left Y-axis
//     right Y-axis
//     category axis
//     title / footnote
//     plot interior
//
// This prevents boundary regions from being ambiguously classified.
//
// =============================================================================

[[nodiscard]]
LabelZone classify_label_zone(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept
{
    if (
        !geometry::valid_candidate(
            candidate
        ) ||
        image_width <= 0 ||
        image_height <= 0
    ) {
        return LabelZone::UNKNOWN;
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

    const double x_ratio =
        center_x /
        static_cast<double>(
            image_width
        );

    const double y_ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    // =========================================================================
    // LEFT Y AXIS
    // =========================================================================

    if (
        x_ratio <=
            LEFT_Y_AXIS_MAX_X_RATIO &&
        y_ratio >=
            Y_AXIS_MIN_Y_RATIO &&
        y_ratio <=
            Y_AXIS_MAX_Y_RATIO
    ) {
        return LabelZone::LEFT_Y_AXIS;
    }

    // =========================================================================
    // RIGHT Y AXIS
    // =========================================================================

    if (
        x_ratio >=
            RIGHT_Y_AXIS_MIN_X_RATIO &&
        y_ratio >=
            Y_AXIS_MIN_Y_RATIO &&
        y_ratio <=
            Y_AXIS_MAX_Y_RATIO
    ) {
        return LabelZone::RIGHT_Y_AXIS;
    }

    // =========================================================================
    // CATEGORY AXIS
    // =========================================================================

    if (
        y_ratio >=
            CATEGORY_ZONE_MIN_Y_RATIO &&
        y_ratio <
            CATEGORY_ZONE_MAX_Y_RATIO
    ) {
        return LabelZone::CATEGORY_AXIS;
    }

    // =========================================================================
    // TITLE / FOOTNOTE
    // =========================================================================

    if (
        y_ratio >=
        TITLE_ZONE_MIN_Y_RATIO
    ) {

        if (
            y_ratio >=
            BOTTOM_EXCLUSION_RATIO
        ) {
            return LabelZone::OUTSIDE;
        }

        return LabelZone::TITLE;
    }

    // =========================================================================
    // PLOT INTERIOR
    // =========================================================================

    return LabelZone::PLOT_INTERIOR;
}

// =============================================================================
// CATEGORY ZONE GEOMETRY
// =============================================================================
//
// Applies category-specific geometry constraints after the candidate has been
// identified as belonging to the category-axis region.
//
// =============================================================================

[[nodiscard]]
bool acceptable_category_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept
{
    if (
        classify_label_zone(
            candidate,
            image_width,
            image_height
        ) !=
        LabelZone::CATEGORY_AXIS
    ) {
        return false;
    }

    if (
        image_width <= 0 ||
        image_height <= 0
    ) {
        return false;
    }

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    // =========================================================================
    // BASIC DIMENSIONS
    // =========================================================================

    if (
        width <
            MIN_LABEL_WIDTH ||
        height <
            MIN_LABEL_HEIGHT
    ) {
        return false;
    }

    // =========================================================================
    // HEIGHT RATIO
    // =========================================================================

    const double height_ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio <
            MIN_CATEGORY_HEIGHT_RATIO ||
        height_ratio >
            MAX_CATEGORY_HEIGHT_RATIO
    ) {
        return false;
    }

    // =========================================================================
    // WIDTH RATIO
    // =========================================================================

    const double width_ratio =
        static_cast<double>(
            width
        ) /
        static_cast<double>(
            image_width
        );

    if (
        width_ratio >
        MAX_CANDIDATE_WIDTH_RATIO
    ) {
        return false;
    }

    // =========================================================================
    // AREA
    // =========================================================================

    const std::size_t area =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            height
        );

    if (
        area == 0u
    ) {
        return false;
    }

    // =========================================================================
    // FOREGROUND DENSITY
    // =========================================================================

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
        density <
            MIN_LABEL_DENSITY
    ) {
        return false;
    }

    return true;
}

} // namespace fin_ocr::chart::label::category
