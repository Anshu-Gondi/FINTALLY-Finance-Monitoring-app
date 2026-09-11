#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fin_ocr {

class MatrixMatcher {
public:
    MatrixMatcher();
    ~MatrixMatcher();

    MatrixMatcher(const MatrixMatcher&);
    MatrixMatcher& operator=(const MatrixMatcher&);

    MatrixMatcher(MatrixMatcher&&) noexcept;
    MatrixMatcher& operator=(MatrixMatcher&&) noexcept;

    // Match one already-cropped binary/grayscale glyph.
    //
    // Non-zero / sufficiently bright pixels are treated as foreground.
    [[nodiscard]]
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
    [[nodiscard]]
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
    [[nodiscard]]
    std::string recognize_chart_labels(
        const uint8_t* hsv_buffer,
        int width,
        int height
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fin_ocr
