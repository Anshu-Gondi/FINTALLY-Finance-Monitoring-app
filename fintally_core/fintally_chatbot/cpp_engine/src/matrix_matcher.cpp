#include "matrix_matcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

namespace fin_ocr {

namespace {

// =============================================================================
// CORE OCR THRESHOLDS
// =============================================================================

constexpr uint8_t DEFAULT_THRESHOLD = 100;
constexpr uint8_t CHART_FOREGROUND_THRESHOLD = 35;

[[nodiscard]]
inline bool is_foreground_pixel(
    uint8_t value
) noexcept {
    return value > DEFAULT_THRESHOLD;
}

[[nodiscard]]
inline bool is_chart_foreground_pixel(
    uint8_t value
) noexcept {
    return value >= CHART_FOREGROUND_THRESHOLD;
}

[[nodiscard]]
inline bool is_foreground_for_channels(
    uint8_t value,
    int channels
) noexcept {
    return channels == 3
        ? is_chart_foreground_pixel(value)
        : is_foreground_pixel(value);
}

// MatrixMatcher thresholds.
constexpr float MIN_MATCH_SCORE = 0.35f;
constexpr float MIN_FOREGROUND_RATIO = 0.005f;
constexpr float DIGIT_PRIORITY_MARGIN = 0.08f;

// =============================================================================
// TESSERACT CONFIGURATION
// =============================================================================
//
// Tesseract is the semantic OCR engine.
//
// MatrixMatcher remains the deterministic low-level fallback.
//
// Input representation:
//
//     native OCR foreground = 255
//     native OCR background = 0
//
// Tesseract receives:
//
//     text       = dark
//     background = light
//
// therefore the cropped native mask is inverted before recognition.
//
// Several PSM modes are evaluated because financial documents frequently
// contain:
//
//     - ordinary text lines
//     - table rows
//     - numeric-only cells
//     - short labels
// =============================================================================

constexpr int TESSERACT_ACCEPT_CONFIDENCE = 55;
constexpr int TESSERACT_WEAK_CONFIDENCE   = 30;

constexpr int TESSERACT_SOURCE_DPI = 300;

// 3x is a better compromise for the small 16-32px glyphs produced by the
// current raster pipeline.
constexpr int TESSERACT_SCALE = 3;

constexpr int TESSERACT_PADDING = 4;

constexpr std::size_t MAX_TESS_CANDIDATES = 6;

// =============================================================================
// BASIC STRUCTURES
// =============================================================================

struct BoundingBox {
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    [[nodiscard]]
    int width() const noexcept {
        return max_x - min_x + 1;
    }

    [[nodiscard]]
    int height() const noexcept {
        return max_y - min_y + 1;
    }

    [[nodiscard]]
    int area() const noexcept {
        return width() * height();
    }

    [[nodiscard]]
    constexpr bool is_digit(
        char c
    ) noexcept {
        return c >= '0' && c <= '9';
    }
};

struct Component {
    BoundingBox box;
};

// =============================================================================
// TESSERACT CANDIDATE
// =============================================================================

struct TesseractCandidate {

    std::string text;

    float confidence = -1.0f;

    int psm = 7;

    bool numeric_mode = false;

    double score = -1.0;
};

// =============================================================================
// TESSERACT THREAD-LOCAL ENGINE
// =============================================================================
//
// TessBaseAPI is stateful.
//
// One engine is kept per worker thread:
//
//     initialize once
//     reuse language data
//     Clear() after every hypothesis
//
// This avoids repeatedly loading tessdata and avoids sharing a stateful
// TessBaseAPI instance across threads.
// =============================================================================

struct TesseractEngine {

    tesseract::TessBaseAPI api;

    bool initialized = false;

    TesseractEngine() {

        const int rc =
            api.Init(
                nullptr,
                "eng",
                tesseract::OEM_LSTM_ONLY
            );

        if (rc != 0) {
            return;
        }

        initialized = true;

        api.SetVariable(
            "preserve_interword_spaces",
            "1"
        );

        api.SetVariable(
            "classify_enable_learning",
            "0"
        );

        api.SetVariable(
            "user_defined_dpi",
            "300"
        );
    }

    ~TesseractEngine() {

        if (initialized) {
            api.End();
        }
    }

    TesseractEngine(
        const TesseractEngine&
    ) = delete;

    TesseractEngine& operator=(
        const TesseractEngine&
    ) = delete;
};

thread_local TesseractEngine g_tesseract;

// =============================================================================
// TESSERACT TEXT CLEANUP
// =============================================================================

static std::string clean_tesseract_text(
    const char* raw
) {
    if (!raw) {
        return {};
    }

    std::string result;

    result.reserve(
        std::strlen(raw)
    );

    bool previous_space = false;

    for (const char* p = raw;
         *p != '\0';
         ++p) {

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

            previous_space = true;

            continue;
        }

        result.push_back(
            static_cast<char>(
                c
            )
        );

        previous_space = false;
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
// OCR PIXEL ACCESS
// =============================================================================
//
// channels == 1:
//
//     [OCR_MASK]
//
// channels >= 3:
//
//     [aux][OCR_FOREGROUND][aux]
//
// This preserves your current MatrixMatcher contract.
// =============================================================================

static inline uint8_t ocr_pixel(
    const uint8_t* image,
    int width,
    int channels,
    int x,
    int y
) noexcept {

    const std::size_t pixel_index =
        (
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                width
            ) +
            static_cast<std::size_t>(
                x
            )
        ) *
        static_cast<std::size_t>(
            channels
        );

    if (channels == 1) {
        return image[pixel_index];
    }

    return image[pixel_index + 1];
}

// =============================================================================
// FIND OCR FOREGROUND BOUNDS
// =============================================================================

static bool find_ocr_bounds(
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
//
// This is deliberately conservative.
//
// We calculate statistics in ONE raster pass instead of doing expensive
// width * height * width column rescans.
//
// Numeric financial lines often have:
//
//     digits
//     decimal separators
//     commas
//     currency symbols
//     +/- signs
//
// The result is only used to decide whether an additional Tesseract
// whitelist hypothesis should be evaluated.
// =============================================================================

static bool looks_like_numeric_line(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels
) noexcept {

    if (
        !image ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0
    ) {
        return false;
    }

    std::size_t foreground_pixels = 0;

    std::size_t narrow_column_pixels = 0;

    int min_x = width;
    int max_x = -1;

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

            const bool foreground =
                is_foreground_for_channels(
                    value,
                    channels
                );

            if (!foreground) {
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
                static_cast<std::size_t>(
                    x
                )
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
                static_cast<std::size_t>(
                    x
                )
            ];

        if (
            count > 0 &&
            count <= narrow_limit
        ) {
            ++narrow_column_pixels;
        }
    }

    const int horizontal_width =
        max_x - min_x + 1;

    const double narrow_ratio =
        horizontal_width > 0
            ? static_cast<double>(
                  narrow_column_pixels
              ) /
              static_cast<double>(
                  horizontal_width
              )
            : 0.0;

    /*
     * A numeric line normally has a relatively high ratio of compact vertical
     * strokes. The condition is intentionally loose because text such as
     *
     *     "Balance 1200"
     *
     * may still pass and receive the extra numeric hypothesis.
     */
    return
        narrow_ratio >= 0.30;
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
// We enlarge using bilinear interpolation rather than simply replicating each
// pixel 3x3.
//
// This provides Tesseract with smoother character contours while preserving
// thin strokes.
// =============================================================================

static void upscale_ocr_region(
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
        TESSERACT_SCALE;

    destination_height =
        source_height *
        TESSERACT_SCALE;

    if (
        source_width <= 0 ||
        source_height <= 0
    ) {

        destination.clear();

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
                static_cast<double>(
                    dy
                ) +
                0.5
            ) /
            static_cast<double>(
                TESSERACT_SCALE
            ) -
            0.5;

        const double y_floor =
            std::floor(
                src_y
            );

        const int y0 =
            std::clamp(
                static_cast<int>(
                    y_floor
                ),
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
            static_cast<std::size_t>(
                dy
            ) *
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
                    static_cast<double>(
                        dx
                    ) +
                    0.5
                ) /
                static_cast<double>(
                    TESSERACT_SCALE
                ) -
                0.5;

            const double x_floor =
                std::floor(
                    src_x
                );

            const int x0 =
                std::clamp(
                    static_cast<int>(
                        x_floor
                    ),
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
                static_cast<float>(
                    fx
                ) *
                (
                    p10 -
                    p00
                );

            const float bottom =
                p01 +
                static_cast<float>(
                    fy
                ) *
                (
                    p11 -
                    p01
                );

            const float interpolated =
                top +
                static_cast<float>(
                    fy
                ) *
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

            /*
             * Invert polarity:
             *
             * native:
             *     text = bright
             *
             * Tesseract:
             *     text = dark
             */
            destination[
                destination_row +
                static_cast<std::size_t>(
                    dx
                )
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

// =============================================================================
// TESSERACT CANDIDATE SCORING
// =============================================================================

static double score_tesseract_candidate(
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

    /*
     * Confidence is the strongest signal.
     */
    double score =
        static_cast<double>(
            confidence
        );

    /*
     * One-character results can be valid, but isolated short hypotheses are
     * inherently more likely to be accidental.
     */
    if (
        text.size() == 1
    ) {
        score -= 8.0;
    }

    std::size_t alnum = 0;
    std::size_t punctuation = 0;

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

    /*
     * Punctuation-only garbage is normally a bad hypothesis.
     */
    if (
        punctuation >
            alnum * 3 &&
        alnum == 0
    ) {
        score -= 20.0;
    }

    /*
     * PSM 7 matches the line-oriented pipeline most directly.
     */
    if (
        source_psm == 7
    ) {
        score += 2.0;
    }

    /*
     * Numeric whitelist hypotheses get a small reward when their output is
     * genuinely compatible with numeric content.
     */
    if (
        numeric_mode
    ) {

        std::size_t numeric_like = 0;

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
// IMPORTANT:
//
// SetImage() MUST happen before SetSourceResolution().
//
// This fixes the warning:
//
//     Please call SetImage before SetSourceResolution.
//
// =============================================================================

static bool run_tesseract_pass(
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
        !g_tesseract.initialized ||
        gray.empty() ||
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    g_tesseract.api.Clear();

    // -------------------------------------------------------------------------
    // IMAGE FIRST
    // -------------------------------------------------------------------------

    g_tesseract.api.SetImage(
        gray.data(),
        width,
        height,
        1,
        width
    );

    // -------------------------------------------------------------------------
    // RESOLUTION AFTER IMAGE
    // -------------------------------------------------------------------------

    g_tesseract.api.SetSourceResolution(
        TESSERACT_SOURCE_DPI
    );

    g_tesseract.api.SetPageSegMode(
        static_cast<
            tesseract::PageSegMode
        >(
            psm
        )
    );

    g_tesseract.api.SetVariable(
        "preserve_interword_spaces",
        "1"
    );

    g_tesseract.api.SetVariable(
        "classify_enable_learning",
        "0"
    );

    // -------------------------------------------------------------------------
    // Numeric whitelist
    // -------------------------------------------------------------------------

    if (
        numeric_mode
    ) {

        g_tesseract.api.SetVariable(
            "tessedit_char_whitelist",
            "0123456789.,-+$%()/₹"
        );

    } else {

        /*
         * Clear any whitelist from a previous numeric pass.
         */
        g_tesseract.api.SetVariable(
            "tessedit_char_whitelist",
            ""
        );
    }

    if (
        g_tesseract.api.Recognize(
            nullptr
        ) != 0
    ) {

        g_tesseract.api.Clear();

        return false;
    }

    confidence =
        static_cast<float>(
            g_tesseract.api.MeanTextConf()
        );

    char* raw =
        g_tesseract.api.GetUTF8Text();

    output =
        clean_tesseract_text(
            raw
        );

    if (
        raw
    ) {
        delete[] raw;
    }

    if (
        output.empty()
    ) {

        g_tesseract.api.Clear();

        return false;
    }

    return true;
}

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
// We run all three and then add numeric whitelist passes for lines that look
// numeric.
//
// The best candidate is selected by:
//
//     OCR confidence
//     structural validity
//     PSM compatibility
//     numeric compatibility
// =============================================================================

static bool recognize_line_tesseract(
    const uint8_t* image,
    int width,
    int y0,
    int y1,
    int channels,
    std::string& output,
    int& confidence
) {

    output.clear();

    confidence =
        0;

    if (
        !image ||
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0 ||
        !g_tesseract.initialized
    ) {
        return false;
    }

    // =========================================================================
    // FIND REAL FOREGROUND
    // =========================================================================

    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;

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
                TESSERACT_PADDING
        );

    max_x =
        std::min(
            width - 1,
            max_x +
                TESSERACT_PADDING
        );

    min_y =
        std::max(
            y0,
            min_y -
                TESSERACT_PADDING
        );

    max_y =
        std::min(
            y1 - 1,
            max_y +
                TESSERACT_PADDING
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

    int scaled_width = 0;
    int scaled_height = 0;

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
    // TESSERACT PASSES
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
        MAX_TESS_CANDIDATES
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
    // NUMERIC WHITELIST PASSES
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

    if (
        candidates.empty()
    ) {
        return false;
    }

    // =========================================================================
    // SELECT BEST
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

// =============================================================================
// MATRIXMATCHER FALLBACK LINE RECOGNITION
// =============================================================================

static std::string recognize_line_matrix(
    const uint8_t* image,
    int img_width,
    int line_y_start,
    int line_y_end,
    int channels,
    const std::vector<BoundingBox>& char_boxes,
    const MatrixMatcher& matcher
) {

    (void)line_y_start;
    (void)line_y_end;

    if (
        char_boxes.empty()
    ) {
        return {};
    }

    std::string recognized_text;

    recognized_text.reserve(
        char_boxes.size() * 2
    );

    int last_max_x = -1;

    constexpr float SPACE_GAP_FACTOR = 0.35f;

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

        std::vector<uint8_t> patch(
            static_cast<std::size_t>(
                patch_w
            ) *
            static_cast<std::size_t>(
                patch_h
            )
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
                    img_width
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

                /*
                 * Canonical MatrixMatcher representation:
                 *
                 *     foreground -> 255
                 *     background -> 0
                 *
                 * Chart channel 1 intentionally uses the lower
                 * threshold (>=35). Convert it here so the
                 * MatrixMatcher normalization stage can continue
                 * using its normal >100 foreground predicate.
                 */
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
            matcher.match_glyph(
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

} // namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

MatrixMatcher::MatrixMatcher() {
    load_default_templates();
}

// =============================================================================
// TEMPLATE DATABASE
// =============================================================================

void MatrixMatcher::load_default_templates() {

    templates_.clear();

    /*
     * The original template definitions are preserved.
     */

    // =========================================================================
    // DIGITS
    // =========================================================================

    templates_.push_back({
        '0',
        {0x0FC0, 0x1FE0, 0x3030, 0x6018,
         0x6018, 0x6018, 0x6018, 0x6018,
         0x6018, 0x6018, 0x6018, 0x6018,
         0x3030, 0x1FE0, 0x0FC0, 0x0000}
    });

    templates_.push_back({
        '1',
        {0x0300, 0x0F00, 0x3300, 0x0300,
         0x0300, 0x0300, 0x0300, 0x0300,
         0x0300, 0x0300, 0x0300, 0x0300,
         0x0300, 0x0FFC, 0x0FFC, 0x0000}
    });

    templates_.push_back({
        '2',
        {0x0FC0, 0x1FE0, 0x3030, 0x6030,
         0x0030, 0x0060, 0x00C0, 0x0180,
         0x0300, 0x0600, 0x0C00, 0x1800,
         0x3030, 0x7FFF, 0x7FFF, 0x0000}
    });

    templates_.push_back({
        '3',
        {0x0FC0, 0x1FE0, 0x3030, 0x0030,
         0x0030, 0x01E0, 0x01E0, 0x0030,
         0x0030, 0x0030, 0x0030, 0x3030,
         0x3030, 0x1FE0, 0x0FC0, 0x0000}
    });

    templates_.push_back({
        '4',
        {0x0060, 0x00E0, 0x01E0, 0x0360,
         0x0660, 0x0C60, 0x1860, 0x3060,
         0x6060, 0x7FFF, 0x7FFF, 0x0060,
         0x0060, 0x0060, 0x0060, 0x0000}
    });

    templates_.push_back({
        '5',
        {0x7FFF, 0x7FFF, 0x6000, 0x6000,
         0x6000, 0x7FC0, 0x7FE0, 0x0030,
         0x0018, 0x0018, 0x0018, 0x0018,
         0x3030, 0x1FE0, 0x0FC0, 0x0000}
    });

    templates_.push_back({
        '6',
        {0x03E0, 0x07F0, 0x0E18, 0x1800,
         0x3000, 0x6000, 0x7FC0, 0x7FE0,
         0x6030, 0x6018, 0x6018, 0x6018,
         0x3030, 0x1FE0, 0x0FC0, 0x0000}
    });

    templates_.push_back({
        '7',
        {0x7FFF, 0x7FFF, 0x0030, 0x0060,
         0x00C0, 0x0180, 0x0300, 0x0600,
         0x0C00, 0x0C00, 0x1800, 0x1800,
         0x1800, 0x1800, 0x1800, 0x0000}
    });

    templates_.push_back({
        '8',
        {0x0FC0, 0x1FE0, 0x3030, 0x3030,
         0x3030, 0x1FE0, 0x0FC0, 0x1FE0,
         0x3030, 0x6018, 0x6018, 0x6018,
         0x3030, 0x1FE0, 0x0FC0, 0x0000}
    });

    templates_.push_back({
        '9',
        {0x0FC0, 0x1FE0, 0x3030, 0x6018,
         0x6018, 0x6018, 0x3030, 0x1FE0,
         0x0FE0, 0x0018, 0x0030, 0x0060,
         0x0C00, 0x1E00, 0x07C0, 0x0000}
    });

    // =========================================================================
    // PUNCTUATION / FINANCIAL SYMBOLS
    // =========================================================================

    templates_.push_back({
        '.',
        {0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0300, 0x0300, 0x0000, 0x0000}
    });

    templates_.push_back({
        ',',
        {0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0180,
         0x0180, 0x0100, 0x0200, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        '$',
        {0x0100, 0x0FE0, 0x1830, 0x1800,
         0x0FC0, 0x0030, 0x3030, 0x1FE0,
         0x0100, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        '%',
        {0x600D, 0x601A, 0x0034, 0x0068,
         0x00D0, 0x01A0, 0x0B00, 0x1600,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        '-',
        {0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x7FE0, 0x7FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        '+',
        {0x0100, 0x0100, 0x0100, 0x0100,
         0x0100, 0x0FE0, 0x0FE0, 0x0100,
         0x0100, 0x0100, 0x0100, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        '/',
        {0x0003, 0x0006, 0x000C, 0x0018,
         0x0030, 0x0060, 0x00C0, 0x0180,
         0x0300, 0x0600, 0x0C00, 0x1800,
         0x3000, 0x6000, 0x0000, 0x0000}
    });

    templates_.push_back({
        ':',
        {0x0000, 0x0000, 0x0300, 0x0300,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0300, 0x0300, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    // =========================================================================
    // UPPERCASE
    // =========================================================================

    templates_.push_back({
        'A',
        {0x0180, 0x03C0, 0x0660, 0x0C30,
         0x1818, 0x300C, 0x3FFF, 0x6006,
         0x6006, 0x6006, 0x6006, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'B',
        {0x7FE0, 0x6030, 0x6030, 0x6030,
         0x7FC0, 0x6030, 0x6018, 0x6018,
         0x7FE0, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'C',
        {0x0FE0, 0x1830, 0x3000, 0x6000,
         0x6000, 0x6000, 0x3000, 0x1830,
         0x0FE0, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'D',
        {0x7FC0, 0x6060, 0x6030, 0x6030,
         0x6030, 0x6030, 0x6060, 0x7FC0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'E',
        {0x7FF0, 0x6000, 0x6000, 0x7FC0,
         0x6000, 0x6000, 0x6000, 0x7FF0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'F',
        {0x7FF0, 0x6000, 0x6000, 0x7FC0,
         0x6000, 0x6000, 0x6000, 0x6000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'G',
        {0x0FE0, 0x1830, 0x3000, 0x6000,
         0x63E0, 0x6030, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'H',
        {0x6006, 0x6006, 0x6006, 0x7FE6,
         0x6006, 0x6006, 0x6006, 0x6006,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'I',
        {0x7FE0, 0x0180, 0x0180, 0x0180,
         0x0180, 0x0180, 0x0180, 0x7FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'J',
        {0x03FE, 0x0030, 0x0030, 0x0030,
         0x0030, 0x3030, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'K',
        {0x6030, 0x6060, 0x60C0, 0x7F00,
         0x6300, 0x6180, 0x60C0, 0x6060,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'L',
        {0x6000, 0x6000, 0x6000, 0x6000,
         0x6000, 0x6000, 0x6000, 0x7FF0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'M',
        {0x6006, 0x700E, 0x581A, 0x4C32,
         0x4662, 0x4002, 0x4002, 0x4002,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'N',
        {0x6006, 0x7006, 0x5806, 0x4C06,
         0x4606, 0x4306, 0x4186, 0x40E6,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'O',
        {0x0FC0, 0x1830, 0x3018, 0x600C,
         0x600C, 0x3018, 0x1830, 0x0FC0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'P',
        {0x7FE0, 0x6030, 0x6030, 0x7FE0,
         0x6000, 0x6000, 0x6000, 0x6000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'Q',
        {0x0FC0, 0x1830, 0x3018, 0x600C,
         0x600C, 0x30D8, 0x1870, 0x0FE8,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'R',
        {0x7FE0, 0x6030, 0x6030, 0x7FE0,
         0x6300, 0x6180, 0x60C0, 0x6060,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'S',
        {0x0FE0, 0x1830, 0x3000, 0x1FE0,
         0x0030, 0x0030, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'T',
        {0x7FFC, 0x0180, 0x0180, 0x0180,
         0x0180, 0x0180, 0x0180, 0x0180,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'U',
        {0x6006, 0x6006, 0x6006, 0x6006,
         0x6006, 0x6006, 0x300C, 0x1FF8,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'V',
        {0x6006, 0x6006, 0x300C, 0x300C,
         0x1818, 0x1818, 0x0C30, 0x07E0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'W',
        {0x6006, 0x6006, 0x6006, 0x6006,
         0x6666, 0x6CE6, 0x381C, 0x1010,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'X',
        {0x6006, 0x300C, 0x1818, 0x0C30,
         0x0C30, 0x1818, 0x300C, 0x6006,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'Y',
        {0x6006, 0x300C, 0x1818, 0x0C30,
         0x0600, 0x0600, 0x0600, 0x0600,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'Z',
        {0x7FFC, 0x000C, 0x0018, 0x0030,
         0x00C0, 0x0180, 0x0300, 0x7FFC,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    // =========================================================================
    // LOWERCASE
    // =========================================================================

    templates_.push_back({
        'a',
        {0x0000, 0x0000, 0x0000, 0x1FE0,
         0x0030, 0x1FE0, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'b',
        {0x6000, 0x6000, 0x6000, 0x7FE0,
         0x6030, 0x6030, 0x6030, 0x7FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'c',
        {0x0000, 0x0000, 0x0000, 0x0FE0,
         0x1830, 0x3000, 0x1830, 0x0FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'd',
        {0x0030, 0x0030, 0x0030, 0x1FE0,
         0x3030, 0x3030, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'e',
        {0x0000, 0x0000, 0x0000, 0x0FC0,
         0x1830, 0x3FF0, 0x3000, 0x0FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'f',
        {0x01E0, 0x0300, 0x0300, 0x0FE0,
         0x0300, 0x0300, 0x0300, 0x0300,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'g',
        {0x0000, 0x0000, 0x0000, 0x1FE0,
         0x3030, 0x1FE0, 0x0030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'h',
        {0x6000, 0x6000, 0x6000, 0x7FE0,
         0x6030, 0x6030, 0x6030, 0x6030,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'i',
        {0x0180, 0x0000, 0x0180, 0x0180,
         0x0180, 0x0180, 0x0180, 0x07C0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'j',
        {0x00C0, 0x0000, 0x00C0, 0x00C0,
         0x00C0, 0x00C0, 0x00C0, 0x0C00,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'k',
        {0x6000, 0x6000, 0x6000, 0x60C0,
         0x6180, 0x7F00, 0x6180, 0x60C0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'l',
        {0x0300, 0x0300, 0x0300, 0x0300,
         0x0300, 0x0300, 0x0300, 0x01E0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'm',
        {0x0000, 0x0000, 0x0000, 0x6EE0,
         0x7990, 0x6990, 0x6990, 0x6990,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'n',
        {0x0000, 0x0000, 0x0000, 0x3FC0,
         0x2060, 0x2060, 0x2060, 0x70E0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'o',
        {0x0000, 0x0000, 0x0000, 0x0FC0,
         0x1830, 0x1830, 0x1830, 0x0FC0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'p',
        {0x0000, 0x0000, 0x0000, 0x7FE0,
         0x6030, 0x7FE0, 0x6000, 0x6000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'q',
        {0x0000, 0x0000, 0x0000, 0x1FE0,
         0x3030, 0x1FE0, 0x0030, 0x0030,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'r',
        {0x0000, 0x0000, 0x0000, 0x3300,
         0x2C00, 0x2000, 0x2000, 0x7000,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        's',
        {0x0000, 0x0000, 0x0000, 0x1FC0,
         0x2000, 0x1F80, 0x0040, 0x3F80,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        't',
        {0x0200, 0x0200, 0x0FC0, 0x0200,
         0x0200, 0x0200, 0x0230, 0x01E0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'u',
        {0x0000, 0x0000, 0x0000, 0x3030,
         0x3030, 0x3030, 0x3030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'v',
        {0x0000, 0x0000, 0x0000, 0x3030,
         0x3030, 0x1860, 0x0CC0, 0x0600,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'w',
        {0x0000, 0x0000, 0x0000, 0x3030,
         0x3330, 0x3660, 0x1C80, 0x0800,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'x',
        {0x0000, 0x0000, 0x0000, 0x3030,
         0x1860, 0x0C00, 0x1860, 0x3030,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'y',
        {0x0000, 0x0000, 0x0000, 0x3030,
         0x3030, 0x1E30, 0x0030, 0x1FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });

    templates_.push_back({
        'z',
        {0x0000, 0x0000, 0x0000, 0x3FE0,
         0x01C0, 0x0300, 0x0E00, 0x3FE0,
         0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000}
    });
}

// =============================================================================
// FAST BIT COUNTING
// =============================================================================

int MatrixMatcher::popcount16(
    uint16_t value
) noexcept {

#if defined(__GNUC__) || defined(__clang__)

    return __builtin_popcount(
        static_cast<unsigned int>(
            value
        )
    );

#elif defined(_MSC_VER)

    return __popcnt16(
        value
    );

#else

    int count = 0;

    while (
        value != 0
    ) {

        value &=
            static_cast<uint16_t>(
                value - 1
            );

        ++count;
    }

    return count;

#endif
}

// =============================================================================
// MATCH SCORE
// =============================================================================

float MatrixMatcher::compute_match_score(
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& candidate,
    const std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& target
) {

    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;

    int candidate_pixels = 0;
    int target_pixels = 0;

    for (
        int row = 0;
        row < GLYPH_GRID_SIZE;
        ++row
    ) {

        const uint16_t c =
            candidate[row];

        const uint16_t t =
            target[row];

        const uint16_t tp =
            static_cast<uint16_t>(
                c & t
            );

        const uint16_t fp =
            static_cast<uint16_t>(
                c &
                static_cast<uint16_t>(
                    ~t
                )
            );

        const uint16_t fn =
            static_cast<uint16_t>(
                t &
                static_cast<uint16_t>(
                    ~c
                )
            );

        true_positive +=
            popcount16(
                tp
            );

        false_positive +=
            popcount16(
                fp
            );

        false_negative +=
            popcount16(
                fn
            );

        candidate_pixels +=
            popcount16(
                c
            );

        target_pixels +=
            popcount16(
                t
            );
    }

    const int denominator =
        2 * true_positive +
        false_positive +
        false_negative;

    if (
        denominator == 0
    ) {
        return 0.0f;
    }

    const float f1 =
        static_cast<float>(
            2 * true_positive
        ) /
        static_cast<float>(
            denominator
        );

    const int pixel_difference =
        std::abs(
            candidate_pixels -
            target_pixels
        );

    const int pixel_total =
        std::max(
            candidate_pixels,
            target_pixels
        );

    const float density_score =
        pixel_total == 0
            ? 0.0f
            : 1.0f -
              static_cast<float>(
                  pixel_difference
              ) /
              static_cast<float>(
                  pixel_total
              );

    return
        0.85f * f1 +
        0.15f * density_score;
}

// =============================================================================
// NORMALIZE ARBITRARY GLYPH -> 16x16
// =============================================================================

void MatrixMatcher::normalize_to_grid(
    const std::vector<uint8_t>& patch,
    int patch_w,
    int patch_h,
    std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    >& output
) {

    output.fill(
        0
    );

    if (
        patch.empty() ||
        patch_w <= 0 ||
        patch_h <= 0
    ) {
        return;
    }

    const std::size_t required =
        static_cast<std::size_t>(
            patch_w
        ) *
        static_cast<std::size_t>(
            patch_h
        );

    if (
        patch.size() <
        required
    ) {
        return;
    }

    int min_x =
        patch_w;

    int min_y =
        patch_h;

    int max_x =
        -1;

    int max_y =
        -1;

    // =========================================================================
    // FIND GLYPH BOUNDS
    // =========================================================================

    for (
        int y = 0;
        y < patch_h;
        ++y
    ) {

        const std::size_t row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                patch_w
            );

        for (
            int x = 0;
            x < patch_w;
            ++x
        ) {

            if (
                !is_foreground_pixel(
                    patch[
                        row +
                        static_cast<std::size_t>(
                            x
                        )
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
        return;
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
                return;
            }
        }
    }

    // =========================================================================
    // EXACT 16x16
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

        return;
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
}

// =============================================================================
// GLYPH MATCHING
// =============================================================================

char MatrixMatcher::match_glyph(
    const std::vector<uint8_t>& cropped_patch,
    int patch_w,
    int patch_h
) const {

    if (
        patch_w <= 0 ||
        patch_h <= 0 ||
        cropped_patch.empty()
    ) {
        return '?';
    }

    const std::size_t expected_size =
        static_cast<std::size_t>(
            patch_w
        ) *
        static_cast<std::size_t>(
            patch_h
        );

    if (
        cropped_patch.size() <
        expected_size
    ) {
        return '?';
    }

    // =========================================================================
    // NORMALIZE
    // =========================================================================

    std::array<
        uint16_t,
        GLYPH_GRID_SIZE
    > candidate{};

    normalize_to_grid(
        cropped_patch,
        patch_w,
        patch_h,
        candidate
    );

    int foreground_pixels =
        0;

    std::array<
        int,
        GLYPH_GRID_SIZE
    > row_counts{};

    std::array<
        int,
        GLYPH_GRID_SIZE
    > column_counts{};

    int min_x =
        GLYPH_GRID_SIZE;

    int min_y =
        GLYPH_GRID_SIZE;

    int max_x =
        -1;

    int max_y =
        -1;

    for (
        int y = 0;
        y < GLYPH_GRID_SIZE;
        ++y
    ) {

        const uint16_t row =
            candidate[y];

        row_counts[y] =
            popcount16(
                row
            );

        foreground_pixels +=
            row_counts[y];

        for (
            int x = 0;
            x < GLYPH_GRID_SIZE;
            ++x
        ) {

            const uint16_t bit =
                static_cast<
                    uint16_t
                >(
                    1u <<
                    (15 - x)
                );

            if (
                (row & bit) == 0
            ) {
                continue;
            }

            ++column_counts[x];

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
        foreground_pixels == 0 ||
        max_x < min_x ||
        max_y < min_y
    ) {
        return '?';
    }

    const float foreground_ratio =
        static_cast<float>(
            foreground_pixels
        ) /
        static_cast<float>(
            GLYPH_BITS
        );

    if (
        foreground_ratio <
        MIN_FOREGROUND_RATIO
    ) {
        return '?';
    }

    // =========================================================================
    // STRUCTURAL FEATURES
    // =========================================================================

    int dominant_column =
        0;

    int dominant_column_pixels =
        0;

    for (
        int x = 0;
        x < GLYPH_GRID_SIZE;
        ++x
    ) {

        if (
            column_counts[x] >
            dominant_column_pixels
        ) {

            dominant_column_pixels =
                column_counts[x];

            dominant_column =
                x;
        }
    }

    const float dominant_column_ratio =
        static_cast<float>(
            dominant_column_pixels
        ) /
        static_cast<float>(
            std::max(
                1,
                foreground_pixels
            )
        );

    int top_row_width =
        0;

    int bottom_row_width =
        0;

    for (
        int y = 0;
        y < 4;
        ++y
    ) {

        top_row_width =
            std::max(
                top_row_width,
                row_counts[y]
            );
    }

    for (
        int y = GLYPH_GRID_SIZE - 4;
        y < GLYPH_GRID_SIZE;
        ++y
    ) {

        if (
            y >= 0
        ) {

            bottom_row_width =
                std::max(
                    bottom_row_width,
                    row_counts[y]
                );
        }
    }

    const bool looks_like_one =
        dominant_column_pixels >= 8 &&
        dominant_column_ratio >= 0.35f &&
        std::abs(
            dominant_column -
            (
                GLYPH_GRID_SIZE / 2
            )
        ) <= 3 &&
        bottom_row_width >= 4;

    // =========================================================================
    // PROFILE SCORE
    // =========================================================================

    const auto profile_score =
        [](
            const std::array<
                int,
                GLYPH_GRID_SIZE
            >& a,

            const std::array<
                int,
                GLYPH_GRID_SIZE
            >& b
        ) noexcept -> float {

            int max_value =
                0;

            for (
                int i = 0;
                i < GLYPH_GRID_SIZE;
                ++i
            ) {

                max_value =
                    std::max(
                        max_value,
                        std::max(
                            a[i],
                            b[i]
                        )
                    );
            }

            if (
                max_value <= 0
            ) {
                return 0.0f;
            }

            int error =
                0;

            for (
                int i = 0;
                i < GLYPH_GRID_SIZE;
                ++i
            ) {

                error +=
                    std::abs(
                        a[i] -
                        b[i]
                    );
            }

            const int max_error =
                GLYPH_GRID_SIZE *
                max_value;

            return
                max_error == 0
                    ? 0.0f
                    : std::max(
                          0.0f,
                          1.0f -
                          static_cast<float>(
                              error
                          ) /
                          static_cast<float>(
                              max_error
                          )
                      );
        };

    // =========================================================================
    // SHIFTED F1
    // =========================================================================

    const auto shifted_f1 =
        [&](
            const std::array<
                uint16_t,
                GLYPH_GRID_SIZE
            >& target,

            int dx,
            int dy
        ) noexcept -> float {

            int tp =
                0;

            int fp =
                0;

            int fn =
                0;

            for (
                int y = 0;
                y < GLYPH_GRID_SIZE;
                ++y
            ) {

                const int ty =
                    y - dy;

                if (
                    ty < 0 ||
                    ty >= GLYPH_GRID_SIZE
                ) {

                    fp +=
                        popcount16(
                            candidate[y]
                        );

                    continue;
                }

                uint16_t aligned =
                    0;

                if (
                    dx >= 0
                ) {

                    aligned =
                        dx <
                            GLYPH_GRID_SIZE
                            ? static_cast<
                                  uint16_t
                              >(
                                  target[ty] >>
                                  dx
                              )
                            : 0;

                } else {

                    const int shift =
                        -dx;

                    aligned =
                        shift <
                            GLYPH_GRID_SIZE
                            ? static_cast<
                                  uint16_t
                              >(
                                  target[ty] <<
                                  shift
                              )
                            : 0;
                }

                const uint16_t tp_bits =
                    static_cast<
                        uint16_t
                    >(
                        candidate[y] &
                        aligned
                    );

                const uint16_t fp_bits =
                    static_cast<
                        uint16_t
                    >(
                        candidate[y] &
                        static_cast<
                            uint16_t
                        >(
                            ~aligned
                        )
                    );

                const uint16_t fn_bits =
                    static_cast<
                        uint16_t
                    >(
                        aligned &
                        static_cast<
                            uint16_t
                        >(
                            ~candidate[y]
                        )
                    );

                tp +=
                    popcount16(
                        tp_bits
                    );

                fp +=
                    popcount16(
                        fp_bits
                    );

                fn +=
                    popcount16(
                        fn_bits
                    );
            }

            const int denominator =
                2 * tp +
                fp +
                fn;

            if (
                denominator == 0
            ) {
                return 0.0f;
            }

            return
                static_cast<float>(
                    2 * tp
                ) /
                static_cast<float>(
                    denominator
                );
        };

    // =========================================================================
    // MATCH STRUCTURE
    // =========================================================================

    struct Match {

        char character =
            '?';

        float score =
            -1.0f;

        float raw_score =
            -1.0f;

        float profile =
            0.0f;
    };

    Match best{};
    Match second{};
    Match best_digit{};

    float score_one =
        -1.0f;

    // =========================================================================
    // TEMPLATE LOOP
    // =========================================================================

    for (
        const GlyphTemplate& tpl :
        templates_
    ) {

        const float base_score =
            compute_match_score(
                candidate,
                tpl.grid
            );

        float aligned_score =
            base_score;

        int best_dx =
            0;

        int best_dy =
            0;

        for (
            int dy = -1;
            dy <= 1;
            ++dy
        ) {

            for (
                int dx = -1;
                dx <= 1;
                ++dx
            ) {

                const float score =
                    shifted_f1(
                        tpl.grid,
                        dx,
                        dy
                    );

                if (
                    score >
                    aligned_score
                ) {

                    aligned_score =
                        score;

                    best_dx =
                        dx;

                    best_dy =
                        dy;
                }
            }
        }

        constexpr float SHIFT_GAIN_REQUIRED =
            0.04f;

        const bool use_shift =
            (
                aligned_score -
                base_score
            ) >=
            SHIFT_GAIN_REQUIRED;

        const float shape_score =
            use_shift
                ? aligned_score
                : base_score;

        std::array<
            int,
            GLYPH_GRID_SIZE
        > template_columns{};

        std::array<
            int,
            GLYPH_GRID_SIZE
        > template_rows{};

        const int dx =
            use_shift
                ? best_dx
                : 0;

        const int dy =
            use_shift
                ? best_dy
                : 0;

        for (
            int y = 0;
            y < GLYPH_GRID_SIZE;
            ++y
        ) {

            const int ty =
                y - dy;

            if (
                ty < 0 ||
                ty >= GLYPH_GRID_SIZE
            ) {
                continue;
            }

            uint16_t row =
                tpl.grid[ty];

            if (
                dx > 0
            ) {

                row =
                    dx <
                        GLYPH_GRID_SIZE
                        ? static_cast<
                              uint16_t
                          >(
                              row >>
                              dx
                          )
                        : 0;

            } else if (
                dx < 0
            ) {

                const int shift =
                    -dx;

                row =
                    shift <
                        GLYPH_GRID_SIZE
                        ? static_cast<
                              uint16_t
                          >(
                              row <<
                              shift
                          )
                        : 0;
            }

            template_rows[y] =
                popcount16(
                    row
                );

            for (
                int x = 0;
                x < GLYPH_GRID_SIZE;
                ++x
            ) {

                const uint16_t bit =
                    static_cast<
                        uint16_t
                    >(
                        1u <<
                        (15 - x)
                    );

                if (
                    row & bit
                ) {
                    ++template_columns[x];
                }
            }
        }

        const float profile =
            0.6f *
            profile_score(
                column_counts,
                template_columns
            ) +

            0.4f *
            profile_score(
                row_counts,
                template_rows
            );

        const float final_score =
            0.93f *
            shape_score +
            0.07f *
            profile;

        const Match current{
            tpl.character,
            final_score,
            shape_score,
            profile
        };

        if (
            current.score >
            best.score
        ) {

            second =
                best;

            best =
                current;

        } else if (
            current.score >
            second.score
        ) {

            second =
                current;
        }

        if (
            tpl.character >= '0' &&
            tpl.character <= '9' &&
            current.score >
                best_digit.score
        ) {

            best_digit =
                current;
        }

        if (
            tpl.character == '1'
        ) {

            score_one =
                current.score;
        }
    }

    // =========================================================================
    // LOW CONFIDENCE
    // =========================================================================

    if (
        best.character == '?' ||
        best.score <
            MIN_MATCH_SCORE
    ) {
        return '?';
    }

    const float margin =
        best.score -
        std::max(
            -1.0f,
            second.score
        );

    constexpr float WEAK_MATCH_MARGIN =
        0.015f;

    // =========================================================================
    // SPECIAL CASE: 1
    // =========================================================================

    if (
        looks_like_one &&
        score_one >= 0.0f
    ) {

        constexpr float ONE_OVERRIDE_MARGIN =
            0.08f;

        if (
            score_one +
                ONE_OVERRIDE_MARGIN >=
            best.score &&

            dominant_column_ratio >=
                0.38f &&

            bottom_row_width >=
                4
        ) {

            return '1';
        }
    }

    // =========================================================================
    // DIGIT PRIORITY
    // =========================================================================

    if (
        best_digit.character != '?' &&
        best_digit.score +
                0.015f >=
            best.score &&
        margin >=
            WEAK_MATCH_MARGIN
    ) {

        return best_digit.character;
    }

    // =========================================================================
    // AMBIGUITY GATE
    // =========================================================================

    if (
        margin <
        WEAK_MATCH_MARGIN
    ) {

        if (
            best.score >=
            0.42f
        ) {
            return best.character;
        }

        return '?';
    }

    return best.character;
}

// =============================================================================
// EXTRACT CONNECTED COMPONENTS + MERGE FRAGMENTS
// =============================================================================

static std::vector<BoundingBox> extract_components(
    const uint8_t* image,
    int width,
    int min_y,
    int max_y,
    int channels
) {

    std::vector<BoundingBox> boxes;

    if (
        image == nullptr ||
        width <= 0 ||
        min_y >= max_y ||
        channels <= 0
    ) {
        return boxes;
    }

    const int roi_height =
        max_y -
        min_y;

    std::vector<uint8_t> visited(
        static_cast<
            std::size_t
        >(width) *
        static_cast<
            std::size_t
        >(roi_height),
        0
    );

    std::vector<int> queue;

    queue.reserve(
        256
    );

    // =========================================================================
    // PIXEL ACCESS
    // =========================================================================

    const auto pixel_value =
        [image, width, channels](
            int x,
            int y
        ) noexcept -> uint8_t {

            const std::size_t index =
                (
                    static_cast<
                        std::size_t
                    >(y) *
                    static_cast<
                        std::size_t
                    >(width) +
                    static_cast<
                        std::size_t
                    >(x)
                ) *
                static_cast<
                    std::size_t
                >(channels);

            if (
                channels == 1
            ) {
                return image[index];
            }

            return image[index + 1];
        };



    const auto visited_index =
        [width, min_y](
            int x,
            int y
        ) noexcept -> std::size_t {

            return
                static_cast<
                    std::size_t
                >(
                    y - min_y
                ) *
                static_cast<
                    std::size_t
                >(width) +
                static_cast<
                    std::size_t
                >(x);
        };

    // =========================================================================
    // STAGE 1: RAW 4-CONNECTED COMPONENTS
    // =========================================================================

    for (
        int y = min_y;
        y < max_y;
        ++y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t start_index =
                visited_index(
                    x,
                    y
                );

            if (
                visited[start_index] != 0
            ) {
                continue;
            }

            visited[start_index] =
                1;

            if (
                !is_foreground_for_channels(
                    pixel_value(
                        x,
                        y
                    ),
                    channels
                )
            ) {
                continue;
            }

            BoundingBox box{
                x,
                y,
                x,
                y
            };

            queue.clear();

            queue.push_back(
                y *
                width +
                x
            );

            std::size_t head =
                0;

            while (
                head <
                queue.size()
            ) {

                const int encoded =
                    queue[head++];

                const int cx =
                    encoded %
                    width;

                const int cy =
                    encoded /
                    width;

                box.min_x =
                    std::min(
                        box.min_x,
                        cx
                    );

                box.max_x =
                    std::max(
                        box.max_x,
                        cx
                    );

                box.min_y =
                    std::min(
                        box.min_y,
                        cy
                    );

                box.max_y =
                    std::max(
                        box.max_y,
                        cy
                    );

                // -----------------------------------------------------------------
                // LEFT
                // -----------------------------------------------------------------

                if (
                    cx > 0
                ) {

                    const int nx =
                        cx - 1;

                    const int ny =
                        cy;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                pixel_value(
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny *
                                width +
                                nx
                            );
                        }
                    }
                }

                // -----------------------------------------------------------------
                // RIGHT
                // -----------------------------------------------------------------

                if (
                    cx + 1 <
                    width
                ) {

                    const int nx =
                        cx + 1;

                    const int ny =
                        cy;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                pixel_value(
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny *
                                width +
                                nx
                            );
                        }
                    }
                }

                // -----------------------------------------------------------------
                // UP
                // -----------------------------------------------------------------

                if (
                    cy >
                    min_y
                ) {

                    const int nx =
                        cx;

                    const int ny =
                        cy - 1;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                pixel_value(
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny *
                                width +
                                nx
                            );
                        }
                    }
                }

                // -----------------------------------------------------------------
                // DOWN
                // -----------------------------------------------------------------

                if (
                    cy + 1 <
                    max_y
                ) {

                    const int nx =
                        cx;

                    const int ny =
                        cy + 1;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                pixel_value(
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny *
                                width +
                                nx
                            );
                        }
                    }
                }
            }

            const int component_w =
                box.width();

            const int component_h =
                box.height();

            const int component_area =
                box.area();

            if (
                component_w < 1 ||
                component_h < 2 ||
                component_area < 2
            ) {
                continue;
            }

            // =========================================================================
            // HORIZONTAL RULE
            // =========================================================================

            const bool horizontal_rule =
                component_w >= 20 &&
                component_h <= 3 &&
                component_w >=
                    component_h * 8;

            if (
                horizontal_rule
            ) {
                continue;
            }

            // =========================================================================
            // VERTICAL RULE
            // =========================================================================

            const bool vertical_rule =
                component_h >= 20 &&
                component_w <= 3 &&
                component_h >=
                    component_w * 8;

            if (
                vertical_rule
            ) {
                continue;
            }

            // =========================================================================
            // HUGE IMAGE BLOB
            // =========================================================================

            const bool image_width_blob =
                component_w >=
                std::max(
                    128,
                    width * 3 / 4
                );

            const bool image_height_blob =
                component_h >=
                std::max(
                    128,
                    roi_height * 3 / 4
                );

            const bool huge_blob =
                image_width_blob &&
                image_height_blob;

            if (
                huge_blob
            ) {
                continue;
            }

            // =========================================================================
            // THICK HORIZONTAL STRUCTURE
            // =========================================================================

            const bool thick_horizontal_structure =
                component_w >=
                    std::max(
                        64,
                        width / 3
                    ) &&

                component_h <= 6 &&

                component_w >=
                    component_h * 12;

            if (
                thick_horizontal_structure
            ) {
                continue;
            }

            boxes.push_back(
                box
            );
        }
    }

    if (
        boxes.empty()
    ) {
        return boxes;
    }

    // =========================================================================
    // STAGE 2: SORT
    // =========================================================================

    std::sort(
        boxes.begin(),
        boxes.end(),
        [](
            const BoundingBox& a,
            const BoundingBox& b
        ) noexcept {

            if (
                a.min_x !=
                b.min_x
            ) {

                return
                    a.min_x <
                    b.min_x;
            }

            return
                a.min_y <
                b.min_y;
        }
    );

    // =========================================================================
    // STAGE 3: MERGE FRAGMENTS
    // =========================================================================

    std::vector<BoundingBox> merged;

    merged.reserve(
        boxes.size()
    );

    for (
        const BoundingBox& current :
        boxes
    ) {

        bool merged_current =
            false;

        const std::size_t search_begin =
            merged.size() > 4
                ? merged.size() - 4
                : 0;

        for (
            std::size_t i =
                merged.size();
            i-- >
                search_begin;
        ) {

            BoundingBox& previous =
                merged[i];

            const int pw =
                previous.width();

            const int ph =
                previous.height();

            const int cw =
                current.width();

            const int ch =
                current.height();

            const int horizontal_overlap =
                std::max(
                    0,
                    std::min(
                        previous.max_x,
                        current.max_x
                    ) -
                    std::max(
                        previous.min_x,
                        current.min_x
                    ) +
                    1
                );

            const int vertical_overlap =
                std::max(
                    0,
                    std::min(
                        previous.max_y,
                        current.max_y
                    ) -
                    std::max(
                        previous.min_y,
                        current.min_y
                    ) +
                    1
                );

            const int horizontal_gap =
                current.min_x >
                    previous.max_x

                    ? current.min_x -
                      previous.max_x -
                      1

                    : previous.min_x -
                      current.max_x -
                      1;

            const int vertical_gap =
                current.min_y >
                    previous.max_y

                    ? current.min_y -
                      previous.max_y -
                      1

                    : previous.min_y -
                      current.max_y -
                      1;

            // =========================================================================
            // DOT + STEM
            // =========================================================================

            const bool previous_is_dot =
                pw <= 5 &&
                ph <= 5;

            const bool current_is_dot =
                cw <= 5 &&
                ch <= 5;

            const bool previous_is_stem =
                ph >= 6 &&
                ph >=
                    pw * 2;

            const bool current_is_stem =
                ch >= 6 &&
                ch >=
                    cw * 2;

            const bool dot_stem_pair =
                (
                    previous_is_dot &&
                    current_is_stem
                ) ||
                (
                    current_is_dot &&
                    previous_is_stem
                );

            if (
                dot_stem_pair
            ) {

                const int dot_width =
                    previous_is_dot
                        ? pw
                        : cw;

                const int stem_width =
                    previous_is_stem
                        ? pw
                        : cw;

                const int required_x_overlap =
                    std::max(
                        1,
                        std::min(
                            dot_width,
                            stem_width
                        ) / 2
                    );

                const bool good_x_alignment =
                    horizontal_overlap >=
                    required_x_overlap;

                const bool good_vertical_gap =
                    vertical_gap <=
                    6;

                if (
                    good_x_alignment &&
                    good_vertical_gap
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
                }
            }

            // =========================================================================
            // GENERAL VERTICAL FRAGMENT
            // =========================================================================

            const int min_width =
                std::min(
                    pw,
                    cw
                );

            const bool strong_x_overlap =
                horizontal_overlap >=
                std::max(
                    1,
                    min_width / 2
                );

            const int height_reference =
                std::max(
                    ph,
                    ch
                );

            const int allowed_vertical_gap =
                std::max(
                    2,
                    std::min(
                        5,
                        height_reference / 4
                    )
                );

            const bool vertical_fragment =
                strong_x_overlap &&
                vertical_gap <=
                    allowed_vertical_gap;

            if (
                vertical_fragment
            ) {

                const bool both_normal_sized =
                    ph >= 6 &&
                    ch >= 6 &&
                    pw >= 3 &&
                    cw >= 3;

                if (
                    !both_normal_sized
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
                }
            }

            // =========================================================================
            // HORIZONTAL FRAGMENT
            // =========================================================================

            const int min_height =
                std::min(
                    ph,
                    ch
                );

            const bool strong_y_overlap =
                vertical_overlap >=
                std::max(
                    1,
                    min_height / 2
                );

            const int width_reference =
                std::max(
                    pw,
                    cw
                );

            const int allowed_horizontal_gap =
                std::max(
                    1,
                    std::min(
                        3,
                        width_reference / 4
                    )
                );

            const bool horizontal_fragment =
                strong_y_overlap &&
                horizontal_gap <=
                    allowed_horizontal_gap;

            if (
                horizontal_fragment
            ) {

                const bool tiny_fragment =
                    pw <= 5 ||
                    cw <= 5;

                if (
                    tiny_fragment
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
                }
            }
        }

        if (
            !merged_current
        ) {

            merged.push_back(
                current
            );
        }
    }

    // =========================================================================
    // STAGE 4: FINAL ORDER
    // =========================================================================

    std::sort(
        merged.begin(),
        merged.end(),
        [](
            const BoundingBox& a,
            const BoundingBox& b
        ) noexcept {

            if (
                a.min_x !=
                b.min_x
            ) {

                return
                    a.min_x <
                    b.min_x;
            }

            return
                a.min_y <
                b.min_y;
        }
    );

    return merged;
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
//     MatrixMatcher
//
// Flow:
//
//     detected line
//         |
//         +--> PSM 7
//         +--> PSM 6
//         +--> PSM 13
//         |
//         +--> numeric whitelist PSM 7/13 if appropriate
//         |
//         +--> candidate selection
//         |
//         +--> MatrixMatcher fallback
//
// =============================================================================

std::string MatrixMatcher::recognize_line(
    const uint8_t* image,
    int img_width,
    int line_y_start,
    int line_y_end,
    int channels
) const {

    if (
        image == nullptr ||
        img_width <= 0 ||
        line_y_start >= line_y_end ||
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
        recognize_line_tesseract(
            image,
            img_width,
            line_y_start,
            line_y_end,
            channels,
            tesseract_text,
            tesseract_confidence
        );

    // =========================================================================
    // MATRIXMATCHER FALLBACK
    // =========================================================================

    const std::vector<BoundingBox> char_boxes =
        extract_components(
            image,
            img_width,
            line_y_start,
            line_y_end,
            channels
        );

    std::string matrix_text;

    if (
        !char_boxes.empty()
    ) {

        matrix_text =
            recognize_line_matrix(
                image,
                img_width,
                line_y_start,
                line_y_end,
                channels,
                char_boxes,
                *this
            );
    }

    // =========================================================================
    // HIGH-CONFIDENCE TESSERACT
    // =========================================================================

    if (
        tesseract_ok &&
        !tesseract_text.empty() &&
        tesseract_confidence >=
            TESSERACT_ACCEPT_CONFIDENCE
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
            TESSERACT_WEAK_CONFIDENCE
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
                std::size_t(2),
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
    // MATRIXMATCHER FALLBACK
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

std::string MatrixMatcher::recognize_chart_labels(
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
        CHART_FOREGROUND_THRESHOLD;

    constexpr double MIN_ROW_DENSITY =
        0.00020;

    constexpr int ROW_GAP =
        3;

    constexpr int MAX_BAND_HEIGHT =
        48;

    constexpr int VERTICAL_PADDING =
        3;

    // =========================================================================
    // STEP 1
    // Row projection.
    //
    // Do not require >127 here.
    //
    // The chart isolation stage may intentionally produce weaker grayscale
    // foreground values.
    // =========================================================================

    std::vector<int> row_counts(
        static_cast<std::size_t>(
            height
        ),
        0
    );

    std::vector<int> row_min_x(
        static_cast<std::size_t>(
            height
        ),
        width
    );

    std::vector<int> row_max_x(
        static_cast<std::size_t>(
            height
        ),
        -1
    );

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        const std::size_t row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                width
            ) *
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
                static_cast<std::size_t>(
                    x
                ) * 3u +
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
            static_cast<std::size_t>(
                y
            )
        ] = count;

        row_min_x[
            static_cast<std::size_t>(
                y
            )
        ] = min_x;

        row_max_x[
            static_cast<std::size_t>(
                y
            )
        ] = max_x;
    }

    // =========================================================================
    // STEP 2
    // Detect active text rows.
    // =========================================================================

    const int minimum_row_pixels =
        std::max(
            2,
            static_cast<int>(
                std::ceil(
                    static_cast<double>(
                        width
                    ) *
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
                static_cast<std::size_t>(
                    y
                )
            ] >=
            minimum_row_pixels;

        if (active) {

            if (band_start < 0) {

                band_start =
                    y;
            }

            last_active_row =
                y;

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

                band_start =
                    -1;

                last_active_row =
                    -1;
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
    // STEP 3
    // Merge nearby rows carefully.
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

        if (
            gap <= ROW_GAP &&
            (
                band.y_end -
                previous.y_start
            ) <=
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
    // STEP 4
    // Recognize each detected band.
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
            y1 - y0;

        if (
            band_height <= 1 ||
            band_height >
                MAX_BAND_HEIGHT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Determine bounding X range.
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
                    static_cast<std::size_t>(
                        y
                    )
                ];

            if (
                row_count <= 0
            ) {
                continue;
            }

            min_x =
                std::min(
                    min_x,
                    row_min_x[
                        static_cast<std::size_t>(
                            y
                        )
                    ]
                );

            max_x =
                std::max(
                    max_x,
                    row_max_x[
                        static_cast<std::size_t>(
                            y
                        )
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
        // Avoid completely empty / accidental chart-wide regions.
        // ---------------------------------------------------------------------

        if (
            density < 0.00015f
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Existing hybrid line recognizer.
        // It now gets the actual band rather than a blind fixed sliding window.
        // ---------------------------------------------------------------------

        std::string text =
            recognize_line(
                chart_buffer,
                width,
                y0,
                y1,
                3
            );

        if (
            text.empty()
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Trim.
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
        // Reject repeated-glyph garbage.
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
        // Deduplicate overlapping bands.
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
                existing.text ==
                    text
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
    // STEP 5
    // Sort top-to-bottom / left-to-right.
    // =========================================================================

    std::sort(
        detected_lines.begin(),
        detected_lines.end(),
        [](
            const DetectedLine& a,
            const DetectedLine& b
        ) noexcept {

            if (
                a.y !=
                b.y
            ) {

                return a.y <
                    b.y;
            }

            return a.min_x <
                b.min_x;
        }
    );

    // =========================================================================
    // STEP 6
    // Output.
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

} // fin_ocr
