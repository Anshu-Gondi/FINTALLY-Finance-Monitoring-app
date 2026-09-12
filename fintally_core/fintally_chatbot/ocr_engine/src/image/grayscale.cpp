#include "fin_ocr/image/luminance.hpp"

#include "fin_ocr/image/grayscale.hpp"

#include <cstddef>
#include <cstdint>

namespace fin_ocr::image {

// =============================================================================
// RGB -> GRAYSCALE
//
// High-information OCR representation.
//
// RGB layout:
//
//     [R][G][B]
//
// Output:
//
//     one grayscale byte per pixel
//
// Tesseract consumes this representation.
// It must remain grayscale and must not be binarized here.
// =============================================================================

void rgb_to_grayscale(
    const uint8_t* rgb,
    uint8_t* grayscale,
    std::size_t num_pixels
) noexcept {

    if (
        rgb == nullptr ||
        grayscale == nullptr ||
        num_pixels == 0
    ) {
        return;
    }

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        const std::size_t index =
            i * 3u;

        grayscale[i] =
            luminance_rgb(
                rgb[index + 0],
                rgb[index + 1],
                rgb[index + 2]
            );
    }
}

// =============================================================================
// BGRA -> GRAYSCALE
//
// PDFium layout:
//
//     [B][G][R][A]
//
// Alpha is intentionally ignored because the OCR grayscale representation
// is derived from RGB luminance.
// =============================================================================

void bgra_to_grayscale(
    const uint8_t* bgra,
    uint8_t* grayscale,
    std::size_t num_pixels
) noexcept {

    if (
        bgra == nullptr ||
        grayscale == nullptr ||
        num_pixels == 0
    ) {
        return;
    }

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        const std::size_t index =
            i * 4u;

        grayscale[i] =
            luminance_rgb(
                bgra[index + 2], // R
                bgra[index + 1], // G
                bgra[index + 0]  // B
            );
    }
}

} // namespace fin_ocr::image
