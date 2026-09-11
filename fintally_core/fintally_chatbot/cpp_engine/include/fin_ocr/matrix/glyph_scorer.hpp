#pragma once

#include <array>
#include <cstdint>

#include "glyph_template.hpp"

namespace fin_ocr {

class GlyphScorer {
public:
    [[nodiscard]]
    static int popcount16(
        uint16_t value
    ) noexcept;

    [[nodiscard]]
    static float compute_match_score(
        const std::array<
            uint16_t,
            GLYPH_GRID_SIZE
        >& candidate,
        const std::array<
            uint16_t,
            GLYPH_GRID_SIZE
        >& target
    ) noexcept;

    [[nodiscard]]
    static float shifted_f1(
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
    ) noexcept;

    [[nodiscard]]
    static float profile_score(
        const std::array<
            int,
            GLYPH_GRID_SIZE
        >& candidate_profile,
        const std::array<
            int,
            GLYPH_GRID_SIZE
        >& target_profile
    ) noexcept;
};

} // namespace fin_ocr
