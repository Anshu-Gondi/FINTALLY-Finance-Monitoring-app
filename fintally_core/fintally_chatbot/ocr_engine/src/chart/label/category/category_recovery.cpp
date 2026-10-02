#include "fin_ocr/chart/label/category/category_recovery.hpp"

#include "fin_ocr/chart/label/category/category_zone.hpp"
#include "fin_ocr/chart/label/label_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace fin_ocr::chart::label::category {

namespace {

// =============================================================================
// CATEGORY GAP RECOVERY CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double CATEGORY_GAP_RECOVERY_MIN_RATIO =
    1.45;

constexpr double CATEGORY_GAP_RECOVERY_MAX_RELATIVE_ERROR =
    0.45;

constexpr int CATEGORY_GAP_RECOVERY_MAX_MISSING_SLOTS =
    3;

constexpr double CATEGORY_GAP_RECOVERY_X_RADIUS_FACTOR =
    0.95;

constexpr double CATEGORY_GAP_RECOVERY_Y_RADIUS_FACTOR =
    1.00;

constexpr int CATEGORY_GAP_RECOVERY_MIN_X_RADIUS =
    18;

constexpr int CATEGORY_GAP_RECOVERY_MAX_X_RADIUS =
    32;

constexpr int CATEGORY_GAP_RECOVERY_MIN_Y_RADIUS =
    12;

constexpr int CATEGORY_GAP_RECOVERY_MAX_Y_RADIUS =
    18;

constexpr double CATEGORY_GAP_RECOVERY_DOMINANT_ROW_RATIO =
    0.85;

constexpr double CATEGORY_GAP_RECOVERY_DOMINANT_COLUMN_RATIO =
    0.85;

constexpr std::size_t CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS =
    20u;

constexpr double CATEGORY_GAP_RECOVERY_MIN_DENSITY =
    0.04;

constexpr double CATEGORY_GAP_RECOVERY_MAX_DENSITY =
    0.75;

constexpr double CATEGORY_GAP_RECOVERY_MAX_CENTER_OFFSET_FACTOR =
    0.50;

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================

constexpr bool CHART_LABEL_DEBUG =
    true;

// =============================================================================
// CANDIDATE GEOMETRY LIMITS
// =============================================================================
//
// Preserved from the legacy recognizer. These are the hard bounds applied to
// a recovered category candidate after local segmentation.
//
// =============================================================================

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr int MAX_CANDIDATE_WIDTH =
    320;

constexpr int MAX_CANDIDATE_HEIGHT =
    32;

} // namespace

// =============================================================================
// MISSING CATEGORY RECOVERY
// =============================================================================
//
// Detects internal gaps in an otherwise regular horizontal category-label
// sequence and inspects the expected missing slots locally.
//
// The recovery stage is deliberately conservative:
//
//     geometry predicts a slot
//         ↓
//     local foreground inspection
//         ↓
//     dominant chart-stroke removal
//         ↓
//     bounded candidate geometry
//         ↓
//     density validation
//
// No OCR text is fabricated here.
//
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> recover_missing_category_candidates(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& sequence_stats,
    std::uint8_t minimum_foreground
)
{
    std::vector<TextCandidate> recovered;

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        category_candidates.size() < 2u ||
        sequence_stats.median_center_spacing <= 0.0 ||
        sequence_stats.median_width <= 0.0 ||
        sequence_stats.median_height <= 0.0
    ) {
        return recovered;
    }

    // =========================================================================
    // ORDER CANDIDATES IN HORIZONTAL READING ORDER
    // =========================================================================

    std::vector<TextCandidate> ordered =
        category_candidates;

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            const double a_center =
                (
                    static_cast<double>(
                        a.min_x
                    ) +
                    static_cast<double>(
                        a.max_x
                    )
                ) *
                0.5;

            const double b_center =
                (
                    static_cast<double>(
                        b.min_x
                    ) +
                    static_cast<double>(
                        b.max_x
                    )
                ) *
                0.5;

            if (
                a_center !=
                b_center
            ) {
                return
                    a_center <
                    b_center;
            }

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

    const double median_spacing =
        sequence_stats.median_center_spacing;

    const double median_width =
        sequence_stats.median_width;

    const double median_height =
        sequence_stats.median_height;

    const double median_center_y =
        sequence_stats.median_center_y;

    // =========================================================================
    // INTERNAL GAPS ONLY
    // =========================================================================

    for (
        std::size_t i = 1u;
        i < ordered.size();
        ++i
    ) {

        const TextCandidate& left =
            ordered[i - 1u];

        const TextCandidate& right =
            ordered[i];

        const double left_center_x =
            (
                static_cast<double>(
                    left.min_x
                ) +
                static_cast<double>(
                    left.max_x
                )
            ) *
            0.5;

        const double right_center_x =
            (
                static_cast<double>(
                    right.min_x
                ) +
                static_cast<double>(
                    right.max_x
                )
            ) *
            0.5;

        const double gap =
            right_center_x -
            left_center_x;

        if (
            gap <=
            median_spacing *
                CATEGORY_GAP_RECOVERY_MIN_RATIO
        ) {
            continue;
        }

        const long long estimated_slot_count =
            static_cast<long long>(
                std::llround(
                    gap /
                    median_spacing
                )
            ) -
            1LL;

        if (
            estimated_slot_count <= 0LL ||
            estimated_slot_count >
                static_cast<long long>(
                    CATEGORY_GAP_RECOVERY_MAX_MISSING_SLOTS
                )
        ) {
            continue;
        }

        const double modeled_gap =
            median_spacing *
            static_cast<double>(
                estimated_slot_count +
                1LL
            );

        if (
            modeled_gap <= 0.0
        ) {
            continue;
        }

        const double relative_gap_error =
            std::abs(
                gap -
                modeled_gap
            ) /
            modeled_gap;

        if (
            relative_gap_error >
            CATEGORY_GAP_RECOVERY_MAX_RELATIVE_ERROR
        ) {
            continue;
        }

        // =====================================================================
        // INSPECT EACH EXPECTED MISSING SLOT
        // =====================================================================

        for (
            long long slot = 1LL;
            slot <= estimated_slot_count;
            ++slot
        ) {

            const double expected_center_x =
                left_center_x +
                median_spacing *
                    static_cast<double>(
                        slot
                    );

            // -----------------------------------------------------------------
            // OCCUPANCY CHECK
            // -----------------------------------------------------------------
            //
            // Prevent recovery where another existing candidate already
            // occupies the predicted slot.
            // -----------------------------------------------------------------

            bool occupied =
                false;

            const double occupancy_radius =
                median_spacing *
                0.35;

            for (
                const TextCandidate& existing :
                ordered
            ) {

                const double existing_center_x =
                    (
                        static_cast<double>(
                            existing.min_x
                        ) +
                        static_cast<double>(
                            existing.max_x
                        )
                    ) *
                    0.5;

                if (
                    std::abs(
                        existing_center_x -
                        expected_center_x
                    ) <=
                    occupancy_radius
                ) {

                    occupied =
                        true;

                    break;
                }
            }

            if (
                occupied
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // BOUNDED LOCAL INSPECTION WINDOW
            // -----------------------------------------------------------------

            const int x_radius =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            median_width *
                            CATEGORY_GAP_RECOVERY_X_RADIUS_FACTOR
                        )
                    ),
                    CATEGORY_GAP_RECOVERY_MIN_X_RADIUS,
                    CATEGORY_GAP_RECOVERY_MAX_X_RADIUS
                );

            const int y_radius =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            median_height *
                            CATEGORY_GAP_RECOVERY_Y_RADIUS_FACTOR
                        )
                    ),
                    CATEGORY_GAP_RECOVERY_MIN_Y_RADIUS,
                    CATEGORY_GAP_RECOVERY_MAX_Y_RADIUS
                );

            const int roi_center_x =
                static_cast<int>(
                    std::llround(
                        expected_center_x
                    )
                );

            const int roi_center_y =
                static_cast<int>(
                    std::llround(
                        median_center_y
                    )
                );

            const int roi_min_x =
                std::max(
                    0,
                    roi_center_x -
                        x_radius
                );

            const int roi_max_x =
                std::min(
                    image_width - 1,
                    roi_center_x +
                        x_radius
                );

            const int roi_min_y =
                std::max(
                    0,
                    roi_center_y -
                        y_radius
                );

            const int roi_max_y =
                std::min(
                    image_height - 1,
                    roi_center_y +
                        y_radius
                );

            if (
                roi_min_x > roi_max_x ||
                roi_min_y > roi_max_y
            ) {
                continue;
            }

            const int roi_width =
                roi_max_x -
                roi_min_x +
                1;

            const int roi_height =
                roi_max_y -
                roi_min_y +
                1;

            if (
                roi_width <= 0 ||
                roi_height <= 0
            ) {
                continue;
            }

            const std::size_t roi_pixels =
                static_cast<std::size_t>(
                    roi_width
                ) *
                static_cast<std::size_t>(
                    roi_height
                );

            // -----------------------------------------------------------------
            // LOCAL FOREGROUND MASK
            // -----------------------------------------------------------------

            std::vector<std::uint8_t> mask(
                roi_pixels,
                std::uint8_t{0}
            );

            std::vector<std::uint16_t> row_counts(
                static_cast<std::size_t>(
                    roi_height
                ),
                std::uint16_t{0}
            );

            std::vector<std::uint16_t> column_counts(
                static_cast<std::size_t>(
                    roi_width
                ),
                std::uint16_t{0}
            );

            std::size_t raw_active_pixels =
                0u;

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                const int image_y =
                    roi_min_y +
                    local_y;

                const std::size_t row_offset =
                    static_cast<std::size_t>(
                        local_y
                    ) *
                    static_cast<std::size_t>(
                        roi_width
                    );

                for (
                    int local_x = 0;
                    local_x < roi_width;
                    ++local_x
                ) {

                    const int image_x =
                        roi_min_x +
                        local_x;

                    const bool active =
                        geometry::chart_foreground(
                            chart_buffer,
                            image_width,
                            image_x,
                            image_y
                        ) >=
                        minimum_foreground;

                    if (
                        !active
                    ) {
                        continue;
                    }

                    mask[
                        row_offset +
                        static_cast<std::size_t>(
                            local_x
                        )
                    ] =
                        1u;

                    ++row_counts[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ];

                    ++column_counts[
                        static_cast<std::size_t>(
                            local_x
                        )
                    ];

                    ++raw_active_pixels;
                }
            }

            if (
                raw_active_pixels <
                CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // REMOVE DOMINANT CHART STROKES
            // -----------------------------------------------------------------

            std::vector<std::uint8_t> dominant_rows(
                static_cast<std::size_t>(
                    roi_height
                ),
                std::uint8_t{0}
            );

            std::vector<std::uint8_t> dominant_columns(
                static_cast<std::size_t>(
                    roi_width
                ),
                std::uint8_t{0}
            );

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                const double occupancy =
                    static_cast<double>(
                        row_counts[
                            static_cast<std::size_t>(
                                local_y
                            )
                        ]
                    ) /
                    static_cast<double>(
                        roi_width
                    );

                if (
                    occupancy >=
                    CATEGORY_GAP_RECOVERY_DOMINANT_ROW_RATIO
                ) {

                    dominant_rows[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ] =
                        1u;
                }
            }

            for (
                int local_x = 0;
                local_x < roi_width;
                ++local_x
            ) {

                const double occupancy =
                    static_cast<double>(
                        column_counts[
                            static_cast<std::size_t>(
                                local_x
                            )
                        ]
                    ) /
                    static_cast<double>(
                        roi_height
                    );

                if (
                    occupancy >=
                    CATEGORY_GAP_RECOVERY_DOMINANT_COLUMN_RATIO
                ) {

                    dominant_columns[
                        static_cast<std::size_t>(
                            local_x
                        )
                    ] =
                        1u;
                }
            }

            // -----------------------------------------------------------------
            // BUILD CLEANED BOUNDS
            // -----------------------------------------------------------------

            int min_x =
                roi_max_x +
                1;

            int min_y =
                roi_max_y +
                1;

            int max_x =
                roi_min_x -
                1;

            int max_y =
                roi_min_y -
                1;

            std::size_t clean_active_pixels =
                0u;

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                if (
                    dominant_rows[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ] != 0u
                ) {
                    continue;
                }

                const std::size_t row_offset =
                    static_cast<std::size_t>(
                        local_y
                    ) *
                    static_cast<std::size_t>(
                        roi_width
                    );

                for (
                    int local_x = 0;
                    local_x < roi_width;
                    ++local_x
                ) {

                    if (
                        dominant_columns[
                            static_cast<std::size_t>(
                                local_x
                            )
                        ] != 0u
                    ) {
                        continue;
                    }

                    if (
                        mask[
                            row_offset +
                            static_cast<std::size_t>(
                                local_x
                            )
                        ] == 0u
                    ) {
                        continue;
                    }

                    const int image_x =
                        roi_min_x +
                        local_x;

                    const int image_y =
                        roi_min_y +
                        local_y;

                    min_x =
                        std::min(
                            min_x,
                            image_x
                        );

                    min_y =
                        std::min(
                            min_y,
                            image_y
                        );

                    max_x =
                        std::max(
                            max_x,
                            image_x
                        );

                    max_y =
                        std::max(
                            max_y,
                            image_y
                        );

                    ++clean_active_pixels;
                }
            }

            // -----------------------------------------------------------------
            // CLEANED COMPONENT VALIDATION
            // -----------------------------------------------------------------

            if (
                clean_active_pixels <
                    CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS ||
                max_x < min_x ||
                max_y < min_y
            ) {
                continue;
            }

            const int candidate_width =
                max_x -
                min_x +
                1;

            const int candidate_height =
                max_y -
                min_y +
                1;

            if (
                candidate_width <
                    MIN_LABEL_WIDTH ||
                candidate_height <
                    MIN_LABEL_HEIGHT ||
                candidate_width >
                    MAX_CANDIDATE_WIDTH ||
                candidate_height >
                    MAX_CANDIDATE_HEIGHT
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // CENTER ALIGNMENT
            // -----------------------------------------------------------------

            const double candidate_center_x =
                (
                    static_cast<double>(
                        min_x
                    ) +
                    static_cast<double>(
                        max_x
                    )
                ) *
                0.5;

            const double center_offset =
                std::abs(
                    candidate_center_x -
                    expected_center_x
                );

            if (
                center_offset >
                static_cast<double>(
                    x_radius
                ) *
                CATEGORY_GAP_RECOVERY_MAX_CENTER_OFFSET_FACTOR
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // DENSITY
            // -----------------------------------------------------------------

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
                continue;
            }

            const double density =
                static_cast<double>(
                    clean_active_pixels
                ) /
                static_cast<double>(
                    area
                );

            if (
                !std::isfinite(
                    density
                ) ||
                density <
                    CATEGORY_GAP_RECOVERY_MIN_DENSITY ||
                density >
                    CATEGORY_GAP_RECOVERY_MAX_DENSITY
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // BUILD CANDIDATE
            // -----------------------------------------------------------------

            TextCandidate candidate{
                min_x,
                min_y,
                max_x,
                max_y,
                clean_active_pixels
            };

            // -----------------------------------------------------------------
            // FINAL CATEGORY GEOMETRY GATE
            // -----------------------------------------------------------------

            if (
                !acceptable_category_geometry(
                    candidate,
                    image_width,
                    image_height
                )
            ) {
                continue;
            }

            recovered.push_back(
                candidate
            );

            // -----------------------------------------------------------------
            // DIAGNOSTIC
            // -----------------------------------------------------------------

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "category_gap_recovered: "
                    "slot=%lld/%lld "
                    "expected_center_x=%.2f "
                    "x=%d..%d y=%d..%d "
                    "w=%d h=%d pixels=%zu "
                    "density=%.4f\n",
                    slot,
                    estimated_slot_count,
                    expected_center_x,
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y,
                    candidate_width,
                    candidate_height,
                    candidate.active_pixels,
                    density
                );
            }
        }
    }

    return recovered;
}

} // namespace fin_ocr::chart::label::category
