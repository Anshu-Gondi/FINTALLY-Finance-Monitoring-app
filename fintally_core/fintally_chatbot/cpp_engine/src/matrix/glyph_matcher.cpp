#include "fin_ocr/matrix/glyph_matcher.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/matrix/glyph_normalizer.hpp"
#include "fin_ocr/matrix/glyph_scorer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr {

// =============================================================================
// CONSTRUCTOR
// =============================================================================

GlyphMatcher::GlyphMatcher(
    const GlyphTemplateTable& templates
) noexcept
    : templates_(templates)
{
}

// =============================================================================
// GLYPH MATCHING
// =============================================================================

char GlyphMatcher::match(
    const std::vector<uint8_t>& cropped_patch,
    int patch_w,
    int patch_h
) const {

    // =========================================================================
    // INPUT VALIDATION
    // =========================================================================

    if (
        patch_w <= 0 ||
        patch_h <= 0 ||
        cropped_patch.empty()
    ) {
        return '?';
    }

    const std::size_t expected_size =
        static_cast<std::size_t>(
            patch_w
        ) *
        static_cast<std::size_t>(
            patch_h
        );

    if (
        cropped_patch.size() <
        expected_size
    ) {
        return '?';
    }

    // =========================================================================
    // NORMALIZE
    // =========================================================================

    std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    > candidate{};

    if (
        !GlyphNormalizer::normalize_to_grid(
            cropped_patch,
            patch_w,
            patch_h,
            candidate
        )
    ) {
        return '?';
    }

    // =========================================================================
    // CANDIDATE STRUCTURAL FEATURES
    // =========================================================================

    int foreground_pixels =
        0;

    std::array<
        int,
        GLYPH_GRID_SIZE
    > row_counts{};

    std::array<
        int,
        GLYPH_GRID_SIZE
    > column_counts{};

    int min_x =
        GLYPH_GRID_SIZE;

    int min_y =
        GLYPH_GRID_SIZE;

    int max_x =
        -1;

    int max_y =
        -1;

    for (
        int y = 0;
        y < GLYPH_GRID_SIZE;
        ++y
    ) {

        const uint16_t row =
            candidate[y];

        row_counts[y] =
            GlyphScorer::popcount16(
                row
            );

        foreground_pixels +=
            row_counts[y];

        for (
            int x = 0;
            x < GLYPH_GRID_SIZE;
            ++x
        ) {

            const uint16_t bit =
                static_cast<uint16_t>(
                    1u <<
                    (15 - x)
                );

            if (
                (row & bit) == 0
            ) {
                continue;
            }

            ++column_counts[x];

            min_x =
                std::min(
                    min_x,
                    x
                );

            max_x =
                std::max(
                    max_x,
                    x
                );

            min_y =
                std::min(
                    min_y,
                    y
                );

            max_y =
                std::max(
                    max_y,
                    y
                );
        }
    }

    if (
        foreground_pixels == 0 ||
        max_x < min_x ||
        max_y < min_y
    ) {
        return '?';
    }

    const float foreground_ratio =
        static_cast<float>(
            foreground_pixels
        ) /
        static_cast<float>(
            GLYPH_BITS
        );

    if (
        foreground_ratio <
        config::MIN_FOREGROUND_RATIO
    ) {
        return '?';
    }

    // =========================================================================
    // STRUCTURAL FEATURES
    // =========================================================================

    int dominant_column =
        0;

    int dominant_column_pixels =
        0;

    for (
        int x = 0;
        x < GLYPH_GRID_SIZE;
        ++x
    ) {

        if (
            column_counts[x] >
            dominant_column_pixels
        ) {

            dominant_column_pixels =
                column_counts[x];

            dominant_column =
                x;
        }
    }

    const float dominant_column_ratio =
        static_cast<float>(
            dominant_column_pixels
        ) /
        static_cast<float>(
            std::max(
                1,
                foreground_pixels
            )
        );

    int top_row_width =
        0;

    int bottom_row_width =
        0;

    for (
        int y = 0;
        y < 4;
        ++y
    ) {

        top_row_width =
            std::max(
                top_row_width,
                row_counts[y]
            );
    }

    for (
        int y = GLYPH_GRID_SIZE - 4;
        y < GLYPH_GRID_SIZE;
        ++y
    ) {

        if (
            y >= 0
        ) {

            bottom_row_width =
                std::max(
                    bottom_row_width,
                    row_counts[y]
                );
        }
    }

    const bool looks_like_one =
        dominant_column_pixels >= 8 &&
        dominant_column_ratio >= 0.35f &&
        std::abs(
            dominant_column -
            (
                GLYPH_GRID_SIZE / 2
            )
        ) <= 3 &&
        bottom_row_width >= 4;

    // =========================================================================
    // MATCH STRUCTURE
    // =========================================================================

    struct Match {
        char character = '?';

        float score = -1.0f;

        float raw_score = -1.0f;

        float profile = 0.0f;
    };

    Match best{};
    Match second{};
    Match best_digit{};

    float score_one =
        -1.0f;

    // =========================================================================
    // TEMPLATE LOOP
    // =========================================================================

    for (
        const GlyphTemplate& tpl :
        templates_
    ) {

        // ---------------------------------------------------------------------
        // BASE SHAPE SCORE
        // ---------------------------------------------------------------------

        const float base_score =
            GlyphScorer::compute_match_score(
                candidate,
                tpl.grid
            );

        float aligned_score =
            base_score;

        int best_dx =
            0;

        int best_dy =
            0;

        // ---------------------------------------------------------------------
        // SEARCH SMALL TRANSLATION
        // ---------------------------------------------------------------------

        for (
            int dy = -1;
            dy <= 1;
            ++dy
        ) {

            for (
                int dx = -1;
                dx <= 1;
                ++dx
            ) {

                const float score =
                    GlyphScorer::shifted_f1(
                        candidate,
                        tpl.grid,
                        dx,
                        dy
                    );

                if (
                    score >
                    aligned_score
                ) {

                    aligned_score =
                        score;

                    best_dx =
                        dx;

                    best_dy =
                        dy;
                }
            }
        }

        constexpr float SHIFT_GAIN_REQUIRED =
            0.04f;

        const bool use_shift =
            (
                aligned_score -
                base_score
            ) >=
            SHIFT_GAIN_REQUIRED;

        const float shape_score =
            use_shift
                ? aligned_score
                : base_score;

        // ---------------------------------------------------------------------
        // BUILD SHIFTED TEMPLATE PROFILES
        // ---------------------------------------------------------------------

        std::array<
            int,
            GLYPH_GRID_SIZE
        > template_columns{};

        std::array<
            int,
            GLYPH_GRID_SIZE
        > template_rows{};

        const int dx =
            use_shift
                ? best_dx
                : 0;

        const int dy =
            use_shift
                ? best_dy
                : 0;

        for (
            int y = 0;
            y < GLYPH_GRID_SIZE;
            ++y
        ) {

            const int ty =
                y - dy;

            if (
                ty < 0 ||
                ty >= GLYPH_GRID_SIZE
            ) {
                continue;
            }

            uint16_t row =
                tpl.grid[ty];

            if (
                dx > 0
            ) {

                row =
                    dx <
                        GLYPH_GRID_SIZE
                        ? static_cast<uint16_t>(
                              row >> dx
                          )
                        : uint16_t{0};

            } else if (
                dx < 0
            ) {

                const int shift =
                    -dx;

                row =
                    shift <
                        GLYPH_GRID_SIZE
                        ? static_cast<uint16_t>(
                              row << shift
                          )
                        : uint16_t{0};
            }

            template_rows[y] =
                GlyphScorer::popcount16(
                    row
                );

            for (
                int x = 0;
                x < GLYPH_GRID_SIZE;
                ++x
            ) {

                const uint16_t bit =
                    static_cast<uint16_t>(
                        1u <<
                        (15 - x)
                    );

                if (
                    (row & bit) != 0
                ) {
                    ++template_columns[x];
                }
            }
        }

        // ---------------------------------------------------------------------
        // PROFILE SCORE
        // ---------------------------------------------------------------------

        const float profile =
            0.6f *
            GlyphScorer::profile_score(
                column_counts,
                template_columns
            ) +

            0.4f *
            GlyphScorer::profile_score(
                row_counts,
                template_rows
            );

        // ---------------------------------------------------------------------
        // FINAL SCORE
        // ---------------------------------------------------------------------

        const float final_score =
            0.93f *
            shape_score +
            0.07f *
            profile;

        const Match current{
            tpl.character,
            final_score,
            shape_score,
            profile
        };

        // ---------------------------------------------------------------------
        // BEST / SECOND-BEST
        // ---------------------------------------------------------------------

        if (
            current.score >
            best.score
        ) {

            second =
                best;

            best =
                current;

        } else if (
            current.score >
            second.score
        ) {

            second =
                current;
        }

        // ---------------------------------------------------------------------
        // BEST DIGIT
        // ---------------------------------------------------------------------

        if (
            tpl.character >= '0' &&
            tpl.character <= '9' &&
            current.score >
                best_digit.score
        ) {

            best_digit =
                current;
        }

        // ---------------------------------------------------------------------
        // REMEMBER "1"
        // ---------------------------------------------------------------------

        if (
            tpl.character == '1'
        ) {

            score_one =
                current.score;
        }
    }

    // =========================================================================
    // LOW CONFIDENCE
    // =========================================================================

    if (
        best.character == '?' ||
        best.score <
            config::MIN_MATCH_SCORE
    ) {
        return '?';
    }

    const float margin =
        best.score -
        std::max(
            -1.0f,
            second.score
        );

    constexpr float WEAK_MATCH_MARGIN =
        0.015f;

    // =========================================================================
    // SPECIAL CASE: 1
    // =========================================================================

    if (
        looks_like_one &&
        score_one >= 0.0f
    ) {

        constexpr float ONE_OVERRIDE_MARGIN =
            0.08f;

        if (
            score_one +
                ONE_OVERRIDE_MARGIN >=
            best.score &&

            dominant_column_ratio >=
                0.38f &&

            bottom_row_width >=
                4
        ) {

            return '1';
        }
    }

    // =========================================================================
    // DIGIT PRIORITY
    // =========================================================================

    if (
        best_digit.character != '?' &&
        best_digit.score +
                0.015f >=
            best.score &&
        margin >=
            WEAK_MATCH_MARGIN
    ) {

        return best_digit.character;
    }

    // =========================================================================
    // AMBIGUITY GATE
    // =========================================================================

    if (
        margin <
        WEAK_MATCH_MARGIN
    ) {

        if (
            best.score >=
            0.42f
        ) {
            return best.character;
        }

        return '?';
    }

    return best.character;
}

} // namespace fin_ocr
