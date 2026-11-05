#include "fin_ocr/matrix/glyph_normalizer.hpp"

#include "fin_ocr/core/pixel_access.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr {

// =============================================================================
// NORMALIZE ARBITRARY GLYPH -> 16x16
// =============================================================================

bool GlyphNormalizer::normalize_to_grid(
    const std::vector<uint8_t>& patch,
    int patch_w,
    int patch_h,
    std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& output
) noexcept {

    output.fill(0);

    // =========================================================================
    // INPUT VALIDATION
    // =========================================================================

    if (
        patch.empty() ||
        patch_w <= 0 ||
        patch_h <= 0
    ) {
        return false;
    }

    const std::size_t required =
        static_cast<std::size_t>(patch_w) *
        static_cast<std::size_t>(patch_h);

    if (
        patch.size() <
        required
    ) {
        return false;
    }

    // =========================================================================
    // FIND GLYPH BOUNDS
    // =========================================================================

    int min_x =
        patch_w;

    int min_y =
        patch_h;

    int max_x =
        -1;

    int max_y =
        -1;

    for (
        int y = 0;
        y < patch_h;
        ++y
    ) {

        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(patch_w);

        for (
            int x = 0;
            x < patch_w;
            ++x
        ) {

            if (
                !is_foreground_pixel(
                    patch[
                        row +
                        static_cast<std::size_t>(x)
                    ]
                )
            ) {
                continue;
            }

            min_x =
                std::min(
                    min_x,
                    x
                );

            max_x =
                std::max(
                    max_x,
                    x
                );

            min_y =
                std::min(
                    min_y,
                    y
                );

            max_y =
                std::max(
                    max_y,
                    y
                );
        }
    }

    if (
        max_x < min_x ||
        max_y < min_y
    ) {
        return false;
    }

    const int glyph_w =
        max_x -
        min_x +
        1;

    const int glyph_h =
        max_y -
        min_y +
        1;

    // =========================================================================
    // HOLLOW-FRAME ARTIFACT REJECTION
    // =========================================================================

    if (
        glyph_w >= 6 &&
        glyph_h >= 6
    ) {

        int border_pixels =
            0;

        const int expected_border_pixels =
            2 *
            (
                glyph_w +
                glyph_h
            ) -
            4;

        // ---------------------------------------------------------------------
        // TOP + BOTTOM
        // ---------------------------------------------------------------------

        for (
            int x = min_x;
            x <= max_x;
            ++x
        ) {

            if (
                is_foreground_pixel(
                    patch[
                        static_cast<
                            std::size_t
                        >(min_y) *
                        static_cast<
                            std::size_t
                        >(patch_w) +
                        static_cast<
                            std::size_t
                        >(x)
                    ]
                )
            ) {
                ++border_pixels;
            }

            if (
                is_foreground_pixel(
                    patch[
                        static_cast<
                            std::size_t
                        >(max_y) *
                        static_cast<
                            std::size_t
                        >(patch_w) +
                        static_cast<
                            std::size_t
                        >(x)
                    ]
                )
            ) {
                ++border_pixels;
            }
        }

        // ---------------------------------------------------------------------
        // LEFT + RIGHT
        // ---------------------------------------------------------------------

        for (
            int y = min_y + 1;
            y < max_y;
            ++y
        ) {

            if (
                is_foreground_pixel(
                    patch[
                        static_cast<
                            std::size_t
                        >(y) *
                        static_cast<
                            std::size_t
                        >(patch_w) +
                        static_cast<
                            std::size_t
                        >(min_x)
                    ]
                )
            ) {
                ++border_pixels;
            }

            if (
                is_foreground_pixel(
                    patch[
                        static_cast<
                            std::size_t
                        >(y) *
                        static_cast<
                            std::size_t
                        >(patch_w) +
                        static_cast<
                            std::size_t
                        >(max_x)
                    ]
                )
            ) {
                ++border_pixels;
            }
        }

        if (
            border_pixels ==
            expected_border_pixels
        ) {

            int interior_fg =
                0;

            for (
                int y = min_y + 1;
                y < max_y &&
                interior_fg == 0;
                ++y
            ) {

                const std::size_t row =
                    static_cast<
                        std::size_t
                    >(y) *
                    static_cast<
                        std::size_t
                    >(patch_w);

                for (
                    int x = min_x + 1;
                    x < max_x;
                    ++x
                ) {

                    if (
                        is_foreground_pixel(
                            patch[
                                row +
                                static_cast<
                                    std::size_t
                                >(x)
                            ]
                        )
                    ) {

                        interior_fg =
                            1;

                        break;
                    }
                }
            }

            if (
                interior_fg == 0
            ) {
                return false;
            }
        }
    }

    // =========================================================================
    // EXACT 16x16 PATH
    // =========================================================================

    if (
        glyph_w ==
            GLYPH_GRID_SIZE &&
        glyph_h ==
            GLYPH_GRID_SIZE
    ) {

        for (
            int y = 0;
            y < GLYPH_GRID_SIZE;
            ++y
        ) {

            uint16_t bits =
                0;

            const std::size_t row =
                static_cast<
                    std::size_t
                >(min_y + y) *
                static_cast<
                    std::size_t
                >(patch_w);

            for (
                int x = 0;
                x < GLYPH_GRID_SIZE;
                ++x
            ) {

                if (
                    is_foreground_pixel(
                        patch[
                            row +
                            static_cast<
                                std::size_t
                            >(min_x + x)
                        ]
                    )
                ) {

                    bits |=
                        static_cast<
                            uint16_t
                        >(
                            1u <<
                            (15 - x)
                        );
                }
            }

            output[y] =
                bits;
        }

        return true;
    }

    // =========================================================================
    // ASPECT-PRESERVING SCALE
    // =========================================================================

    constexpr int INNER_SIZE =
        14;

    const double scale_x =
        static_cast<double>(
            INNER_SIZE
        ) /
        static_cast<double>(
            glyph_w
        );

    const double scale_y =
        static_cast<double>(
            INNER_SIZE
        ) /
        static_cast<double>(
            glyph_h
        );

    const double scale =
        std::min(
            scale_x,
            scale_y
        );

    const int dst_w =
        std::clamp(
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        glyph_w
                    ) *
                    scale
                )
            ),
            1,
            INNER_SIZE
        );

    const int dst_h =
        std::clamp(
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        glyph_h
                    ) *
                    scale
                )
            ),
            1,
            INNER_SIZE
        );

    const int offset_x =
        (
            GLYPH_GRID_SIZE -
            dst_w
        ) / 2;

    const int offset_y =
        (
            GLYPH_GRID_SIZE -
            dst_h
        ) / 2;

    constexpr int MIN_COVERAGE_PERCENT =
        20;

    // =========================================================================
    // COVERAGE-BASED NORMALIZATION
    // =========================================================================

    for (
        int dy = 0;
        dy < dst_h;
        ++dy
    ) {

        const int src_y0 =
            (
                dy *
                glyph_h
            ) /
            dst_h;

        const int src_y1 =
            std::min(
                glyph_h,
                (
                    (dy + 1) *
                    glyph_h +
                    dst_h -
                    1
                ) /
                dst_h
            );

        uint16_t row_bits =
            0;

        for (
            int dx = 0;
            dx < dst_w;
            ++dx
        ) {

            const int src_x0 =
                (
                    dx *
                    glyph_w
                ) /
                dst_w;

            const int src_x1 =
                std::min(
                    glyph_w,
                    (
                        (dx + 1) *
                        glyph_w +
                        dst_w -
                        1
                    ) /
                    dst_w
                );

            int source_pixels =
                0;

            int foreground_pixels =
                0;

            for (
                int sy = src_y0;
                sy < src_y1;
                ++sy
            ) {

                const std::size_t row =
                    static_cast<
                        std::size_t
                    >(min_y + sy) *
                    static_cast<
                        std::size_t
                    >(patch_w);

                for (
                    int sx = src_x0;
                    sx < src_x1;
                    ++sx
                ) {

                    ++source_pixels;

                    if (
                        is_foreground_pixel(
                            patch[
                                row +
                                static_cast<
                                    std::size_t
                                >(min_x + sx)
                            ]
                        )
                    ) {

                        ++foreground_pixels;
                    }
                }
            }

            if (
                source_pixels <= 0
            ) {
                continue;
            }

            const int coverage_percent =
                (
                    foreground_pixels *
                    100
                ) /
                source_pixels;

            if (
                coverage_percent >=
                MIN_COVERAGE_PERCENT
            ) {

                const int output_x =
                    offset_x +
                    dx;

                row_bits |=
                    static_cast<
                        uint16_t
                    >(
                        1u <<
                        (15 - output_x)
                    );
            }
        }

        output[
            offset_y +
            dy
        ] =
            row_bits;
    }

    return true;
}

} // namespace fin_ocr
