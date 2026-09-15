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
// One engine is reused per worker thread.
//
// TessBaseAPI remains fully encapsulated inside TesseractEngine.
// =============================================================================

thread_local TesseractEngine g_tesseract;

// =============================================================================
// ASCII HELPERS
// =============================================================================

[[nodiscard]]
bool is_ascii_alpha(
    unsigned char c
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
    unsigned char c
) noexcept {

    return
        c >= '0' &&
        c <= '9';
}

[[nodiscard]]
bool is_ascii_space(
    unsigned char c
) noexcept {

    return
        c == ' ' ||
        c == '\t' ||
        c == '\r' ||
        c == '\n';
}

[[nodiscard]]
bool is_ascii_punctuation(
    unsigned char c
) noexcept {

    return
        c == '.' ||
        c == ',' ||
        c == '-' ||
        c == '+' ||
        c == '$' ||
        c == '%' ||
        c == '/' ||
        c == ':' ||
        c == ';' ||
        c == '(' ||
        c == ')' ||
        c == '=' ||
        c == '|' ||
        c == '_' ||
        c == '*' ||
        c == '\'' ||
        c == '"' ||
        c == '`' ||
        c == '~';
}

// =============================================================================
// TEXT METRICS
// =============================================================================

struct TextMetrics {

    std::size_t glyphs = 0;

    std::size_t alphabetic = 0;

    std::size_t digits = 0;

    std::size_t punctuation = 0;

    std::size_t whitespace = 0;

    std::size_t other = 0;
};

[[nodiscard]]
TextMetrics analyze_text(
    const std::string& text
) noexcept {

    TextMetrics metrics{};

    for (
        const unsigned char c :
        text
    ) {

        if (
            is_ascii_space(c)
        ) {

            ++metrics.whitespace;

            continue;
        }

        ++metrics.glyphs;

        if (
            is_ascii_alpha(c)
        ) {

            ++metrics.alphabetic;

        } else if (
            is_ascii_digit(c)
        ) {

            ++metrics.digits;

        } else if (
            is_ascii_punctuation(c)
        ) {

            ++metrics.punctuation;

        } else {

            ++metrics.other;
        }
    }

    return metrics;
}

// =============================================================================
// REPEATED CHARACTER DETECTION
// =============================================================================
//
// Detects OCR hallucinations such as:
//
//     555555555
//     888888888
//     aaaaaaaa
//
// =============================================================================

[[nodiscard]]
bool repeated_character_noise(
    const TextMetrics& metrics,
    const std::string& text
) noexcept {

    if (
        metrics.glyphs < 5
    ) {

        return false;
    }

    char first =
        '\0';

    std::size_t count =
        0;

    for (
        const unsigned char c :
        text
    ) {

        if (
            is_ascii_space(c)
        ) {

            continue;
        }

        if (
            count == 0
        ) {

            first =
                static_cast<char>(c);

        } else if (
            c !=
            static_cast<unsigned char>(
                first
            )
        ) {

            return false;
        }

        ++count;
    }

    return true;
}

// =============================================================================
// NUMERIC-DOMINANT GARBAGE
// =============================================================================
//
// Important:
//
// Legitimate numeric OCR exists:
//
//     2024
//     1024.5
//     -12
//
// But a chart-geometry hallucination tends to produce:
//
//     long digit streams
//     very high digit ratio
//     almost no alphabetic characters
//
// =============================================================================

[[nodiscard]]
bool numeric_geometry_noise(
    const TextMetrics& metrics,
    const std::string& text
) noexcept {

    if (
        metrics.glyphs < 7
    ) {

        return false;
    }

    const double digit_ratio =
        static_cast<double>(
            metrics.digits
        ) /
        static_cast<double>(
            metrics.glyphs
        );

    const double normal_ratio =
        static_cast<double>(
            metrics.alphabetic +
            metrics.digits
        ) /
        static_cast<double>(
            metrics.glyphs
        );

    // Very long digit-dominated strings are almost always geometry.
    if (
        digit_ratio >= 0.80 &&
        metrics.alphabetic == 0
    ) {

        return true;
    }

    // A long mostly numeric string containing one accidental OCR letter is
    // still highly suspicious.
    if (
        digit_ratio >= 0.75 &&
        metrics.alphabetic <= 1 &&
        metrics.glyphs >= 10
    ) {

        return true;
    }

    // Numeric/punctuation streams with no alphabetic structure.
    if (
        normal_ratio >= 0.90 &&
        metrics.alphabetic == 0 &&
        metrics.glyphs >= 12
    ) {

        return true;
    }

    // Explicit repeated digit hallucination.
    if (
        metrics.digits >= 8 &&
        metrics.alphabetic == 0
    ) {

        std::size_t distinct_digits =
            0;

        bool seen[10]{};

        for (
            const unsigned char c :
            text
        ) {

            if (
                is_ascii_digit(c)
            ) {

                const std::size_t d =
                    static_cast<std::size_t>(
                        c - '0'
                    );

                if (
                    !seen[d]
                ) {

                    seen[d] =
                        true;

                    ++distinct_digits;
                }
            }
        }

        if (
            distinct_digits <= 3
        ) {

            return true;
        }
    }

    return false;
}

// =============================================================================
// ALPHABETIC FRAGMENT QUALITY
// =============================================================================
//
// Rejects long OCR fragments that technically contain letters but are very
// unlikely to represent a meaningful chart label.
//
// Examples of suspicious outputs from chart geometry:
//
//     Cpeay 2 ipa
//     PE CE FS
//     ++1f 51
//
// These cannot be rejected purely by Tesseract confidence.
// =============================================================================

[[nodiscard]]
bool alphabetic_fragment_noise(
    const TextMetrics& metrics,
    const std::string& text
) noexcept {

    if (
        metrics.glyphs < 4
    ) {

        return false;
    }

    const double alpha_ratio =
        static_cast<double>(
            metrics.alphabetic
        ) /
        static_cast<double>(
            metrics.glyphs
        );

    // -------------------------------------------------------------------------
    // Very long output containing almost no real alphabetic structure.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs >= 8 &&
        metrics.alphabetic <= 1
    ) {

        return true;
    }

    // -------------------------------------------------------------------------
    // Long mixed garbage with heavy punctuation.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs >= 6 &&
        metrics.punctuation >= 2 &&
        metrics.alphabetic <= 2
    ) {

        return true;
    }

    // -------------------------------------------------------------------------
    // Long strings with only a tiny amount of alphabetic content.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs >= 10 &&
        alpha_ratio < 0.20
    ) {

        return true;
    }

    // -------------------------------------------------------------------------
    // Excessively space-separated fragments.
    //
    // Chart lines often make Tesseract hallucinate multiple tiny tokens.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs >= 7 &&
        metrics.whitespace >= 3 &&
        metrics.alphabetic <= 3
    ) {

        return true;
    }

    return false;
}

// =============================================================================
// TESSERACT TEXT QUALITY GATE
// =============================================================================
//
// This is deliberately independent of TessBaseAPI confidence.
//
// Tesseract confidence answers:
//
//     "How confident is the OCR engine in its interpretation?"
//
// It does NOT answer:
//
//     "Is this interpretation actually chart text?"
//
// =============================================================================

[[nodiscard]]
bool acceptable_tesseract_text(
    const std::string& text,
    float confidence,
    bool numeric_mode
) noexcept {

    if (
        text.empty()
    ) {

        return false;
    }

    if (
        !std::isfinite(
            static_cast<double>(
                confidence
            )
        )
    ) {

        return false;
    }

    const TextMetrics metrics =
        analyze_text(
            text
        );

    if (
        metrics.glyphs == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // No useful character classes.
    // -------------------------------------------------------------------------

    if (
        metrics.alphabetic == 0 &&
        metrics.digits == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Repeated-character hallucination.
    // -------------------------------------------------------------------------

    if (
        repeated_character_noise(
            metrics,
            text
        )
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Numeric geometry hallucination.
    //
    // This rule intentionally applies even when numeric_mode == true.
    // numeric_mode means "this region may contain numbers"; it does not mean
    // that every long digit stream is valid.
    // -------------------------------------------------------------------------

    if (
        numeric_geometry_noise(
            metrics,
            text
        )
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Alphabetic fragment hallucination.
    // -------------------------------------------------------------------------

    if (
        alphabetic_fragment_noise(
            metrics,
            text
        )
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Punctuation-dominated output.
    // -------------------------------------------------------------------------

    if (
        metrics.punctuation > 0 &&
        metrics.punctuation >=
            metrics.alphabetic +
            metrics.digits
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Other/unclassified characters.
    // -------------------------------------------------------------------------

    if (
        metrics.other > 0
    ) {

        const std::size_t normal =
            metrics.alphabetic +
            metrics.digits;

        if (
            normal == 0 ||
            metrics.other > normal
        ) {

            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Single glyph.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs == 1
    ) {

        if (
            !is_ascii_alpha(
                static_cast<unsigned char>(
                    text.front()
                )
            )
        ) {

            return false;
        }

        if (
            confidence < 70.0f
        ) {

            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Two glyphs.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs == 2
    ) {

        if (
            metrics.alphabetic == 0
        ) {

            return false;
        }

        if (
            confidence < 45.0f
        ) {

            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Three glyphs.
    // -------------------------------------------------------------------------

    if (
        metrics.glyphs == 3
    ) {

        if (
            metrics.digits == 3
        ) {

            return false;
        }

        if (
            metrics.punctuation >= 2
        ) {

            return false;
        }

        if (
            metrics.alphabetic == 3
        ) {

            if (
                confidence < 45.0f
            ) {

                return false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Long numeric output.
    //
    // A genuinely numeric line is allowed only when it stays compact.
    // This is especially important for chart geometry.
    // -------------------------------------------------------------------------

    if (
        numeric_mode &&
        metrics.alphabetic == 0 &&
        metrics.digits > 0
    ) {

        if (
            metrics.glyphs > 6
        ) {

            const double digit_ratio =
                static_cast<double>(
                    metrics.digits
                ) /
                static_cast<double>(
                    metrics.glyphs
                );

            if (
                digit_ratio >= 0.75
            ) {

                return false;
            }
        }
    }

    return true;
}

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
        !acceptable_tesseract_text(
            text,
            confidence,
            numeric_mode
        )
    ) {

        return -1.0;
    }

    const TextMetrics metrics =
        analyze_text(
            text
        );

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
        metrics.glyphs == 1
    ) {

        score -=
            12.0;
    } else if (
        metrics.glyphs == 2
    ) {

        score -=
            3.0;
    }

    // =========================================================================
    // ALPHABETIC REWARD
    // =========================================================================

    if (
        metrics.alphabetic >= 2
    ) {

        score +=
            4.0;
    }

    if (
        metrics.alphabetic >= 3
    ) {

        score +=
            2.0;
    }

    // =========================================================================
    // PUNCTUATION PENALTY
    // =========================================================================

    if (
        metrics.punctuation > 0
    ) {

        const double punctuation_ratio =
            static_cast<double>(
                metrics.punctuation
            ) /
            static_cast<double>(
                std::max(
                    std::size_t{1},
                    metrics.glyphs
                )
            );

        score -=
            punctuation_ratio *
            20.0;
    }

    // =========================================================================
    // NUMERIC HYPOTHESIS
    // =========================================================================
    //
    // The old scoring strongly rewarded numeric strings. That caused long
    // digit-heavy chart geometry to win despite not being meaningful text.
    //
    // Numeric mode now gives only a small reward when the output is compact
    // and genuinely numeric.
    // =========================================================================

    if (
        numeric_mode
    ) {

        const double digit_ratio =
            static_cast<double>(
                metrics.digits
            ) /
            static_cast<double>(
                std::max(
                    std::size_t{1},
                    metrics.glyphs
                )
            );

        if (
            metrics.alphabetic == 0 &&
            digit_ratio >= 0.60 &&
            metrics.glyphs <= 6
        ) {

            score +=
                2.0;

        } else if (
            metrics.glyphs > 6 &&
            digit_ratio >= 0.70
        ) {

            score -=
                15.0;
        }
    }

    // =========================================================================
    // LONG TEXT PENALTY
    // =========================================================================
    //
    // Chart-label OCR is generally compact. Extremely long output from a
    // 20-30px high crop is suspicious.
    // =========================================================================

    if (
        metrics.glyphs >= 12
    ) {

        score -=
            static_cast<double>(
                metrics.glyphs -
                11
            ) *
            2.5;
    }

    if (
        metrics.glyphs >= 20
    ) {

        score -=
            20.0;
    }

    // =========================================================================
    // PSM 7 PREFERENCE
    // =========================================================================

    if (
        source_psm == 7
    ) {

        score +=
            2.0;
    }

    // =========================================================================
    // FINAL SANITY
    // =========================================================================

    if (
        !std::isfinite(score)
    ) {

        return -1.0;
    }

    return score;
}

// =============================================================================
// SINGLE TESSERACT PASS
// =============================================================================
//
// TesseractEngine owns direct TessBaseAPI interaction.
//
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

    return
        g_tesseract.recognize(
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
// Numeric regions additionally receive:
//
//     PSM 7 + numeric whitelist
//     PSM 13 + numeric whitelist
//
// Every OCR result passes through acceptable_tesseract_text() before it can
// compete for selection.
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

            // -----------------------------------------------------------------
            // Do not even store invalid OCR hypotheses.
            // -----------------------------------------------------------------

            if (
                score >= 0.0
            ) {

                candidates.push_back({
                    std::move(text),
                    conf,
                    psm,
                    false,
                    score
                });
            }
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

                if (
                    score >= 0.0
                ) {

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
    }

    // =========================================================================
    // NO VALID CANDIDATES
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

    // =========================================================================
    // FINAL QUALITY CHECK
    // =========================================================================
    //
    // Protect against future scoring changes accidentally publishing an
    // invalid candidate.
    // =========================================================================

    if (
        !acceptable_tesseract_text(
            best_it->text,
            best_it->confidence,
            best_it->numeric_mode
        )
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
