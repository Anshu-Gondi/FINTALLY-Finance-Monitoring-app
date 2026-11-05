#include "fin_ocr/image/contrast_normalizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fin_ocr::image {

// =============================================================================
// HISTOGRAM-BASED GRAYSCALE CONTRAST NORMALIZATION
//
// IMPORTANT:
//
// This never binarizes.
//
// It stretches the useful intensity range while preserving all 256 grayscale
// levels. This gives Tesseract better stroke/background separation without
// destroying anti-aliased edges.
//
// Pipeline:
//
//     input grayscale
//          |
//          v
//     256-bin histogram
//          |
//          v
//     low percentile / high percentile
//          |
//          v
//     contrast stretch
//          |
//          v
//     output grayscale
//
// Configuration:
//
//     config::GRAY_LOW_PERCENTILE
//     config::GRAY_HIGH_PERCENTILE
// =============================================================================

void normalize_grayscale_for_ocr(
    const uint8_t* input,
    uint8_t* output,
    std::size_t num_pixels
) noexcept {

    if (
        input == nullptr ||
        output == nullptr ||
        num_pixels == 0
    ) {
        return;
    }

    // =========================================================================
    // BUILD 8-BIT HISTOGRAM
    // =========================================================================

    std::array<
        std::size_t,
        256
    > histogram{};

    histogram.fill(0);

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        ++histogram[
            static_cast<std::size_t>(
                input[i]
            )
        ];
    }

    // =========================================================================
    // COMPUTE TARGET HISTOGRAM POSITIONS
    // =========================================================================

    const std::size_t low_target =
        std::max(
            std::size_t{1},
            (
                num_pixels *
                static_cast<std::size_t>(
                    config::GRAY_LOW_PERCENTILE
                )
            ) /
            std::size_t{100}
        );

    const std::size_t high_target =
        std::max(
            low_target + std::size_t{1},
            (
                num_pixels *
                static_cast<std::size_t>(
                    config::GRAY_HIGH_PERCENTILE
                )
            ) /
            std::size_t{100}
        );

    // =========================================================================
    // FIND LOW PERCENTILE
    // =========================================================================

    std::size_t cumulative =
        0;

    int low =
        0;

    for (
        int value = 0;
        value < 256;
        ++value
    ) {

        cumulative +=
            histogram[
                static_cast<std::size_t>(
                    value
                )
            ];

        if (
            cumulative >=
            low_target
        ) {

            low =
                value;

            break;
        }
    }

    // =========================================================================
    // FIND HIGH PERCENTILE
    // =========================================================================

    cumulative =
        0;

    int high =
        255;

    for (
        int value = 0;
        value < 256;
        ++value
    ) {

        cumulative +=
            histogram[
                static_cast<std::size_t>(
                    value
                )
            ];

        if (
            cumulative >=
            high_target
        ) {

            high =
                value;

            break;
        }
    }

    // =========================================================================
    // GUARANTEE A MINIMUM USEFUL RANGE
    //
    // Preserve the behavior of the legacy implementation:
    //
    //     high >= low + 8
    //
    // This prevents a nearly uniform image from producing a zero/small
    // normalization range.
    // =========================================================================

    high =
        std::max(
            high,
            low + 8
        );

    const int range =
        high -
        low;

    // =========================================================================
    // NO EFFECTIVE RANGE
    // =========================================================================

    if (
        range <= 0
    ) {

        if (
            input != output
        ) {

            std::memcpy(
                output,
                input,
                num_pixels
            );
        }

        return;
    }

    // =========================================================================
    // CONTRAST STRETCH
    //
    //     <= low  ->   0
    //     >= high -> 255
    //     middle  -> linearly mapped to [0,255]
    //
    // Still grayscale.
    // No thresholding / binarization occurs here.
    // =========================================================================

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        const int value =
            static_cast<int>(
                input[i]
            );

        if (
            value <= low
        ) {

            output[i] =
                uint8_t{0};

        } else if (
            value >= high
        ) {

            output[i] =
                uint8_t{255};

        } else {

            output[i] =
                static_cast<uint8_t>(
                    (
                        (
                            value -
                            low
                        ) *
                        255
                    ) /
                    range
                );
        }
    }
}

} // namespace fin_ocr::image
