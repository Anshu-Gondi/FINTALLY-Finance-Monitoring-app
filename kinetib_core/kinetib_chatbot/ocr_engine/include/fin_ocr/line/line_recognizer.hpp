#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fin_ocr/core/ocr_types.hpp"

namespace fin_ocr {

class GlyphMatcher;
class TesseractRecognizer;

class LineRecognizer {
public:
    LineRecognizer(
        const GlyphMatcher& glyph_matcher,
        const TesseractRecognizer& tesseract_recognizer
    ) noexcept;

    [[nodiscard]]
    std::string recognize(
        const uint8_t* image,
        int width,
        int y0,
        int y1,
        int channels
    ) const;

private:
    const GlyphMatcher& glyph_matcher_;
    const TesseractRecognizer& tesseract_recognizer_;

    [[nodiscard]]
    std::string recognize_matrix_fallback(
        const uint8_t* image,
        int width,
        int y0,
        int y1,
        int channels,
        const std::vector<BoundingBox>& char_boxes
    ) const;
};

} // namespace fin_ocr
