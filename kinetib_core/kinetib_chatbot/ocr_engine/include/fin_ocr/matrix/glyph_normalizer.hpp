#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "glyph_template.hpp"

namespace fin_ocr {

class GlyphNormalizer {
public:
    [[nodiscard]]
    static bool normalize_to_grid(
        const std::vector<uint8_t>& patch,
        int patch_w,
        int patch_h,
        std::array<
            uint16_t,
            GLYPH_GRID_SIZE
        >& output
    ) noexcept;
};

} // namespace fin_ocr
