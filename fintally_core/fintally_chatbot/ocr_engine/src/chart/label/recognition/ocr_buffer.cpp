#include "fin_ocr/chart/label/recognition/ocr_buffer.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label::recognition {

namespace {

// =============================================================================
// GENERIC OCR CROP PADDING
// =============================================================================
//
// Preserved from the legacy implementation.
//
// =============================================================================

constexpr int OCR_PADDING_X =
    4;

constexpr int OCR_PADDING_Y =
    3;

// =============================================================================
// Y-AXIS LUMINANCE CROP PADDING
// =============================================================================

constexpr int Y_AXIS_OCR_PADDING_X =
    5;

constexpr int Y_AXIS_OCR_PADDING_Y =
    3;

} // namespace

// =============================================================================
// GENERIC CANDIDATE OCR BUFFER
// =============================================================================
//
// Extracts channel 1 from the packed 3-channel chart representation.
//
// The resulting OcrBuffer is a one-channel grayscale-style buffer suitable
// for LineRecognizer.
//
// =============================================================================

[[nodiscard]]
OcrBuffer build_candidate_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
)
{
    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        !geometry::valid_candidate(
            candidate
        )
    ) {
        return {};
    }

    const int crop_min_x =
        std::max(
            0,
            candidate.min_x -
                OCR_PADDING_X
        );

    const int crop_max_x =
        std::min(
            image_width - 1,
            candidate.max_x +
                OCR_PADDING_X
        );

    const int crop_min_y =
        std::max(
            0,
            candidate.min_y -
                OCR_PADDING_Y
        );

    const int crop_max_y =
        std::min(
            image_height - 1,
            candidate.max_y +
                OCR_PADDING_Y
        );

    if (
        crop_min_x > crop_max_x ||
        crop_min_y > crop_max_y
    ) {
        return {};
    }

    const int crop_width =
        crop_max_x -
        crop_min_x +
        1;

    const int crop_height =
        crop_max_y -
        crop_min_y +
        1;

    if (
        crop_width <= 0 ||
        crop_height <= 0
    ) {
        return {};
    }

    const std::size_t crop_pixels =
        static_cast<std::size_t>(
            crop_width
        ) *
        static_cast<std::size_t>(
            crop_height
        );

    if (
        crop_pixels == 0
    ) {
        return {};
    }

    std::vector<std::uint8_t> buffer(
        crop_pixels,
        std::uint8_t{0}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                image_width
            ) *
            3u;

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y -
                crop_min_y
            ) *
            static_cast<std::size_t>(
                crop_width
            );

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            ++x
        ) {

            const std::size_t source_index =
                source_row +
                static_cast<std::size_t>(
                    x
                ) *
                3u +
                1u;

            const std::size_t destination_index =
                destination_row +
                static_cast<std::size_t>(
                    x -
                    crop_min_x
                );

            buffer[
                destination_index
            ] =
                chart_buffer[
                    source_index
                ];
        }
    }

    return OcrBuffer{
        std::move(
            buffer
        ),
        crop_width,
        crop_height
    };
}

// =============================================================================
// OCR BUFFER INVERSION
// =============================================================================

[[nodiscard]]
OcrBuffer invert_ocr_buffer(
    const OcrBuffer& source
)
{
    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0
    ) {
        return {};
    }

    OcrBuffer output;

    output.width =
        source.width;

    output.height =
        source.height;

    output.data.resize(
        source.data.size()
    );

    for (
        std::size_t i = 0u;
        i < source.data.size();
        ++i
    ) {

        output.data[i] =
            static_cast<std::uint8_t>(
                255u -
                static_cast<unsigned>(
                    source.data[i]
                )
            );
    }

    return output;
}

// =============================================================================
// OCR BUFFER SCALE
// =============================================================================
//
// Nearest-neighbor integer scaling.
//
// This is intentionally simple because the buffer is an OCR retry
// representation rather than a photographic image.
//
// =============================================================================

[[nodiscard]]
OcrBuffer scale_ocr_buffer(
    const OcrBuffer& source,
    int scale
)
{
    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0 ||
        scale <= 1
    ) {
        return source;
    }

    const int scaled_width =
        source.width *
        scale;

    const int scaled_height =
        source.height *
        scale;

    if (
        scaled_width <= 0 ||
        scaled_height <= 0
    ) {
        return {};
    }

    const std::size_t scaled_pixels =
        static_cast<std::size_t>(
            scaled_width
        ) *
        static_cast<std::size_t>(
            scaled_height
        );

    OcrBuffer output;

    output.width =
        scaled_width;

    output.height =
        scaled_height;

    output.data.assign(
        scaled_pixels,
        std::uint8_t{0}
    );

    for (
        int y = 0;
        y < scaled_height;
        ++y
    ) {

        const int source_y =
            y /
            scale;

        const std::size_t source_row =
            static_cast<std::size_t>(
                source_y
            ) *
            static_cast<std::size_t>(
                source.width
            );

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                scaled_width
            );

        for (
            int x = 0;
            x < scaled_width;
            ++x
        ) {

            const int source_x =
                x /
                scale;

            output.data[
                destination_row +
                static_cast<std::size_t>(
                    x
                )
            ] =
                source.data[
                    source_row +
                    static_cast<std::size_t>(
                        source_x
                    )
                ];
        }
    }

    return output;
}

// =============================================================================
// Y-AXIS LUMINANCE OCR BUFFER
// =============================================================================
//
// Produces conventional dark-text-on-light-background luminance.
//
// The chart representation is:
//
//     channel 0 = supporting chart information
//     channel 1 = OCR foreground
//     channel 2 = supporting chart information
//
// This path intentionally reconstructs luminance from all three source
// channels because some Y-axis text can be weak in the existing foreground
// representation.
//
// =============================================================================

[[nodiscard]]
OcrBuffer build_y_axis_luminance_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
)
{
    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        !geometry::valid_candidate(
            candidate
        )
    ) {
        return {};
    }

    const int crop_min_x =
        std::max(
            0,
            candidate.min_x -
                Y_AXIS_OCR_PADDING_X
        );

    const int crop_max_x =
        std::min(
            image_width - 1,
            candidate.max_x +
                Y_AXIS_OCR_PADDING_X
        );

    const int crop_min_y =
        std::max(
            0,
            candidate.min_y -
                Y_AXIS_OCR_PADDING_Y
        );

    const int crop_max_y =
        std::min(
            image_height - 1,
            candidate.max_y +
                Y_AXIS_OCR_PADDING_Y
        );

    if (
        crop_min_x > crop_max_x ||
        crop_min_y > crop_max_y
    ) {
        return {};
    }

    const int crop_width =
        crop_max_x -
        crop_min_x +
        1;

    const int crop_height =
        crop_max_y -
        crop_min_y +
        1;

    if (
        crop_width <= 0 ||
        crop_height <= 0
    ) {
        return {};
    }

    const std::size_t crop_pixels =
        static_cast<std::size_t>(
            crop_width
        ) *
        static_cast<std::size_t>(
            crop_height
        );

    OcrBuffer output;

    output.width =
        crop_width;

    output.height =
        crop_height;

    output.data.assign(
        crop_pixels,
        std::uint8_t{255}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                image_width
            ) *
            3u;

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y -
                crop_min_y
            ) *
            static_cast<std::size_t>(
                crop_width
            );

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            ++x
        ) {

            const std::size_t source_index =
                source_row +
                static_cast<std::size_t>(
                    x
                ) *
                3u;

            const std::uint8_t r =
                chart_buffer[
                    source_index +
                    0u
                ];

            const std::uint8_t g =
                chart_buffer[
                    source_index +
                    1u
                ];

            const std::uint8_t b =
                chart_buffer[
                    source_index +
                    2u
                ];

            // Integer approximation of:
            //
            //     Y = 0.299 R + 0.587 G + 0.114 B
            //
            // implemented using integer coefficients matching the legacy
            // implementation.
            const std::uint8_t luminance =
                static_cast<std::uint8_t>(
                    (
                        77u *
                            static_cast<unsigned>(
                                r
                            ) +
                        150u *
                            static_cast<unsigned>(
                                g
                            ) +
                        29u *
                            static_cast<unsigned>(
                                b
                            )
                    ) >>
                    8u
                );

            output.data[
                destination_row +
                static_cast<std::size_t>(
                    x -
                    crop_min_x
                )
            ] =
                luminance;
        }
    }

    return output;
}

// =============================================================================
// THRESHOLD OCR BUFFER
// =============================================================================

[[nodiscard]]
OcrBuffer threshold_ocr_buffer(
    const OcrBuffer& source,
    std::uint8_t threshold,
    bool dark_foreground
)
{
    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0
    ) {
        return {};
    }

    OcrBuffer output;

    output.width =
        source.width;

    output.height =
        source.height;

    output.data.resize(
        source.data.size()
    );

    for (
        std::size_t i = 0u;
        i < source.data.size();
        ++i
    ) {

        const bool foreground =
            dark_foreground
                ? source.data[i] <= threshold
                : source.data[i] >= threshold;

        output.data[i] =
            foreground
                ? std::uint8_t{0}
                : std::uint8_t{255};
    }

    return output;
}

[[nodiscard]]
OcrBuffer
build_y_axis_candidate_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
) {

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        !geometry::valid_candidate(candidate)
    ) {

        return {};
    }

    constexpr int OCR_PADDING_X =
        5;

    constexpr int OCR_PADDING_Y =
        3;

    const int crop_min_x =
        std::max(
            0,
            candidate.min_x - OCR_PADDING_X
        );

    const int crop_max_x =
        std::min(
            image_width - 1,
            candidate.max_x + OCR_PADDING_X
        );

    const int crop_min_y =
        std::max(
            0,
            candidate.min_y - OCR_PADDING_Y
        );

    const int crop_max_y =
        std::min(
            image_height - 1,
            candidate.max_y + OCR_PADDING_Y
        );

    if (
        crop_min_x > crop_max_x ||
        crop_min_y > crop_max_y
    ) {

        return {};
    }

    const int crop_width =
        crop_max_x -
        crop_min_x +
        1;

    const int crop_height =
        crop_max_y -
        crop_min_y +
        1;

    if (
        crop_width <= 0 ||
        crop_height <= 0
    ) {

        return {};
    }

    const std::size_t crop_pixels =
        static_cast<std::size_t>(crop_width) *
        static_cast<std::size_t>(crop_height);

    if (
        crop_pixels == 0u
    ) {

        return {};
    }

    // -------------------------------------------------------------------------
    // Estimate background polarity from a bounded sample.
    // -------------------------------------------------------------------------

    std::size_t bright_samples =
        0u;

    std::size_t sample_count =
        0u;

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        y += 2
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_width) *
            3u;

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            x += 2
        ) {

            const std::size_t index =
                source_row +
                static_cast<std::size_t>(x) *
                    3u;

            const std::uint8_t r =
                chart_buffer[index + 0u];

            const std::uint8_t g =
                chart_buffer[index + 1u];

            const std::uint8_t b =
                chart_buffer[index + 2u];

            const std::uint8_t luminance =
                static_cast<std::uint8_t>(
                    (
                        77u * static_cast<unsigned>(r) +
                        150u * static_cast<unsigned>(g) +
                        29u * static_cast<unsigned>(b)
                    ) >> 8u
                );

            if (
                luminance >= 128u
            ) {

                ++bright_samples;
            }

            ++sample_count;
        }
    }

    const bool light_background =
        sample_count > 0u &&
        bright_samples * 2u >= sample_count;

    OcrBuffer output;

    output.width =
        crop_width;

    output.height =
        crop_height;

    output.data.assign(
        crop_pixels,
        std::uint8_t{0}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_width) *
            3u;

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y - crop_min_y
            ) *
            static_cast<std::size_t>(crop_width);

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            ++x
        ) {

            const std::size_t source_index =
                source_row +
                static_cast<std::size_t>(x) *
                    3u;

            const std::uint8_t r =
                chart_buffer[source_index + 0u];

            const std::uint8_t g =
                chart_buffer[source_index + 1u];

            const std::uint8_t b =
                chart_buffer[source_index + 2u];

            const std::uint8_t cmin =
                std::min(
                    r,
                    std::min(g, b)
                );

            const std::uint8_t cmax =
                std::max(
                    r,
                    std::max(g, b)
                );

            const std::uint8_t strength =
                light_background
                    ? static_cast<std::uint8_t>(
                          255u -
                          static_cast<unsigned>(cmin)
                      )
                    : cmax;

            output.data[
                destination_row +
                static_cast<std::size_t>(
                    x - crop_min_x
                )
            ] =
                strength;
        }
    }

    return output;
}

} // namespace fin_ocr::chart::label::recognition
