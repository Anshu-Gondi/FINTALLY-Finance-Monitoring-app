#pragma once

#include <cstdint>

namespace fin_ocr::image {

[[nodiscard]]
uint8_t luminance_rgb(
    uint8_t r,
    uint8_t g,
    uint8_t b
) noexcept;

} // namespace fin_ocr::image
