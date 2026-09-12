#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fin_ocr {

[[nodiscard]]
std::string clean_tesseract_text(
    const char* raw
);

[[nodiscard]]
bool find_ocr_bounds(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels,
    int& min_x,
    int& min_y,
    int& max_x,
    int& max_y
) noexcept;

[[nodiscard]]
bool looks_like_numeric_line(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels
) noexcept;

void upscale_ocr_region(
    const uint8_t* image,
    int source_width,
    int source_height,
    int channels,
    std::vector<uint8_t>& destination,
    int& destination_width,
    int& destination_height
);

} // namespace fin_ocr
