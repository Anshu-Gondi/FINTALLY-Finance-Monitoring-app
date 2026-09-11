#include "fin_ocr/line/line_recognizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/pixel_access.hpp"
#include "fin_ocr/matrix/glyph_matcher.hpp"
#include "fin_ocr/segmentation/connected_components.hpp"
#include "fin_ocr/tesseract/tesseract_recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fin_ocr {

// =============================================================================
// CONSTRUCTOR
// =============================================================================

LineRecognizer::LineRecognizer(
    const GlyphMatcher& glyph_matcher,
    const TesseractRecognizer& tesseract_recognizer
) noexcept
    : glyph_matcher_(glyph_matcher),
      tesseract_recognizer_(tesseract_recognizer)
{
}

// =============================================================================
// MATRIXMATCHER FALLBACK LINE RECOGNITION
// =============================================================================

std::string LineRecognizer::recognize_matrix_fallback(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels,
    const std::vector<BoundingBox>& char_boxes
) const {

    if (
        image == nullptr ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0 ||
        char_boxes.empty()
    ) {
        return {};
    }

    std::string recognized_text;

    recognized_text.reserve(
        char_boxes.size() * 2
    );

    int last_max_x =
        -1;

    constexpr float SPACE_GAP_FACTOR =
        config::SPACE_GAP_FACTOR;

    for (
        const BoundingBox& box :
        char_boxes
    ) {

        const int patch_w =
            box.width();

        const int patch_h =
            box.height();

        if (
            patch_w <= 0 ||
            patch_h <= 0
        ) {
            continue;
        }

        // =========================================================================
        // CHARACTER SPACING
        // =========================================================================

        if (
            last_max_x >= 0
        ) {

            const int gap =
                box.min_x -
                last_max_x -
                1;

            const int reference_height =
                std::max(
                    1,
                    patch_h
                );

            const int space_gap =
                std::max(
                    2,
                    static_cast<int>(
                        std::round(
                            reference_height *
                            SPACE_GAP_FACTOR
                        )
                    )
                );

            if (
                gap > space_gap
            ) {
                recognized_text.push_back(
                    ' '
                );
            }
        }

        // =========================================================================
        // COPY PATCH
        // =========================================================================

        const std::size_t patch_size =
            static_cast<std::size_t>(
                patch_w
            ) *
            static_cast<std::size_t>(
                patch_h
            );

        std::vector<uint8_t> patch(
            patch_size
        );

        for (
            int py = 0;
            py < patch_h;
            ++py
        ) {

            const int img_y =
                box.min_y +
                py;

            const std::size_t base =
                static_cast<std::size_t>(
                    img_y
                ) *
                static_cast<std::size_t>(
                    width
                );

            const std::size_t dst_base =
                static_cast<std::size_t>(
                    py
                ) *
                static_cast<std::size_t>(
                    patch_w
                );

            for (
                int px = 0;
                px < patch_w;
                ++px
            ) {

                const int img_x =
                    box.min_x +
                    px;

                const std::size_t pixel_index =
                    base +
                    static_cast<std::size_t>(
                        img_x
                    );

                const std::size_t index =
                    pixel_index *
                    static_cast<std::size_t>(
                        channels
                    );

                const uint8_t value =
                    channels == 1
                        ? image[index]
                        : image[index + 1];

                // -----------------------------------------------------------------
                // Canonical MatrixMatcher representation:
                //
                //     foreground -> 255
                //     background -> 0
                //
                // For chart input channel 1 uses the lower threshold.
                // -----------------------------------------------------------------

                const bool foreground =
                    is_foreground_for_channels(
                        value,
                        channels
                    );

                patch[
                    dst_base +
                    static_cast<std::size_t>(
                        px
                    )
                ] =
                    foreground
                        ? uint8_t{255}
                        : uint8_t{0};
            }
        }

        const char glyph =
            glyph_matcher_.match(
                patch,
                patch_w,
                patch_h
            );

        recognized_text.push_back(
            glyph
        );

        last_max_x =
            box.max_x;
    }

    return recognized_text;
}

// =============================================================================
// LINE RECOGNITION
// =============================================================================
//
// PRIMARY:
//
//     Multi-pass Tesseract LSTM
//
// FALLBACK:
//
//     Matrix glyph matching
//
// Flow:
//
//     detected line
//         |
//         +--> TesseractRecognizer
//         |
//         +--> ConnectedComponents
//                  |
//                  +--> GlyphMatcher
//         |
//         +--> confidence arbitration
//
// =============================================================================

std::string LineRecognizer::recognize(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels
) const {

    if (
        image == nullptr ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0
    ) {
        return {};
    }

    // =========================================================================
    // TESSERACT PRIMARY PATH
    // =========================================================================

    std::string tesseract_text;

    int tesseract_confidence =
        0;

    const bool tesseract_ok =
        tesseract_recognizer_.recognize_line(
            image,
            width,
            y0,
            y1,
            channels,
            tesseract_text,
            tesseract_confidence
        );

    // =========================================================================
    // MATRIX FALLBACK
    // =========================================================================

    const std::vector<BoundingBox> char_boxes =
        ConnectedComponents::extract(
            image,
            width,
            y0,
            y1,
            channels
        );

    std::string matrix_text;

    if (
        !char_boxes.empty()
    ) {

        matrix_text =
            recognize_matrix_fallback(
                image,
                width,
                y0,
                y1,
                channels,
                char_boxes
            );
    }

    // =========================================================================
    // HIGH-CONFIDENCE TESSERACT
    // =========================================================================

    if (
        tesseract_ok &&
        !tesseract_text.empty() &&
        tesseract_confidence >=
            config::TESSERACT_ACCEPT_CONFIDENCE
    ) {

        return tesseract_text;
    }

    // =========================================================================
    // MODERATE-CONFIDENCE TESSERACT
    // =========================================================================

    if (
        tesseract_ok &&
        !tesseract_text.empty() &&
        tesseract_confidence >=
            config::TESSERACT_WEAK_CONFIDENCE
    ) {

        if (
            matrix_text.empty()
        ) {
            return tesseract_text;
        }

        const std::size_t tesseract_length =
            tesseract_text.size();

        const std::size_t matrix_length =
            matrix_text.size();

        const std::size_t length_difference =
            tesseract_length >
                matrix_length

                ? tesseract_length -
                  matrix_length

                : matrix_length -
                  tesseract_length;

        const std::size_t allowed_difference =
            std::max(
                std::size_t{2},
                std::max(
                    tesseract_length,
                    matrix_length
                ) /
                3
            );

        if (
            length_difference <=
            allowed_difference
        ) {

            return tesseract_text;
        }
    }

    // =========================================================================
    // MATRIX FALLBACK
    // =========================================================================

    if (
        !matrix_text.empty()
    ) {
        return matrix_text;
    }

    // =========================================================================
    // LAST TESSERACT FALLBACK
    // =========================================================================

    if (
        tesseract_ok &&
        !tesseract_text.empty()
    ) {
        return tesseract_text;
    }

    return {};
}

} // namespace fin_ocr
