#include "fin_ocr/chart/chart_color_isolator.hpp"

#include "fin_ocr/image/luminance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fin_ocr {

// =============================================================================
// RGB -> CUSTOM CHART OCR CHANNELS
//
// output channel 0 = saturation
// output channel 1 = OCR foreground strength
// output channel 2 = chroma / RGB delta
//
// IMPORTANT:
//
//     ChartLabelRecognizer::chart_foreground() reads:
//
//         output[index + 1]
//
//     Therefore channel 1 is the canonical OCR text-likelihood channel.
//
// The isolator must NOT perform aggressive semantic classification here.
// Its job is to preserve plausible text signal while suppressing obviously
// colored chart graphics.
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
    // These thresholds are intentionally conservative.
    //
    // The previous implementation used one fixed background median and then
    // required both:
    //
    //     contrast >= 20
    //     saturation <= 45
    //
    // That is too aggressive for real financial chart screenshots because
    // anti-aliased text can be:
    //
    //     - gray
    //     - slightly colored
    //     - only moderately darker than its local background
    //
    // We therefore compute:
    //
    //     1. global background estimate
    //     2. absolute contrast
    //     3. chroma/saturation
    //     4. a neutral-text contribution
    //     5. a colored-but-dark text contribution
    //
    // and combine them into a stable foreground strength.
    //
    // =========================================================================

    constexpr std::size_t HISTOGRAM_SAMPLE_STRIDE = 16;

    constexpr uint8_t MAX_TEXT_SATURATION = 96;

    constexpr uint8_t SOFT_CONTRAST_THRESHOLD = 8;

    constexpr uint8_t STRONG_CONTRAST_THRESHOLD = 18;

    constexpr uint8_t MAX_BACKGROUND_CHROMA_FOR_NEUTRAL = 48;

    // =========================================================================
    // STEP 1: SAMPLE GRAYSCALE HISTOGRAM
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
            i * 3u;

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
    // STEP 2: ROBUST BACKGROUND ESTIMATE
    // =========================================================================
    //
    // Use the histogram median, but clamp extreme pathological estimates.
    //
    // =========================================================================

    const std::size_t median_position =
        sampled_pixels / 2u;

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

    const bool light_background =
        background_gray >= 128;

    // =========================================================================
    // STEP 3: BUILD OUTPUT CHANNELS
    // =========================================================================

    for (
        std::size_t i = 0;
        i < num_pixels;
        ++i
    ) {

        const std::size_t index =
            i * 3u;

        const uint8_t r =
            rgb[index + 0];

        const uint8_t g =
            rgb[index + 1];

        const uint8_t b =
            rgb[index + 2];

        // ---------------------------------------------------------------------
        // RGB extrema / chroma
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
                cmax -
                cmin
            );

        // ---------------------------------------------------------------------
        // Approximate saturation.
        // ---------------------------------------------------------------------

        const uint8_t saturation =
            cmax == 0
                ? uint8_t{0}
                : static_cast<uint8_t>(
                      (
                          255u *
                          static_cast<unsigned>(delta)
                      ) /
                      static_cast<unsigned>(cmax)
                  );

        // ---------------------------------------------------------------------
        // Luminance
        // ---------------------------------------------------------------------

        const uint8_t gray =
            image::luminance_rgb(
                r,
                g,
                b
            );

        // =========================================================================
        // CONTRAST
        // =========================================================================
        //
        // Positive contrast means "pixel differs from chart background in the
        // expected direction".
        //
        // =========================================================================

        const int signed_contrast =
            light_background
                ? (
                      static_cast<int>(
                          background_gray
                      ) -
                      static_cast<int>(
                          gray
                      )
                  )
                : (
                      static_cast<int>(
                          gray
                      ) -
                      static_cast<int>(
                          background_gray
                      )
                  );

        const int contrast =
            std::max(
                0,
                signed_contrast
            );

        // =========================================================================
        // TEXT-LIKELIHOOD COMPONENTS
        // =========================================================================
        //
        // We deliberately separate:
        //
        //     neutral text
        //     moderately colored text
        //
        // because screenshots may contain anti-aliased text whose RGB channels
        // are not perfectly equal.
        //
        // =========================================================================

        const bool neutral_enough =
            saturation <=
            MAX_BACKGROUND_CHROMA_FOR_NEUTRAL;

        const bool moderately_neutral =
            saturation <=
            MAX_TEXT_SATURATION;

        // =========================================================================
        // PRIMARY FOREGROUND STRENGTH
        // =========================================================================

        int foreground_strength =
            0;

        // ---------------------------------------------------------------------
        // Strong neutral text.
        // ---------------------------------------------------------------------

        if (
            contrast >=
                static_cast<int>(
                    STRONG_CONTRAST_THRESHOLD
                ) &&
            neutral_enough
        ) {

            foreground_strength =
                std::min(
                    255,
                    contrast * 6
                );
        }

        // ---------------------------------------------------------------------
        // Moderate neutral text.
        //
        // Important for anti-aliased small fonts.
        // ---------------------------------------------------------------------

        else if (
            contrast >=
                static_cast<int>(
                    SOFT_CONTRAST_THRESHOLD
                ) &&
            neutral_enough
        ) {

            foreground_strength =
                std::min(
                    255,
                    contrast * 4
                );
        }

        // ---------------------------------------------------------------------
        // Moderately colored but dark text.
        //
        // Keep this weaker than neutral text so obvious colored chart lines
        // are still suppressed later by label geometry/OCR admission.
        // ---------------------------------------------------------------------

        else if (
            contrast >=
                static_cast<int>(
                    STRONG_CONTRAST_THRESHOLD
                ) &&
            moderately_neutral
        ) {

            foreground_strength =
                std::min(
                    255,
                    contrast * 3
                );
        }

        // =========================================================================
        // MICRO-CONTRAST RECOVERY
        // =========================================================================
        //
        // Extremely small anti-aliased characters may only differ from the
        // background by 8-12 grayscale levels.
        //
        // Preserve a low-strength signal instead of deleting it entirely.
        //
        // This is still safe because ChartLabelRecognizer has independent
        // geometry/text/confidence rejection.
        //
        // =========================================================================

        if (
            foreground_strength == 0 &&
            contrast >=
                static_cast<int>(
                    SOFT_CONTRAST_THRESHOLD
                ) &&
            moderately_neutral
        ) {

            foreground_strength =
                std::min(
                    96,
                    contrast * 2
                );
        }

        // =========================================================================
        // VERY DARK / VERY LIGHT PIXEL RECOVERY
        // =========================================================================
        //
        // Dark text on a light chart and light text on a dark chart should
        // survive even when saturation is not perfectly neutral.
        //
        // =========================================================================

        if (
            foreground_strength == 0
        ) {

            if (
                light_background
            ) {

                if (
                    gray <= 96 &&
                    contrast >= 6
                ) {

                    foreground_strength =
                        std::min(
                            255,
                            contrast * 2
                        );
                }

            } else {

                if (
                    gray >= 160 &&
                    contrast >= 6
                ) {

                    foreground_strength =
                        std::min(
                            255,
                            contrast * 2
                        );
                }
            }
        }

        // =========================================================================
        // WRITE CUSTOM CHANNELS
        // =========================================================================

        output[index + 0] =
            saturation;

        output[index + 1] =
            static_cast<uint8_t>(
                std::clamp(
                    foreground_strength,
                    0,
                    255
                )
            );

        output[index + 2] =
            delta;
    }

    return true;
}

} // namespace fin_ocr
