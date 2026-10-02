#include "fin_ocr/chart/label/recognition/candidate_ocr.hpp"

#include "fin_ocr/line/line_recognizer.hpp"

#include "fin_ocr/chart/label/category/category_canonicalizer.hpp"
#include "fin_ocr/chart/label/category/category_scorer.hpp"
#include "fin_ocr/chart/label/category/category_sequence.hpp"
#include "fin_ocr/chart/label/category/category_zone.hpp"
#include "fin_ocr/chart/label/label_filter.hpp"
#include "fin_ocr/chart/label/recognition/ocr_buffer.hpp"
#include "fin_ocr/chart/label/text/text_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label::recognition {

namespace {

// =============================================================================
// CATEGORY ADMISSION
// =============================================================================
//
// Preserved from the legacy recognizer.
//
// =============================================================================

constexpr double MIN_CATEGORY_ZONE_SCORE =
    0.38;

constexpr std::size_t MIN_CATEGORY_SEQUENCE_SIZE =
    3u;

constexpr double MIN_CATEGORY_SEQUENCE_SCORE =
    0.52;

// =============================================================================
// SHORT OCR ADMISSION
// =============================================================================
//
// Preserved from the legacy recognizer.
//
// =============================================================================

constexpr float MIN_SINGLE_ALPHA_CONFIDENCE =
    0.78f;

constexpr float MIN_TWO_GLYPH_CONFIDENCE =
    0.68f;

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================
//
// Preserved from the current legacy implementation.
//
// =============================================================================

constexpr bool CHART_LABEL_DEBUG =
    true;

} // namespace

// =============================================================================
// RECOGNIZE CANDIDATE
// =============================================================================
//
// Pipeline:
//
//     geometry
//         ↓
//     category zone
//         ↓
//     category spatial score
//         ↓
//     category sequence score
//         ↓
//     OCR crop
//         ↓
//     OCR
//         ↓
//     canonicalization
//         ↓
//     text quality
//         ↓
//     density
//         ↓
//     confidence
//         ↓
//     character composition
//         ↓
//     short-label safeguards
//         ↓
//     geometry-like text rejection
//         ↓
//     DetectedLabel
//
// =============================================================================

[[nodiscard]]
bool recognize_candidate(
    const ::fin_ocr::LineRecognizer& line_recognizer,
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate,
    int max_band_height,
    const std::vector<TextCandidate>& category_candidates,
    const category::CategorySequenceStats& sequence_stats,
    DetectedLabel& output,
    CandidateRejectReason& reject_reason
)
{
    reject_reason =
        CandidateRejectReason::NONE;

    // =========================================================================
    // GEOMETRY
    // =========================================================================

    if (
        filter::acceptable_candidate_geometry(
            candidate,
            image_width,
            image_height,
            max_band_height
        ) == false
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY;

        return false;
    }

    // =========================================================================
    // SPATIAL ZONE GATE
    // =========================================================================

    const LabelZone zone =
        category::classify_label_zone(
            candidate,
            image_width,
            image_height
        );

    if (
        zone !=
        LabelZone::CATEGORY_AXIS
    ) {

        reject_reason =
            CandidateRejectReason::ZONE;

        return false;
    }

    // =========================================================================
    // CATEGORY SPATIAL SCORE
    // =========================================================================

    const double zone_score =
        category::category_candidate_score(
            candidate,
            category_candidates,
            image_width,
            image_height
        );

    if (
        zone_score <
        MIN_CATEGORY_ZONE_SCORE
    ) {

        reject_reason =
            CandidateRejectReason::ZONE_SCORE;

        return false;
    }

    // =========================================================================
    // CATEGORY SEQUENCE GATE
    // =========================================================================

    const double sequence_score =
        category::category_sequence_score(
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

        reject_reason =
            CandidateRejectReason::SEQUENCE;

        return false;
    }

    // =========================================================================
    // DETECTED CANDIDATE DIMENSIONS
    // =========================================================================

    const int candidate_width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int candidate_height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        candidate_width <= 0 ||
        candidate_height <= 1
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY;

        return false;
    }

    // =========================================================================
    // BUILD PADDED OCR BUFFER
    // =========================================================================

    const OcrBuffer ocr_buffer =
        build_candidate_buffer(
            chart_buffer,
            image_width,
            image_height,
            candidate
        );

    if (
        ocr_buffer.data.empty() ||
        ocr_buffer.width <= 0 ||
        ocr_buffer.height <= 1
    ) {

        reject_reason =
            CandidateRejectReason::EMPTY_CROP;

        return false;
    }

    // =========================================================================
    // OCR
    // =========================================================================

    std::string text =
        line_recognizer.recognize(
            ocr_buffer.data.data(),
            ocr_buffer.width,
            0,
            ocr_buffer.height,
            1
        );

    ::fin_ocr::chart::label::text::trim_text(
        text
    );

    if (
        text.empty()
    ) {

        reject_reason =
            CandidateRejectReason::OCR_EMPTY;

        return false;
    }

    // =========================================================================
    // CATEGORY LABEL CANONICALIZATION
    // =========================================================================

    const std::string original_ocr_text =
        text;

    const bool canonicalized =
        category::canonicalize_category_label(
            text
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        if (
            canonicalized
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "category_label_canonicalized: "
                "\"%s\" -> \"%s\"\n",
                original_ocr_text.c_str(),
                text.c_str()
            );
        }
    }

    // =========================================================================
    // BASIC TEXT QUALITY
    // =========================================================================

    const std::size_t glyph_count =
        ::fin_ocr::chart::label::text::
            count_glyphs(
                text
            );

    if (
        glyph_count == 0
    ) {

        reject_reason =
            CandidateRejectReason::TEXT_QUALITY;

        return false;
    }

    if (
        ::fin_ocr::chart::label::text::
            is_repeated_glyph_noise(
                text
            )
    ) {

        reject_reason =
            CandidateRejectReason::REPEATED_NOISE;

        return false;
    }

    if (
        filter::acceptable_label_text(
            text,
            glyph_count
        ) == false
    ) {

        reject_reason =
            CandidateRejectReason::TEXT_QUALITY;

        return false;
    }

    if (
        filter::reject_geometry_like_text(
            text
        )
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY_TEXT;

        return false;
    }

    // =========================================================================
    // CANDIDATE DENSITY
    // =========================================================================
    //
    // Density is computed from the detected candidate itself rather than the
    // padded OCR buffer.
    //
    // Padding therefore cannot artificially reduce the confidence.
    //
    // =========================================================================

    const std::size_t candidate_area =
        static_cast<std::size_t>(
            candidate_width
        ) *
        static_cast<std::size_t>(
            candidate_height
        );

    if (
        candidate_area == 0
    ) {

        reject_reason =
            CandidateRejectReason::DENSITY;

        return false;
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

        reject_reason =
            CandidateRejectReason::DENSITY;

        return false;
    }

    // =========================================================================
    // OCR CONFIDENCE
    // =========================================================================

    const float confidence =
        filter::label_confidence(
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

        reject_reason =
            CandidateRejectReason::CONFIDENCE;

        return false;
    }

    // =========================================================================
    // CHARACTER COMPOSITION
    // =========================================================================

    std::size_t alphabetic_count =
        0u;

    std::size_t digit_count =
        0u;

    std::size_t punctuation_count =
        0u;

    std::size_t other_count =
        0u;

    for (
        const char c :
        text
    ) {

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n'
        ) {

            continue;
        }

        if (
            ::fin_ocr::chart::label::text::
                is_ascii_alpha(
                    c
                )
        ) {

            ++alphabetic_count;

        } else if (
            ::fin_ocr::chart::label::text::
                is_ascii_digit(
                    c
                )
        ) {

            ++digit_count;

        } else if (
            ::fin_ocr::chart::label::text::
                is_ascii_punctuation(
                    c
                )
        ) {

            ++punctuation_count;

        } else {

            ++other_count;
        }
    }

    const std::size_t classified_count =
        alphabetic_count +
        digit_count +
        punctuation_count +
        other_count;

    if (
        classified_count == 0u
    ) {

        reject_reason =
            CandidateRejectReason::COMPOSITION;

        return false;
    }

    // =========================================================================
    // SINGLE GLYPH
    // =========================================================================

    if (
        glyph_count == 1u
    ) {

        if (
            alphabetic_count != 1u ||
            digit_count != 0u ||
            punctuation_count != 0u ||
            other_count != 0u
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        if (
            confidence <
            MIN_SINGLE_ALPHA_CONFIDENCE
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        const double aspect_ratio =
            static_cast<double>(
                candidate_width
            ) /
            static_cast<double>(
                candidate_height
            );

        if (
            aspect_ratio > 3.5
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        if (
            static_cast<double>(
                candidate.active_pixels
            ) < 4.0
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }
    }

    // =========================================================================
    // TWO GLYPHS
    // =========================================================================

    if (
        glyph_count == 2u
    ) {

        const bool alpha_alpha =
            alphabetic_count == 2u &&
            digit_count == 0u &&
            punctuation_count == 0u &&
            other_count == 0u;

        const bool alpha_numeric =
            alphabetic_count == 1u &&
            digit_count == 1u &&
            punctuation_count == 0u &&
            other_count == 0u;

        if (
            !alpha_alpha &&
            !alpha_numeric
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        if (
            confidence <
            MIN_TWO_GLYPH_CONFIDENCE
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        const double aspect_ratio =
            static_cast<double>(
                candidate_width
            ) /
            static_cast<double>(
                candidate_height
            );

        if (
            aspect_ratio > 6.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        const double pixels_per_glyph =
            static_cast<double>(
                candidate.active_pixels
            ) /
            2.0;

        if (
            pixels_per_glyph < 3.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        // Mixed alpha/numeric labels need additional occupancy.
        if (
            alpha_numeric &&
            pixels_per_glyph < 8.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        if (
            alpha_numeric &&
            candidate_width < 14
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }
    }

    // =========================================================================
    // THREE GLYPHS
    // =========================================================================

    if (
        glyph_count == 3u
    ) {

        const bool all_alpha =
            alphabetic_count == 3u &&
            digit_count == 0u &&
            punctuation_count == 0u;

        const bool mixed_alpha_numeric =
            alphabetic_count > 0u &&
            digit_count > 0u;

        // ---------------------------------------------------------------------
        // Pure three-digit fragments are treated as suspicious.
        // ---------------------------------------------------------------------

        if (
            digit_count == 3u
        ) {

            reject_reason =
                CandidateRejectReason::THREE_GLYPH;

            return false;
        }

        // ---------------------------------------------------------------------
        // Excessive punctuation.
        // ---------------------------------------------------------------------

        if (
            punctuation_count >= 2u
        ) {

            reject_reason =
                CandidateRejectReason::THREE_GLYPH;

            return false;
        }

        // ---------------------------------------------------------------------
        // Alphabetic three-glyph labels.
        // ---------------------------------------------------------------------

        if (
            all_alpha
        ) {

            if (
                candidate_width < 14 ||
                candidate_height < 7
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            const double aspect_ratio =
                static_cast<double>(
                    candidate_width
                ) /
                static_cast<double>(
                    candidate_height
                );

            if (
                aspect_ratio > 7.0
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            const double pixels_per_glyph =
                static_cast<double>(
                    candidate.active_pixels
                ) /
                3.0;

            if (
                pixels_per_glyph < 4.0
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                confidence < 0.70f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                density < 0.08f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }
        }

        // ---------------------------------------------------------------------
        // Mixed alpha/numeric three-glyph labels.
        // ---------------------------------------------------------------------

        if (
            mixed_alpha_numeric
        ) {

            if (
                confidence < 0.60f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                candidate_width < 12 ||
                candidate_height < 6
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }
        }
    }

    // =========================================================================
    // PUNCTUATION
    // =========================================================================

    if (
        punctuation_count > 0u &&
        punctuation_count >=
            alphabetic_count +
            digit_count
    ) {

        reject_reason =
            CandidateRejectReason::PUNCTUATION;

        return false;
    }

    // =========================================================================
    // OTHER CHARACTERS
    // =========================================================================

    if (
        other_count > 0u
    ) {

        const std::size_t normal_text_count =
            alphabetic_count +
            digit_count;

        if (
            normal_text_count == 0u ||
            other_count >
                normal_text_count
        ) {

            reject_reason =
                CandidateRejectReason::OTHER_CHARS;

            return false;
        }
    }

    // =========================================================================
    // NUMERIC-ONLY SHORT FRAGMENTS
    // =========================================================================

    if (
        alphabetic_count == 0u &&
        digit_count > 0u
    ) {

        if (
            glyph_count <= 3u
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }

        if (
            candidate_width < 18
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }

        if (
            punctuation_count > 1u
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }
    }

    // =========================================================================
    // LONG GEOMETRY
    // =========================================================================

    const double width_ratio =
        image_width > 0
            ? static_cast<double>(
                  candidate_width
              ) /
              static_cast<double>(
                  image_width
              )
            : 1.0;

    if (
        width_ratio >= 0.20 &&
        density >= 0.06 &&
        glyph_count > 8u
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    if (
        width_ratio >= 0.35 &&
        glyph_count <= 6u
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    const double aspect_ratio =
        candidate_height > 0
            ? static_cast<double>(
                  candidate_width
              ) /
              static_cast<double>(
                  candidate_height
              )
            : 0.0;

    if (
        aspect_ratio > 45.0 &&
        glyph_count <= 6u
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    // =========================================================================
    // SHORT-TEXT OCCUPANCY
    // =========================================================================

    if (
        glyph_count <= 3u
    ) {

        const double pixels_per_glyph =
            static_cast<double>(
                candidate.active_pixels
            ) /
            static_cast<double>(
                glyph_count
            );

        if (
            pixels_per_glyph < 3.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }

        if (
            glyph_count == 3u &&
            pixels_per_glyph < 4.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }

        if (
            aspect_ratio > 8.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }
    }

    // =========================================================================
    // BUILD STRUCTURED LABEL
    // =========================================================================

    ::fin_ocr::chart::association::ChartLabel label{};

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
        ::fin_ocr::chart::association::ChartLabelKind::UNKNOWN;

    label.series_index =
        -1;

    label.category_index =
        -1;

    // =========================================================================
    // COMBINE RECOGNITION + STRUCTURAL CONFIDENCE
    // =========================================================================
    //
    // OCR confidence remains dominant.
    //
    // Spatial and sequence scores only influence the final confidence; they
    // never fabricate text.
    //
    // =========================================================================

    label.confidence =
        static_cast<float>(
            std::clamp(
                (
                    static_cast<double>(
                        confidence
                    ) *
                    0.65
                ) +
                (
                    zone_score *
                    0.15
                ) +
                (
                    sequence_score *
                    0.20
                ),
                0.0,
                1.0
            )
        );

    output.label =
        std::move(
            label
        );

    output.density =
        density;

    output.glyph_count =
        glyph_count;

    return true;
}

} // namespace fin_ocr::chart::label::recognition
