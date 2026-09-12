#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fin_ocr {

inline constexpr int GLYPH_GRID_SIZE = 16;
inline constexpr std::size_t GLYPH_BITS =
    static_cast<std::size_t>(GLYPH_GRID_SIZE) *
    static_cast<std::size_t>(GLYPH_GRID_SIZE);

struct GlyphTemplate {
    char character = '?';

    std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    > grid{};
};

} // namespace fin_ocr
