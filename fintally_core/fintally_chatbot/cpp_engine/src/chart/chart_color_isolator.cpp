#include "fin_ocr/chart/chart_color_isolator.hpp"

#include "fin_ocr/image/luminance.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace fin_ocr {

// =============================================================================
// RGB -> CUSTOM CHART OCR CHANNELS
//
// NOT HSV.
//
// output channel 0 = saturation
// output channel 1 = OCR foreground strength
// output channel 2 = chroma
//
// The input is standard RGB:
//
//     rgb[3*i + 0] = R
//     rgb[3*i + 1] = G
//     rgb[3*i + 2] = B
//
// The output preserves the original chart OCR contract.
//
// =============================================================================

bool ChartColorIsolator::isolate(
    const uint8_t* rgb,
    uint8_t* output,
    std::size_t num_pixels
) noexcept {

    if (
        rgb == nullptr ||
        output == nullptr ||
        num_pixels == 0
    ) {
        return false;
    }

    // =========================================================================
    // LOCAL CONFIGURATION
    // =========================================================================
    //
    // These values are algorithm-specific rather than global OCR thresholds.
    // Keeping them local prevents unrelated modules from depending on them.
    //
    // =========================================================================

    constexpr uint8_t MAX_TEXT_SATURATION =
        45;

    constexpr uint8_t MIN_TEXT_CONTRAST =
        20;

    constexpr std::size_t HISTOGRAM_SAMPLE_STRIDE =
        16;

    // =========================================================================
    // STEP 1: ESTIMATE BACKGROUND GRAYSCALE
    // =========================================================================
    //
    // We use a sampled grayscale histogram rather than scanning every pixel
    // solely for background estimation.
    //
    // =========================================================================

    std::array<
        std::size_t,
        256
    > histogram{};

    histogram.fill(
        0
    );

    std::size_t sampled_pixels =
        0;

    for (
        std::size_t i = 0;
        i < num_pixels;
        i += HISTOGRAM_SAMPLE_STRIDE
    ) {

        const std::size_t index =
            i * 3;

        const uint8_t gray =
            image::luminance_rgb(
                rgb[index + 0],
                rgb[index + 1],
                rgb[index + 2]
            );

        ++histogram[
            static_cast<std::size_t>(
                gray
            )
        ];

        ++sampled_pixels;
    }

    if (
        sampled_pixels == 0
    ) {
        return false;
    }

    // =========================================================================
    // MEDIAN BACKGROUND ESTIMATE
    // =========================================================================

    const std::size_t median_position =
        sampled_pixels / 2;

    std::size_t cumulative =
        0;

    uint8_t background_gray =
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
            median_position
        ) {

            background_gray =
                static_cast<uint8_t>(
                    value
                );

            break;
        }
    }

    const bool dark_background =
        background_gray < 128;

    // =========================================================================
    // STEP 2: ISOLATE CHART TEXT SIGNAL
    // =========================================================================
    //
    // Output format:
    //
    //     channel 0 -> saturation
    //     channel 1 -> foreground strength
    //     channel 2 -> chroma/delta
    //
    // This intentionally remains compatible with:
    //
    //     ocr_pixel(... channels == 3 ...)
    //
    // which reads output channel 1 as the OCR foreground channel.
    //
    // =========================================================================

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        const std::size_t index =
            i * 3;

        const uint8_t r =
            rgb[index + 0];

        const uint8_t g =
            rgb[index + 1];

        const uint8_t b =
            rgb[index + 2];

        // ---------------------------------------------------------------------
        // RGB extrema.
        // ---------------------------------------------------------------------

        const uint8_t cmax =
            std::max({
                r,
                g,
                b
            });

        const uint8_t cmin =
            std::min({
                r,
                g,
                b
            });

        const uint8_t delta =
            static_cast<uint8_t>(
                cmax - cmin
            );

        // ---------------------------------------------------------------------
        // Approximate HSV-style saturation.
        //
        // This is not converting the whole image to HSV. We only calculate
        // the saturation component required by the existing chart isolator.
        // ---------------------------------------------------------------------

        const uint8_t saturation =
            cmax == 0
                ? uint8_t{0}
                : static_cast<uint8_t>(
                      (
                          255u *
                          static_cast<unsigned>(
                              delta
                          )
                      ) /
                      static_cast<unsigned>(
                          cmax
                      )
                  );

        // ---------------------------------------------------------------------
        // Grayscale.
        // ---------------------------------------------------------------------

        const uint8_t gray =
            image::luminance_rgb(
                r,
                g,
                b
            );

        // ---------------------------------------------------------------------
        // Text is expected to be relatively neutral.
        //
        // This suppresses strongly colored chart lines and fills.
        // ---------------------------------------------------------------------

        const bool sufficiently_neutral =
            saturation <=
            MAX_TEXT_SATURATION;

        bool foreground =
            false;

        uint8_t foreground_strength =
            0;

        // =====================================================================
        // LIGHT BACKGROUND
        // =====================================================================

        if (
            !dark_background
        ) {

            const int contrast =
                static_cast<int>(
                    background_gray
                ) -
                static_cast<int>(
                    gray
                );

            if (
                contrast >=
                    static_cast<int>(
                        MIN_TEXT_CONTRAST
                    ) &&
                sufficiently_neutral
            ) {

                foreground =
                    true;

                foreground_strength =
                    static_cast<uint8_t>(
                        std::min(
                            255,
                            contrast
                        )
                    );
            }

        // =====================================================================
        // DARK BACKGROUND
        // =====================================================================

        } else {

            const int contrast =
                static_cast<int>(
                    gray
                ) -
                static_cast<int>(
                    background_gray
                );

            if (
                contrast >=
                    static_cast<int>(
                        MIN_TEXT_CONTRAST
                    ) &&
                sufficiently_neutral
            ) {

                foreground =
                    true;

                foreground_strength =
                    static_cast<uint8_t>(
                        std::min(
                            255,
                            contrast
                        )
                    );
            }
        }

        // =========================================================================
        // WRITE CUSTOM CHART OCR CHANNELS
        // =========================================================================

        output[index + 0] =
            saturation;

        output[index + 1] =
            foreground
                ? foreground_strength
                : uint8_t{0};

        output[index + 2] =
            delta;
    }

    return true;
}

} // namespace fin_ocr
