#include "fin_ocr/tesseract/tesseract_preprocess.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/pixel_access.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fin_ocr {

// =============================================================================
// TESSERACT TEXT CLEANUP
// =============================================================================

std::string clean_tesseract_text(
    const char* raw
) {

    if (raw == nullptr) {
        return {};
    }

    std::string result;

    result.reserve(
        std::strlen(raw)
    );

    bool previous_space =
        false;

    for (
        const char* p = raw;
        *p != '\0';
        ++p
    ) {

        const unsigned char c =
            static_cast<unsigned char>(
                *p
            );

        if (
            c == ' ' ||
            c == '\n' ||
            c == '\r' ||
            c == '\t'
        ) {

            if (!previous_space) {
                result.push_back(' ');
            }

            previous_space =
                true;

            continue;
        }

        result.push_back(
            static_cast<char>(c)
        );

        previous_space =
            false;
    }

    while (
        !result.empty() &&
        result.front() == ' '
    ) {

        result.erase(
            result.begin()
        );
    }

    while (
        !result.empty() &&
        result.back() == ' '
    ) {

        result.pop_back();
    }

    return result;
}

// =============================================================================
// FIND OCR FOREGROUND BOUNDS
// =============================================================================

bool find_ocr_bounds(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels,
    int& min_x,
    int& min_y,
    int& max_x,
    int& max_y
) noexcept {

    min_x = width;
    min_y = y1;

    max_x = -1;
    max_y = -1;

    if (
        image == nullptr ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0
    ) {
        return false;
    }

    for (
        int y = y0;
        y < y1;
        ++y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const uint8_t value =
                ocr_pixel(
                    image,
                    width,
                    channels,
                    x,
                    y
                );

            const bool foreground =
                is_foreground_for_channels(
                    value,
                    channels
                );

            if (!foreground) {
                continue;
            }

            min_x =
                std::min(
                    min_x,
                    x
                );

            min_y =
                std::min(
                    min_y,
                    y
                );

            max_x =
                std::max(
                    max_x,
                    x
                );

            max_y =
                std::max(
                    max_y,
                    y
                );
        }
    }

    return
        max_x >= min_x &&
        max_y >= min_y;
}

// =============================================================================
// FINANCIAL NUMERIC-LINE HEURISTIC
// =============================================================================

bool looks_like_numeric_line(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels
) noexcept {

    if (
        image == nullptr ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0
    ) {
        return false;
    }

    std::size_t foreground_pixels =
        0;

    std::size_t narrow_column_pixels =
        0;

    int min_x =
        width;

    int max_x =
        -1;

    std::vector<int> column_counts(
        static_cast<std::size_t>(
            width
        ),
        0
    );

    for (
        int y = y0;
        y < y1;
        ++y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const uint8_t value =
                ocr_pixel(
                    image,
                    width,
                    channels,
                    x,
                    y
                );

            if (
                !is_foreground_for_channels(
                    value,
                    channels
                )
            ) {
                continue;
            }

            ++foreground_pixels;

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

            ++column_counts[
                static_cast<std::size_t>(x)
            ];
        }
    }

    if (
        foreground_pixels < 8 ||
        max_x < min_x
    ) {
        return false;
    }

    const int line_height =
        std::max(
            1,
            y1 - y0
        );

    const int narrow_limit =
        std::max(
            2,
            line_height / 2
        );

    for (
        int x = min_x;
        x <= max_x;
        ++x
    ) {

        const int count =
            column_counts[
                static_cast<std::size_t>(x)
            ];

        if (
            count > 0 &&
            count <= narrow_limit
        ) {
            ++narrow_column_pixels;
        }
    }

    const int horizontal_width =
        max_x -
        min_x +
        1;

    const double narrow_ratio =
        horizontal_width > 0
            ? static_cast<double>(
                  narrow_column_pixels
              ) /
              static_cast<double>(
                  horizontal_width
              )
            : 0.0;

    return narrow_ratio >= 0.30;
}

// =============================================================================
// UPSCALE OCR REGION
// =============================================================================
//
// Native mask:
//
//     foreground = 255
//     background = 0
//
// Tesseract image:
//
//     foreground/text = 0
//     background = 255
//
// =============================================================================

void upscale_ocr_region(
    const uint8_t* image,
    int source_width,
    int source_height,
    int channels,
    std::vector<uint8_t>& destination,
    int& destination_width,
    int& destination_height
) {

    destination_width =
        source_width *
        config::TESSERACT_SCALE;

    destination_height =
        source_height *
        config::TESSERACT_SCALE;

    if (
        image == nullptr ||
        source_width <= 0 ||
        source_height <= 0 ||
        channels <= 0
    ) {

        destination.clear();

        destination_width = 0;
        destination_height = 0;

        return;
    }

    const std::size_t total_pixels =
        static_cast<std::size_t>(
            destination_width
        ) *
        static_cast<std::size_t>(
            destination_height
        );

    destination.assign(
        total_pixels,
        255
    );

    for (
        int dy = 0;
        dy < destination_height;
        ++dy
    ) {

        const double src_y =
            (
                static_cast<double>(dy) +
                0.5
            ) /
            static_cast<double>(
                config::TESSERACT_SCALE
            ) -
            0.5;

        const double y_floor =
            std::floor(src_y);

        const int y0 =
            std::clamp(
                static_cast<int>(y_floor),
                0,
                source_height - 1
            );

        const int y1 =
            std::clamp(
                y0 + 1,
                0,
                source_height - 1
            );

        const double fy =
            std::clamp(
                src_y -
                    y_floor,
                0.0,
                1.0
            );

        const std::size_t destination_row =
            static_cast<std::size_t>(dy) *
            static_cast<std::size_t>(
                destination_width
            );

        for (
            int dx = 0;
            dx < destination_width;
            ++dx
        ) {

            const double src_x =
                (
                    static_cast<double>(dx) +
                    0.5
                ) /
                static_cast<double>(
                    config::TESSERACT_SCALE
                ) -
                0.5;

            const double x_floor =
                std::floor(src_x);

            const int x0 =
                std::clamp(
                    static_cast<int>(x_floor),
                    0,
                    source_width - 1
                );

            const int x1 =
                std::clamp(
                    x0 + 1,
                    0,
                    source_width - 1
                );

            const double fx =
                std::clamp(
                    src_x -
                        x_floor,
                    0.0,
                    1.0
                );

            const float p00 =
                static_cast<float>(
                    ocr_pixel(
                        image,
                        source_width,
                        channels,
                        x0,
                        y0
                    )
                );

            const float p10 =
                static_cast<float>(
                    ocr_pixel(
                        image,
                        source_width,
                        channels,
                        x1,
                        y0
                    )
                );

            const float p01 =
                static_cast<float>(
                    ocr_pixel(
                        image,
                        source_width,
                        channels,
                        x0,
                        y1
                    )
                );

            const float p11 =
                static_cast<float>(
                    ocr_pixel(
                        image,
                        source_width,
                        channels,
                        x1,
                        y1
                    )
                );

            const float top =
                p00 +
                static_cast<float>(fx) *
                (
                    p10 -
                    p00
                );

            const float bottom =
                p01 +
                static_cast<float>(fy) *
                (
                    p11 -
                    p01
                );

            const float interpolated =
                top +
                static_cast<float>(fy) *
                (
                    bottom -
                    top
                );

            const uint8_t native_value =
                static_cast<uint8_t>(
                    std::clamp(
                        std::lround(
                            interpolated
                        ),
                        0L,
                        255L
                    )
                );

            // -----------------------------------------------------------------
            // Invert native OCR polarity for Tesseract.
            // -----------------------------------------------------------------

            destination[
                destination_row +
                static_cast<std::size_t>(dx)
            ] =
                static_cast<uint8_t>(
                    255u -
                    static_cast<unsigned>(
                        native_value
                    )
                );
        }
    }
}

} // namespace fin_ocr
