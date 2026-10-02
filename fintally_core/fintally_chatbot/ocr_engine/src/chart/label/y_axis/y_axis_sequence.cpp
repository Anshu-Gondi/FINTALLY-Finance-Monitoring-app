#include "fin_ocr/chart/label/y_axis/y_axis_sequence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// Y-AXIS SEQUENCE CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double Y_AXIS_SEQUENCE_X_TOLERANCE =
    28.0;

constexpr double Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE =
    0.55;

constexpr double Y_AXIS_SEQUENCE_SPACING_TOLERANCE =
    0.65;

constexpr int Y_AXIS_SEQUENCE_NEIGHBOUR_RADIUS =
    180;

constexpr double Y_AXIS_SEQUENCE_X_WEIGHT =
    0.35;

constexpr double Y_AXIS_SEQUENCE_HEIGHT_WEIGHT =
    0.20;

constexpr double Y_AXIS_SEQUENCE_SPACING_WEIGHT =
    0.45;

// =============================================================================
// MEDIAN
// =============================================================================
//
// Private statistical helper.
//
// Kept local because median_value() is an implementation detail rather than
// part of the public Y-axis sequence API.
//
// =============================================================================

[[nodiscard]]
double median_value(
    std::vector<double> values
) noexcept
{
    if (
        values.empty()
    ) {
        return 0.0;
    }

    const std::size_t middle =
        values.size() / 2u;

    std::nth_element(
        values.begin(),
        values.begin() +
            middle,
        values.end()
    );

    const double upper =
        values[middle];

    if (
        values.size() % 2u != 0u
    ) {
        return upper;
    }

    std::nth_element(
        values.begin(),
        values.begin() +
            middle -
            1u,
        values.end()
    );

    const double lower =
        values[
            middle -
            1u
        ];

    return
        (
            lower +
            upper
        ) *
        0.5;
}

} // namespace

// =============================================================================
// Y-AXIS MEDIAN SPACING
// =============================================================================

[[nodiscard]]
double y_axis_median_spacing(
    const std::vector<TextCandidate>& candidates
) noexcept
{
    if (
        candidates.size() < 2u
    ) {
        return 0.0;
    }

    std::vector<double> centers;

    centers.reserve(
        candidates.size()
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        centers.push_back(
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5
        );
    }

    std::sort(
        centers.begin(),
        centers.end()
    );

    std::vector<double> spacings;

    spacings.reserve(
        centers.size() -
        1u
    );

    for (
        std::size_t i = 1u;
        i < centers.size();
        ++i
    ) {

        const double spacing =
            centers[i] -
            centers[i - 1u];

        if (
            spacing > 0.0
        ) {

            spacings.push_back(
                spacing
            );
        }
    }

    if (
        spacings.empty()
    ) {
        return 0.0;
    }

    return
        median_value(
            std::move(
                spacings
            )
        );
}

// =============================================================================
// Y-AXIS SPACING STATISTICS
// =============================================================================

[[nodiscard]]
YAxisSequenceStats
build_y_axis_sequence_stats(
    const std::vector<TextCandidate>& candidates
) noexcept
{
    YAxisSequenceStats stats{};

    if (
        candidates.empty()
    ) {
        return stats;
    }

    stats.candidate_count =
        candidates.size();

    std::vector<double> center_x;
    std::vector<double> heights;

    center_x.reserve(
        candidates.size()
    );

    heights.reserve(
        candidates.size()
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        center_x.push_back(
            (
                static_cast<double>(
                    candidate.min_x
                ) +
                static_cast<double>(
                    candidate.max_x
                )
            ) *
            0.5
        );

        heights.push_back(
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            )
        );
    }

    stats.median_center_x =
        median_value(
            std::move(
                center_x
            )
        );

    stats.median_height =
        median_value(
            std::move(
                heights
            )
        );

    stats.median_center_spacing =
        y_axis_median_spacing(
            candidates
        );

    // =========================================================================
    // COHERENCE COUNT
    // =========================================================================

    for (
        const TextCandidate& candidate :
        candidates
    ) {

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

        const double height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double x_distance =
            std::abs(
                center_x -
                stats.median_center_x
            );

        const double height_error =
            stats.median_height > 0.0
                ? std::abs(
                      height -
                      stats.median_height
                  ) /
                  stats.median_height
                : 1.0;

        if (
            x_distance <=
                Y_AXIS_SEQUENCE_X_TOLERANCE &&
            height_error <=
                Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE
        ) {

            ++stats.coherent_count;
        }
    }

    return stats;
}

// =============================================================================
// Y-AXIS SEQUENCE X SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_x_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
) noexcept
{
    if (
        stats.median_center_x <= 0.0
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

    return std::clamp(
        1.0 -
            std::abs(
                center_x -
                stats.median_center_x
            ) /
            Y_AXIS_SEQUENCE_X_TOLERANCE,
        0.0,
        1.0
    );
}

// =============================================================================
// Y-AXIS SEQUENCE HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_height_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
) noexcept
{
    if (
        stats.median_height <= 0.0
    ) {
        return 0.0;
    }

    const double height =
        static_cast<double>(
            candidate.max_y -
            candidate.min_y +
            1
        );

    const double error =
        std::abs(
            height -
            stats.median_height
        ) /
        stats.median_height;

    return std::clamp(
        1.0 -
            (
                error /
                Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// Y-AXIS SEQUENCE SPACING SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept
{
    if (
        stats.median_center_spacing <= 0.0
    ) {
        return 0.50;
    }

    const double candidate_center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    const double candidate_center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    double best_error =
        std::numeric_limits<double>::max();

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

        const double other_center_x =
            (
                static_cast<double>(
                    other.min_x
                ) +
                static_cast<double>(
                    other.max_x
                )
            ) *
            0.5;

        if (
            std::abs(
                candidate_center_x -
                other_center_x
            ) >
            Y_AXIS_SEQUENCE_X_TOLERANCE
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
            ) *
            0.5;

        const double vertical_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            vertical_distance <= 0.0 ||
            vertical_distance >
                static_cast<double>(
                    Y_AXIS_SEQUENCE_NEIGHBOUR_RADIUS
                )
        ) {
            continue;
        }

        const double spacing_error =
            std::abs(
                vertical_distance -
                stats.median_center_spacing
            ) /
            stats.median_center_spacing;

        best_error =
            std::min(
                best_error,
                spacing_error
            );
    }

    if (
        !std::isfinite(
            best_error
        )
    ) {
        return 0.40;
    }

    return std::clamp(
        1.0 -
            (
                best_error /
                Y_AXIS_SEQUENCE_SPACING_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// COMPLETE Y-AXIS SEQUENCE SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept
{
    return
        y_axis_sequence_x_score(
            candidate,
            stats
        ) *
            Y_AXIS_SEQUENCE_X_WEIGHT +

        y_axis_sequence_height_score(
            candidate,
            stats
        ) *
            Y_AXIS_SEQUENCE_HEIGHT_WEIGHT +

        y_axis_sequence_spacing_score(
            candidate,
            candidates,
            stats
        ) *
            Y_AXIS_SEQUENCE_SPACING_WEIGHT;
}

} // namespace fin_ocr::chart::label::y_axis
