#include "fin_ocr/chart/label/label_engine.hpp"

#include "fin_ocr/chart/label/category/category_canonicalizer.hpp"
#include "fin_ocr/chart/label/category/category_recovery.hpp"
#include "fin_ocr/chart/label/category/category_scorer.hpp"
#include "fin_ocr/chart/label/category/category_sequence.hpp"
#include "fin_ocr/chart/label/category/category_zone.hpp"

#include "fin_ocr/chart/label/candidate/candidate_builder.hpp"

#include "fin_ocr/chart/label/label_filter.hpp"
#include "fin_ocr/chart/label/label_geometry.hpp"
#include "fin_ocr/chart/label/label_merger.hpp"

#include "fin_ocr/chart/label/recognition/candidate_ocr.hpp"

#include "fin_ocr/chart/label/text/text_utils.hpp"

#include "fin_ocr/chart/label/y_axis/y_axis_fragment_merger.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_ocr.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_parser.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_scorer.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_sequence.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_value_recovery.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include "fin_ocr/line/line_recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label {

namespace {

// =============================================================================
// LABEL ENGINE CONFIGURATION
// =============================================================================
//
// These values are retained from the legacy ChartLabelRecognizer orchestration
// layer. They are intentionally local while the modular migration is being
// completed.
//
// =============================================================================

constexpr std::uint8_t LABEL_FOREGROUND_THRESHOLD =
    20u;

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MAX_CANDIDATE_HEIGHT =
    32;

constexpr double MIN_CATEGORY_ZONE_SCORE =
    0.38;

constexpr std::size_t MIN_CATEGORY_SEQUENCE_SIZE =
    3u;

constexpr double MIN_CATEGORY_SEQUENCE_SCORE =
    0.52;

constexpr double MIN_Y_AXIS_SCORE =
    0.30;

constexpr std::size_t MIN_Y_AXIS_SEQUENCE_SIZE =
    3u;

constexpr double MIN_Y_AXIS_SEQUENCE_SCORE =
    0.45;

constexpr std::size_t MAX_Y_AXIS_TEXT_LENGTH =
    20u;

constexpr std::size_t MIN_LEFT_Y_NUMERIC_SUPPORT =
    2u;

constexpr bool CHART_LABEL_DEBUG =
    true;

} // namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

LabelEngine::LabelEngine(
    const ::fin_ocr::LineRecognizer& line_recognizer
) noexcept
    : line_recognizer_(
          line_recognizer
      )
{
}

// =============================================================================
// STRUCTURED CHART LABEL RECOGNITION
// =============================================================================
//
// High-level orchestration only.
//
// Pipeline:
//
//     chart buffer
//         ↓
//     candidate generation
//         ↓
//     global filtering
//         ↓
//     category pipeline
//         ↓
//     Y-axis pipelines
//         ↓
//     duplicate suppression
//         ↓
//     structured ChartLabel[]
//
// Formatting is intentionally NOT performed here.
//
// =============================================================================

[[nodiscard]]
std::vector<
    ::fin_ocr::chart::association::ChartLabel
>
LabelEngine::recognize_labels(
    const std::uint8_t* chart_buffer,
    int width,
    int height
) const
{
    using ::fin_ocr::chart::association::ChartLabel;
    using ::fin_ocr::chart::association::ChartLabelKind;

    using ::fin_ocr::chart::label::candidate::
        build_band_candidates;

    using ::fin_ocr::chart::label::category::
        CategorySequenceStats;

    using ::fin_ocr::chart::label::category::
        build_category_sequence_stats;

    using ::fin_ocr::chart::label::category::
        category_candidate_score;

    using ::fin_ocr::chart::label::category::
        category_sequence_score;

    using ::fin_ocr::chart::label::category::
        classify_label_zone;

    using ::fin_ocr::chart::label::category::
        recover_missing_category_candidates;

    using ::fin_ocr::chart::label::geometry::
        horizontal_overlap;

    using ::fin_ocr::chart::label::geometry::
        vertical_overlap;

    using ::fin_ocr::chart::label::merger::
        duplicate_label;

    using ::fin_ocr::chart::label::filter::
        acceptable_candidate_geometry;

    using ::fin_ocr::chart::label::filter::
        acceptable_label_text;

    using ::fin_ocr::chart::label::filter::
        candidate_reject_reason_string;

    using ::fin_ocr::chart::label::filter::
        label_confidence;

    using ::fin_ocr::chart::label::text::
        is_repeated_glyph_noise;

    using ::fin_ocr::chart::label::filter::
        reject_geometry_like_text;

    using ::fin_ocr::chart::label::recognition::
        recognize_candidate;

    using ::fin_ocr::chart::label::text::
        count_glyphs;

    using ::fin_ocr::chart::label::text::
        trim_text;

    using ::fin_ocr::chart::label::text::
        is_ascii_alpha;

    using ::fin_ocr::chart::label::text::
        is_ascii_digit;

    using ::fin_ocr::chart::label::text::
        is_ascii_punctuation;

    using ::fin_ocr::chart::label::y_axis::
        consolidate_y_axis_fragments;

    using ::fin_ocr::chart::label::y_axis::
        YAxisKind;

    using ::fin_ocr::chart::label::y_axis::
        YAxisSequenceStats;

    using ::fin_ocr::chart::label::y_axis::
        build_y_axis_sequence_stats;

    using ::fin_ocr::chart::label::y_axis::
        y_axis_candidate_score;

    using ::fin_ocr::chart::label::y_axis::
        y_axis_sequence_score;

    using ::fin_ocr::chart::label::y_axis::
        recognize_y_axis_text;

    using ::fin_ocr::chart::label::y_axis::
        recover_right_y_axis_decimal;

    using ::fin_ocr::chart::label::y_axis::
        reconcile_y_axis_values;

    using ::fin_ocr::chart::label::y_axis::
        y_axis_sequence_inlier_mask;

    using ::fin_ocr::chart::label::y_axis::
        parse_y_axis_numeric_value;

    using ::fin_ocr::chart::label::DetectedLabel;

    std::vector<ChartLabel> labels;

    // =========================================================================
    // INPUT VALIDATION
    // =========================================================================

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        height <= 0
    ) {
        return labels;
    }

    constexpr std::uint8_t MIN_FOREGROUND =
        LABEL_FOREGROUND_THRESHOLD;

    constexpr int MIN_COMPONENT_WIDTH =
        MIN_LABEL_WIDTH;

    constexpr std::size_t MAX_LABELS =
        config::CHART_MAX_LABELS;

    // =========================================================================
    // STEP 1: FULL-IMAGE COMPONENT SEGMENTATION
    // =========================================================================

    std::vector<TextCandidate> candidates =
        build_band_candidates(
            chart_buffer,
            width,
            0,
            height,
            MIN_FOREGROUND,
            MIN_COMPONENT_WIDTH
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[LabelEngine] "
            "full_image_component_segmentation: "
            "width=%d height=%d candidates=%zu\n",
            width,
            height,
            candidates.size()
        );
    }

    if (
        candidates.empty()
    ) {
        return labels;
    }

    // =========================================================================
    // STEP 2: GLOBAL GEOMETRY FILTER
    // =========================================================================

    std::vector<TextCandidate> filtered_candidates;

    filtered_candidates.reserve(
        std::min(
            candidates.size(),
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        )
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        if (
            !acceptable_candidate_geometry(
                candidate,
                width,
                height,
                MAX_CANDIDATE_HEIGHT
            )
        ) {
            continue;
        }

        filtered_candidates.push_back(
            candidate
        );
    }

    candidates.swap(
        filtered_candidates
    );

    if (
        candidates.empty()
    ) {

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[LabelEngine] "
                "no_candidates_after_global_filter\n"
            );
        }

        return labels;
    }

    // =========================================================================
    // STEP 3: ZONE DIAGNOSTICS
    // =========================================================================

    std::size_t left_y_axis_zone_candidates = 0u;
    std::size_t right_y_axis_zone_candidates = 0u;
    std::size_t category_zone_candidates = 0u;
    std::size_t title_zone_candidates = 0u;
    std::size_t plot_zone_candidates = 0u;
    std::size_t outside_zone_candidates = 0u;

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        const LabelZone zone =
            classify_label_zone(
                candidate,
                width,
                height
            );

        switch (
            zone
        ) {

            case LabelZone::LEFT_Y_AXIS:
                ++left_y_axis_zone_candidates;
                break;

            case LabelZone::RIGHT_Y_AXIS:
                ++right_y_axis_zone_candidates;
                break;

            case LabelZone::CATEGORY_AXIS:
                ++category_zone_candidates;
                break;

            case LabelZone::TITLE:
                ++title_zone_candidates;
                break;

            case LabelZone::PLOT_INTERIOR:
                ++plot_zone_candidates;
                break;

            case LabelZone::OUTSIDE:
                ++outside_zone_candidates;
                break;

            default:
                break;
        }
    }

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[LabelEngine] "
            "zone_summary: "
            "left_y=%zu "
            "right_y=%zu "
            "category=%zu "
            "title=%zu "
            "plot=%zu "
            "outside=%zu\n",
            left_y_axis_zone_candidates,
            right_y_axis_zone_candidates,
            category_zone_candidates,
            title_zone_candidates,
            plot_zone_candidates,
            outside_zone_candidates
        );
    }

    std::vector<DetectedLabel> detected;

    detected.reserve(
        std::min(
            MAX_LABELS,
            candidates.size()
        )
    );

    // =========================================================================
    // STEP 4: CATEGORY PIPELINE
    // =========================================================================

    std::vector<TextCandidate> category_candidates;

    std::size_t recovered_count =
        0u;

    CategorySequenceStats final_sequence_stats{};

    if (
        category_zone_candidates > 0u
    ) {

        category_candidates.reserve(
            std::min(
                category_zone_candidates,
                MAX_LABELS * 4u
            )
        );

        for (
            const TextCandidate& candidate :
            candidates
        ) {

            if (
                classify_label_zone(
                    candidate,
                    width,
                    height
                ) !=
                LabelZone::CATEGORY_AXIS
            ) {
                continue;
            }

            const double initial_score =
                category_candidate_score(
                    candidate,
                    candidates,
                    width,
                    height
                );

            if (
                initial_score <
                MIN_CATEGORY_ZONE_SCORE
            ) {
                continue;
            }

            category_candidates.push_back(
                candidate
            );
        }

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[LabelEngine] "
                "category_candidates=%zu\n",
                category_candidates.size()
            );
        }

        if (
            !category_candidates.empty()
        ) {

            const CategorySequenceStats initial_sequence_stats =
                build_category_sequence_stats(
                    category_candidates
                );

            // =================================================================
            // CATEGORY GAP RECOVERY
            // =================================================================

            const std::vector<TextCandidate>
                recovered_category_candidates =
                    recover_missing_category_candidates(
                        chart_buffer,
                        width,
                        height,
                        category_candidates,
                        initial_sequence_stats,
                        MIN_FOREGROUND
                    );

            for (
                const TextCandidate& recovered_candidate :
                recovered_category_candidates
            ) {

                if (
                    category_candidates.size() >=
                    static_cast<std::size_t>(
                        MAX_CANDIDATE_COMPONENTS
                    )
                ) {
                    break;
                }

                bool duplicate_geometry =
                    false;

                const double recovered_center_x =
                    (
                        static_cast<double>(
                            recovered_candidate.min_x
                        ) +
                        static_cast<double>(
                            recovered_candidate.max_x
                        )
                    ) *
                    0.5;

                for (
                    const TextCandidate& existing :
                    category_candidates
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
                        initial_sequence_stats
                            .median_center_spacing > 0.0 &&
                        std::abs(
                            existing_center_x -
                            recovered_center_x
                        ) <
                            initial_sequence_stats
                                .median_center_spacing *
                            0.35
                    ) {

                        duplicate_geometry =
                            true;

                        break;
                    }
                }

                if (
                    duplicate_geometry
                ) {
                    continue;
                }

                category_candidates.push_back(
                    recovered_candidate
                );

                ++recovered_count;
            }

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[LabelEngine] "
                    "category_gap_recovery: "
                    "recovered=%zu "
                    "candidates_after_recovery=%zu\n",
                    recovered_count,
                    category_candidates.size()
                );
            }

            // =================================================================
            // CATEGORY SEQUENCE MODEL
            // =================================================================

            const CategorySequenceStats sequence_stats =
                build_category_sequence_stats(
                    category_candidates
                );

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[LabelEngine] "
                    "category_sequence_stats: "
                    "count=%zu "
                    "coherent=%zu "
                    "median_y=%.2f "
                    "median_height=%.2f "
                    "median_width=%.2f "
                    "median_spacing=%.2f "
                    "recovered=%zu\n",
                    sequence_stats.candidate_count,
                    sequence_stats.coherent_count,
                    sequence_stats.median_center_y,
                    sequence_stats.median_height,
                    sequence_stats.median_width,
                    sequence_stats.median_center_spacing,
                    recovered_count
                );
            }

            // =================================================================
            // SEQUENCE FILTER
            // =================================================================

            std::vector<TextCandidate> sequence_candidates;

            sequence_candidates.reserve(
                category_candidates.size()
            );

            for (
                const TextCandidate& candidate :
                category_candidates
            ) {

                const double zone_score =
                    category_candidate_score(
                        candidate,
                        category_candidates,
                        width,
                        height
                    );

                const double sequence_score =
                    category_sequence_score(
                        candidate,
                        category_candidates,
                        sequence_stats
                    );

                if (
                    sequence_stats.coherent_count >=
                        MIN_CATEGORY_SEQUENCE_SIZE &&
                    sequence_score <
                        MIN_CATEGORY_SEQUENCE_SCORE
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        std::fprintf(
                            stderr,
                            "[LabelEngine] "
                            "sequence_candidate_rejected: "
                            "x=%d..%d y=%d..%d "
                            "zone=%.4f sequence=%.4f "
                            "reason=sequence\n",
                            candidate.min_x,
                            candidate.max_x,
                            candidate.min_y,
                            candidate.max_y,
                            zone_score,
                            sequence_score
                        );
                    }

                    continue;
                }

                sequence_candidates.push_back(
                    candidate
                );
            }

            category_candidates.swap(
                sequence_candidates
            );

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[LabelEngine] "
                    "category_candidates_after_sequence=%zu\n",
                    category_candidates.size()
                );
            }

            if (
                !category_candidates.empty()
            ) {

                final_sequence_stats =
                    build_category_sequence_stats(
                        category_candidates
                    );

                // -------------------------------------------------------------
                // Deterministic category order.
                // -------------------------------------------------------------

                std::sort(
                    category_candidates.begin(),
                    category_candidates.end(),
                    [](
                        const TextCandidate& a,
                        const TextCandidate& b
                    ) noexcept {

                        if (
                            a.min_x !=
                            b.min_x
                        ) {
                            return
                                a.min_x <
                                b.min_x;
                        }

                        if (
                            a.min_y !=
                            b.min_y
                        ) {
                            return
                                a.min_y <
                                b.min_y;
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

                // =============================================================
                // CATEGORY OCR
                // =============================================================

                std::size_t rejected_by_ocr =
                    0u;

                std::size_t rejected_by_duplicate =
                    0u;

                for (
                    const TextCandidate& candidate :
                    category_candidates
                ) {

                    if (
                        detected.size() >=
                        MAX_LABELS
                    ) {
                        break;
                    }

                    DetectedLabel recognized{};

                    CandidateRejectReason reject_reason =
                        CandidateRejectReason::NONE;

                    if (
                        !recognize_candidate(
                            line_recognizer_,
                            chart_buffer,
                            width,
                            height,
                            candidate,
                            MAX_CANDIDATE_HEIGHT,
                            category_candidates,
                            final_sequence_stats,
                            recognized,
                            reject_reason
                        )
                    ) {

                        ++rejected_by_ocr;

                        if constexpr (
                            CHART_LABEL_DEBUG
                        ) {

                            const double zone_score =
                                category_candidate_score(
                                    candidate,
                                    category_candidates,
                                    width,
                                    height
                                );

                            const double sequence_score =
                                category_sequence_score(
                                    candidate,
                                    category_candidates,
                                    final_sequence_stats
                                );

                            std::fprintf(
                                stderr,
                                "[LabelEngine] "
                                "category_candidate_rejected: "
                                "x=%d..%d y=%d..%d "
                                "zone=%.4f sequence=%.4f "
                                "reason=%s\n",
                                candidate.min_x,
                                candidate.max_x,
                                candidate.min_y,
                                candidate.max_y,
                                zone_score,
                                sequence_score,
                                candidate_reject_reason_string(
                                    reject_reason
                                )
                            );
                        }

                        continue;
                    }

                    if (
                        duplicate_label(
                            recognized,
                            detected
                        )
                    ) {

                        ++rejected_by_duplicate;
                        continue;
                    }

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        const double zone_score =
                            category_candidate_score(
                                candidate,
                                category_candidates,
                                width,
                                height
                            );

                        const double sequence_score =
                            category_sequence_score(
                                candidate,
                                category_candidates,
                                final_sequence_stats
                            );

                        std::fprintf(
                            stderr,
                            "[LabelEngine] "
                            "category_OCR_accepted: "
                            "x=%d..%d y=%d..%d "
                            "text=\"%s\" "
                            "confidence=%.4f density=%.4f "
                            "glyphs=%zu zone=%.4f sequence=%.4f\n",
                            candidate.min_x,
                            candidate.max_x,
                            candidate.min_y,
                            candidate.max_y,
                            recognized.label.text.c_str(),
                            static_cast<double>(
                                recognized.label.confidence
                            ),
                            static_cast<double>(
                                recognized.density
                            ),
                            recognized.glyph_count,
                            zone_score,
                            sequence_score
                        );
                    }

                    detected.push_back(
                        std::move(
                            recognized
                        )
                    );
                }

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    std::fprintf(
                        stderr,
                        "[LabelEngine] "
                        "category_recognition_summary: "
                        "candidates=%zu detected=%zu "
                        "rejected_by_ocr=%zu "
                        "rejected_by_duplicate=%zu\n",
                        category_candidates.size(),
                        detected.size(),
                        rejected_by_ocr,
                        rejected_by_duplicate
                    );
                }
            }
        }
    }

    // =========================================================================
    // STEP 5: Y-AXIS CANDIDATE POOLS
    // =========================================================================

    std::vector<TextCandidate> left_y_axis_candidates;
    std::vector<TextCandidate> right_y_axis_candidates;

    left_y_axis_candidates.reserve(
        left_y_axis_zone_candidates
    );

    right_y_axis_candidates.reserve(
        right_y_axis_zone_candidates
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        const LabelZone zone =
            classify_label_zone(
                candidate,
                width,
                height
            );

        if (
            zone ==
            LabelZone::LEFT_Y_AXIS
        ) {

            const double score =
                y_axis_candidate_score(
                    candidate,
                    width,
                    height,
                    YAxisKind::LEFT
                );

            if (
                score >=
                MIN_Y_AXIS_SCORE
            ) {

                left_y_axis_candidates.push_back(
                    candidate
                );
            }
        }

        if (
            zone ==
            LabelZone::RIGHT_Y_AXIS
        ) {

            const double score =
                y_axis_candidate_score(
                    candidate,
                    width,
                    height,
                    YAxisKind::RIGHT
                );

            if (
                score >=
                MIN_Y_AXIS_SCORE
            ) {

                right_y_axis_candidates.push_back(
                    candidate
                );
            }
        }
    }

    // =========================================================================
    // STEP 5A: Y-AXIS FRAGMENT CONSOLIDATION
    // =========================================================================
    //
    // Consolidation occurs before sequence modeling so one numeric tick is
    // represented by one TextCandidate rather than several glyph fragments.
    //
    // =========================================================================

    left_y_axis_candidates =
        consolidate_y_axis_fragments(
            chart_buffer,
            width,
            height,
            left_y_axis_candidates,
            MIN_FOREGROUND
        );

    right_y_axis_candidates =
        consolidate_y_axis_fragments(
            chart_buffer,
            width,
            height,
            right_y_axis_candidates,
            MIN_FOREGROUND
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[LabelEngine] "
            "y_axis_fragment_consolidation: "
            "left=%zu right=%zu\n",
            left_y_axis_candidates.size(),
            right_y_axis_candidates.size()
        );
    }

    // =========================================================================
    // Y-AXIS PROCESSOR
    // =========================================================================

    const auto process_y_axis =
        [&](
            std::vector<TextCandidate>& axis_candidates,
            YAxisKind axis,
            const char* axis_name
        ) {

        if (
            axis_candidates.empty() ||
            detected.size() >=
                MAX_LABELS
        ) {
            return;
        }

        const YAxisSequenceStats sequence_stats =
            build_y_axis_sequence_stats(
                axis_candidates
            );

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[LabelEngine] "
                "%s_y_axis_sequence_stats: "
                "count=%zu coherent=%zu "
                "median_x=%.2f "
                "median_height=%.2f "
                "median_spacing=%.2f\n",
                axis_name,
                sequence_stats.candidate_count,
                sequence_stats.coherent_count,
                sequence_stats.median_center_x,
                sequence_stats.median_height,
                sequence_stats.median_center_spacing
            );
        }

        std::sort(
            axis_candidates.begin(),
            axis_candidates.end(),
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

        std::size_t axis_detected =
            0u;

        std::vector<DetectedLabel> axis_results;

        axis_results.reserve(
            axis_candidates.size()
        );

        // =====================================================================
        // Y-AXIS OCR
        // =====================================================================

        for (
            const TextCandidate& candidate :
            axis_candidates
        ) {

            if (
                detected.size() >=
                MAX_LABELS
            ) {
                break;
            }

            const double spatial_score =
                y_axis_candidate_score(
                    candidate,
                    width,
                    height,
                    axis
                );

            const double sequence_score =
                y_axis_sequence_score(
                    candidate,
                    axis_candidates,
                    sequence_stats
                );

            if (
                sequence_stats.coherent_count >=
                    MIN_Y_AXIS_SEQUENCE_SIZE &&
                sequence_score <
                    MIN_Y_AXIS_SEQUENCE_SCORE
            ) {

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    std::fprintf(
                        stderr,
                        "[LabelEngine] "
                        "%s_y_axis_candidate_rejected: "
                        "x=%d..%d y=%d..%d "
                        "spatial=%.4f sequence=%.4f "
                        "reason=sequence\n",
                        axis_name,
                        candidate.min_x,
                        candidate.max_x,
                        candidate.min_y,
                        candidate.max_y,
                        spatial_score,
                        sequence_score
                    );
                }

                continue;
            }

            // =================================================================
            // MULTI-PASS Y-AXIS OCR
            // =================================================================

            std::string adaptive_original_text;
            std::string text;

            if (
                !recognize_y_axis_text(
                    line_recognizer_,
                    chart_buffer,
                    width,
                    height,
                    candidate,
                    axis,
                    adaptive_original_text,
                    text
                )
            ) {

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    std::fprintf(
                        stderr,
                        "[LabelEngine] "
                        "%s_y_axis_candidate_rejected: "
                        "x=%d..%d y=%d..%d "
                        "reason=ocr_empty_or_invalid\n",
                        axis_name,
                        candidate.min_x,
                        candidate.max_x,
                        candidate.min_y,
                        candidate.max_y
                    );
                }

                continue;
            }

            // =================================================================
            // RIGHT-AXIS DECIMAL RECOVERY
            // =================================================================

            if (
                axis ==
                YAxisKind::RIGHT
            ) {

                double original_numeric_value =
                    0.0;

                double recovered_numeric_value =
                    0.0;

                if (
                    recover_right_y_axis_decimal(
                        text,
                        axis_results,
                        candidate.min_y,
                        candidate.max_y,
                        original_numeric_value,
                        recovered_numeric_value
                    )
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        std::fprintf(
                            stderr,
                            "[LabelEngine] "
                            "right_y_axis_decimal_recovered: "
                            "\"%.10g%%\" -> \"%s\" "
                            "predicted=%.6f\n",
                            original_numeric_value,
                            text.c_str(),
                            recovered_numeric_value
                        );
                    }
                }
            }

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                if (
                    text !=
                    adaptive_original_text
                ) {

                    std::fprintf(
                        stderr,
                        "[LabelEngine] "
                        "%s_y_axis_canonicalized: "
                        "\"%s\" -> \"%s\"\n",
                        axis_name,
                        adaptive_original_text.c_str(),
                        text.c_str()
                    );
                }
            }

            // =================================================================
            // Y-AXIS TEXT VALIDATION
            // =================================================================

            const std::size_t glyph_count =
                count_glyphs(
                    text
                );

            if (
                glyph_count == 0u ||
                glyph_count >
                    MAX_Y_AXIS_TEXT_LENGTH
            ) {
                continue;
            }

            // Y-axis values use the dedicated parser/normalizer. The generic
            // category text gate is intentionally not authoritative here.
            (void)acceptable_label_text;

            const int candidate_width =
                candidate.max_x -
                candidate.min_x +
                1;

            const int candidate_height =
                candidate.max_y -
                candidate.min_y +
                1;

            const std::size_t candidate_area =
                static_cast<std::size_t>(
                    candidate_width
                ) *
                static_cast<std::size_t>(
                    candidate_height
                );

            if (
                candidate_area == 0u
            ) {
                continue;
            }

            const float density =
                static_cast<float>(
                    static_cast<double>(
                        candidate.active_pixels
                    ) /
                    static_cast<double>(
                        candidate_area
                    )
                );

            if (
                !std::isfinite(
                    static_cast<double>(
                        density
                    )
                ) ||
                density <= 0.0f
            ) {
                continue;
            }

            const float confidence =
                label_confidence(
                    density,
                    candidate_width,
                    candidate_height,
                    glyph_count
                );

            if (
                !std::isfinite(
                    static_cast<double>(
                        confidence
                    )
                ) ||
                confidence <= 0.0f
            ) {
                continue;
            }

            ChartLabel label{};

            label.text =
                std::move(
                    text
                );

            label.min_x =
                candidate.min_x;

            label.min_y =
                candidate.min_y;

            label.max_x =
                candidate.max_x;

            label.max_y =
                candidate.max_y;

            label.kind =
                ChartLabelKind::UNKNOWN;

            label.series_index =
                -1;

            label.category_index =
                -1;

            label.confidence =
                static_cast<float>(
                    std::clamp(
                        static_cast<double>(
                            confidence
                        ) *
                            0.70 +
                        spatial_score *
                            0.15 +
                        sequence_score *
                            0.15,
                        0.0,
                        1.0
                    )
                );

            DetectedLabel y_label{};

            y_label.label =
                std::move(
                    label
                );

            y_label.density =
                density;

            y_label.glyph_count =
                glyph_count;

            // ================================================================
            // DUPLICATE CHECK
            // ================================================================

            bool duplicate =
                false;

            for (
                const DetectedLabel& existing :
                detected
            ) {

                if (
                    existing.label.text ==
                        y_label.label.text &&
                    vertical_overlap(
                        existing.label.min_y,
                        existing.label.max_y,
                        y_label.label.min_y,
                        y_label.label.max_y
                    ) &&
                    horizontal_overlap(
                        existing.label.min_x,
                        existing.label.max_x,
                        y_label.label.min_x,
                        y_label.label.max_x
                    )
                ) {

                    duplicate =
                        true;

                    break;
                }
            }

            if (
                duplicate
            ) {
                continue;
            }

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[LabelEngine] "
                    "%s_y_axis_OCR_accepted: "
                    "x=%d..%d y=%d..%d "
                    "text=\"%s\" "
                    "confidence=%.4f density=%.4f "
                    "glyphs=%zu spatial=%.4f "
                    "sequence=%.4f\n",
                    axis_name,
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y,
                    y_label.label.text.c_str(),
                    static_cast<double>(
                        y_label.label.confidence
                    ),
                    static_cast<double>(
                        y_label.density
                    ),
                    y_label.glyph_count,
                    spatial_score,
                    sequence_score
                );
            }

            axis_results.push_back(
                std::move(
                    y_label
                )
            );
        }

        // =====================================================================
        // LEFT-AXIS NUMERIC SUPPORT GUARD
        // =====================================================================

        if (
            axis ==
            YAxisKind::LEFT
        ) {

            std::size_t numeric_support =
                0u;

            for (
                const DetectedLabel& axis_result :
                axis_results
            ) {

                double value =
                    0.0;

                if (
                    parse_y_axis_numeric_value(
                        axis_result.label.text,
                        axis,
                        value
                    )
                ) {

                    ++numeric_support;
                }
            }

            if (
                numeric_support <
                MIN_LEFT_Y_NUMERIC_SUPPORT
            ) {

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    for (
                        const DetectedLabel& axis_result :
                        axis_results
                    ) {

                        std::fprintf(
                            stderr,
                            "[LabelEngine] "
                            "%s_y_axis_isolated_numeric_rejected: "
                            "x=%d..%d y=%d..%d "
                            "text=\"%s\" "
                            "numeric_support=%zu "
                            "required=%zu "
                            "reason=insufficient_sequence_support\n",
                            axis_name,
                            axis_result.label.min_x,
                            axis_result.label.max_x,
                            axis_result.label.min_y,
                            axis_result.label.max_y,
                            axis_result.label.text.c_str(),
                            numeric_support,
                            MIN_LEFT_Y_NUMERIC_SUPPORT
                        );
                    }
                }

                axis_results.clear();
            }
        }

        // =====================================================================
        // SPATIAL VALUE RECONCILIATION
        // =====================================================================

        reconcile_y_axis_values(
            axis_candidates,
            axis,
            axis_results
        );

        // =====================================================================
        // ROBUST NUMERIC SEQUENCE VALIDATION
        // =====================================================================

        const std::vector<bool> inlier_mask =
            y_axis_sequence_inlier_mask(
                axis_results,
                axis
            );

        for (
            std::size_t i = 0u;
            i < axis_results.size();
            ++i
        ) {

            if (
                i >=
                inlier_mask.size()
            ) {
                break;
            }

            if (
                !inlier_mask[i]
            ) {

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    double value =
                        0.0;

                    if (
                        parse_y_axis_numeric_value(
                            axis_results[i].label.text,
                            axis,
                            value
                        )
                    ) {

                        std::fprintf(
                            stderr,
                            "[LabelEngine] "
                            "%s_y_axis_outlier_rejected: "
                            "x=%d..%d y=%d..%d "
                            "text=\"%s\" value=%.6f "
                            "reason=sequence_outlier\n",
                            axis_name,
                            axis_results[i].label.min_x,
                            axis_results[i].label.max_x,
                            axis_results[i].label.min_y,
                            axis_results[i].label.max_y,
                            axis_results[i].label.text.c_str(),
                            value
                        );
                    }
                }

                continue;
            }

            detected.push_back(
                std::move(
                    axis_results[i]
                )
            );

            ++axis_detected;
        }

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[LabelEngine] "
                "%s_y_axis_summary: "
                "candidates=%zu detected=%zu\n",
                axis_name,
                axis_candidates.size(),
                axis_detected
            );
        }
    };

    process_y_axis(
        left_y_axis_candidates,
        YAxisKind::LEFT,
        "left"
    );

    process_y_axis(
        right_y_axis_candidates,
        YAxisKind::RIGHT,
        "right"
    );

    // =========================================================================
    // STEP 6: GLOBAL DETERMINISTIC SORT
    // =========================================================================

    std::sort(
        detected.begin(),
        detected.end(),
        [](
            const DetectedLabel& a,
            const DetectedLabel& b
        ) noexcept {

            if (
                a.label.min_x !=
                b.label.min_x
            ) {
                return
                    a.label.min_x <
                    b.label.min_x;
            }

            if (
                a.label.min_y !=
                b.label.min_y
            ) {
                return
                    a.label.min_y <
                    b.label.min_y;
            }

            return
                a.label.text <
                b.label.text;
        }
    );

    // =========================================================================
    // STEP 7: STRUCTURED PUBLIC OUTPUT
    // =========================================================================
    //
    // No formatted text is constructed here.
    //
    // label_output.cpp owns that responsibility.
    //
    // =========================================================================

    labels.reserve(
        std::min(
            detected.size(),
            MAX_LABELS
        )
    );

    for (
        std::size_t i = 0u;
        i < detected.size() &&
        labels.size() <
            MAX_LABELS;
        ++i
    ) {

        labels.push_back(
            std::move(
                detected[i].label
            )
        );
    }

    // =========================================================================
    // FINAL DIAGNOSTICS
    // =========================================================================

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[LabelEngine] "
            "recognition_summary: "
            "all_candidates=%zu "
            "left_y_candidates=%zu "
            "right_y_candidates=%zu "
            "category_candidates=%zu "
            "detected=%zu "
            "final_labels=%zu\n",
            candidates.size(),
            left_y_axis_candidates.size(),
            right_y_axis_candidates.size(),
            category_candidates.size(),
            detected.size(),
            labels.size()
        );
    }

    return labels;
}

} // namespace fin_ocr::chart::label
