#include "fin_ocr/image/binary.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <cstdint>
#include <cstddef>

namespace fin_ocr::image {

// =============================================================================
// GRAYSCALE -> BINARY
//
// Public MatrixMatcher / binary representation:
//
//     dark text       -> 255
//     light background -> 0
//
// The threshold is centralized in ocr_config.hpp.
// =============================================================================

void grayscale_to_binary(
    const uint8_t* grayscale,
    uint8_t* binary,
    std::size_t num_pixels
) noexcept {

    if (
        grayscale == nullptr ||
        binary == nullptr ||
        num_pixels == 0
    ) {
        return;
    }

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        binary[i] =
            grayscale[i] <
                config::MATRIX_BINARY_THRESHOLD
                ? uint8_t{255}
                : uint8_t{0};
    }
}

// =============================================================================
// RGB -> OCR MASK
//
// Converts RGB directly into the canonical binary OCR representation:
//
//     dark text       -> 255
//     light background -> 0
//
// Luminance conversion intentionally matches the legacy pipeline.
// =============================================================================

void rgb_to_ocr_mask(
    const uint8_t* rgb,
    uint8_t* mask,
    std::size_t num_pixels
) noexcept {

    if (
        rgb == nullptr ||
        mask == nullptr ||
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

        const uint8_t gray =
            static_cast<uint8_t>(
                (
                    77u *
                        static_cast<unsigned>(
                            rgb[index + 0]
                        ) +
                    150u *
                        static_cast<unsigned>(
                            rgb[index + 1]
                        ) +
                    29u *
                        static_cast<unsigned>(
                            rgb[index + 2]
                        )
                ) >> 8
            );

        mask[i] =
            gray <
                config::MATRIX_BINARY_THRESHOLD
                ? uint8_t{255}
                : uint8_t{0};
    }
}

// =============================================================================
// BGRA -> OCR MASK
//
// PDFium produces BGRA:
//
//     [B][G][R][A]
//
// Alpha is intentionally ignored because OCR is based on luminance.
// =============================================================================

void bgra_to_ocr_mask(
    const uint8_t* bgra,
    uint8_t* mask,
    std::size_t num_pixels
) noexcept {

    if (
        bgra == nullptr ||
        mask == nullptr ||
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

        const uint8_t gray =
            static_cast<uint8_t>(
                (
                    77u *
                        static_cast<unsigned>(
                            bgra[index + 2]
                        ) +
                    150u *
                        static_cast<unsigned>(
                            bgra[index + 1]
                        ) +
                    29u *
                        static_cast<unsigned>(
                            bgra[index + 0]
                        )
                ) >> 8
            );

        mask[i] =
            gray <
                config::MATRIX_BINARY_THRESHOLD
                ? uint8_t{255}
                : uint8_t{0};
    }
}

// =============================================================================
// FORCE BINARY IN PLACE
//
// Used for legacy/fallback buffers:
//
//     >= 128 -> 255
//     <  128 -> 0
//
// This threshold intentionally remains separate from
// MATRIX_BINARY_THRESHOLD because the original pipeline used 128 here.
// =============================================================================

void force_binary_inplace(
    uint8_t* buffer,
    std::size_t count
) noexcept {

    if (
        buffer == nullptr ||
        count == 0
    ) {
        return;
    }

    for (
        std::size_t i = 0;
        i < count;
        ++i
    ) {

        buffer[i] =
            buffer[i] >= 128
                ? uint8_t{255}
                : uint8_t{0};
    }
}

} // namespace fin_ocr::image
