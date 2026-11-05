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
// LOCAL LINE-OCR ARBITRATION LIMITS
// =============================================================================
//
// Chart OCR has a specific failure mode:
//
//     chart geometry
//          ↓
//     connected components
//          ↓
//     glyph matcher / Tesseract
//          ↓
//     apparently high-confidence numeric text
//
// Confidence alone is therefore insufficient.
//
// The validator below is deliberately deterministic and engine-independent.
//
// =============================================================================

namespace {

constexpr int SMALL_LINE_MAX_HEIGHT = 32;

constexpr int SMALL_LINE_MAX_WIDTH = 320;

// Maximum normal text length accepted from a short/small chart crop.
constexpr std::size_t MAX_REASONABLE_SHORT_TEXT_LENGTH = 16;

// Hard upper bound for useful text in a small chart crop.
constexpr std::size_t MAX_SMALL_REGION_GLYPHS = 32;

// Repetition threshold.
constexpr int MAX_REPEATED_CHAR_PERCENT = 55;

// Numeric dominance threshold.
//
// IMPORTANT:
//
// This is applied even when a tiny amount of alphabetic OCR corruption is
// present:
//
//     545845555885514585711l
//                              ^ one bogus alpha character
//
// Such a result is still numeric geometry, not useful text.
constexpr int MAX_NUMERIC_DOMINANCE_PERCENT = 70;

// Stronger threshold for pure numeric streams.
constexpr int MAX_PURE_NUMERIC_PERCENT = 85;

// Too many tiny space-separated fragments usually means geometry.
constexpr std::size_t MAX_FRAGMENTED_TOKENS = 6;

// Tesseract confidence required when it is the only surviving result.
constexpr int MIN_TESSERACT_SUPPORT_CONFIDENCE = 55;

// Long strings require actual alphabetic support.
constexpr std::size_t LONG_TEXT_GLYPH_THRESHOLD = 8;

// A long result with this few alphabetic characters is suspicious.
constexpr std::size_t MIN_ALPHA_FOR_LONG_TEXT = 3;

// =============================================================================
// REGION CLASSIFICATION
// =============================================================================

[[nodiscard]]
bool is_small_line_region(
    int width,
    int y0,
    int y1,
    int channels
) noexcept {

    if (
        width <= 0 ||
        y0 >= y1 ||
        channels <= 0
    ) {

        return false;
    }

    const int height =
        y1 -
        y0;

    return
        height <=
            SMALL_LINE_MAX_HEIGHT &&
        width <=
            SMALL_LINE_MAX_WIDTH;
}

// =============================================================================
// TEXT NORMALIZATION
// =============================================================================

void trim_text(
    std::string& text
) {

    while (
        !text.empty() &&
        (
            text.front() == ' ' ||
            text.front() == '\t' ||
            text.front() == '\r' ||
            text.front() == '\n'
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
            text.back() == '\t' ||
            text.back() == '\r' ||
            text.back() == '\n'
        )
    ) {

        text.pop_back();
    }
}

// =============================================================================
// CHARACTER HELPERS
// =============================================================================

[[nodiscard]]
bool is_ascii_alpha(
    char c
) noexcept {

    return
        (
            c >= 'A' &&
            c <= 'Z'
        ) ||
        (
            c >= 'a' &&
            c <= 'z'
        );
}

[[nodiscard]]
bool is_ascii_digit(
    char c
) noexcept {

    return
        c >= '0' &&
        c <= '9';
}

[[nodiscard]]
bool is_ascii_punctuation(
    char c
) noexcept {

    return
        c == '-' ||
        c == '+' ||
        c == '=' ||
        c == '|' ||
        c == '_' ||
        c == '.' ||
        c == ',' ||
        c == ':' ||
        c == ';' ||
        c == '*' ||
        c == '`' ||
        c == '\'' ||
        c == '"' ||
        c == '~' ||
        c == '/' ||
        c == '\\';
}

// =============================================================================
// GLYPH COUNT
// =============================================================================

[[nodiscard]]
std::size_t count_glyphs(
    const std::string& text
) noexcept {

    std::size_t count =
        0;

    for (
        const char c :
        text
    ) {

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n'
        ) {

            continue;
        }

        ++count;
    }

    return count;
}

// =============================================================================
// TEXT STATISTICS
// =============================================================================

struct TextStats {

    std::size_t glyphs = 0;

    std::size_t alpha = 0;

    std::size_t digits = 0;

    std::size_t punctuation = 0;

    std::size_t other = 0;

    std::size_t spaces = 0;

    char most_common = '\0';

    std::size_t most_common_count = 0;
};

// =============================================================================
// TEXT ANALYSIS
// =============================================================================

[[nodiscard]]
TextStats analyze_text(
    const std::string& text
) noexcept {

    TextStats stats{};

    // -------------------------------------------------------------------------
    // First pass: classification.
    // -------------------------------------------------------------------------

    for (
        const char c :
        text
    ) {

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n'
        ) {

            ++stats.spaces;
            continue;
        }

        ++stats.glyphs;

        if (
            is_ascii_alpha(c)
        ) {

            ++stats.alpha;

        } else if (
            is_ascii_digit(c)
        ) {

            ++stats.digits;

        } else if (
            is_ascii_punctuation(c)
        ) {

            ++stats.punctuation;

        } else {

            ++stats.other;
        }
    }

    if (
        stats.glyphs == 0
    ) {

        return stats;
    }

    // -------------------------------------------------------------------------
    // Second pass: most-common non-whitespace character.
    // -------------------------------------------------------------------------

    for (
        const char c :
        text
    ) {

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n'
        ) {

            continue;
        }

        std::size_t frequency =
            0;

        for (
            const char candidate :
            text
        ) {

            if (
                candidate == c
            ) {

                ++frequency;
            }
        }

        if (
            frequency >
            stats.most_common_count
        ) {

            stats.most_common_count =
                frequency;

            stats.most_common =
                c;
        }
    }

    return stats;
}

// =============================================================================
// TOKEN COUNT
// =============================================================================

[[nodiscard]]
std::size_t count_tokens(
    const std::string& text
) noexcept {

    std::size_t token_count =
        0;

    std::size_t token_length =
        0;

    for (
        std::size_t i = 0;
        i <= text.size();
        ++i
    ) {

        const bool end =
            i == text.size();

        const char c =
            end
                ? ' '
                : text[i];

        const bool separator =
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n';

        if (
            separator
        ) {

            if (
                token_length > 0
            ) {

                ++token_count;
                token_length =
                    0;
            }

        } else {

            ++token_length;
        }
    }

    return token_count;
}

// =============================================================================
// NUMERIC/GEOMETRY DOMINANCE
// =============================================================================
//
// This is the critical fix.
//
// Previous logic only rejected numeric-heavy strings when:
//
//     alpha == 0
//
// That allowed:
//
//     545845555885514585711l
//
// because one accidental 'l' made alpha > 0.
//
// We now evaluate the ratio directly.
//
// =============================================================================

[[nodiscard]]
bool is_numeric_geometry_stream(
    const TextStats& stats
) noexcept {

    if (
        stats.glyphs < 4 ||
        stats.digits == 0
    ) {

        return false;
    }

    const std::size_t normal =
        stats.digits +
        stats.alpha +
        stats.punctuation +
        stats.other;

    if (
        normal == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Numeric dominance regardless of tiny alphabetic corruption.
    // -------------------------------------------------------------------------

    const bool numeric_dominant =
        stats.digits * 100u >=
        static_cast<std::size_t>(
            MAX_NUMERIC_DOMINANCE_PERCENT
        ) *
        normal;

    if (
        !numeric_dominant
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Mostly numeric + only one/two alphabetic characters is still geometry.
    // -------------------------------------------------------------------------

    if (
        stats.alpha <= 2 &&
        stats.punctuation <= 2 &&
        stats.other <= 1
    ) {

        return true;
    }

    // -------------------------------------------------------------------------
    // Pure numeric streams are geometry unless they are clearly a compact
    // legitimate numeric value.
    // -------------------------------------------------------------------------

    if (
        stats.alpha == 0 &&
        stats.other == 0 &&
        stats.digits * 100u >=
            static_cast<std::size_t>(
                MAX_PURE_NUMERIC_PERCENT
            ) *
            normal
    ) {

        return true;
    }

    return false;
}

// =============================================================================
// REPETITIVE GEOMETRY
// =============================================================================

[[nodiscard]]
bool is_repetitive_geometry(
    const TextStats& stats
) noexcept {

    if (
        stats.glyphs < 4
    ) {

        return false;
    }

    return
        stats.most_common_count * 100u >=
        static_cast<std::size_t>(
            MAX_REPEATED_CHAR_PERCENT
        ) *
        stats.glyphs;
}

// =============================================================================
// PUNCTUATION GEOMETRY
// =============================================================================

[[nodiscard]]
bool is_punctuation_geometry(
    const TextStats& stats
) noexcept {

    if (
        stats.punctuation == 0
    ) {

        return false;
    }

    return
        stats.punctuation >=
        stats.alpha +
        stats.digits;
}

// =============================================================================
// EDGE PUNCTUATION
// =============================================================================

[[nodiscard]]
bool has_edge_punctuation(
    const std::string& text
) noexcept {

    std::size_t first =
        std::string::npos;

    std::size_t last =
        std::string::npos;

    for (
        std::size_t i = 0;
        i < text.size();
        ++i
    ) {

        const char c =
            text[i];

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n'
        ) {

            continue;
        }

        if (
            first ==
            std::string::npos
        ) {

            first =
                i;
        }

        last =
            i;
    }

    if (
        first ==
            std::string::npos ||
        last ==
            std::string::npos
    ) {

        return false;
    }

    return
        is_ascii_punctuation(
            text[first]
        ) ||
        is_ascii_punctuation(
            text[last]
        );
}

// =============================================================================
// SMALL TOKEN FRAGMENTATION
// =============================================================================

[[nodiscard]]
bool is_fragmented_short_tokens(
    const std::string& text
) noexcept {

    const std::size_t tokens =
        count_tokens(
            text
        );

    if (
        tokens < 2 ||
        tokens > MAX_FRAGMENTED_TOKENS
    ) {

        return false;
    }

    std::size_t short_tokens =
        0;

    std::size_t token_length =
        0;

    for (
        std::size_t i = 0;
        i <= text.size();
        ++i
    ) {

        const bool end =
            i == text.size();

        const char c =
            end
                ? ' '
                : text[i];

        const bool separator =
            c == ' ' ||
            c == '\t' ||
            c == '\r' ||
            c == '\n';

        if (
            separator
        ) {

            if (
                token_length > 0
            ) {

                if (
                    token_length <= 3
                ) {

                    ++short_tokens;
                }

                token_length =
                    0;
            }

        } else {

            ++token_length;
        }
    }

    return
        short_tokens ==
        tokens;
}

// =============================================================================
// COMMON OCR PLAUSIBILITY
// =============================================================================
//
// Applied to:
//
//     MatrixMatcher
//     Tesseract
//
// and for:
//
//     small chart crops
//     normal OCR regions
//
// It intentionally does not depend on OCR confidence.
//
// =============================================================================

[[nodiscard]]
bool line_text_plausible(
    const std::string& text
) noexcept {

    if (
        text.empty()
    ) {

        return false;
    }

    const TextStats stats =
        analyze_text(
            text
        );

    if (
        stats.glyphs == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // At least one meaningful OCR character.
    // -------------------------------------------------------------------------

    if (
        stats.alpha == 0 &&
        stats.digits == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // A single numeric glyph is too weak to be useful.
    // -------------------------------------------------------------------------

    if (
        stats.glyphs == 1 &&
        stats.digits == 1
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Repeated-character garbage.
    //
    // This remains safe in the generic recognizer because something like:
    //
    //     555555555555
    //
    // has essentially no meaningful OCR diversity.
    // -------------------------------------------------------------------------

    if (
        stats.glyphs >= 6 &&
        stats.most_common_count * 100u >=
            75u *
            stats.glyphs
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Pure punctuation/symbol garbage.
    // -------------------------------------------------------------------------

    if (
        stats.punctuation >=
            stats.alpha +
            stats.digits &&
        stats.alpha == 0 &&
        stats.digits == 0
    ) {

        return false;
    }

    if (
        stats.other >
        stats.alpha +
        stats.digits
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Leading/trailing punctuation is usually not useful OCR.
    // -------------------------------------------------------------------------

    if (
        has_edge_punctuation(
            text
        )
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Do NOT reject numeric-heavy text here.
    //
    // ChartLabelRecognizer owns chart-specific numeric-vs-geometry
    // classification.
    // -------------------------------------------------------------------------

    return true;
}

// =============================================================================
// MATRIX TEXT PLAUSIBILITY
// =============================================================================

[[nodiscard]]
bool matrix_text_plausible(
    const std::string& text
) noexcept {

    return
        line_text_plausible(
            text
        );
}

// =============================================================================
// SMALL CHART TEXT PLAUSIBILITY
// =============================================================================
//
// Small-region OCR receives additional restrictions.
//
// =============================================================================

[[nodiscard]]
bool small_chart_text_plausible(
    const std::string& text
) noexcept {

    if (
        !line_text_plausible(
            text
        )
    ) {

        return false;
    }

    const TextStats stats =
        analyze_text(
            text
        );

    if (
        stats.glyphs >
        MAX_SMALL_REGION_GLYPHS
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Small chart crops should not emit long numeric streams.
    // -------------------------------------------------------------------------

    if (
        stats.glyphs >= 6 &&
        stats.digits * 100u >=
            60u *
            stats.glyphs &&
        stats.alpha <= 2
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Long small-region text needs more alphabetic evidence.
    // -------------------------------------------------------------------------

    if (
        stats.glyphs >= 12 &&
        stats.alpha < 3
    ) {

        return false;
    }

    return true;
}

// =============================================================================
// TEXT LENGTH AGREEMENT
// =============================================================================

[[nodiscard]]
bool text_lengths_agree(
    const std::string& a,
    const std::string& b
) noexcept {

    const std::size_t a_len =
        count_glyphs(
            a
        );

    const std::size_t b_len =
        count_glyphs(
            b
        );

    if (
        a_len == 0 ||
        b_len == 0
    ) {

        return false;
    }

    const std::size_t difference =
        a_len > b_len
            ? a_len - b_len
            : b_len - a_len;

    const std::size_t allowed =
        std::max(
            std::size_t{1},
            std::max(
                a_len,
                b_len
            ) /
            3
        );

    return
        difference <=
        allowed;
}

// =============================================================================
// TEXT COMPOSITION SCORE
// =============================================================================

[[nodiscard]]
int text_composition_score(
    const std::string& text
) noexcept {

    if (
        text.empty()
    ) {

        return 0;
    }

    const TextStats stats =
        analyze_text(
            text
        );

    int score =
        0;

    score +=
        static_cast<int>(
            stats.alpha * 5
        );

    score +=
        static_cast<int>(
            stats.digits
        );

    score +=
        static_cast<int>(
            stats.spaces
        );

    score -=
        static_cast<int>(
            stats.punctuation * 4
        );

    score -=
        static_cast<int>(
            stats.other * 5
        );

    if (
        is_repetitive_geometry(
            stats
        )
    ) {

        score -=
            100;
    }

    if (
        is_numeric_geometry_stream(
            stats
        )
    ) {

        score -=
            100;
    }

    if (
        stats.glyphs >= 8 &&
        stats.alpha <
            MIN_ALPHA_FOR_LONG_TEXT
    ) {

        score -=
            30;
    }

    return score;
}

// =============================================================================
// OCR RESULT STRUCTURE
// =============================================================================

struct OcrResult {

    std::string text;

    int confidence = 0;

    bool engine_ok = false;

    bool plausible = false;
};

// =============================================================================
// COMMON RESULT SANITIZATION
// =============================================================================

[[nodiscard]]
OcrResult sanitize_result(
    std::string text,
    int confidence,
    bool engine_ok,
    bool small_region
) {

    trim_text(
        text
    );

    OcrResult result{
        std::move(text),
        confidence,
        engine_ok,
        false
    };

    if (
        result.text.empty()
    ) {

        result.confidence =
            0;

        return result;
    }

    result.plausible =
        small_region
            ? small_chart_text_plausible(
                  result.text
              )
            : line_text_plausible(
                  result.text
              );

    if (
        !result.plausible
    ) {

        result.text.clear();
        result.confidence =
            0;
    }

    return result;
}

} // namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

LineRecognizer::LineRecognizer(
    const GlyphMatcher& glyph_matcher,
    const TesseractRecognizer& tesseract_recognizer
) noexcept
    : glyph_matcher_(
          glyph_matcher
      ),
      tesseract_recognizer_(
          tesseract_recognizer
      )
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
                gap >
                space_gap
            ) {

                recognized_text.push_back(
                    ' '
                );
            }
        }

        // =========================================================================
        // PATCH
        // =========================================================================

        const std::size_t patch_size =
            static_cast<std::size_t>(
                patch_w
            ) *
            static_cast<std::size_t>(
                patch_h
            );

        std::vector<uint8_t> patch(
            patch_size,
            uint8_t{0}
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

    trim_text(
        recognized_text
    );

    // =========================================================================
    // CRITICAL:
    //
    // MatrixMatcher is deterministic, but deterministic output may still be
    // geometry hallucination.
    //
    // Reject it before returning it to the caller.
    // =========================================================================

    if (
        !line_text_plausible(
            recognized_text
        )
    ) {

        return {};
    }

    return recognized_text;
}

// =============================================================================
// LINE RECOGNITION
// =============================================================================
//
// Primary:
//
//     Tesseract
//
// Deterministic fallback:
//
//     MatrixMatcher
//
// Both paths are sanitized before arbitration.
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
    // REGION CLASSIFICATION
    // =========================================================================

    const bool small_region =
        is_small_line_region(
            width,
            y0,
            y1,
            channels
        );

    // =========================================================================
    // CONNECTED COMPONENTS
    // =========================================================================

    const std::vector<BoundingBox> char_boxes =
        ConnectedComponents::extract(
            image,
            width,
            y0,
            y1,
            channels
        );

    // =========================================================================
    // MATRIX MATCHER
    // =========================================================================

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

        trim_text(
            matrix_text
        );
    }

    // =========================================================================
    // TESSERACT
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

    trim_text(
        tesseract_text
    );

    // =========================================================================
    // SANITIZE BOTH ENGINES
    // =========================================================================

    OcrResult matrix_result =
        sanitize_result(
            matrix_text,
            0,
            !matrix_text.empty(),
            small_region
        );

    OcrResult tesseract_result =
        sanitize_result(
            tesseract_text,
            tesseract_confidence,
            tesseract_ok,
            small_region
        );

    matrix_text =
        std::move(
            matrix_result.text
        );

    tesseract_text =
        std::move(
            tesseract_result.text
        );

    const bool matrix_valid =
        matrix_result.engine_ok &&
        matrix_result.plausible &&
        !matrix_text.empty();

    const bool tesseract_valid =
        tesseract_result.engine_ok &&
        tesseract_result.plausible &&
        !tesseract_text.empty();

    // =========================================================================
    // NEITHER ENGINE VALID
    // =========================================================================

    if (
        !matrix_valid &&
        !tesseract_valid
    ) {

        return {};
    }

    // =========================================================================
    // MATRIX ONLY
    // =========================================================================

    if (
        matrix_valid &&
        !tesseract_valid
    ) {

        return matrix_text;
    }

    // =========================================================================
    // TESSERACT ONLY
    // =========================================================================

    if (
        !matrix_valid &&
        tesseract_valid
    ) {

        const std::size_t glyphs =
            count_glyphs(
                tesseract_text
            );

        if (
            small_region
        ) {

            if (
                glyphs >
                MAX_REASONABLE_SHORT_TEXT_LENGTH
            ) {

                return {};
            }

            if (
                tesseract_confidence <
                MIN_TESSERACT_SUPPORT_CONFIDENCE
            ) {

                return {};
            }
        }

        return tesseract_text;
    }

    // =========================================================================
    // EXACT AGREEMENT
    // =========================================================================

    if (
        matrix_text ==
        tesseract_text
    ) {

        return matrix_text;
    }

    // =========================================================================
    // BOTH ENGINES VALID
    // =========================================================================

    const TextStats matrix_stats =
        analyze_text(
            matrix_text
        );

    const TextStats tesseract_stats =
        analyze_text(
            tesseract_text
        );

    const bool matrix_geometry =
        is_numeric_geometry_stream(
            matrix_stats
        ) ||
        is_repetitive_geometry(
            matrix_stats
        ) ||
        is_punctuation_geometry(
            matrix_stats
        );

    const bool tesseract_geometry =
        is_numeric_geometry_stream(
            tesseract_stats
        ) ||
        is_repetitive_geometry(
            tesseract_stats
        ) ||
        is_punctuation_geometry(
            tesseract_stats
        );

    // =========================================================================
    // GEOMETRY-WEIGHTED ARBITRATION
    // =========================================================================

    if (
        matrix_geometry &&
        !tesseract_geometry
    ) {

        if (
            tesseract_confidence >=
            MIN_TESSERACT_SUPPORT_CONFIDENCE
        ) {

            return tesseract_text;
        }

        return {};
    }

    if (
        tesseract_geometry &&
        !matrix_geometry
    ) {

        return matrix_text;
    }

    // =========================================================================
    // LENGTH AGREEMENT
    // =========================================================================

    const bool lengths_agree =
        text_lengths_agree(
            matrix_text,
            tesseract_text
        );

    if (
        lengths_agree
    ) {

        const int matrix_score =
            text_composition_score(
                matrix_text
            );

        const int tesseract_score =
            text_composition_score(
                tesseract_text
            );

        if (
            matrix_score >
            tesseract_score
        ) {

            return matrix_text;
        }

        if (
            tesseract_score >
            matrix_score
        ) {

            if (
                tesseract_confidence >=
                MIN_TESSERACT_SUPPORT_CONFIDENCE
            ) {

                return tesseract_text;
            }

            return matrix_text;
        }

        // Deterministic tie-break for small chart crops.
        if (
            small_region
        ) {

            return matrix_text;
        }

        if (
            tesseract_confidence >=
            MIN_TESSERACT_SUPPORT_CONFIDENCE
        ) {

            return tesseract_text;
        }

        return matrix_text;
    }

    // =========================================================================
    // LARGE TESSERACT EXPANSION
    // =========================================================================

    const std::size_t matrix_glyphs =
        matrix_stats.glyphs;

    const std::size_t tesseract_glyphs =
        tesseract_stats.glyphs;

    if (
        tesseract_glyphs >
        matrix_glyphs + 3
    ) {

        return matrix_text;
    }

    // =========================================================================
    // LARGE MATRIX EXPANSION
    // =========================================================================

    if (
        matrix_glyphs >
        tesseract_glyphs + 3
    ) {

        if (
            tesseract_confidence >=
            MIN_TESSERACT_SUPPORT_CONFIDENCE
        ) {

            return tesseract_text;
        }

        return matrix_text;
    }

    // =========================================================================
    // FINAL CONSERVATIVE ARBITRATION
    // =========================================================================

    if (
        tesseract_confidence >=
        MIN_TESSERACT_SUPPORT_CONFIDENCE
    ) {

        return tesseract_text;
    }

    return matrix_text;
}

} // namespace fin_ocr
