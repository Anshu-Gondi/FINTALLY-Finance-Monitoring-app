#include "fin_ocr/tesseract/tesseract_recognizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/pixel_access.hpp"
#include "fin_ocr/tesseract/tesseract_engine.hpp"
#include "fin_ocr/tesseract/tesseract_preprocess.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

namespace {

// =============================================================================
// THREAD-LOCAL TESSERACT ENGINE
// =============================================================================
//
// The actual TessBaseAPI remains hidden inside TesseractEngine.
//
// One engine is reused per worker thread.
// =============================================================================

thread_local TesseractEngine g_tesseract;

// =============================================================================
// TESSERACT CANDIDATE SCORING
// =============================================================================

double score_tesseract_candidate(
    const std::string& text,
    float confidence,
    int source_psm,
    bool numeric_mode
) {

    if (
        text.empty()
    ) {
        return -1.0;
    }

    // =========================================================================
    // BASE CONFIDENCE
    // =========================================================================

    double score =
        static_cast<double>(
            confidence
        );

    // =========================================================================
    // SHORT RESULT PENALTY
    // =========================================================================

    if (
        text.size() == 1
    ) {
        score -= 8.0;
    }

    std::size_t alnum =
        0;

    std::size_t punctuation =
        0;

    // =========================================================================
    // CHARACTER CLASSIFICATION
    // =========================================================================

    for (
        unsigned char c :
        text
    ) {

        if (
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')
        ) {

            ++alnum;

        } else if (
            c == '.' ||
            c == ',' ||
            c == '-' ||
            c == '+' ||
            c == '$' ||
            c == '%' ||
            c == '/' ||
            c == ':' ||
            c == '(' ||
            c == ')'
        ) {

            ++punctuation;
        }
    }

    const std::size_t useful =
        alnum +
        punctuation;

    if (
        useful == 0
    ) {
        return -1.0;
    }

    // =========================================================================
    // PUNCTUATION-ONLY GARBAGE
    // =========================================================================

    if (
        punctuation >
            alnum * 3 &&
        alnum == 0
    ) {

        score -= 20.0;
    }

    // =========================================================================
    // PSM 7 PREFERENCE
    // =========================================================================

    if (
        source_psm == 7
    ) {
        score += 2.0;
    }

    // =========================================================================
    // NUMERIC HYPOTHESIS REWARD / PENALTY
    // =========================================================================

    if (
        numeric_mode
    ) {

        std::size_t numeric_like =
            0;

        for (
            unsigned char c :
            text
        ) {

            if (
                (c >= '0' && c <= '9') ||
                c == '.' ||
                c == ',' ||
                c == '-' ||
                c == '+' ||
                c == '$' ||
                c == '%' ||
                c == '/' ||
                c == '(' ||
                c == ')'
            ) {

                ++numeric_like;
            }
        }

        if (
            !text.empty() &&
            numeric_like * 100 >=
                text.size() * 70
        ) {

            score += 5.0;

        } else {

            score -= 5.0;
        }
    }

    return score;
}

// =============================================================================
// SINGLE TESSERACT PASS
// =============================================================================
//
// TesseractEngine owns all direct TessBaseAPI interaction.
//
// This function is now only an adapter between the recognizer and the engine.
// =============================================================================

bool run_tesseract_pass(
    const std::vector<uint8_t>& gray,
    int width,
    int height,
    int psm,
    bool numeric_mode,
    std::string& output,
    float& confidence
) {

    output.clear();

    confidence =
        0.0f;

    if (
        !g_tesseract.initialized() ||
        gray.empty() ||
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    return g_tesseract.recognize(
        gray,
        width,
        height,
        psm,
        numeric_mode,
        output,
        confidence
    );
}

} // namespace

// =============================================================================
// MULTI-PASS TESSERACT RECOGNITION
// =============================================================================
//
// PSM:
//
//     7  = single line
//     6  = uniform text block
//     13 = raw line
//
// Numeric lines additionally receive:
//
//     PSM 7 + numeric whitelist
//     PSM 13 + numeric whitelist
//
// =============================================================================

bool TesseractRecognizer::recognize_line(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels,
    std::string& output,
    int& confidence
) const {

    output.clear();

    confidence =
        0;

    if (
        image == nullptr ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0 ||
        !g_tesseract.initialized()
    ) {
        return false;
    }

    // =========================================================================
    // FIND REAL FOREGROUND
    // =========================================================================

    int min_x =
        0;

    int min_y =
        0;

    int max_x =
        0;

    int max_y =
        0;

    if (
        !find_ocr_bounds(
            image,
            width,
            y0,
            y1,
            channels,
            min_x,
            min_y,
            max_x,
            max_y
        )
    ) {
        return false;
    }

    // =========================================================================
    // PADDING
    // =========================================================================

    min_x =
        std::max(
            0,
            min_x -
                config::TESSERACT_PADDING
        );

    max_x =
        std::min(
            width - 1,
            max_x +
                config::TESSERACT_PADDING
        );

    min_y =
        std::max(
            y0,
            min_y -
                config::TESSERACT_PADDING
        );

    max_y =
        std::min(
            y1 - 1,
            max_y +
                config::TESSERACT_PADDING
        );

    const int crop_width =
        max_x -
        min_x +
        1;

    const int crop_height =
        max_y -
        min_y +
        1;

    if (
        crop_width <= 0 ||
        crop_height <= 0
    ) {
        return false;
    }

    // =========================================================================
    // COPY CROPPED NATIVE MASK
    // =========================================================================

    const std::size_t crop_size =
        static_cast<std::size_t>(
            crop_width
        ) *
        static_cast<std::size_t>(
            crop_height
        );

    std::vector<uint8_t> cropped_native(
        crop_size
    );

    for (
        int y = 0;
        y < crop_height;
        ++y
    ) {

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                crop_width
            );

        for (
            int x = 0;
            x < crop_width;
            ++x
        ) {

            cropped_native[
                destination_row +
                static_cast<std::size_t>(
                    x
                )
            ] =
                ocr_pixel(
                    image,
                    width,
                    channels,
                    min_x + x,
                    min_y + y
                );
        }
    }

    // =========================================================================
    // UPSCALE
    // =========================================================================

    std::vector<uint8_t> gray;

    int scaled_width =
        0;

    int scaled_height =
        0;

    upscale_ocr_region(
        cropped_native.data(),
        crop_width,
        crop_height,
        1,
        gray,
        scaled_width,
        scaled_height
    );

    if (
        gray.empty() ||
        scaled_width <= 0 ||
        scaled_height <= 0
    ) {
        return false;
    }

    // =========================================================================
    // NUMERIC HINT
    // =========================================================================

    const bool numeric_mode =
        looks_like_numeric_line(
            image,
            width,
            y0,
            y1,
            channels
        );

    // =========================================================================
    // NORMAL TESSERACT PSM MODES
    // =========================================================================

    constexpr std::array<int, 3>
        PSM_MODES{
            7,
            6,
            13
        };

    std::vector<TesseractCandidate>
        candidates;

    candidates.reserve(
        config::MAX_TESS_CANDIDATES
    );

    for (
        const int psm :
        PSM_MODES
    ) {

        std::string text;

        float conf =
            0.0f;

        if (
            run_tesseract_pass(
                gray,
                scaled_width,
                scaled_height,
                psm,
                false,
                text,
                conf
            )
        ) {

            const double score =
                score_tesseract_candidate(
                    text,
                    conf,
                    psm,
                    false
                );

            candidates.push_back({
                std::move(text),
                conf,
                psm,
                false,
                score
            });
        }
    }

    // =========================================================================
    // NUMERIC WHITELIST PSM MODES
    // =========================================================================

    if (
        numeric_mode
    ) {

        constexpr std::array<int, 2>
            NUMERIC_PSM_MODES{
                7,
                13
            };

        for (
            const int psm :
            NUMERIC_PSM_MODES
        ) {

            std::string text;

            float conf =
                0.0f;

            if (
                run_tesseract_pass(
                    gray,
                    scaled_width,
                    scaled_height,
                    psm,
                    true,
                    text,
                    conf
                )
            ) {

                const double score =
                    score_tesseract_candidate(
                        text,
                        conf,
                        psm,
                        true
                    );

                candidates.push_back({
                    std::move(text),
                    conf,
                    psm,
                    true,
                    score
                });
            }
        }
    }

    // =========================================================================
    // NO CANDIDATES
    // =========================================================================

    if (
        candidates.empty()
    ) {
        return false;
    }

    // =========================================================================
    // SELECT BEST CANDIDATE
    // =========================================================================

    const auto best_it =
        std::max_element(
            candidates.begin(),
            candidates.end(),
            [](
                const TesseractCandidate& a,
                const TesseractCandidate& b
            ) noexcept {

                return
                    a.score <
                    b.score;
            }
        );

    if (
        best_it ==
            candidates.end() ||
        best_it->text.empty()
    ) {
        return false;
    }

    output =
        best_it->text;

    confidence =
        static_cast<int>(
            std::clamp(
                std::lround(
                    best_it->confidence
                ),
                0L,
                100L
            )
        );

    return true;
}

} // namespace fin_ocr
