#include "fin_ocr/matrix/glyph_scorer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fin_ocr {

// =============================================================================
// FAST BIT COUNTING
// =============================================================================

int GlyphScorer::popcount16(
    uint16_t value
) noexcept {

#if defined(__GNUC__) || defined(__clang__)

    return __builtin_popcount(
        static_cast<unsigned int>(value)
    );

#elif defined(_MSC_VER)

    return __popcnt16(value);

#else

    int count = 0;

    while (value != 0) {

        value &=
            static_cast<uint16_t>(
                value - 1
            );

        ++count;
    }

    return count;

#endif
}

// =============================================================================
// MATCH SCORE
// =============================================================================

float GlyphScorer::compute_match_score(
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& candidate,
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& target
) noexcept {

    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;

    int candidate_pixels = 0;
    int target_pixels = 0;

    for (
        int row = 0;
        row < GLYPH_GRID_SIZE;
        ++row
    ) {

        const uint16_t c =
            candidate[row];

        const uint16_t t =
            target[row];

        const uint16_t tp =
            static_cast<uint16_t>(
                c & t
            );

        const uint16_t fp =
            static_cast<uint16_t>(
                c &
                static_cast<uint16_t>(~t)
            );

        const uint16_t fn =
            static_cast<uint16_t>(
                t &
                static_cast<uint16_t>(~c)
            );

        true_positive +=
            popcount16(tp);

        false_positive +=
            popcount16(fp);

        false_negative +=
            popcount16(fn);

        candidate_pixels +=
            popcount16(c);

        target_pixels +=
            popcount16(t);
    }

    const int denominator =
        2 * true_positive +
        false_positive +
        false_negative;

    if (denominator == 0) {
        return 0.0f;
    }

    const float f1 =
        static_cast<float>(
            2 * true_positive
        ) /
        static_cast<float>(
            denominator
        );

    const int pixel_difference =
        std::abs(
            candidate_pixels -
            target_pixels
        );

    const int pixel_total =
        std::max(
            candidate_pixels,
            target_pixels
        );

    const float density_score =
        pixel_total == 0
            ? 0.0f
            : 1.0f -
              static_cast<float>(
                  pixel_difference
              ) /
              static_cast<float>(
                  pixel_total
              );

    return
        0.85f * f1 +
        0.15f * density_score;
}

// =============================================================================
// PROFILE SCORE
// =============================================================================
//
// Compares one-dimensional row/column occupancy profiles.
//
// The implementation is identical to the lambda currently embedded inside
// GlyphMatcher::match().
// =============================================================================

float GlyphScorer::profile_score(
    const std::array<
        int,
        GLYPH_GRID_SIZE
    >& candidate_profile,
    const std::array<
        int,
        GLYPH_GRID_SIZE
    >& target_profile
) noexcept {

    int max_value = 0;

    for (
        int i = 0;
        i < GLYPH_GRID_SIZE;
        ++i
    ) {

        max_value =
            std::max(
                max_value,
                std::max(
                    candidate_profile[i],
                    target_profile[i]
                )
            );
    }

    if (max_value <= 0) {
        return 0.0f;
    }

    int error = 0;

    for (
        int i = 0;
        i < GLYPH_GRID_SIZE;
        ++i
    ) {

        error +=
            std::abs(
                candidate_profile[i] -
                target_profile[i]
            );
    }

    const int max_error =
        GLYPH_GRID_SIZE *
        max_value;

    return
        max_error == 0
            ? 0.0f
            : std::max(
                  0.0f,
                  1.0f -
                  static_cast<float>(
                      error
                  ) /
                  static_cast<float>(
                      max_error
                  )
              );
}

// =============================================================================
// SHIFTED F1
// =============================================================================
//
// Compares the candidate against a target after applying a small positional
// shift.
//
// The legacy implementation searches dx/dy in [-1, 1] and calls this function
// for every template.
// =============================================================================

float GlyphScorer::shifted_f1(
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& candidate,
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& target,
    int dx,
    int dy
) noexcept {

    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;

    for (
        int y = 0;
        y < GLYPH_GRID_SIZE;
        ++y
    ) {

        const int target_y =
            y - dy;

        if (
            target_y < 0 ||
            target_y >= GLYPH_GRID_SIZE
        ) {

            false_positive +=
                popcount16(
                    candidate[y]
                );

            continue;
        }

        uint16_t aligned = 0;

        if (dx >= 0) {

            aligned =
                dx < GLYPH_GRID_SIZE
                    ? static_cast<uint16_t>(
                          target[target_y] >>
                          dx
                      )
                    : uint16_t{0};

        } else {

            const int shift =
                -dx;

            aligned =
                shift < GLYPH_GRID_SIZE
                    ? static_cast<uint16_t>(
                          target[target_y] <<
                          shift
                      )
                    : uint16_t{0};
        }

        const uint16_t tp_bits =
            static_cast<uint16_t>(
                candidate[y] &
                aligned
            );

        const uint16_t fp_bits =
            static_cast<uint16_t>(
                candidate[y] &
                static_cast<uint16_t>(
                    ~aligned
                )
            );

        const uint16_t fn_bits =
            static_cast<uint16_t>(
                aligned &
                static_cast<uint16_t>(
                    ~candidate[y]
                )
            );

        true_positive +=
            popcount16(tp_bits);

        false_positive +=
            popcount16(fp_bits);

        false_negative +=
            popcount16(fn_bits);
    }

    const int denominator =
        2 * true_positive +
        false_positive +
        false_negative;

    if (denominator == 0) {
        return 0.0f;
    }

    return
        static_cast<float>(
            2 * true_positive
        ) /
        static_cast<float>(
            denominator
        );
}

} // namespace fin_ocr
