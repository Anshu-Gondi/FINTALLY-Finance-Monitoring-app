#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::image {

void rgb_to_grayscale(
    const uint8_t* rgb,
    uint8_t* grayscale,
    std::size_t num_pixels
) noexcept;

void bgra_to_grayscale(
    const uint8_t* bgra,
    uint8_t* grayscale,
    std::size_t num_pixels
) noexcept;

} // namespace fin_ocr::image
