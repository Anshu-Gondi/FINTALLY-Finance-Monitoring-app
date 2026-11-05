#pragma once

#include <cstdint>
#include <vector>

#include "glyph_template.hpp"
#include "glyph_templates.hpp"

namespace fin_ocr {

class GlyphMatcher {
public:
    explicit GlyphMatcher(
        const GlyphTemplateTable& templates
    ) noexcept;

    [[nodiscard]]
    char match(
        const std::vector<uint8_t>& patch,
        int patch_w,
        int patch_h
    ) const;

private:
    const GlyphTemplateTable& templates_;
};

} // namespace fin_ocr
