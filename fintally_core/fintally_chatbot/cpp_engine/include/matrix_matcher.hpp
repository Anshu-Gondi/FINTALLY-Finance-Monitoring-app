#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fin_ocr {

constexpr int GLYPH_GRID_SIZE = 16;
constexpr std::size_t GLYPH_BITS = 16 * 16;

struct GlyphTemplate {
    char character;
    std::array<uint16_t, GLYPH_GRID_SIZE> grid;
};

class MatrixMatcher {
public:
    MatrixMatcher();

    // Match one already-cropped binary/grayscale glyph.
    // Non-zero / sufficiently bright pixels are treated as foreground.
    char match_glyph(
        const std::vector<uint8_t>& cropped_patch,
        int patch_w,
        int patch_h
    ) const;

    // Recognize a horizontal text region.
    //
    // channels == 1:
    //     image[y * width + x]
    //
    // channels == 3:
    //     channel 1 is treated as the OCR mask/intensity channel.
    std::string recognize_line(
        const uint8_t* image,
        int img_width,
        int line_y_start,
        int line_y_end,
        int channels = 1
    ) const;

    // Scan a chart buffer for text lines.
    //
    // For a 3-channel input, channel 1 is the OCR mask channel.
    std::string recognize_chart_labels(
        const uint8_t* hsv_buffer,
        int width,
        int height
    ) const;

private:
    std::vector<GlyphTemplate> templates_;

    void load_default_templates();

    static uint16_t resize_row_to_16(
        const uint8_t* row,
        int width
    );

    static void normalize_to_grid(
        const std::vector<uint8_t>& patch,
        int patch_w,
        int patch_h,
        std::array<uint16_t, GLYPH_GRID_SIZE>& output
    );

    static float compute_match_score(
        const std::array<uint16_t, GLYPH_GRID_SIZE>& candidate,
        const std::array<uint16_t, GLYPH_GRID_SIZE>& target
    );

    static int popcount16(uint16_t value) noexcept;
};

} // namespace fin_ocr
