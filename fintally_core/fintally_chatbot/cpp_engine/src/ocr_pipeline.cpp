#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include "matrix_matcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

#ifdef FIN_USE_TESSERACT
#include <tesseract/baseapi.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <fpdfview.h>

#define FIN_CHECK(cond, msg)                                      \
    do {                                                          \
        if (!(cond)) {                                            \
            throw std::invalid_argument(                         \
                std::string("[FIN_ERROR] ") +                     \
                __FILE__ + ":" +                                  \
                std::to_string(__LINE__) +                       \
                " - " + (msg));                                  \
        }                                                         \
    } while (0)

namespace {

// =============================================================================
// INTERNAL CONSTANTS
// =============================================================================

constexpr uint8_t OCR_THRESHOLD = 100;

// MatrixMatcher/public binary threshold.
constexpr uint8_t MATRIX_BINARY_THRESHOLD = 160;

// Tesseract input normalization.
constexpr int TESSERACT_DPI = 300;

// PDF OCR rasterization.
constexpr double PDF_OCR_DPI = 300.0;

// Never allow a PDF OCR raster to explode memory on unusual pages.
constexpr size_t PDF_MAX_OCR_PIXELS =
    12'000'000;

// Minimum useful OCR dimensions.
constexpr int PDF_MIN_OCR_WIDTH = 1600;
constexpr int PDF_MIN_OCR_HEIGHT = 1200;

// Grayscale histogram normalization.
// We retain grayscale information rather than binarizing it.
constexpr int GRAY_LOW_PERCENTILE = 1;
constexpr int GRAY_HIGH_PERCENTILE = 99;

// Tesseract candidate limits.
constexpr size_t TESSERACT_MAX_TEXT_BYTES = 1'000'000;

// =============================================================================
// OCR FOREGROUND
// =============================================================================

inline bool is_ocr_foreground(
    uint8_t value
) noexcept {
    return value > 127;
}

// =============================================================================
// SAFE MULTIPLICATION
// =============================================================================

bool safe_mul(
    size_t a,
    size_t b,
    size_t& out
) noexcept {

#if defined(__GNUC__) || defined(__clang__)

    return !__builtin_mul_overflow(
        a,
        b,
        &out
    );

#else

    if (a != 0 &&
        b > std::numeric_limits<size_t>::max() / a) {

        return false;
    }

    out = a * b;
    return true;

#endif
}

// =============================================================================
// SAFE ADDITION
// =============================================================================

bool safe_add(
    size_t a,
    size_t b,
    size_t& out
) noexcept {

    if (b >
        std::numeric_limits<size_t>::max() - a) {

        return false;
    }

    out = a + b;
    return true;
}

// =============================================================================
// RAW-BUFFER FALLBACK
// =============================================================================

void copy_raw_fallback(
    const uint8_t* input,
    size_t input_len,
    uint8_t* output,
    size_t output_len
) noexcept {

    if (!input ||
        !output ||
        output_len == 0) {

        return;
    }

    const size_t copy_size =
        std::min(
            input_len,
            output_len
        );

    if (copy_size > 0) {

        std::memcpy(
            output,
            input,
            copy_size
        );
    }

    if (copy_size < output_len) {

        std::memset(
            output + copy_size,
            0,
            output_len - copy_size
        );
    }
}

// =============================================================================
// PIXEL HELPERS
// =============================================================================

inline uint8_t luminance_rgb(
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

// =============================================================================
// RGB -> GRAYSCALE
//
// This is the high-information OCR representation.
//
// IMPORTANT:
// Tesseract receives this representation.
// It must NOT be binarized before recognition.
// =============================================================================

void rgb_to_grayscale(
    const uint8_t* rgb,
    uint8_t* grayscale,
    size_t num_pixels
) noexcept {

    if (!rgb ||
        !grayscale ||
        num_pixels == 0) {

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const size_t idx =
            i * 3;

        grayscale[i] =
            luminance_rgb(
                rgb[idx + 0],
                rgb[idx + 1],
                rgb[idx + 2]
            );
    }
}

// =============================================================================
// BGRA -> GRAYSCALE
// =============================================================================

void bgra_to_grayscale(
    const uint8_t* bgra,
    uint8_t* grayscale,
    size_t num_pixels
) noexcept {

    if (!bgra ||
        !grayscale ||
        num_pixels == 0) {

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const size_t idx =
            i * 4;

        grayscale[i] =
            luminance_rgb(
                bgra[idx + 2], // R
                bgra[idx + 1], // G
                bgra[idx + 0]  // B
            );
    }
}

// =============================================================================
// GRAYSCALE -> BINARY
//
// MatrixMatcher/public representation only.
//
// Dark text -> 255
// Light background -> 0
// =============================================================================

void grayscale_to_binary(
    const uint8_t* grayscale,
    uint8_t* binary,
    size_t num_pixels
) noexcept {

    if (!grayscale ||
        !binary ||
        num_pixels == 0) {

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        binary[i] =
            grayscale[i] <
                MATRIX_BINARY_THRESHOLD

                ? uint8_t{255}

                : uint8_t{0};
    }
}

// =============================================================================
// RGB -> BINARY
// =============================================================================

void rgb_to_ocr_mask(
    const uint8_t* rgb,
    uint8_t* mask,
    size_t num_pixels
) noexcept {

    if (!rgb ||
        !mask ||
        num_pixels == 0) {

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const size_t idx =
            i * 3;

        const uint8_t gray =
            luminance_rgb(
                rgb[idx + 0],
                rgb[idx + 1],
                rgb[idx + 2]
            );

        mask[i] =
            gray <
                MATRIX_BINARY_THRESHOLD

                ? uint8_t{255}

                : uint8_t{0};
    }
}

// =============================================================================
// BGRA -> BINARY
// =============================================================================

void bgra_to_ocr_mask(
    const uint8_t* bgra,
    uint8_t* mask,
    size_t num_pixels
) noexcept {

    if (!bgra ||
        !mask ||
        num_pixels == 0) {

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const size_t idx =
            i * 4;

        const uint8_t gray =
            luminance_rgb(
                bgra[idx + 2],
                bgra[idx + 1],
                bgra[idx + 0]
            );

        mask[i] =
            gray <
                MATRIX_BINARY_THRESHOLD

                ? uint8_t{255}

                : uint8_t{0};
    }
}

// =============================================================================
// RESIZE GRAYSCALE - AREA/BOX AVERAGE WHEN DOWNSCALING
//
// This is better than nearest-neighbour for OCR.
//
// Downscaling by averaging preserves stroke information and suppresses
// single-pixel noise.
//
// Upscaling uses nearest-neighbour because no new information exists.
// =============================================================================

void resize_grayscale_ocr(
    const uint8_t* source,
    int source_width,
    int source_height,
    uint8_t* destination,
    int target_width,
    int target_height
) noexcept {

    if (!source ||
        !destination ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0) {

        return;
    }

    // -------------------------------------------------------------------------
    // Upscaling / same size.
    // -------------------------------------------------------------------------

    if (target_width >= source_width ||
        target_height >= source_height) {

        for (int y = 0;
             y < target_height;
             ++y) {

            const int src_y =
                std::min(
                    source_height - 1,
                    (
                        y * source_height
                    ) /
                    target_height
                );

            for (int x = 0;
                 x < target_width;
                 ++x) {

                const int src_x =
                    std::min(
                        source_width - 1,
                        (
                            x * source_width
                        ) /
                        target_width
                    );

                destination[
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(
                            target_width
                        ) +
                    static_cast<std::size_t>(x)
                ] =
                    source[
                        static_cast<std::size_t>(src_y) *
                            static_cast<std::size_t>(
                                source_width
                            ) +
                        static_cast<std::size_t>(src_x)
                    ];
            }
        }

        return;
    }

    // -------------------------------------------------------------------------
    // Genuine downscale.
    // -------------------------------------------------------------------------

    for (int dy = 0;
         dy < target_height;
         ++dy) {

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

        for (int dx = 0;
             dx < target_width;
             ++dx) {

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

            uint64_t sum = 0;
            uint32_t count = 0;

            for (int sy = src_y0;
                 sy < std::min(
                         src_y1,
                         source_height
                     );
                 ++sy) {

                const std::size_t row =
                    static_cast<std::size_t>(sy) *
                    static_cast<std::size_t>(
                        source_width
                    );

                for (int sx = src_x0;
                     sx < std::min(
                             src_x1,
                             source_width
                         );
                     ++sx) {

                    sum +=
                        source[
                            row +
                            static_cast<std::size_t>(
                                sx
                            )
                        ];

                    ++count;
                }
            }

            destination[
                static_cast<std::size_t>(dy) *
                    static_cast<std::size_t>(
                        target_width
                    ) +
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
// HISTOGRAM-BASED GRAYSCALE CONTRAST NORMALIZATION
//
// IMPORTANT:
//
// This never binarizes.
//
// It stretches the useful intensity range while preserving 256 grayscale
// levels. This gives Tesseract better stroke/background separation without
// destroying anti-aliased edges.
// =============================================================================

void normalize_grayscale_for_ocr(
    const uint8_t* input,
    uint8_t* output,
    size_t num_pixels
) noexcept {

    if (!input ||
        !output ||
        num_pixels == 0) {

        return;
    }

    std::array<size_t, 256> histogram{};
    histogram.fill(0);

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        ++histogram[input[i]];
    }

    const size_t low_target =
        std::max(
            size_t{1},
            (
                num_pixels *
                static_cast<size_t>(
                    GRAY_LOW_PERCENTILE
                )
            ) /
            100
        );

    const size_t high_target =
        std::max(
            low_target + 1,
            (
                num_pixels *
                static_cast<size_t>(
                    GRAY_HIGH_PERCENTILE
                )
            ) /
            100
        );

    size_t cumulative = 0;
    int low = 0;

    for (int value = 0;
         value < 256;
         ++value) {

        cumulative +=
            histogram[
                static_cast<std::size_t>(
                    value
                )
            ];

        if (cumulative >= low_target) {

            low = value;
            break;
        }
    }

    cumulative = 0;
    int high = 255;

    for (int value = 0;
         value < 256;
         ++value) {

        cumulative +=
            histogram[
                static_cast<std::size_t>(
                    value
                )
            ];

        if (cumulative >= high_target) {

            high = value;
            break;
        }
    }

    high =
        std::max(
            high,
            low + 8
        );

    const int range =
        high - low;

    if (range <= 0) {

        std::memcpy(
            output,
            input,
            num_pixels
        );

        return;
    }

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const int value =
            static_cast<int>(
                input[i]
            );

        if (value <= low) {

            output[i] =
                0;

        } else if (value >= high) {

            output[i] =
                255;

        } else {

            output[i] =
                static_cast<uint8_t>(
                    (
                        (
                            value - low
                        ) *
                        255
                    ) /
                    range
                );
        }
    }
}

// =============================================================================
// RGB -> CUSTOM CHART OCR CHANNELS
//
// NOT HSV.
//
// channel 0 = saturation
// channel 1 = OCR foreground strength
// channel 2 = chroma
// =============================================================================

void isolate_chart_color_channels(
    const uint8_t* rgb_in,
    uint8_t* out,
    size_t num_pixels
) {

    if (!rgb_in ||
        !out ||
        num_pixels == 0) {

        return;
    }

    constexpr uint8_t MAX_TEXT_SATURATION = 45;
    constexpr uint8_t MIN_TEXT_CONTRAST = 20;
    constexpr size_t HISTOGRAM_SAMPLE_STRIDE = 16;

    std::array<size_t, 256> histogram{};
    histogram.fill(0);

    size_t sampled_pixels = 0;

    for (size_t i = 0;
         i < num_pixels;
         i += HISTOGRAM_SAMPLE_STRIDE) {

        const size_t idx =
            i * 3;

        const uint8_t gray =
            luminance_rgb(
                rgb_in[idx + 0],
                rgb_in[idx + 1],
                rgb_in[idx + 2]
            );

        ++histogram[gray];
        ++sampled_pixels;
    }

    if (sampled_pixels == 0) {
        return;
    }

    const size_t median_position =
        sampled_pixels / 2;

    size_t cumulative = 0;
    uint8_t background_gray = 255;

    for (int value = 0;
         value < 256;
         ++value) {

        cumulative +=
            histogram[
                static_cast<size_t>(
                    value
                )
            ];

        if (cumulative >= median_position) {

            background_gray =
                static_cast<uint8_t>(
                    value
                );

            break;
        }
    }

    const bool dark_background =
        background_gray < 128;

    for (size_t i = 0;
         i < num_pixels;
         ++i) {

        const size_t idx =
            i * 3;

        const uint8_t r =
            rgb_in[idx + 0];

        const uint8_t g =
            rgb_in[idx + 1];

        const uint8_t b =
            rgb_in[idx + 2];

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

        const uint8_t saturation =
            cmax == 0
                ? 0
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

        const uint8_t gray =
            luminance_rgb(
                r,
                g,
                b
            );

        const bool sufficiently_neutral =
            saturation <=
            MAX_TEXT_SATURATION;

        bool foreground = false;
        uint8_t foreground_strength = 0;

        if (!dark_background) {

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

                foreground = true;

                foreground_strength =
                    static_cast<uint8_t>(
                        std::min(
                            255,
                            contrast
                        )
                    );
            }

        } else {

            const int contrast =
                static_cast<int>(gray) -
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

                foreground = true;

                foreground_strength =
                    static_cast<uint8_t>(
                        std::min(
                            255,
                            contrast
                        )
                    );
            }
        }

        out[idx + 0] =
            saturation;

        out[idx + 1] =
            foreground
                ? foreground_strength
                : 0;

        out[idx + 2] =
            delta;
    }
}

// =============================================================================
// MATRIXMATCHER DOCUMENT OCR
//
// Deterministic fallback recognizer.
// =============================================================================

std::string recognize_document_text_matrix(
    const uint8_t* image,
    int width,
    int height
) {

    if (!image ||
        width <= 0 ||
        height <= 0) {

        return {};
    }

    fin_ocr::MatrixMatcher matcher;

    std::string result;

    result.reserve(
        static_cast<std::size_t>(
            std::max(
                64,
                width / 4
            )
        )
    );

    // =========================================================================
    // STAGE 1: HORIZONTAL PROJECTION
    // =========================================================================

    std::vector<int> row_counts(
        static_cast<std::size_t>(
            height
        ),
        0
    );

    for (int y = 0;
         y < height;
         ++y) {

        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        int count = 0;

        for (int x = 0;
             x < width;
             ++x) {

            if (
                is_ocr_foreground(
                    image[
                        row +
                        static_cast<std::size_t>(
                            x
                        )
                    ]
                )
            ) {

                ++count;
            }
        }

        row_counts[
            static_cast<std::size_t>(y)
        ] = count;
    }

    const int minimum_row_foreground =
        std::max(
            1,
            static_cast<int>(
                std::ceil(
                    static_cast<double>(
                        width
                    ) *
                    0.0015
                )
            )
        );

    // =========================================================================
    // STAGE 2: ROW BANDS
    // =========================================================================

    struct TextBand {
        int y_start;
        int y_end;
    };

    std::vector<TextBand> raw_bands;

    raw_bands.reserve(
        static_cast<std::size_t>(
            height / 12 + 1
        )
    );

    int active_start = -1;
    int active_end = -1;

    constexpr int ROW_GAP_TOLERANCE = 2;

    for (int y = 0;
         y < height;
         ++y) {

        const bool active =
            row_counts[
                static_cast<std::size_t>(
                    y
                )
            ] >=
            minimum_row_foreground;

        if (active) {

            if (active_start < 0) {
                active_start = y;
            }

            active_end = y;

            continue;
        }

        if (active_start >= 0) {

            const int gap =
                y -
                active_end -
                1;

            if (
                gap >
                ROW_GAP_TOLERANCE
            ) {

                raw_bands.push_back({
                    active_start,
                    active_end + 1
                });

                active_start = -1;
                active_end = -1;
            }
        }
    }

    if (active_start >= 0) {

        raw_bands.push_back({
            active_start,
            active_end + 1
        });
    }

    if (raw_bands.empty()) {
        return {};
    }

    // =========================================================================
    // STAGE 3: MERGE CLOSE BANDS
    //
    // Strongly capped to avoid the previous runaway 64/96-pixel merged rows.
    // =========================================================================

    std::vector<TextBand> merged_bands;

    merged_bands.reserve(
        raw_bands.size()
    );

    constexpr int BASE_MERGE_GAP = 2;
    constexpr int MAX_MERGE_GAP = 6;

    for (const TextBand& band :
         raw_bands) {

        if (merged_bands.empty()) {

            merged_bands.push_back(
                band
            );

            continue;
        }

        TextBand& previous =
            merged_bands.back();

        const int gap =
            band.y_start -
            previous.y_end;

        const int previous_height =
            previous.y_end -
            previous.y_start;

        const int current_height =
            band.y_end -
            band.y_start;

        const int reference_height =
            std::max(
                previous_height,
                current_height
            );

        const int adaptive_gap =
            std::min(
                MAX_MERGE_GAP,
                std::max(
                    BASE_MERGE_GAP,
                    reference_height / 8
                )
            );

        if (
            gap <= adaptive_gap &&
            (
                previous_height <= 64 &&
                current_height <= 64
            )
        ) {

            previous.y_end =
                std::max(
                    previous.y_end,
                    band.y_end
                );

        } else {

            merged_bands.push_back(
                band
            );
        }
    }

    // =========================================================================
    // STAGE 4: RECOGNIZE
    // =========================================================================

    struct CandidateLine {
        int y;
        int height;
        std::string text;
    };

    std::vector<CandidateLine> candidates;

    candidates.reserve(
        merged_bands.size()
    );

    constexpr int VERTICAL_PADDING = 2;

    for (const TextBand& band :
         merged_bands) {

        const int y_start =
            std::max(
                0,
                band.y_start -
                    VERTICAL_PADDING
            );

        const int y_end =
            std::min(
                height,
                band.y_end +
                    VERTICAL_PADDING
            );

        if (y_start >= y_end) {
            continue;
        }

        const int band_height =
            y_end -
            y_start;

        if (band_height < 2) {
            continue;
        }

        if (
            band_height >
            std::max(
                64,
                height / 4
            )
        ) {

            continue;
        }

        std::string text =
            matcher.recognize_line(
                image,
                width,
                y_start,
                y_end,
                1
            );

        if (text.empty()) {
            continue;
        }

        if (
            text.find_first_not_of(' ') ==
            std::string::npos
        ) {

            continue;
        }

        candidates.push_back({
            y_start,
            band_height,
            std::move(text)
        });
    }

    if (candidates.empty()) {

        constexpr int FALLBACK_BAND_HEIGHT = 24;
        constexpr int FALLBACK_STRIDE = 24;

        for (int y = 0;
             y < height;
             y += FALLBACK_STRIDE) {

            const int y_end =
                std::min(
                    height,
                    y +
                        FALLBACK_BAND_HEIGHT
                );

            std::string text =
                matcher.recognize_line(
                    image,
                    width,
                    y,
                    y_end,
                    1
                );

            if (text.empty()) {
                continue;
            }

            if (
                text.find_first_not_of(' ') ==
                std::string::npos
            ) {

                continue;
            }

            candidates.push_back({
                y,
                y_end - y,
                std::move(text)
            });
        }
    }

    if (candidates.empty()) {
        return {};
    }

    // =========================================================================
    // STAGE 5: SORT
    // =========================================================================

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const CandidateLine& a,
           const CandidateLine& b) noexcept {

            if (a.y != b.y) {
                return a.y < b.y;
            }

            return a.height <
                   b.height;
        }
    );

    // =========================================================================
    // STAGE 6: DEDUP
    // =========================================================================

    std::vector<CandidateLine> selected;

    selected.reserve(
        candidates.size()
    );

    for (const CandidateLine& candidate :
         candidates) {

        bool duplicate = false;

        const int candidate_start =
            candidate.y;

        const int candidate_end =
            candidate.y +
            candidate.height;

        for (const CandidateLine& existing :
             selected) {

            const int existing_start =
                existing.y;

            const int existing_end =
                existing.y +
                existing.height;

            const int overlap_start =
                std::max(
                    candidate_start,
                    existing_start
                );

            const int overlap_end =
                std::min(
                    candidate_end,
                    existing_end
                );

            const int overlap =
                std::max(
                    0,
                    overlap_end -
                    overlap_start
                );

            const int smaller_height =
                std::min(
                    candidate.height,
                    existing.height
                );

            if (
                overlap > 0 &&
                overlap * 2 >=
                    smaller_height &&
                candidate.text ==
                    existing.text
            ) {

                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            selected.push_back(
                candidate
            );
        }
    }

    // =========================================================================
    // STAGE 7: CLEANUP
    // =========================================================================

    for (const CandidateLine& line :
         selected) {

        if (line.text.empty()) {
            continue;
        }

        std::string cleaned;

        cleaned.reserve(
            line.text.size()
        );

        bool previous_space = false;

        for (const char c :
             line.text) {

            if (
                c == ' ' ||
                c == '\t'
            ) {

                if (!previous_space) {
                    cleaned.push_back(' ');
                }

                previous_space = true;

            } else {

                cleaned.push_back(c);
                previous_space = false;
            }
        }

        while (
            !cleaned.empty() &&
            cleaned.front() == ' '
        ) {

            cleaned.erase(
                cleaned.begin()
            );
        }

        while (
            !cleaned.empty() &&
            cleaned.back() == ' '
        ) {

            cleaned.pop_back();
        }

        if (cleaned.empty()) {
            continue;
        }

        result += cleaned;
        result.push_back('\n');
    }

    return result;
}

// =============================================================================
// TESSERACT TEXT NORMALIZATION
// =============================================================================

std::string clean_tesseract_text(
    const char* raw
) {

    if (!raw) {
        return {};
    }

    const size_t raw_length =
        std::strlen(raw);

    if (
        raw_length == 0 ||
        raw_length >
            TESSERACT_MAX_TEXT_BYTES
    ) {

        return {};
    }

    std::string cleaned;

    cleaned.reserve(
        raw_length
    );

    bool previous_newline = false;
    bool previous_space = false;

    for (const char* p = raw;
         *p != '\0';
         ++p) {

        const unsigned char c =
            static_cast<unsigned char>(
                *p
            );

        if (c == '\r') {
            continue;
        }

        if (
            c == '\n'
        ) {

            if (!previous_newline) {

                while (
                    !cleaned.empty() &&
                    cleaned.back() == ' '
                ) {

                    cleaned.pop_back();
                }

                cleaned.push_back('\n');
            }

            previous_newline = true;
            previous_space = false;

            continue;
        }

        if (
            c == ' ' ||
            c == '\t'
        ) {

            if (!previous_space) {
                cleaned.push_back(' ');
            }

            previous_space = true;
            previous_newline = false;

            continue;
        }

        cleaned.push_back(
            static_cast<char>(c)
        );

        previous_space = false;
        previous_newline = false;
    }

    while (
        !cleaned.empty() &&
        (
            cleaned.back() == '\n' ||
            cleaned.back() == ' '
        )
    ) {

        cleaned.pop_back();
    }

    while (
        !cleaned.empty() &&
        (
            cleaned.front() == '\n' ||
            cleaned.front() == ' '
        )
    ) {

        cleaned.erase(
            cleaned.begin()
        );
    }

    return cleaned;
}

// =============================================================================
// TESSERACT PASS RESULT
// =============================================================================

struct TesseractPassResult {

    std::string text;

    int confidence = 0;

    int psm = 0;

    bool valid = false;
};

// =============================================================================
// TESSERACT SINGLE PASS
//
// IMPORTANT:
//
// SetImage() happens BEFORE SetSourceResolution().
// =============================================================================

TesseractPassResult recognize_tesseract_pass(
    const uint8_t* grayscale,
    int width,
    int height,
    tesseract::PageSegMode psm
) {

    TesseractPassResult result;

    result.psm =
        static_cast<int>(psm);

    if (
        !grayscale ||
        width <= 0 ||
        height <= 0
    ) {

        return result;
    }

    tesseract::TessBaseAPI api;

    if (
        api.Init(
            nullptr,
            "eng",
            tesseract::OEM_LSTM_ONLY
        ) != 0
    ) {

        return result;
    }

    api.SetPageSegMode(
        psm
    );

    // Better fit for financial data where dictionary-driven corrections can
    // turn unusual column labels into plausible but incorrect English words.
    api.SetVariable(
        "preserve_interword_spaces",
        "1"
    );

    api.SetVariable(
        "load_system_dawg",
        "0"
    );

    api.SetVariable(
        "load_freq_dawg",
        "0"
    );

    // -------------------------------------------------------------------------
    // CRITICAL ORDER
    // -------------------------------------------------------------------------

    api.SetImage(
        grayscale,
        width,
        height,
        1,
        width
    );

    api.SetSourceResolution(
        TESSERACT_DPI
    );

    if (
        api.Recognize(nullptr) != 0
    ) {

        api.End();
        return result;
    }

    result.confidence =
        std::max(
            0,
            api.MeanTextConf()
        );

    char* raw_text =
        api.GetUTF8Text();

    if (raw_text) {

        result.text =
            clean_tesseract_text(
                raw_text
            );

        delete[] raw_text;
    }

    api.End();

    result.valid =
        !result.text.empty();

    return result;
}

// =============================================================================
// TESSERACT RESULT SCORE
//
// We combine confidence with useful text density.
//
// This prevents a nearly empty result with a high confidence on one tiny token
// from beating a complete line/page result.
// =============================================================================

double score_tesseract_result(
    const TesseractPassResult& result
) {

    if (!result.valid ||
        result.text.empty()) {

        return -1.0;
    }

    size_t non_whitespace = 0;
    size_t total = 0;

    for (
        const unsigned char c :
        result.text
    ) {

        ++total;

        if (
            c != ' ' &&
            c != '\n' &&
            c != '\t'
        ) {

            ++non_whitespace;
        }
    }

    if (total == 0) {
        return -1.0;
    }

    const double density =
        static_cast<double>(
            non_whitespace
        ) /
        static_cast<double>(
            total
        );

    const double confidence =
        static_cast<double>(
            std::clamp(
                result.confidence,
                0,
                100
            )
        ) /
        100.0;

    // Confidence remains dominant, but complete textual output matters too.
    return
        confidence * 0.78 +
        density * 0.22;
}

// =============================================================================
// TESSERACT DOCUMENT OCR
//
// Multiple layout hypotheses.
//
// PDF:
//     AUTO              -> normal document/table layout
//     SPARSE_TEXT       -> separated table cells / labels
//     SINGLE_BLOCK      -> dense financial block
//     SINGLE_COLUMN     -> column-oriented statements
//     SPARSE_TEXT_OSD   -> sparse text with orientation handling
//     RAW_LINE          -> useful for strongly separated rows
//
// Receipt/image:
//     AUTO
//     SINGLE_BLOCK
//
// The best meaningful result wins.
// =============================================================================

std::string recognize_document_text_tesseract(
    const uint8_t* grayscale,
    int width,
    int height,
    FinInputType input_type
) {

#ifndef FIN_USE_TESSERACT

    (void)grayscale;
    (void)width;
    (void)height;
    (void)input_type;

    return {};

#else

    if (
        !grayscale ||
        width <= 0 ||
        height <= 0
    ) {
        return {};
    }

    // -------------------------------------------------------------------------
    // Candidate passes.
    // -------------------------------------------------------------------------

    std::vector<TesseractPassResult> passes;

    passes.reserve(8);

    if (
        input_type ==
        FIN_INPUT_PDF_PAGE
    ) {

        // ---------------------------------------------------------------------
        // 1. Automatic page segmentation.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_AUTO
            )
        );

        // ---------------------------------------------------------------------
        // 2. Sparse text.
        //
        // Very useful for scanned financial statements because labels and
        // numbers may be spatially separated.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_SPARSE_TEXT
            )
        );

        // ---------------------------------------------------------------------
        // 3. Single block.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_SINGLE_BLOCK
            )
        );

        // ---------------------------------------------------------------------
        // 4. Single column.
        //
        // Financial statements often contain strong vertical column geometry.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_SINGLE_COLUMN
            )
        );

        // ---------------------------------------------------------------------
        // 5. Single line-like sparse text.
        //
        // Tesseract's sparse mode sometimes fails to preserve individual
        // table rows. RAW_LINE gives another hypothesis.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_RAW_LINE
            )
        );

        // ---------------------------------------------------------------------
        // 6. Sparse text with OSD.
        //
        // Useful when raster orientation/geometry is slightly unusual.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_SPARSE_TEXT_OSD
            )
        );

    } else {

        // ---------------------------------------------------------------------
        // Ordinary image / receipt.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_AUTO
            )
        );

        passes.push_back(
            recognize_tesseract_pass(
                grayscale,
                width,
                height,
                tesseract::PSM_SINGLE_BLOCK
            )
        );
    }

    // -------------------------------------------------------------------------
    // Pick best candidate.
    // -------------------------------------------------------------------------

    const TesseractPassResult* best =
        nullptr;

    double best_score =
        -1.0;

    for (
        const TesseractPassResult& pass :
        passes
    ) {

        const double score =
            score_tesseract_result(
                pass
            );

        if (
            score >
            best_score
        ) {

            best_score = score;
            best = &pass;
        }
    }

    if (
        !best ||
        !best->valid
    ) {
        return {};
    }

    return best->text;

#endif
}

// =============================================================================
// HYBRID DOCUMENT RECOGNIZER
//
// PRIMARY:
//     Tesseract on grayscale.
//
// FALLBACK:
//     MatrixMatcher on binary.
//
// IMPORTANT:
// Tesseract never receives the binary MatrixMatcher representation.
// =============================================================================

std::string recognize_document_text(
    const uint8_t* grayscale,
    const uint8_t* binary_mask,
    int width,
    int height,
    FinInputType input_type
) {

    if (
        !grayscale ||
        width <= 0 ||
        height <= 0
    ) {

        return {};
    }

    const std::string tesseract_text =
        recognize_document_text_tesseract(
            grayscale,
            width,
            height,
            input_type
        );

    if (!tesseract_text.empty()) {
        return tesseract_text;
    }

    if (!binary_mask) {
        return {};
    }

    return recognize_document_text_matrix(
        binary_mask,
        width,
        height
    );
}

// =============================================================================
// PDF PAGE SIZE
// =============================================================================

bool get_pdf_page_dimensions(
    const uint8_t* pdf_bytes,
    size_t pdf_len,
    double& page_width_points,
    double& page_height_points
) {

    page_width_points = 0.0;
    page_height_points = 0.0;

    if (
        !pdf_bytes ||
        pdf_len == 0
    ) {

        return false;
    }

    FPDF_InitLibrary();

    FPDF_DOCUMENT doc =
        FPDF_LoadMemDocument(
            pdf_bytes,
            static_cast<int>(
                pdf_len
            ),
            nullptr
        );

    if (!doc) {

        FPDF_DestroyLibrary();

        return false;
    }

    FPDF_PAGE page =
        FPDF_LoadPage(
            doc,
            0
        );

    if (!page) {

        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();

        return false;
    }

    page_width_points =
        FPDF_GetPageWidth(
            page
        );

    page_height_points =
        FPDF_GetPageHeight(
            page
        );

    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();

    return
        page_width_points > 0.0 &&
        page_height_points > 0.0;
}

// =============================================================================
// COMPUTE PDF OCR RASTER SIZE
//
// Uses page points -> pixels at 300 DPI.
//
// A safe pixel ceiling prevents pathological documents from consuming huge
// amounts of memory.
//
// The caller can still choose a separate public output resolution.
// =============================================================================

void compute_pdf_ocr_size(
    double page_width_points,
    double page_height_points,
    int public_width,
    int public_height,
    int& ocr_width,
    int& ocr_height
) noexcept {

    ocr_width =
        std::max(
            1,
            public_width
        );

    ocr_height =
        std::max(
            1,
            public_height
        );

    if (
        page_width_points <= 0.0 ||
        page_height_points <= 0.0
    ) {

        return;
    }

    const double scale =
        PDF_OCR_DPI /
        72.0;

    const double requested_width =
        std::ceil(
            page_width_points *
            scale
        );

    const double requested_height =
        std::ceil(
            page_height_points *
            scale
        );

    if (
        requested_width <= 0.0 ||
        requested_height <= 0.0
    ) {

        return;
    }

    size_t requested_pixels = 0;

    const size_t req_w =
        static_cast<size_t>(
            requested_width
        );

    const size_t req_h =
        static_cast<size_t>(
            requested_height
        );

    if (
        !safe_mul(
            req_w,
            req_h,
            requested_pixels
        )
    ) {

        return;
    }

    double factor = 1.0;

    if (
        requested_pixels >
        PDF_MAX_OCR_PIXELS
    ) {

        factor =
            std::sqrt(
                static_cast<double>(
                    PDF_MAX_OCR_PIXELS
                ) /
                static_cast<double>(
                    requested_pixels
                )
            );
    }

    ocr_width =
        std::max(
            PDF_MIN_OCR_WIDTH,
            static_cast<int>(
                std::floor(
                    requested_width *
                    factor
                )
            )
        );

    ocr_height =
        std::max(
            PDF_MIN_OCR_HEIGHT,
            static_cast<int>(
                std::floor(
                    requested_height *
                    factor
                )
            )
        );

    // Preserve aspect ratio while respecting the pixel ceiling.
    size_t final_pixels = 0;

    if (
        safe_mul(
            static_cast<size_t>(
                ocr_width
            ),
            static_cast<size_t>(
                ocr_height
            ),
            final_pixels
        ) &&
        final_pixels >
            PDF_MAX_OCR_PIXELS
    ) {

        const double reduce =
            std::sqrt(
                static_cast<double>(
                    PDF_MAX_OCR_PIXELS
                ) /
                static_cast<double>(
                    final_pixels
                )
            );

        ocr_width =
            std::max(
                1,
                static_cast<int>(
                    std::floor(
                        static_cast<double>(
                            ocr_width
                        ) *
                        reduce
                    )
                )
            );

        ocr_height =
            std::max(
                1,
                static_cast<int>(
                    std::floor(
                        static_cast<double>(
                            ocr_height
                        ) *
                        reduce
                    )
                ));
    }
}

// =============================================================================
// PDF PAGE -> BGRA
// =============================================================================

bool decode_pdf_page_to_bgra(
    const uint8_t* pdf_bytes,
    size_t pdf_len,
    uint8_t* target,
    size_t max_bytes,
    int width,
    int height
) {

    if (
        !pdf_bytes ||
        !target ||
        width <= 0 ||
        height <= 0
    ) {

        return false;
    }

    size_t pixels = 0;
    size_t required = 0;

    if (
        !safe_mul(
            static_cast<size_t>(width),
            static_cast<size_t>(height),
            pixels
        )
    ) {

        return false;
    }

    if (
        !safe_mul(
            pixels,
            size_t(4),
            required
        )
    ) {

        return false;
    }

    if (
        required >
        max_bytes
    ) {

        return false;
    }

    FPDF_InitLibrary();

    FPDF_DOCUMENT doc =
        FPDF_LoadMemDocument(
            pdf_bytes,
            static_cast<int>(
                pdf_len
            ),
            nullptr
        );

    if (!doc) {

        FPDF_DestroyLibrary();

        return false;
    }

    FPDF_PAGE page =
        FPDF_LoadPage(
            doc,
            0
        );

    if (!page) {

        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();

        return false;
    }

    FPDF_BITMAP bitmap =
        FPDFBitmap_CreateEx(
            width,
            height,
            FPDFBitmap_BGRA,
            target,
            width * 4
        );

    if (!bitmap) {

        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();

        return false;
    }

    // White page background.
    FPDFBitmap_FillRect(
        bitmap,
        0,
        0,
        width,
        height,
        0xFFFFFFFF
    );

    FPDF_RenderPageBitmap(
        bitmap,
        page,
        0,
        0,
        width,
        height,
        0,
        0
    );

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();

    return true;
}

// =============================================================================
// RESIZE BGRA -> GRAYSCALE
//
// Used when high-resolution PDF rasterization needs to be reduced to the
// public output dimensions.
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
        !source_bgra ||
        !destination_gray ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0
    ) {

        return;
    }

    if (
        target_width >= source_width ||
        target_height >= source_height
    ) {

        for (int y = 0;
             y < target_height;
             ++y) {

            const int src_y =
                std::min(
                    source_height - 1,
                    (
                        y * source_height
                    ) /
                    target_height
                );

            for (int x = 0;
                 x < target_width;
                 ++x) {

                const int src_x =
                    std::min(
                        source_width - 1,
                        (
                            x * source_width
                        ) /
                        target_width
                    );

                const std::size_t src_index =
                    (
                        static_cast<std::size_t>(
                            src_y
                        ) *
                        static_cast<std::size_t>(
                            source_width
                        ) +
                        static_cast<std::size_t>(
                            src_x
                        )
                    ) * 4u;

                destination_gray[
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(
                            target_width
                        ) +
                    static_cast<std::size_t>(x)
                ] =
                    luminance_rgb(
                        source_bgra[src_index + 2],
                        source_bgra[src_index + 1],
                        source_bgra[src_index + 0]
                    );
            }
        }

        return;
    }

    // Downscale through temporary grayscale source.
    size_t source_pixels = 0;

    if (
        !safe_mul(
            static_cast<size_t>(
                source_width
            ),
            static_cast<size_t>(
                source_height
            ),
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
// FORCE BINARY
// =============================================================================

inline void force_binary_inplace(
    uint8_t* buffer,
    size_t count
) noexcept {

    if (
        !buffer ||
        count == 0
    ) {

        return;
    }

    for (size_t i = 0;
         i < count;
         ++i) {

        buffer[i] =
            buffer[i] >= 128
                ? 255
                : 0;
    }
}

// =============================================================================
// RESIZE RGB NEAREST
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
        !source ||
        !destination ||
        source_width <= 0 ||
        source_height <= 0 ||
        target_width <= 0 ||
        target_height <= 0
    ) {

        return;
    }

    for (int y = 0;
         y < target_height;
         ++y) {

        const int src_y =
            std::min(
                source_height - 1,
                (
                    y * source_height
                ) /
                target_height
            );

        for (int x = 0;
             x < target_width;
             ++x) {

            const int src_x =
                std::min(
                    source_width - 1,
                    (
                        x * source_width
                    ) /
                    target_width
            );

            const std::size_t src_index =
                (
                    static_cast<std::size_t>(
                        src_y
                    ) *
                    static_cast<std::size_t>(
                        source_width
                    ) +
                    static_cast<std::size_t>(
                        src_x
                    )
                ) * 3u;

            const std::size_t dst_index =
                (
                    static_cast<std::size_t>(
                        y
                    ) *
                    static_cast<std::size_t>(
                        target_width
                    ) +
                    static_cast<std::size_t>(
                        x
                    )
                ) * 3u;

            destination[dst_index + 0] =
                source[src_index + 0];

            destination[dst_index + 1] =
                source[src_index + 1];

            destination[dst_index + 2] =
                source[src_index + 2];
        }
    }
}

} // anonymous namespace

// =============================================================================
// MAIN NATIVE VISION PIPELINE
// =============================================================================

extern "C"
FinProcessedBuffer* execute_vision_pipeline(
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
) {

    // =========================================================================
    // VALIDATION
    // =========================================================================

    FIN_CHECK(
        input_bytes != nullptr,
        "Input buffer is null."
    );

    FIN_CHECK(
        input_len > 0,
        "Input buffer is empty."
    );

    FIN_CHECK(
        target_width > 0 &&
        target_height > 0,
        "Target dimensions must be strictly positive."
    );

    FIN_CHECK(
        target_channels > 0,
        "Target channel count must be strictly positive."
    );

    // =========================================================================
    // SIZE CALCULATIONS
    // =========================================================================

    size_t num_pixels = 0;
    size_t data_len = 0;

    FIN_CHECK(
        safe_mul(
            target_width,
            target_height,
            num_pixels
        ),
        "Integer overflow detected in width * height."
    );

    FIN_CHECK(
        safe_mul(
            num_pixels,
            target_channels,
            data_len
        ),
        "Integer overflow detected in total buffer calculation."
    );

    // =========================================================================
    // RESULT ALLOCATION
    // =========================================================================

    auto* result =
        new (std::nothrow)
        FinProcessedBuffer();

    if (!result) {
        return nullptr;
    }

    result->width =
        target_width;

    result->height =
        target_height;

    result->channels =
        target_channels;

    result->data_len =
        data_len;

    result->is_binarized =
        0;

    result->extracted_text =
        nullptr;

    result->data =
        static_cast<uint8_t*>(
            fin::ops::aligned_alloc(
                64,
                data_len
            )
        );

    if (!result->data) {

        delete result;

        return nullptr;
    }

    std::memset(
        result->data,
        0,
        data_len
    );

    // =========================================================================
    // OCR BUFFERS
    //
    // grayscale:
    //     preserved, high-information Tesseract input.
    //
    // binary:
    //     deterministic MatrixMatcher/public binary representation.
    // =========================================================================

    uint8_t* ocr_grayscale = nullptr;
    uint8_t* ocr_mask = nullptr;

    bool owns_ocr_mask = false;

    bool decode_success = false;

    // OCR buffer dimensions.
    // Normal images use the public dimensions.
    // PDFs may use a higher-resolution OCR raster.
    int ocr_width =
        static_cast<int>(
            target_width
        );

    int ocr_height =
        static_cast<int>(
            target_height
        );

    size_t ocr_pixels = 0;

    FIN_CHECK(
        safe_mul(
            static_cast<size_t>(ocr_width),
            static_cast<size_t>(ocr_height),
            ocr_pixels
        ),
        "OCR width * height overflow."
    );

    // =========================================================================
    // PDF PATH
    // =========================================================================

    if (
        input_type ==
        FIN_INPUT_PDF_PAGE
    ) {

        FIN_CHECK(
            target_channels == 4 ||
            target_channels == 1,
            "PDF target_channels must be 1 or 4."
        );

        // ---------------------------------------------------------------------
        // Determine PDF page dimensions.
        // ---------------------------------------------------------------------

        double page_width_points = 0.0;
        double page_height_points = 0.0;

        int pdf_ocr_width =
            static_cast<int>(
                target_width
            );

        int pdf_ocr_height =
            static_cast<int>(
                target_height
            );

        if (
            get_pdf_page_dimensions(
                input_bytes,
                input_len,
                page_width_points,
                page_height_points
            )
        ) {

            compute_pdf_ocr_size(
                page_width_points,
                page_height_points,
                static_cast<int>(
                    target_width
                ),
                static_cast<int>(
                    target_height
                ),
                pdf_ocr_width,
                pdf_ocr_height
            );
        }

        ocr_width =
            pdf_ocr_width;

        ocr_height =
            pdf_ocr_height;

        FIN_CHECK(
            safe_mul(
                static_cast<size_t>(ocr_width),
                static_cast<size_t>(ocr_height),
                ocr_pixels
            ),
            "PDF OCR pixel count overflow."
        );

        // ---------------------------------------------------------------------
        // High-resolution PDF raster for Tesseract.
        // ---------------------------------------------------------------------

        size_t pdf_ocr_pixels = 0;
        size_t pdf_ocr_bgra_bytes = 0;

        const bool pdf_ocr_size_ok =
            safe_mul(
                static_cast<size_t>(
                    pdf_ocr_width
                ),
                static_cast<size_t>(
                    pdf_ocr_height
                ),
                pdf_ocr_pixels
            ) &&
            safe_mul(
                pdf_ocr_pixels,
                size_t(4),
                pdf_ocr_bgra_bytes
            );

        if (pdf_ocr_size_ok) {

            uint8_t* pdf_ocr_bgra =
                static_cast<uint8_t*>(
                    fin::ops::aligned_alloc(
                        64,
                        pdf_ocr_bgra_bytes
                    )
                );

            if (pdf_ocr_bgra) {

                const bool pdf_ocr_rendered =
                    decode_pdf_page_to_bgra(
                        input_bytes,
                        input_len,
                        pdf_ocr_bgra,
                        pdf_ocr_bgra_bytes,
                        pdf_ocr_width,
                        pdf_ocr_height
                    );

                if (pdf_ocr_rendered) {

                    // ---------------------------------------------------------
                    // OCR grayscale keeps high-resolution information.
                    // ---------------------------------------------------------

                    ocr_grayscale =
                        static_cast<uint8_t*>(
                            fin::ops::aligned_alloc(
                                64,
                                pdf_ocr_pixels
                            )
                        );

                    if (ocr_grayscale) {

                        bgra_to_grayscale(
                            pdf_ocr_bgra,
                            ocr_grayscale,
                            pdf_ocr_pixels
                        );

                        // -----------------------------------------------------
                        // Contrast normalize without binarization.
                        // -----------------------------------------------------

                        uint8_t* normalized =
                            static_cast<uint8_t*>(
                                fin::ops::aligned_alloc(
                                    64,
                                    pdf_ocr_pixels
                                )
                            );

                        if (normalized) {

                            normalize_grayscale_for_ocr(
                                ocr_grayscale,
                                normalized,
                                pdf_ocr_pixels
                            );

                            std::memcpy(
                                ocr_grayscale,
                                normalized,
                                pdf_ocr_pixels
                            );

                            fin::ops::aligned_free(
                                normalized
                            );
                        }
                    }
                }

                // -------------------------------------------------------------
                // Public PDF buffer.
                //
                // For target_channels == 1:
                //     create public binary output at requested dimensions.
                //
                // For target_channels == 4:
                //     produce requested-size BGRA output separately.
                // -------------------------------------------------------------

                if (
                    target_channels == 1
                ) {

                    size_t target_bgra_bytes = 0;

                    const bool target_bgra_ok =
                        safe_mul(
                            num_pixels,
                            size_t(4),
                            target_bgra_bytes
                        );

                    if (target_bgra_ok) {

                        uint8_t* target_bgra =
                            static_cast<uint8_t*>(
                                fin::ops::aligned_alloc(
                                    64,
                                    target_bgra_bytes
                                )
                            );

                        if (target_bgra) {

                            const bool public_render_ok =
                                decode_pdf_page_to_bgra(
                                    input_bytes,
                                    input_len,
                                    target_bgra,
                                    target_bgra_bytes,
                                    static_cast<int>(
                                        target_width
                                    ),
                                    static_cast<int>(
                                        target_height
                                    )
                                );

                            if (
                                public_render_ok
                            ) {

                                bgra_to_ocr_mask(
                                    target_bgra,
                                    result->data,
                                    num_pixels
                                );

                                result->is_binarized =
                                    1;

                                decode_success =
                                    ocr_grayscale != nullptr;
                            }

                            fin::ops::aligned_free(
                                target_bgra
                            );
                        }
                    }

                } else {

                    // target_channels == 4
                    //
                    // Render directly at requested public dimensions.

                    const bool public_render_ok =
                        decode_pdf_page_to_bgra(
                            input_bytes,
                            input_len,
                            result->data,
                            data_len,
                            static_cast<int>(
                                target_width
                            ),
                            static_cast<int>(
                                target_height
                            )
                        );

                    decode_success =
                        public_render_ok &&
                        (
                            ocr_grayscale !=
                            nullptr
                        );

                    if (
                        public_render_ok
                    ) {

                        result->is_binarized =
                            0;
                    }
                }

                fin::ops::aligned_free(
                    pdf_ocr_bgra
                );
            }
        }

    } else {

        // =========================================================================
        // ORDINARY IMAGE / RECEIPT / CHART PATH
        // =========================================================================

        int decoded_width = 0;
        int decoded_height = 0;
        int source_channels = 0;

        unsigned char* decoded =
            stbi_load_from_memory(
                input_bytes,
                static_cast<int>(
                    input_len
                ),
                &decoded_width,
                &decoded_height,
                &source_channels,
                3
            );

        if (decoded) {

            size_t source_pixels = 0;
            size_t source_bytes = 0;

            const bool source_size_ok =
                safe_mul(
                    static_cast<size_t>(
                        decoded_width
                    ),
                    static_cast<size_t>(
                        decoded_height
                    ),
                    source_pixels
                ) &&
                safe_mul(
                    source_pixels,
                    size_t(3),
                    source_bytes
                );

            if (source_size_ok) {

                if (
                    target_channels == 3
                ) {

                    resize_rgb_nearest(
                        decoded,
                        decoded_width,
                        decoded_height,
                        result->data,
                        static_cast<int>(
                            target_width
                        ),
                        static_cast<int>(
                            target_height
                        )
                    );

                    // ---------------------------------------------------------
                    // Preserve grayscale OCR representation for document/image.
                    //
                    // Charts do not use this representation later.
                    // ---------------------------------------------------------

                    if (
                        input_type !=
                        FIN_INPUT_FIN_CHART
                    ) {

                        ocr_grayscale =
                            static_cast<uint8_t*>(
                                fin::ops::aligned_alloc(
                                    64,
                                    num_pixels
                                )
                            );

                        if (ocr_grayscale) {

                            rgb_to_grayscale(
                                result->data,
                                ocr_grayscale,
                                num_pixels
                            );

                            uint8_t* normalized =
                                static_cast<uint8_t*>(
                                    fin::ops::aligned_alloc(
                                        64,
                                        num_pixels
                                    )
                                );

                            if (normalized) {

                                normalize_grayscale_for_ocr(
                                    ocr_grayscale,
                                    normalized,
                                    num_pixels
                                );

                                std::memcpy(
                                    ocr_grayscale,
                                    normalized,
                                    num_pixels
                                );

                                fin::ops::aligned_free(
                                    normalized
                                );
                            }
                        }
                    }

                    decode_success =
                        true;

                } else if (
                    target_channels == 1
                ) {

                    size_t target_rgb_bytes = 0;

                    const bool target_rgb_ok =
                        safe_mul(
                            num_pixels,
                            size_t(3),
                            target_rgb_bytes
                        );

                    if (target_rgb_ok) {

                        uint8_t* resized_rgb =
                            static_cast<uint8_t*>(
                                fin::ops::aligned_alloc(
                                    64,
                                    target_rgb_bytes
                                )
                            );

                        if (resized_rgb) {

                            resize_rgb_nearest(
                                decoded,
                                decoded_width,
                                decoded_height,
                                resized_rgb,
                                static_cast<int>(
                                    target_width
                                ),
                                static_cast<int>(
                                    target_height
                                )
                            );

                            if (
                                input_type !=
                                FIN_INPUT_FIN_CHART
                            ) {

                                ocr_grayscale =
                                    static_cast<uint8_t*>(
                                        fin::ops::aligned_alloc(
                                            64,
                                            num_pixels
                                        )
                                    );

                                if (ocr_grayscale) {

                                    rgb_to_grayscale(
                                        resized_rgb,
                                        ocr_grayscale,
                                        num_pixels
                                    );

                                    uint8_t* normalized =
                                        static_cast<uint8_t*>(
                                            fin::ops::aligned_alloc(
                                                64,
                                                num_pixels
                                            )
                                        );

                                    if (normalized) {

                                        normalize_grayscale_for_ocr(
                                            ocr_grayscale,
                                            normalized,
                                            num_pixels
                                        );

                                        std::memcpy(
                                            ocr_grayscale,
                                            normalized,
                                            num_pixels
                                        );

                                        fin::ops::aligned_free(
                                            normalized
                                        );
                                    }

                                    grayscale_to_binary(
                                        ocr_grayscale,
                                        result->data,
                                        num_pixels
                                    );

                                    result->is_binarized =
                                        1;

                                    decode_success =
                                        true;
                                }

                            } else {

                                // Chart target_channels == 1 is still not a
                                // supported chart OCR path.

                                rgb_to_ocr_mask(
                                    resized_rgb,
                                    result->data,
                                    num_pixels
                                );

                                result->is_binarized =
                                    1;

                                decode_success =
                                    true;
                            }

                            fin::ops::aligned_free(
                                resized_rgb
                            );
                        }
                    }
                }
            }

            stbi_image_free(
                decoded
            );
        }
    }

    // =========================================================================
    // LEGACY RAW BUFFER FALLBACK
    // =========================================================================

    if (!decode_success) {

        copy_raw_fallback(
            input_bytes,
            input_len,
            result->data,
            result->data_len
        );

        if (
            input_type ==
                FIN_INPUT_PDF_PAGE &&
            target_channels == 1
        ) {

            force_binary_inplace(
                result->data,
                result->data_len
            );

            result->is_binarized =
                1;

        } else {

            result->is_binarized =
                0;
        }
    }

    // =========================================================================
    // FINANCIAL CHART NON-RGB PATH
    // =========================================================================

    if (
        input_type ==
            FIN_INPUT_FIN_CHART &&
        target_channels != 3
    ) {

        if (ocr_grayscale) {

            fin::ops::aligned_free(
                ocr_grayscale
            );

            ocr_grayscale =
                nullptr;
        }

        result->is_binarized =
            0;

        return result;
    }

    // =========================================================================
    // THERMAL METRICS
    // =========================================================================

    const fin::ThermalMetrics thermal =
        fin::ThermalMonitor::instance()
            .read_metrics();

    (void)thermal;

    // =========================================================================
    // FINANCIAL CHART RGB PATH
    // =========================================================================

    if (
        input_type ==
            FIN_INPUT_FIN_CHART &&
        target_channels == 3
    ) {

        uint8_t* chart_ocr =
            static_cast<uint8_t*>(
                fin::ops::aligned_alloc(
                    64,
                    data_len
                )
            );

        if (chart_ocr) {

            isolate_chart_color_channels(
                result->data,
                chart_ocr,
                num_pixels
            );

            fin_ocr::MatrixMatcher matcher;

            const std::string chart_text =
                matcher.recognize_chart_labels(
                    chart_ocr,
                    static_cast<int>(
                        target_width
                    ),
                    static_cast<int>(
                        target_height
                    )
                );

            if (!chart_text.empty()) {

                result->extracted_text =
                    static_cast<char*>(
                        std::malloc(
                            chart_text.size() +
                            1
                        )
                    );

                if (
                    result->extracted_text
                ) {

                    std::memcpy(
                        result->extracted_text,
                        chart_text.c_str(),
                        chart_text.size() + 1
                    );
                }
            }

            std::memcpy(
                result->data,
                chart_ocr,
                data_len
            );

            fin::ops::aligned_free(
                chart_ocr
            );
        }

        if (ocr_grayscale) {

            fin::ops::aligned_free(
                ocr_grayscale
            );

            ocr_grayscale =
                nullptr;
        }

        result->is_binarized =
            0;

        return result;
    }

    // =========================================================================
    // DOCUMENT / IMAGE OCR PATH
    // =========================================================================

    if (
        input_type == FIN_INPUT_PDF_PAGE
    ) {

        // PDF OCR uses the SAME high-resolution raster dimensions
        // as ocr_grayscale.

        if (
            !ocr_mask &&
            ocr_grayscale &&
            ocr_pixels > 0
        ) {

            ocr_mask =
                static_cast<uint8_t*>(
                    fin::ops::aligned_alloc(
                        64,
                        ocr_pixels
                    )
                );

            if (ocr_mask) {

                grayscale_to_binary(
                    ocr_grayscale,
                    ocr_mask,
                    ocr_pixels
                );

                owns_ocr_mask =
                    true;
            }
        }

    } else if (
        target_channels == 1
    ) {

        // Normal 1-channel image.
        ocr_mask =
            result->data;

    } else if (
        target_channels == 3
    ) {

        // Normal RGB image.
        ocr_mask =
            static_cast<uint8_t*>(
                fin::ops::aligned_alloc(
                    64,
                    num_pixels
                )
            );

        if (ocr_mask) {

            rgb_to_ocr_mask(
                result->data,
                ocr_mask,
                num_pixels
            );

            owns_ocr_mask =
                true;
        }
    }

    // =========================================================================
    // RAW / UNUSUAL INPUT FALLBACK
    // =========================================================================

    if (!ocr_grayscale) {

        ocr_grayscale =
            static_cast<uint8_t*>(
                fin::ops::aligned_alloc(
                    64,
                    num_pixels
                )
            );

        if (ocr_grayscale) {

            if (
                target_channels == 1
            ) {

                // -------------------------------------------------------------
                // Binary public representation is the only information left.
                // -------------------------------------------------------------

                for (size_t i = 0;
                     i < num_pixels;
                     ++i) {

                    ocr_grayscale[i] =
                        result->data[i] != 0
                            ? uint8_t{0}
                            : uint8_t{255};
                }

            } else if (
                target_channels == 3
            ) {

                rgb_to_grayscale(
                    result->data,
                    ocr_grayscale,
                    num_pixels
                );

            } else {

                std::memset(
                    ocr_grayscale,
                    255,
                    num_pixels
                );
            }
        }
    }

    // =============================================================================
    // HYBRID OCR
    // =============================================================================

    if (
        ocr_mask &&
        ocr_grayscale
    ) {

        // -------------------------------------------------------------------------
        // IMPORTANT:
        //
        // Tesseract receives preserved grayscale.
        //
        // MatrixMatcher receives binary.
        //
        // PDF uses the high-resolution OCR dimensions.
        //
        // Normal images use the public target dimensions.
        // -------------------------------------------------------------------------

        const std::string text =
            recognize_document_text(
                ocr_grayscale,
                ocr_mask,
                ocr_width,
                ocr_height,
                input_type
            );

        if (!text.empty()) {

            if (
                result->extracted_text
            ) {

                std::free(
                    result->extracted_text
                );

                result->extracted_text =
                    nullptr;
            }

            result->extracted_text =
                static_cast<char*>(
                    std::malloc(
                        text.size() + 1
                    )
                );

            if (
                result->extracted_text
            ) {

                std::memcpy(
                    result->extracted_text,
                    text.c_str(),
                    text.size() + 1
                );
            }
        }

        // -------------------------------------------------------------------------
        // Public contract.
        // -------------------------------------------------------------------------

        if (
            target_channels == 1
        ) {

            result->is_binarized =
                1;
        }

        // -------------------------------------------------------------------------
        // Free only owned OCR mask.
        // -------------------------------------------------------------------------

        if (
            owns_ocr_mask &&
            ocr_mask
        ) {

            fin::ops::aligned_free(
                ocr_mask
            );

            ocr_mask =
                nullptr;

            owns_ocr_mask =
                false;
        }
    }

    // =========================================================================
    // FREE OCR GRAYSCALE
    // =========================================================================

    if (ocr_grayscale) {

        fin::ops::aligned_free(
            ocr_grayscale
        );

        ocr_grayscale =
            nullptr;
    }

    // =========================================================================
    // FINAL PDF BINARY CONTRACT
    // =========================================================================

    if (
        input_type ==
            FIN_INPUT_PDF_PAGE &&
        target_channels == 1 &&
        result->data != nullptr
    ) {

        force_binary_inplace(
            result->data,
            result->data_len
        );

        result->is_binarized =
            1;
    }

    return result;
}
