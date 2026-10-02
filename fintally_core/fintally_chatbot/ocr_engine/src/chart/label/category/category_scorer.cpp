#include "fin_ocr/chart/label/category/category_scorer.hpp"

#include "fin_ocr/chart/label/category/category_zone.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fin_ocr::chart::label::category {

namespace {

// =============================================================================
// CATEGORY SCORING CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double CATEGORY_CENTER_SCORE_WEIGHT =
    0.40;

constexpr double CATEGORY_HEIGHT_SCORE_WEIGHT =
    0.20;

constexpr double CATEGORY_WIDTH_SCORE_WEIGHT =
    0.10;

constexpr double CATEGORY_DENSITY_SCORE_WEIGHT =
    0.10;

constexpr double CATEGORY_ALIGNMENT_SCORE_WEIGHT =
    0.20;

// =============================================================================
// CATEGORY LABEL LAYOUT
// =============================================================================

constexpr int CATEGORY_ALIGNMENT_RADIUS =
    140;

constexpr std::size_t
    MIN_CATEGORY_NEIGHBOURS_FOR_STRONG_ALIGNMENT =
        2u;

// =============================================================================
// CATEGORY GEOMETRY
// =============================================================================
//
// These values are shared conceptually with category_zone.cpp, but remain local
// during the first migration pass to avoid introducing a premature shared
// configuration dependency.
// =============================================================================

constexpr double CATEGORY_ZONE_MIN_Y_RATIO =
    0.68;

constexpr double CATEGORY_ZONE_MAX_Y_RATIO =
    0.87;

constexpr double MIN_CATEGORY_HEIGHT_RATIO =
    0.010;

constexpr double MAX_CATEGORY_HEIGHT_RATIO =
    0.045;

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr double MAX_CANDIDATE_WIDTH_RATIO =
    0.35;

constexpr double MIN_LABEL_DENSITY =
    0.010;

} // namespace

// =============================================================================
// CATEGORY CENTER SCORE
// =============================================================================

[[nodiscard]]
double category_center_score(
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
        ) /
        2.0;

    const double y_ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    const double center =
        (
            CATEGORY_ZONE_MIN_Y_RATIO +
            CATEGORY_ZONE_MAX_Y_RATIO
        ) /
        2.0;

    const double half_range =
        (
            CATEGORY_ZONE_MAX_Y_RATIO -
            CATEGORY_ZONE_MIN_Y_RATIO
        ) /
        2.0;

    if (
        half_range <= 0.0
    ) {
        return 0.0;
    }

    const double distance =
        std::abs(
            y_ratio -
            center
        );

    return std::clamp(
        1.0 -
            (
                distance /
                half_range
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double category_height_score(
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
            MIN_CATEGORY_HEIGHT_RATIO +
            MAX_CATEGORY_HEIGHT_RATIO
        ) /
        2.0;

    const double range =
        (
            MAX_CATEGORY_HEIGHT_RATIO -
            MIN_CATEGORY_HEIGHT_RATIO
        ) /
        2.0;

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
// CATEGORY WIDTH SCORE
// =============================================================================

[[nodiscard]]
double category_width_score(
    const TextCandidate& candidate,
    int image_width
) noexcept
{
    if (
        image_width <= 0
    ) {
        return 0.0;
    }

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const double ratio =
        static_cast<double>(
            width
        ) /
        static_cast<double>(
            image_width
        );

    if (
        ratio <= 0.0
    ) {
        return 0.0;
    }

    if (
        ratio >= 0.12
    ) {
        return 0.0;
    }

    return std::clamp(
        1.0 -
            ratio /
            0.12,
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY DENSITY SCORE
// =============================================================================

[[nodiscard]]
double category_density_score(
    const TextCandidate& candidate
) noexcept
{
    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        width <= 0 ||
        height <= 0
    ) {
        return 0.0;
    }

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
        return 0.0;
    }

    const double density =
        static_cast<double>(
            candidate.active_pixels
        ) /
        static_cast<double>(
            area
        );

    return std::clamp(
        (
            density -
            0.015
        ) /
        0.20,
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY HORIZONTAL ALIGNMENT SCORE
// =============================================================================
//
// image_width is part of the API because classify_label_zone() needs the real
// image width to classify neighbouring candidates correctly.
//
// =============================================================================

[[nodiscard]]
double category_alignment_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept
{
    if (
        image_width <= 0 ||
        image_height <= 0
    ) {
        return 0.0;
    }

    const double candidate_center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) /
        2.0;

    std::size_t neighbours =
        0u;

    double accumulated_y_distance =
        0.0;

    for (
        const TextCandidate& other :
        candidates
    ) {

        if (
            &other ==
            &candidate
        ) {
            continue;
        }

        if (
            classify_label_zone(
                other,
                image_width,
                image_height
            ) !=
            LabelZone::CATEGORY_AXIS
        ) {
            continue;
        }

        const int horizontal_distance =
            other.min_x >
                candidate.max_x
                ? other.min_x -
                  candidate.max_x -
                  1
                : candidate.min_x >
                      other.max_x
                    ? candidate.min_x -
                      other.max_x -
                      1
                    : 0;

        if (
            horizontal_distance >
            CATEGORY_ALIGNMENT_RADIUS
        ) {
            continue;
        }

        const double other_center_y =
            (
                static_cast<double>(
                    other.min_y
                ) +
                static_cast<double>(
                    other.max_y
                )
            ) /
            2.0;

        const double y_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            y_distance >
            10.0
        ) {
            continue;
        }

        ++neighbours;

        accumulated_y_distance +=
            y_distance;
    }

    // =========================================================================
    // STRONG ALIGNMENT
    // =========================================================================

    if (
        neighbours >=
        MIN_CATEGORY_NEIGHBOURS_FOR_STRONG_ALIGNMENT
    ) {

        const double average_distance =
            accumulated_y_distance /
            static_cast<double>(
                neighbours
            );

        return std::clamp(
            1.0 -
                (
                    average_distance /
                    10.0
                ),
            0.0,
            1.0
        );
    }

    // =========================================================================
    // WEAK ALIGNMENT
    // =========================================================================

    if (
        neighbours == 1u
    ) {
        return 0.55;
    }

    return 0.35;
}

// =============================================================================
// CATEGORY CANDIDATE SCORE
// =============================================================================

[[nodiscard]]
double category_candidate_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept
{
    if (
        !acceptable_category_geometry(
            candidate,
            image_width,
            image_height
        )
    ) {
        return 0.0;
    }

    const double center_score =
        category_center_score(
            candidate,
            image_height
        );

    const double height_score =
        category_height_score(
            candidate,
            image_height
        );

    const double width_score =
        category_width_score(
            candidate,
            image_width
        );

    const double density_score =
        category_density_score(
            candidate
        );

    const double alignment_score =
        category_alignment_score(
            candidate,
            candidates,
            image_width,
            image_height
        );

    return
        center_score *
            CATEGORY_CENTER_SCORE_WEIGHT +

        height_score *
            CATEGORY_HEIGHT_SCORE_WEIGHT +

        width_score *
            CATEGORY_WIDTH_SCORE_WEIGHT +

        density_score *
            CATEGORY_DENSITY_SCORE_WEIGHT +

        alignment_score *
            CATEGORY_ALIGNMENT_SCORE_WEIGHT;
}

} // namespace fin_ocr::chart::label::category
