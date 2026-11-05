#include "fin_ocr/image/luminance.hpp"

#include <cstdint>

namespace fin_ocr::image {

// =============================================================================
// RGB -> LUMINANCE
//
// Integer approximation of:
//
//     0.299 * R + 0.587 * G + 0.114 * B
//
// Using:
//
//     77  ~= 0.299 * 256
//     150 ~= 0.587 * 256
//     29  ~= 0.114 * 256
//
// The >> 8 performs the final division by 256.
//
// This preserves the exact arithmetic behavior of the legacy implementation.
// =============================================================================

uint8_t luminance_rgb(
    uint8_t r,
    uint8_t g,
    uint8_t b
) noexcept {

    return static_cast<uint8_t>(
        (
            77u *
                static_cast<unsigned>(r) +

            150u *
                static_cast<unsigned>(g) +

            29u *
                static_cast<unsigned>(b)
        ) >> 8
    );
}

} // namespace fin_ocr::image
