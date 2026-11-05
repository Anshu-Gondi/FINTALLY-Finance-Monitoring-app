#pragma once

#include <array>
#include <cstddef>

#include "glyph_template.hpp"

namespace fin_ocr {

inline constexpr std::size_t GLYPH_TEMPLATE_COUNT = 70;

using GlyphTemplateTable =
    std::array<
        GlyphTemplate,
        GLYPH_TEMPLATE_COUNT
    >;

[[nodiscard]]
const GlyphTemplateTable&
default_glyph_templates() noexcept;

} // namespace fin_ocr
