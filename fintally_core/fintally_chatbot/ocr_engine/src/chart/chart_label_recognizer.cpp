#include "fin_ocr/chart/chart_label_recognizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/line/line_recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ChartLabelRecognizer::ChartLabelRecognizer(
    const LineRecognizer& line_recognizer
) noexcept
    : line_recognizer_(line_recognizer)
{
}

// =============================================================================
// CHART LABEL RECOGNITION
//
// Input:
//
//     3-channel chart buffer
//
//     channel 0 = saturation
//     channel 1 = OCR foreground strength
//     channel 2 = chroma
//
// Strategy:
//
//     1. Build row-level foreground projection.
//     2. Detect contiguous text bands.
//     3. Merge only nearby bands.
//     4. Determine horizontal candidate bounds.
//     5. Reject tiny isolated regions.
//     6. Reject chart-wide geometry.
//     7. Convert chart channel 1 into a 1-channel OCR mask.
//     8. Recognize each candidate through LineRecognizer.
//     9. Reject obvious repeated-glyph garbage.
//    10. Deduplicate overlapping hypotheses.
//    11. Sort top-to-bottom / left-to-right.
// =============================================================================

std::string ChartLabelRecognizer::recognize(
    const uint8_t* chart_buffer,
    int width,
    int height
) const {

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        height <= 0
    ) {

        return
            "[CHART_TEXT_DATA]\n"
            "  - Invalid chart buffer.\n";
    }

    // =========================================================================
    // INTERNAL DETECTED-LINE REPRESENTATION
    // =========================================================================

    struct DetectedLine {

        int y = 0;
        int height = 0;

        int min_x = 0;
        int max_x = 0;

        std::string text;

        float density = 0.0f;

        std::size_t glyph_count = 0;
    };

    std::vector<DetectedLine> detected_lines;

    detected_lines.reserve(
        static_cast<std::size_t>(
            height / 12 + 8
        )
    );

    // =========================================================================
    // CHART OCR THRESHOLDS
    // =========================================================================

    constexpr uint8_t MIN_FOREGROUND =
        config::CHART_FOREGROUND_THRESHOLD;

    constexpr double MIN_ROW_DENSITY =
        config::CHART_MIN_ROW_DENSITY;

    constexpr int ROW_GAP =
        config::CHART_ROW_GAP;

    constexpr int MAX_BAND_HEIGHT =
        config::CHART_MAX_BAND_HEIGHT;

    constexpr int VERTICAL_PADDING =
        config::CHART_VERTICAL_PADDING;

    // =========================================================================
    // STEP 1: ROW PROJECTION
    // =========================================================================

    std::vector<int> row_counts(
        static_cast<std::size_t>(height),
        0
    );

    std::vector<int> row_min_x(
        static_cast<std::size_t>(height),
        width
    );

    std::vector<int> row_max_x(
        static_cast<std::size_t>(height),
        -1
    );

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) *
            3u;

        int count = 0;
        int min_x = width;
        int max_x = -1;

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t index =
                row +
                static_cast<std::size_t>(x) * 3u +
                1u;

            const uint8_t foreground_strength =
                chart_buffer[index];

            if (
                foreground_strength <
                MIN_FOREGROUND
            ) {

                continue;
            }

            ++count;

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
        }

        row_counts[
            static_cast<std::size_t>(y)
        ] = count;

        row_min_x[
            static_cast<std::size_t>(y)
        ] = min_x;

        row_max_x[
            static_cast<std::size_t>(y)
        ] = max_x;
    }

    // =========================================================================
    // STEP 2: DETECT ACTIVE TEXT ROWS
    // =========================================================================

    const int minimum_row_pixels =
        std::max(
            config::CHART_MIN_ROW_FOREGROUND_PIXELS,
            static_cast<int>(
                std::ceil(
                    static_cast<double>(width) *
                    MIN_ROW_DENSITY
                )
            )
        );

    struct TextBand {

        int y_start = 0;
        int y_end = 0;
    };

    std::vector<TextBand> raw_bands;

    raw_bands.reserve(
        static_cast<std::size_t>(
            height / 8 + 8
        )
    );

    int band_start = -1;
    int last_active_row = -1;

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        const bool active =
            row_counts[
                static_cast<std::size_t>(y)
            ] >=
            minimum_row_pixels;

        if (active) {

            if (
                band_start < 0
            ) {

                band_start = y;
            }

            last_active_row = y;

            continue;
        }

        if (
            band_start >= 0
        ) {

            const int gap =
                y -
                last_active_row -
                1;

            if (
                gap >
                ROW_GAP
            ) {

                raw_bands.push_back({
                    band_start,
                    last_active_row + 1
                });

                band_start = -1;
                last_active_row = -1;
            }
        }
    }

    if (
        band_start >= 0
    ) {

        raw_bands.push_back({
            band_start,
            last_active_row + 1
        });
    }

    // =========================================================================
    // STEP 3: MERGE NEARBY ROWS
    // =========================================================================

    std::vector<TextBand> merged_bands;

    merged_bands.reserve(
        raw_bands.size()
    );

    for (
        const TextBand& band :
        raw_bands
    ) {

        if (
            merged_bands.empty()
        ) {

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

        const int merged_height =
            band.y_end -
            previous.y_start;

        if (
            gap <= ROW_GAP &&
            merged_height <=
                MAX_BAND_HEIGHT
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
    // STEP 4: FALLBACK BAND
    // =========================================================================
    //
    // Some very sparse WebP chart labels can fail the normal row-band
    // segmentation. Before declaring the chart empty, calculate a global
    // foreground region.
    //
    // This fallback is intentionally bounded by MAX_BAND_HEIGHT.
    // =============================================================================

    if (
        merged_bands.empty()
    ) {

        int global_min_x =
            width;

        int global_max_x =
            -1;

        int global_min_y =
            height;

        int global_max_y =
            -1;

        std::size_t global_active_pixels =
            0;

        for (
            int y = 0;
            y < height;
            ++y
        ) {

            const std::size_t row_index =
                static_cast<std::size_t>(y);

            const int row_count =
                row_counts[row_index];

            if (
                row_count <= 0
            ) {
                continue;
            }

            global_min_y =
                std::min(
                    global_min_y,
                    y
                );

            global_max_y =
                std::max(
                    global_max_y,
                    y
                );

            global_min_x =
                std::min(
                    global_min_x,
                    row_min_x[row_index]
                );

            global_max_x =
                std::max(
                    global_max_x,
                    row_max_x[row_index]
                );

            global_active_pixels +=
                static_cast<std::size_t>(
                    row_count
                );
        }

        if (
            global_min_x <= global_max_x &&
            global_min_y <= global_max_y &&
            global_active_pixels > 0
        ) {

            const int global_height =
                global_max_y -
                global_min_y +
                1;

            const int global_width =
                global_max_x -
                global_min_x +
                1;

            if (
                global_height <=
                    MAX_BAND_HEIGHT &&
                global_width >=
                    config::CHART_MIN_HORIZONTAL_EXTENT
            ) {

                merged_bands.push_back({
                    global_min_y,
                    global_max_y + 1
                });
            }
        }
    }

    // =========================================================================
    // STEP 5: RECOGNIZE EACH DETECTED BAND
    // =========================================================================

    for (
        const TextBand& band :
        merged_bands
    ) {

        const int y0 =
            std::max(
                0,
                band.y_start -
                    VERTICAL_PADDING
            );

        const int y1 =
            std::min(
                height,
                band.y_end +
                    VERTICAL_PADDING
            );

        if (
            y0 >= y1
        ) {
            continue;
        }

        const int band_height =
            y1 -
            y0;

        if (
            band_height <= 1 ||
            band_height >
                MAX_BAND_HEIGHT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // DETERMINE BOUNDING X RANGE
        // ---------------------------------------------------------------------

        int min_x =
            width;

        int max_x =
            -1;

        std::size_t active_pixels =
            0;

        for (
            int y = y0;
            y < y1;
            ++y
        ) {

            const std::size_t row_index =
                static_cast<std::size_t>(y);

            const int row_count =
                row_counts[row_index];

            if (
                row_count <= 0
            ) {
                continue;
            }

            min_x =
                std::min(
                    min_x,
                    row_min_x[row_index]
                );

            max_x =
                std::max(
                    max_x,
                    row_max_x[row_index]
                );

            active_pixels +=
                static_cast<std::size_t>(
                    row_count
                );
        }

        // ---------------------------------------------------------------------
        // Validate candidate
        // ---------------------------------------------------------------------

        if (
            max_x < min_x ||
            active_pixels == 0
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Horizontal extent
        // ---------------------------------------------------------------------

        const int horizontal_extent =
            max_x -
            min_x +
            1;

        if (
            horizontal_extent <
            config::CHART_MIN_HORIZONTAL_EXTENT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Candidate width ratio
        // ---------------------------------------------------------------------

        const double width_ratio =
            width > 0
                ? static_cast<double>(
                      horizontal_extent
                  ) /
                  static_cast<double>(
                      width
                  )
                : 0.0;

        // ---------------------------------------------------------------------
        // Density
        // ---------------------------------------------------------------------

        const std::size_t total_pixels =
            static_cast<std::size_t>(
                width
            ) *
            static_cast<std::size_t>(
                band_height
            );

        const float density =
            total_pixels > 0
                ? static_cast<float>(
                      static_cast<double>(
                          active_pixels
                      ) /
                      static_cast<double>(
                          total_pixels
                      )
                  )
                : 0.0f;

        // ---------------------------------------------------------------------
        // Reject extremely sparse regions.
        // ---------------------------------------------------------------------

        if (
            density <
            static_cast<float>(
                config::CHART_MIN_BAND_DENSITY
            )
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // CHART GEOMETRY REJECTION
        // ---------------------------------------------------------------------

        if (
            width_ratio >=
            config::CHART_MAX_TEXT_WIDTH_RATIO
        ) {

            const bool dense_geometry =
                band_height <= 16 &&
                density >=
                    static_cast<float>(
                        config::CHART_FULL_WIDTH_DENSE_THRESHOLD
                    );

            const bool sparse_geometry =
                density <
                static_cast<float>(
                    config::CHART_FULL_WIDTH_SPARSE_THRESHOLD
                );

            if (
                dense_geometry ||
                sparse_geometry
            ) {

                continue;
            }
        }

        // =========================================================================
        // BUILD 1-CHANNEL OCR REPRESENTATION
        // =========================================================================
        //
        // ChartColorIsolator intentionally exposes three channels:
        //
        //     0 = saturation
        //     1 = foreground strength
        //     2 = chroma
        //
        // LineRecognizer must NOT interpret those channels as ordinary RGB.
        //
        // Extract channel 1 explicitly into a canonical 1-channel foreground
        // mask:
        //
        //     foreground -> 255
        //     background -> 0
        //
        // This creates a clean representation boundary:
        //
        //     chart representation
        //            |
        //            v
        //     channel-1 extraction
        //            |
        //            v
        //     binary OCR mask
        //            |
        //            v
        //     LineRecognizer
        //
        // =========================================================================

        const std::size_t line_pixels =
            static_cast<std::size_t>(
                width
            ) *
            static_cast<std::size_t>(
                band_height
            );

        std::vector<uint8_t> line_buffer(
            line_pixels,
            uint8_t{0}
        );

        for (
            int y = y0;
            y < y1;
            ++y
        ) {

            const std::size_t source_row =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) *
                3u;

            const std::size_t destination_row =
                static_cast<std::size_t>(
                    y - y0
                ) *
                static_cast<std::size_t>(
                    width
                );

            for (
                int x = 0;
                x < width;
                ++x
            ) {

                const std::size_t source_index =
                    source_row +
                    static_cast<std::size_t>(x) *
                        3u +
                    1u;

                const uint8_t foreground =
                    chart_buffer[
                        source_index
                    ];

                line_buffer[
                    destination_row +
                    static_cast<std::size_t>(x)
                ] =
                    foreground >=
                        MIN_FOREGROUND
                        ? uint8_t{255}
                        : uint8_t{0};
            }
        }

        // =========================================================================
        // HYBRID LINE RECOGNIZER
        // =========================================================================
        //
        // The recognizer now receives:
        //
        //     1 channel
        //     foreground = 255
        //     background = 0
        //
        // instead of the custom 3-channel chart representation.
        // =========================================================================

        std::string text =
            line_recognizer_.recognize(
                line_buffer.data(),
                width,
                0,
                band_height,
                1
            );

        if (
            text.empty()
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // TRIM LEADING WHITESPACE
        // ---------------------------------------------------------------------

        while (
            !text.empty() &&
            (
                text.front() == ' ' ||
                text.front() == '\t'
            )
        ) {

            text.erase(
                text.begin()
            );
        }

        // ---------------------------------------------------------------------
        // TRIM TRAILING WHITESPACE
        // ---------------------------------------------------------------------

        while (
            !text.empty() &&
            (
                text.back() == ' ' ||
                text.back() == '\t'
            )
        ) {

            text.pop_back();
        }

        if (
            text.empty()
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // REJECT REPEATED-GLYPH GARBAGE
        // ---------------------------------------------------------------------

        bool repeated =
            true;

        char first =
            '\0';

        std::size_t glyphs =
            0;

        for (
            char c :
            text
        ) {

            if (
                c == ' '
            ) {
                continue;
            }

            if (
                glyphs == 0
            ) {

                first =
                    c;

            } else if (
                c != first
            ) {

                repeated =
                    false;

                break;
            }

            ++glyphs;
        }

        if (
            repeated &&
            glyphs >= 3
        ) {

            continue;
        }

        // ---------------------------------------------------------------------
        // DEDUPLICATE OVERLAPPING BANDS
        // ---------------------------------------------------------------------

        bool duplicate =
            false;

        for (
            const DetectedLine& existing :
            detected_lines
        ) {

            const int existing_start =
                existing.y;

            const int existing_end =
                existing.y +
                existing.height;

            const int overlap_start =
                std::max(
                    existing_start,
                    y0
                );

            const int overlap_end =
                std::min(
                    existing_end,
                    y1
                );

            const int overlap =
                std::max(
                    0,
                    overlap_end -
                    overlap_start
                );

            const int smaller_height =
                std::min(
                    existing.height,
                    band_height
                );

            if (
                overlap > 0 &&
                overlap * 2 >=
                    smaller_height &&
                existing.text == text
            ) {

                duplicate =
                    true;

                break;
            }
        }

        if (
            duplicate
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // STORE DETECTED LINE
        // ---------------------------------------------------------------------

        detected_lines.push_back({
            y0,
            band_height,
            min_x,
            max_x,
            std::move(text),
            density,
            glyphs
        });
    }

    // =========================================================================
    // STEP 6: SORT
    // =========================================================================

    std::sort(
        detected_lines.begin(),
        detected_lines.end(),
        [](
            const DetectedLine& a,
            const DetectedLine& b
        ) noexcept {

            if (
                a.y != b.y
            ) {

                return a.y <
                       b.y;
            }

            return a.min_x <
                   b.min_x;
        }
    );

    // =========================================================================
    // STEP 7: OUTPUT
    // =========================================================================

    std::string result;

    result.reserve(
        64 +
        detected_lines.size() * 64
    );

    result +=
        "[CHART_TEXT_DATA]\n";

    if (
        detected_lines.empty()
    ) {

        result +=
            "  - No structured glyphs matched on chart axes.\n";

        return result;
    }

    for (
        std::size_t i = 0;
        i < detected_lines.size();
        ++i
    ) {

        const DetectedLine& line =
            detected_lines[i];

        result +=
            "  - Line " +
            std::to_string(
                i + 1
            );

        result +=
            " [Y:" +
            std::to_string(
                line.y
            );

        result +=
            "-" +
            std::to_string(
                line.y +
                line.height
            );

        result +=
            ", X:" +
            std::to_string(
                line.min_x
            );

        result +=
            "-" +
            std::to_string(
                line.max_x
            );

        result +=
            "]: " +
            line.text;

        result +=
            " [density=" +
            std::to_string(
                line.density
            ) +
            "]\n";
    }

    return result;
}

} // namespace fin_ocr
