#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::image {

void grayscale_to_binary(
    const uint8_t* grayscale,
    uint8_t* binary,
    std::size_t num_pixels
) noexcept;

void rgb_to_ocr_mask(
    const uint8_t* rgb,
    uint8_t* mask,
    std::size_t num_pixels
) noexcept;

void bgra_to_ocr_mask(
    const uint8_t* bgra,
    uint8_t* mask,
    std::size_t num_pixels
) noexcept;

void force_binary_inplace(
    uint8_t* buffer,
    std::size_t count
) noexcept;

} // namespace fin_ocr::image
