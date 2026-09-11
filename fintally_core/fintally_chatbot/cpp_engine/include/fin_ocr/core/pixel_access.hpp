#pragma once

#include <cstddef>
#include <cstdint>

#include "ocr_config.hpp"

namespace fin_ocr {

[[nodiscard]]
inline bool is_foreground_pixel(
    uint8_t value
) noexcept {
    return value > config::DEFAULT_THRESHOLD;
}

[[nodiscard]]
inline bool is_chart_foreground_pixel(
    uint8_t value
) noexcept {
    return value >= config::CHART_FOREGROUND_THRESHOLD;
}

[[nodiscard]]
inline bool is_foreground_for_channels(
    uint8_t value,
    int channels
) noexcept {
    return channels == 3
        ? is_chart_foreground_pixel(value)
        : is_foreground_pixel(value);
}

[[nodiscard]]
inline uint8_t ocr_pixel(
    const uint8_t* image,
    int width,
    int channels,
    int x,
    int y
) noexcept {
    const std::size_t pixel_index =
        (
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)
        ) *
        static_cast<std::size_t>(channels);

    if (channels == 1) {
        return image[pixel_index];
    }

    return image[pixel_index + 1];
}

} // namespace fin_ocr
