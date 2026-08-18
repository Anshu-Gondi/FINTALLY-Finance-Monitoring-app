#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace fin_ocr {

constexpr int GLYPH_GRID_SIZE = 16;

struct GlyphTemplate {
    char character;
    uint16_t grid[GLYPH_GRID_SIZE]; // 16 rows of 16-bit bitmask
};

class MatrixMatcher {
public:
    MatrixMatcher();

    // Normalizes a cropped patch and matches it against standard financial glyphs
    char match_glyph(const std::vector<uint8_t>& cropped_patch, int patch_w, int patch_h);

    // Bounding box scanning across binarized image buffers
    std::string recognize_line(const uint8_t* image, int img_width, int line_y_start, int line_y_end, int channels = 1);

    // Specialized extraction for financial charts (scans across isolated HSV/Color-separated chart buffers)
    std::string recognize_chart_labels(const uint8_t* hsv_buffer, int width, int height);

private:
    std::vector<GlyphTemplate> templates_;

    void load_default_templates();
    float compute_match_score(const uint16_t candidate[GLYPH_GRID_SIZE], const uint16_t target[GLYPH_GRID_SIZE]);
};

} // namespace fin_ocr
