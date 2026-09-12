#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::image {

void normalize_grayscale_for_ocr(
    const uint8_t* input,
    uint8_t* output,
    std::size_t num_pixels
) noexcept;

} // namespace fin_ocr::image
