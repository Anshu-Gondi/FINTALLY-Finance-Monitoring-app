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
//     4. Recognize each band through the existing hybrid line recognizer.
//     5. Deduplicate overlapping hypotheses.
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
        int y;
        int height;
        int min_x;
        int max_x;

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

            const uint8_t value =
                chart_buffer[index];

            if (
                value < MIN_FOREGROUND
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
            2,
            static_cast<int>(
                std::ceil(
                    static_cast<double>(width) *
                    MIN_ROW_DENSITY
                )
            )
        );

    struct TextBand {
        int y_start;
        int y_end;
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
            ] >= minimum_row_pixels;

        if (active) {

            if (band_start < 0) {
                band_start = y;
            }

            last_active_row = y;

            continue;
        }

        if (band_start >= 0) {

            const int gap =
                y -
                last_active_row -
                1;

            if (gap > ROW_GAP) {

                raw_bands.push_back({
                    band_start,
                    last_active_row + 1
                });

                band_start = -1;
                last_active_row = -1;
            }
        }
    }

    if (band_start >= 0) {

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

        if (
            gap <= ROW_GAP &&
            (
                band.y_end -
                previous.y_start
            ) <= MAX_BAND_HEIGHT
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
    // STEP 4: RECOGNIZE EACH DETECTED BAND
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

        if (y0 >= y1) {
            continue;
        }

        const int band_height =
            y1 - y0;

        if (
            band_height <= 1 ||
            band_height > MAX_BAND_HEIGHT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // DETERMINE BOUNDING X RANGE
        // ---------------------------------------------------------------------

        int min_x = width;
        int max_x = -1;

        std::size_t active_pixels = 0;

        for (
            int y = y0;
            y < y1;
            ++y
        ) {

            const int row_count =
                row_counts[
                    static_cast<std::size_t>(y)
                ];

            if (row_count <= 0) {
                continue;
            }

            min_x =
                std::min(
                    min_x,
                    row_min_x[
                        static_cast<std::size_t>(y)
                    ]
                );

            max_x =
                std::max(
                    max_x,
                    row_max_x[
                        static_cast<std::size_t>(y)
                    ]
                );

            active_pixels +=
                static_cast<std::size_t>(
                    row_count
                );
        }

        if (
            max_x < min_x ||
            active_pixels == 0
        ) {
            continue;
        }

        const std::size_t total_pixels =
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(band_height);

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
        // REJECT VERY LOW-DENSITY REGIONS
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
        // HYBRID LINE RECOGNIZER
        // ---------------------------------------------------------------------

        std::string text =
            line_recognizer_.recognize(
                chart_buffer,
                width,
                y0,
                y1,
                3
            );

        if (text.empty()) {
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

        if (text.empty()) {
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

            if (c == ' ') {
                continue;
            }

            if (glyphs == 0) {

                first = c;

            } else if (c != first) {

                repeated = false;

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

            const int overlap_start =
                std::max(
                    existing.y,
                    y0
                );

            const int overlap_end =
                std::min(
                    existing.y +
                        existing.height,
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

        if (duplicate) {
            continue;
        }

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
    // STEP 5: SORT TOP-TO-BOTTOM / LEFT-TO-RIGHT
    // =========================================================================

    std::sort(
        detected_lines.begin(),
        detected_lines.end(),
        [](
            const DetectedLine& a,
            const DetectedLine& b
        ) noexcept {

            if (a.y != b.y) {
                return a.y < b.y;
            }

            return a.min_x < b.min_x;
        }
    );

    // =========================================================================
    // STEP 6: OUTPUT
    // =========================================================================

    std::string result;

    result.reserve(
        64 +
        detected_lines.size() * 64
    );

    result +=
        "[CHART_TEXT_DATA]\n";

    if (detected_lines.empty()) {

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
