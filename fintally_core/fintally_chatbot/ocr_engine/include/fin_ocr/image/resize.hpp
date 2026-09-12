#pragma once

#include <cstdint>

namespace fin_ocr::image {

void resize_grayscale_ocr(
    const uint8_t* source,
    int source_width,
    int source_height,
    uint8_t* destination,
    int target_width,
    int target_height
) noexcept;

void resize_rgb_nearest(
    const uint8_t* source,
    int source_width,
    int source_height,
    uint8_t* destination,
    int target_width,
    int target_height
) noexcept;

void resize_bgra_to_grayscale(
    const uint8_t* source_bgra,
    int source_width,
    int source_height,
    uint8_t* destination_gray,
    int target_width,
    int target_height
) noexcept;

} // namespace fin_ocr::image
