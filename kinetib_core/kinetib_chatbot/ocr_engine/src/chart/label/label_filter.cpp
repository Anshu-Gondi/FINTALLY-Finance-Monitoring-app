#include "fin_ocr/chart/label/label_filter.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"
#include "fin_ocr/chart/label/text/text_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fin_ocr::chart::label::filter {

namespace {

// =============================================================================
// LOCAL LABEL DETECTION LIMITS
// =============================================================================
//
// These values are preserved from the legacy ChartLabelRecognizer
// implementation.
//
// They are intentionally local to the filtering subsystem for now.
// If these become shared by candidate/category/recognition modules, they
// should later move into the central OCR configuration layer.
// =============================================================================

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr int MAX_CANDIDATE_HEIGHT =
    32;

constexpr double MAX_CANDIDATE_WIDTH_RATIO =
    0.35;

constexpr double MAX_CANDIDATE_HEIGHT_RATIO =
    0.20;

constexpr double MIN_LABEL_DENSITY =
    0.010;

constexpr double MAX_WIDE_DENSE_RATIO =
    0.25;

constexpr double MAX_WIDE_DENSE_DENSITY =
    0.08;

} // namespace

// =============================================================================
// REJECTION REASON
// =============================================================================

[[nodiscard]]
const char* candidate_reject_reason_string(
    CandidateRejectReason reason
) noexcept
{
    switch (
        reason
    ) {

        case CandidateRejectReason::NONE:
            return "none";

        case CandidateRejectReason::GEOMETRY:
            return "geometry";

        case CandidateRejectReason::ZONE:
            return "zone";

        case CandidateRejectReason::ZONE_SCORE:
            return "zone_score";

        case CandidateRejectReason::SEQUENCE:
            return "sequence";

        case CandidateRejectReason::EMPTY_CROP:
            return "empty_crop";

        case CandidateRejectReason::OCR_EMPTY:
            return "ocr_empty";

        case CandidateRejectReason::REPEATED_NOISE:
            return "repeated_noise";

        case CandidateRejectReason::TEXT_QUALITY:
            return "text_quality";

        case CandidateRejectReason::GEOMETRY_TEXT:
            return "geometry_text";

        case CandidateRejectReason::DENSITY:
            return "density";

        case CandidateRejectReason::CONFIDENCE:
            return "confidence";

        case CandidateRejectReason::COMPOSITION:
            return "composition";

        case CandidateRejectReason::SINGLE_GLYPH:
            return "single_glyph";

        case CandidateRejectReason::TWO_GLYPH:
            return "two_glyph";

        case CandidateRejectReason::THREE_GLYPH:
            return "three_glyph";

        case CandidateRejectReason::PUNCTUATION:
            return "punctuation";

        case CandidateRejectReason::OTHER_CHARS:
            return "other_chars";

        case CandidateRejectReason::NUMERIC_ONLY:
            return "numeric_only";

        case CandidateRejectReason::LONG_GEOMETRY:
            return "long_geometry";

        case CandidateRejectReason::OCCUPANCY:
            return "occupancy";

        default:
            return "unknown";
    }
}

// =============================================================================
// TEXT QUALITY
// =============================================================================

[[nodiscard]]
bool acceptable_label_text(
    const std::string& text,
    std::size_t glyph_count
) noexcept
{
    if (
        text.empty() ||
        glyph_count == 0
    ) {
        return false;
    }

    std::size_t alphabetic =
        0;

    std::size_t digits =
        0;

    std::size_t punctuation =
        0;

    std::size_t whitespace =
        0;

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

            ++whitespace;

        } else if (
            text::is_ascii_alpha(c)
        ) {

            ++alphabetic;

        } else if (
            text::is_ascii_digit(c)
        ) {

            ++digits;

        } else if (
            text::is_ascii_punctuation(c)
        ) {

            ++punctuation;
        }
    }

    if (
        alphabetic == 0 &&
        digits == 0
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Single glyph
    // -------------------------------------------------------------------------

    if (
        glyph_count == 1
    ) {

        if (
            digits == 1 ||
            punctuation == 1
        ) {
            return false;
        }

        return
            alphabetic == 1;
    }

    // -------------------------------------------------------------------------
    // Two glyphs
    // -------------------------------------------------------------------------

    if (
        glyph_count == 2
    ) {

        if (
            alphabetic >= 1
        ) {
            return true;
        }

        if (
            digits ==
            glyph_count
        ) {
            return false;
        }

        return
            punctuation == 0;
    }

    // -------------------------------------------------------------------------
    // Numeric / punctuation-only fragments
    // -------------------------------------------------------------------------

    if (
        alphabetic == 0 &&
        punctuation > 0 &&
        digits <= 2
    ) {
        return false;
    }

    if (
        punctuation >
            alphabetic + 2 &&
        alphabetic == 0
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Alphabetic labels
    // -------------------------------------------------------------------------

    if (
        alphabetic > 0
    ) {
        return true;
    }

    // -------------------------------------------------------------------------
    // Numeric axis values
    // -------------------------------------------------------------------------

    return
        digits >= 3 &&
        punctuation <= 1 &&
        whitespace < glyph_count;
}

// =============================================================================
// CANDIDATE GEOMETRY FILTER
// =============================================================================

[[nodiscard]]
bool acceptable_candidate_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    int max_band_height
) noexcept
{
    if (
        !geometry::valid_candidate(
            candidate
        ) ||
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

    // -------------------------------------------------------------------------
    // Minimum geometry.
    // -------------------------------------------------------------------------

    if (
        width <
            MIN_LABEL_WIDTH ||
        height <
            MIN_LABEL_HEIGHT
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Maximum geometry.
    // -------------------------------------------------------------------------

    if (
        height >
            max_band_height ||
        height >
            MAX_CANDIDATE_HEIGHT
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Relative width.
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Relative height.
    // -------------------------------------------------------------------------

    const double height_ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio >
        MAX_CANDIDATE_HEIGHT_RATIO
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Density.
    // -------------------------------------------------------------------------

    const std::size_t area =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            height
        );

    if (
        area == 0
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
        density <
            MIN_LABEL_DENSITY
    ) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Wide dense geometry rejection.
    //
    // These structures are more likely chart primitives, borders, grid lines,
    // or other geometry than text.
    // -------------------------------------------------------------------------

    if (
        width_ratio >=
            MAX_WIDE_DENSE_RATIO &&
        density >=
            MAX_WIDE_DENSE_DENSITY
    ) {
        return false;
    }

    return true;
}

// =============================================================================
// LABEL CONFIDENCE
// =============================================================================

[[nodiscard]]
float label_confidence(
    float density,
    int width,
    int height,
    std::size_t glyph_count
) noexcept
{
    if (
        width <= 0 ||
        height <= 0 ||
        glyph_count == 0
    ) {
        return 0.0f;
    }

    const double density_score =
        std::clamp(
            static_cast<double>(
                density
            ) *
            18.0,
            0.0,
            1.0
        );

    const double width_score =
        std::clamp(
            static_cast<double>(
                width
            ) /
            32.0,
            0.0,
            1.0
        );

    const double height_score =
        std::clamp(
            static_cast<double>(
                height
            ) /
            12.0,
            0.0,
            1.0
        );

    const double glyph_score =
        std::clamp(
            static_cast<double>(
                glyph_count
            ) /
            8.0,
            0.0,
            1.0
        );

    return
        static_cast<float>(
            std::clamp(
                density_score *
                    0.45 +
                width_score *
                    0.15 +
                height_score *
                    0.15 +
                glyph_score *
                    0.25,
                0.0,
                1.0
            )
        );
}

// =============================================================================
// ASCII TEXT WORD-LIKE VALIDATION
// =============================================================================

[[nodiscard]]
bool reject_geometry_like_text(
    const std::string& text
) noexcept {

    if (
        text.empty()
    ) {

        return true;
    }

    const std::size_t glyph_count =
        text::count_glyphs(
            text
        );

    if (
        glyph_count == 0
    ) {

        return true;
    }

    std::size_t alpha =
        0;

    std::size_t digits =
        0;

    std::size_t punctuation =
        0;

    std::size_t other =
        0;

    std::size_t spaces =
        0;

    std::size_t longest_alpha_run =
        0;

    std::size_t current_alpha_run =
        0;

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

            ++spaces;

            current_alpha_run =
                0;

            continue;
        }

        if (
            text::is_ascii_alpha(c)
        ) {

            ++alpha;
            ++current_alpha_run;

            longest_alpha_run =
                std::max(
                    longest_alpha_run,
                    current_alpha_run
                );

        } else {

            current_alpha_run =
                0;
        }

        if (
            text::is_ascii_digit(c)
        ) {

            ++digits;

        } else if (
            text::is_ascii_punctuation(c)
        ) {

            ++punctuation;

        } else if (
            !text::is_ascii_alpha(c)
        ) {

            ++other;
        }
    }

    // =========================================================================
    // MOST COMMON CHARACTER
    // =========================================================================

    std::size_t most_common_count =
        0;

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

        std::size_t frequency =
            0;

        for (
            const char candidate :
            text
        ) {

            if (
                candidate ==
                c
            ) {

                ++frequency;
            }
        }

        most_common_count =
            std::max(
                most_common_count,
                frequency
            );
    }

    if (
        glyph_count >= 4 &&
        most_common_count * 100u >=
            55u *
            glyph_count
    ) {

        return true;
    }

    // =========================================================================
    // NUMERIC-ONLY GEOMETRY
    // =========================================================================

    if (
        alpha == 0 &&
        digits > 0
    ) {

        if (
            glyph_count > 8
        ) {

            return true;
        }

        if (
            digits * 100u >=
                75u *
                glyph_count &&
            glyph_count >= 4
        ) {

            return true;
        }
    }

    // =========================================================================
    // PUNCTUATION
    // =========================================================================

    if (
        punctuation >=
        alpha +
        digits
    ) {

        return true;
    }

    // =========================================================================
    // SYMBOL CONTAMINATION
    // =========================================================================

    if (
        other >
        alpha +
        digits
    ) {

        return true;
    }

    // =========================================================================
    // FRAGMENTED ALPHABETIC GARBAGE
    // =========================================================================

    if (
        alpha > 0 &&
        digits == 0 &&
        longest_alpha_run < 2 &&
        glyph_count >= 4
    ) {

        return true;
    }

    // =========================================================================
    // SHORT-TOKEN FRAGMENTATION
    // =========================================================================

    if (
        spaces > 0 &&
        glyph_count <= 12
    ) {

        std::size_t token_count =
            0;

        std::size_t short_token_count =
            0;

        std::size_t token_length =
            0;

        for (
            std::size_t i = 0;
            i <= text.size();
            ++i
        ) {

            const bool end =
                i == text.size();

            const char c =
                end
                    ? ' '
                    : text[i];

            const bool separator =
                c == ' ' ||
                c == '\t' ||
                c == '\r' ||
                c == '\n';

            if (
                separator
            ) {

                if (
                    token_length > 0
                ) {

                    ++token_count;

                    if (
                        token_length <= 2
                    ) {

                        ++short_token_count;
                    }

                    token_length =
                        0;
                }

            } else {

                ++token_length;
            }
        }

        if (
            token_count >= 3 &&
            short_token_count ==
                token_count
        ) {

            return true;
        }
    }

    return false;
}

} // namespace fin_ocr::chart::label::filter
