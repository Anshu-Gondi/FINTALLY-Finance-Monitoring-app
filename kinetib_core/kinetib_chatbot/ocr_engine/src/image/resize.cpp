#include "fin_ocr/image/resize.hpp"
#include "fin_ocr/image/luminance.hpp"

#include "fin_ocr/image/grayscale.hpp"
#include "fin_ocr/core/safe_arithmetric.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::image {

// =============================================================================
// RESIZE GRAYSCALE
//
// Downscaling:
//     area / box averaging
//
// Upscaling / same-size:
//     nearest-neighbour
//
// This preserves the legacy OCR preprocessing behavior.
// =============================================================================

void resize_grayscale_ocr(
    const uint8_t* source,
    int source_width,
    int source_height,
    uint8_t* destination,
    int target_width,
    int target_height
) noexcept {

    if (
        source == nullptr ||
        destination == nullptr ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0
    ) {
        return;
    }

    // =========================================================================
    // UPSCALING / SAME SIZE
    //
    // Preserve the original pipeline behavior:
    // nearest-neighbour is used whenever either target dimension is >= source.
    // =========================================================================

    if (
        target_width >= source_width ||
        target_height >= source_height
    ) {

        for (
            int y = 0;
            y < target_height;
            ++y
        ) {

            const int src_y =
                std::min(
                    source_height - 1,
                    (
                        y * source_height
                    ) /
                    target_height
                );

            const std::size_t destination_row =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(target_width);

            const std::size_t source_row =
                static_cast<std::size_t>(src_y) *
                static_cast<std::size_t>(source_width);

            for (
                int x = 0;
                x < target_width;
                ++x
            ) {

                const int src_x =
                    std::min(
                        source_width - 1,
                        (
                            x * source_width
                        ) /
                        target_width
                    );

                destination[
                    destination_row +
                    static_cast<std::size_t>(x)
                ] =
                    source[
                        source_row +
                        static_cast<std::size_t>(src_x)
                    ];
            }
        }

        return;
    }

    // =========================================================================
    // GENUINE DOWNSCALE
    //
    // Area / box averaging suppresses isolated source-pixel noise while
    // preserving stroke information for OCR.
    // =========================================================================

    for (
        int dy = 0;
        dy < target_height;
        ++dy
    ) {

        const int src_y0 =
            (
                dy *
                source_height
            ) /
            target_height;

        const int src_y1 =
            std::max(
                src_y0 + 1,
                (
                    (dy + 1) *
                    source_height +
                    target_height -
                    1
                ) /
                target_height
            );

        const int clamped_src_y1 =
            std::min(
                src_y1,
                source_height
            );

        const std::size_t destination_row =
            static_cast<std::size_t>(dy) *
            static_cast<std::size_t>(target_width);

        for (
            int dx = 0;
            dx < target_width;
            ++dx
        ) {

            const int src_x0 =
                (
                    dx *
                    source_width
                ) /
                target_width;

            const int src_x1 =
                std::max(
                    src_x0 + 1,
                    (
                        (dx + 1) *
                        source_width +
                        target_width -
                        1
                    ) /
                    target_width
                );

            const int clamped_src_x1 =
                std::min(
                    src_x1,
                    source_width
                );

            uint64_t sum = 0;
            uint32_t count = 0;

            for (
                int sy = src_y0;
                sy < clamped_src_y1;
                ++sy
            ) {

                const std::size_t source_row =
                    static_cast<std::size_t>(sy) *
                    static_cast<std::size_t>(source_width);

                for (
                    int sx = src_x0;
                    sx < clamped_src_x1;
                    ++sx
                ) {

                    sum +=
                        source[
                            source_row +
                            static_cast<std::size_t>(sx)
                        ];

                    ++count;
                }
            }

            destination[
                destination_row +
                static_cast<std::size_t>(dx)
            ] =
                count == 0
                    ? uint8_t{255}
                    : static_cast<uint8_t>(
                          sum / count
                      );
        }
    }
}

// =============================================================================
// RESIZE BGRA -> GRAYSCALE
//
// Used by the PDF path when the high-resolution BGRA raster must be converted
// to the grayscale representation consumed by OCR.
//
// Fast path:
//     nearest-neighbour resize when target is not strictly smaller in both
//     dimensions.
//
// Downscale path:
//     BGRA -> grayscale temporary buffer
//     then area-based grayscale resize.
// =============================================================================

void resize_bgra_to_grayscale(
    const uint8_t* source_bgra,
    int source_width,
    int source_height,
    uint8_t* destination_gray,
    int target_width,
    int target_height
) noexcept {

    if (
        source_bgra == nullptr ||
        destination_gray == nullptr ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0
    ) {
        return;
    }

    // =========================================================================
    // UPSCALING / SAME SIZE
    // =========================================================================

    if (
        target_width >= source_width ||
        target_height >= source_height
    ) {

        for (
            int y = 0;
            y < target_height;
            ++y
        ) {

            const int src_y =
                std::min(
                    source_height - 1,
                    (
                        y * source_height
                    ) /
                    target_height
                );

            const std::size_t source_row =
                static_cast<std::size_t>(src_y) *
                static_cast<std::size_t>(source_width);

            const std::size_t destination_row =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(target_width);

            for (
                int x = 0;
                x < target_width;
                ++x
            ) {

                const int src_x =
                    std::min(
                        source_width - 1,
                        (
                            x * source_width
                        ) /
                        target_width
                    );

                const std::size_t source_pixel =
                    (
                        source_row +
                        static_cast<std::size_t>(src_x)
                    ) * 4u;

                destination_gray[
                    destination_row +
                    static_cast<std::size_t>(x)
                ] =
                    luminance_rgb(
                        source_bgra[
                            source_pixel + 2
                        ],
                        source_bgra[
                            source_pixel + 1
                        ],
                        source_bgra[
                            source_pixel + 0
                        ]
                    );
            }
        }

        return;
    }

    // =========================================================================
    // DOWNSCALE
    //
    // First convert BGRA -> grayscale, then use the same area averaging
    // implementation as resize_grayscale_ocr().
    // =========================================================================

    std::size_t source_pixels = 0;

    if (
        !core::safe_mul(
            static_cast<std::size_t>(source_width),
            static_cast<std::size_t>(source_height),
            source_pixels
        )
    ) {
        return;
    }

    std::vector<uint8_t> source_gray(
        source_pixels
    );

    bgra_to_grayscale(
        source_bgra,
        source_gray.data(),
        source_pixels
    );

    resize_grayscale_ocr(
        source_gray.data(),
        source_width,
        source_height,
        destination_gray,
        target_width,
        target_height
    );
}

// =============================================================================
// RESIZE RGB -> RGB
//
// Pure nearest-neighbour resize.
//
// No color conversion is performed here.
// =============================================================================

void resize_rgb_nearest(
    const uint8_t* source,
    int source_width,
    int source_height,
    uint8_t* destination,
    int target_width,
    int target_height
) noexcept {

    if (
        source == nullptr ||
        destination == nullptr ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0
    ) {
        return;
    }

    for (
        int y = 0;
        y < target_height;
        ++y
    ) {

        const int src_y =
            std::min(
                source_height - 1,
                (
                    y * source_height
                ) /
                target_height
            );

        const std::size_t source_row =
            static_cast<std::size_t>(src_y) *
            static_cast<std::size_t>(source_width);

        const std::size_t destination_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(target_width);

        for (
            int x = 0;
            x < target_width;
            ++x
        ) {

            const int src_x =
                std::min(
                    source_width - 1,
                    (
                        x * source_width
                    ) /
                    target_width
                );

            const std::size_t source_pixel =
                (
                    source_row +
                    static_cast<std::size_t>(src_x)
                ) * 3u;

            const std::size_t destination_pixel =
                (
                    destination_row +
                    static_cast<std::size_t>(x)
                ) * 3u;

            destination[
                destination_pixel + 0
            ] =
                source[
                    source_pixel + 0
                ];

            destination[
                destination_pixel + 1
            ] =
                source[
                    source_pixel + 1
                ];

            destination[
                destination_pixel + 2
            ] =
                source[
                    source_pixel + 2
                ];
        }
    }
}

} // namespace fin_ocr::image
