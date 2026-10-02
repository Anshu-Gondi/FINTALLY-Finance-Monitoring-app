#include "fin_ocr/chart/label/category/category_sequence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label::category {

namespace {

// =============================================================================
// CATEGORY SEQUENCE CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double CATEGORY_SEQUENCE_BASELINE_TOLERANCE =
    8.0;

constexpr double CATEGORY_SEQUENCE_HEIGHT_TOLERANCE =
    0.45;

constexpr double CATEGORY_SEQUENCE_SPACING_TOLERANCE =
    0.60;

constexpr int CATEGORY_SEQUENCE_NEIGHBOUR_RADIUS =
    180;

constexpr double CATEGORY_SEQUENCE_BASELINE_WEIGHT =
    0.40;

constexpr double CATEGORY_SEQUENCE_HEIGHT_WEIGHT =
    0.25;

constexpr double CATEGORY_SEQUENCE_SPACING_WEIGHT =
    0.35;

// =============================================================================
// MEDIAN
// =============================================================================
//
// Private statistical helper.
//
// Kept local because the median operation is an implementation detail of the
// sequence model and is not part of the public category-sequence API.
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
// BUILD CATEGORY SEQUENCE MODEL
// =============================================================================

[[nodiscard]]
CategorySequenceStats
build_category_sequence_stats(
    const std::vector<TextCandidate>& category_candidates
) noexcept
{
    CategorySequenceStats stats{};

    if (
        category_candidates.empty()
    ) {
        return stats;
    }

    stats.candidate_count =
        category_candidates.size();

    std::vector<double> center_y;
    std::vector<double> heights;
    std::vector<double> widths;

    center_y.reserve(
        category_candidates.size()
    );

    heights.reserve(
        category_candidates.size()
    );

    widths.reserve(
        category_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        const double center =
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5;

        const double candidate_height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double candidate_width =
            static_cast<double>(
                candidate.max_x -
                candidate.min_x +
                1
            );

        center_y.push_back(
            center
        );

        heights.push_back(
            candidate_height
        );

        widths.push_back(
            candidate_width
        );
    }

    stats.median_center_y =
        median_value(
            std::move(
                center_y
            )
        );

    stats.median_height =
        median_value(
            std::move(
                heights
            )
        );

    stats.median_width =
        median_value(
            std::move(
                widths
            )
        );

    // =========================================================================
    // HORIZONTAL CENTER SPACING
    // =========================================================================

    std::vector<double> center_x;

    center_x.reserve(
        category_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        category_candidates
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
    }

    std::sort(
        center_x.begin(),
        center_x.end()
    );

    std::vector<double> spacings;

    if (
        center_x.size() >= 2u
    ) {

        spacings.reserve(
            center_x.size() -
            1u
        );

        for (
            std::size_t i = 1u;
            i < center_x.size();
            ++i
        ) {

            const double spacing =
                center_x[i] -
                center_x[i - 1u];

            if (
                spacing > 0.0
            ) {

                spacings.push_back(
                    spacing
                );
            }
        }
    }

    stats.median_center_spacing =
        median_value(
            std::move(
                spacings
            )
        );

    // =========================================================================
    // COHERENT CANDIDATES
    // =========================================================================

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        const double center =
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5;

        const double height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double baseline_distance =
            std::abs(
                center -
                stats.median_center_y
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
            baseline_distance <=
                CATEGORY_SEQUENCE_BASELINE_TOLERANCE &&
            height_error <=
                CATEGORY_SEQUENCE_HEIGHT_TOLERANCE
        ) {

            ++stats.coherent_count;
        }
    }

    return stats;
}

// =============================================================================
// CATEGORY SEQUENCE BASELINE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_baseline_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
) noexcept
{
    if (
        stats.median_center_y <= 0.0
    ) {
        return 0.0;
    }

    const double center =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    const double distance =
        std::abs(
            center -
            stats.median_center_y
        );

    return std::clamp(
        1.0 -
            (
                distance /
                CATEGORY_SEQUENCE_BASELINE_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY SEQUENCE HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_height_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
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

    const double relative_error =
        std::abs(
            height -
            stats.median_height
        ) /
        stats.median_height;

    return std::clamp(
        1.0 -
            (
                relative_error /
                CATEGORY_SEQUENCE_HEIGHT_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY SEQUENCE SPACING SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept
{
    if (
        stats.median_center_spacing <= 0.0
    ) {
        return 0.50;
    }

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

    double best_error =
        std::numeric_limits<double>::max();

    std::size_t neighbours =
        0u;

    for (
        const TextCandidate& other :
        category_candidates
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

        const double y_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            y_distance >
            CATEGORY_SEQUENCE_BASELINE_TOLERANCE
        ) {
            continue;
        }

        const double horizontal_distance =
            std::abs(
                candidate_center_x -
                other_center_x
            );

        if (
            horizontal_distance <= 0.0 ||
            horizontal_distance >
                static_cast<double>(
                    CATEGORY_SEQUENCE_NEIGHBOUR_RADIUS
                )
        ) {
            continue;
        }

        ++neighbours;

        const double spacing_error =
            std::abs(
                horizontal_distance -
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
        neighbours == 0u ||
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
                CATEGORY_SEQUENCE_SPACING_TOLERANCE
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// FINAL CATEGORY SEQUENCE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept
{
    const double baseline_score =
        category_sequence_baseline_score(
            candidate,
            stats
        );

    const double height_score =
        category_sequence_height_score(
            candidate,
            stats
        );

    const double spacing_score =
        category_sequence_spacing_score(
            candidate,
            category_candidates,
            stats
        );

    return
        baseline_score *
            CATEGORY_SEQUENCE_BASELINE_WEIGHT +
        height_score *
            CATEGORY_SEQUENCE_HEIGHT_WEIGHT +
        spacing_score *
            CATEGORY_SEQUENCE_SPACING_WEIGHT;
}

} // namespace fin_ocr::chart::label::category
