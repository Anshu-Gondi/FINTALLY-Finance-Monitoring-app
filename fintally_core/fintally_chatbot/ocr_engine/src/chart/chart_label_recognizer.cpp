#include "fin_ocr/chart/chart_label_recognizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/line/line_recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

namespace {

// =============================================================================
// INTERNAL DETECTED LABEL
// =============================================================================

struct DetectedLabel {

    chart::ChartLabel label;

    float density = 0.0f;

    std::size_t glyph_count = 0;
};

// =============================================================================
// OCR REJECTION REASON
// =============================================================================

enum class CandidateRejectReason : std::uint8_t {

    NONE = 0,

    GEOMETRY,

    ZONE,

    ZONE_SCORE,

    SEQUENCE,

    EMPTY_CROP,

    OCR_EMPTY,

    REPEATED_NOISE,

    TEXT_QUALITY,

    GEOMETRY_TEXT,

    DENSITY,

    CONFIDENCE,

    COMPOSITION,

    SINGLE_GLYPH,

    TWO_GLYPH,

    THREE_GLYPH,

    PUNCTUATION,

    OTHER_CHARS,

    NUMERIC_ONLY,

    LONG_GEOMETRY,

    OCCUPANCY
};

[[nodiscard]]
const char* candidate_reject_reason_string(
    CandidateRejectReason reason
) noexcept {

    switch (
        reason
    ) {

        case CandidateRejectReason::NONE:
            return "none";

        case CandidateRejectReason::GEOMETRY:
            return "geometry";

        case CandidateRejectReason::ZONE:
            return "zone";

        case CandidateRejectReason::ZONE_SCORE:
            return "zone_score";

        case CandidateRejectReason::SEQUENCE:
            return "sequence";

        case CandidateRejectReason::EMPTY_CROP:
            return "empty_crop";

        case CandidateRejectReason::OCR_EMPTY:
            return "ocr_empty";

        case CandidateRejectReason::REPEATED_NOISE:
            return "repeated_noise";

        case CandidateRejectReason::TEXT_QUALITY:
            return "text_quality";

        case CandidateRejectReason::GEOMETRY_TEXT:
            return "geometry_text";

        case CandidateRejectReason::DENSITY:
            return "density";

        case CandidateRejectReason::CONFIDENCE:
            return "confidence";

        case CandidateRejectReason::COMPOSITION:
            return "composition";

        case CandidateRejectReason::SINGLE_GLYPH:
            return "single_glyph";

        case CandidateRejectReason::TWO_GLYPH:
            return "two_glyph";

        case CandidateRejectReason::THREE_GLYPH:
            return "three_glyph";

        case CandidateRejectReason::PUNCTUATION:
            return "punctuation";

        case CandidateRejectReason::OTHER_CHARS:
            return "other_chars";

        case CandidateRejectReason::NUMERIC_ONLY:
            return "numeric_only";

        case CandidateRejectReason::LONG_GEOMETRY:
            return "long_geometry";

        case CandidateRejectReason::OCCUPANCY:
            return "occupancy";

        default:
            return "unknown";
    }
}

// =============================================================================
// INTERNAL TEXT CANDIDATE
// =============================================================================

struct TextCandidate {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    std::size_t active_pixels = 0;
};

// =============================================================================
// OCR BUFFER
// =============================================================================
//
// The detector operates on channel 1.
//
// OCR receives a padded grayscale crop.
//
// Keeping the dimensions together with the buffer prevents the OCR path from
// accidentally using the unpadded candidate dimensions with a padded buffer.
//
// =============================================================================

struct OcrBuffer {

    std::vector<std::uint8_t> data;

    int width = 0;

    int height = 0;
};

// =============================================================================
// LABEL ZONE
// =============================================================================

enum class LabelZone : std::uint8_t {

    UNKNOWN = 0,

    CATEGORY_AXIS,

    TITLE,

    PLOT_INTERIOR,

    OUTSIDE
};

// =============================================================================
// LOCAL LABEL DETECTION LIMITS
// =============================================================================

constexpr std::uint8_t LABEL_FOREGROUND_THRESHOLD =
    20;

constexpr int MIN_LABEL_WIDTH =
    4;

constexpr int MIN_LABEL_HEIGHT =
    5;

constexpr int MAX_CANDIDATE_WIDTH =
    320;

constexpr int MAX_CANDIDATE_HEIGHT =
    32;

constexpr int MAX_CANDIDATE_COMPONENTS =
    4096;

constexpr double MAX_CANDIDATE_WIDTH_RATIO =
    0.35;

constexpr double MAX_CANDIDATE_HEIGHT_RATIO =
    0.20;

constexpr double MIN_LABEL_DENSITY =
    0.010;

constexpr double MAX_WIDE_DENSE_RATIO =
    0.25;

constexpr double MAX_WIDE_DENSE_DENSITY =
    0.08;

// =============================================================================
// SHORT OCR ADMISSION
// =============================================================================

constexpr float MIN_SINGLE_ALPHA_CONFIDENCE =
    0.78f;

constexpr float MIN_SHORT_LABEL_CONFIDENCE =
    0.55f;

// A two-glyph label is particularly vulnerable to OCR hallucinations such as:
//
//     Ju
//     2s
//     5u
//
// Require stronger confidence than the generic short-label threshold.
constexpr float MIN_TWO_GLYPH_CONFIDENCE =
    0.68f;

// =============================================================================
// COMPONENT FILTERING
// =============================================================================

constexpr int MIN_COMPONENT_PIXELS =
    2;

constexpr int MIN_GLYPH_HEIGHT =
    4;

constexpr int MAX_GLYPH_HEIGHT =
    24;

constexpr int MAX_GLYPH_WIDTH =
    32;

constexpr double MAX_COMPONENT_DENSITY =
    0.90;

constexpr double MIN_COMPONENT_DENSITY =
    0.025;

constexpr int MAX_GLYPH_HORIZONTAL_GAP =
    12;

constexpr int MAX_BASELINE_DELTA =
    5;

constexpr int MAX_CENTER_Y_DELTA =
    6;

constexpr double MAX_GLYPH_HEIGHT_RATIO =
    2.0;

// =============================================================================
// ZONE ESTIMATION
// =============================================================================

constexpr double CATEGORY_ZONE_MIN_Y_RATIO =
    0.68;

constexpr double CATEGORY_ZONE_MAX_Y_RATIO =
    0.87;

constexpr double TITLE_ZONE_MIN_Y_RATIO =
    0.87;

constexpr double BOTTOM_EXCLUSION_RATIO =
    0.985;

// =============================================================================
// CATEGORY-AXIS SPATIAL SCORING
// =============================================================================

constexpr double CATEGORY_CENTER_SCORE_WEIGHT =
    0.40;

constexpr double CATEGORY_HEIGHT_SCORE_WEIGHT =
    0.20;

constexpr double CATEGORY_WIDTH_SCORE_WEIGHT =
    0.10;

constexpr double CATEGORY_DENSITY_SCORE_WEIGHT =
    0.10;

constexpr double CATEGORY_ALIGNMENT_SCORE_WEIGHT =
    0.20;

constexpr double MIN_CATEGORY_ZONE_SCORE =
    0.38;

constexpr double MIN_CATEGORY_HEIGHT_RATIO =
    0.010;

constexpr double MAX_CATEGORY_HEIGHT_RATIO =
    0.045;

// =============================================================================
// CATEGORY LABEL LAYOUT
// =============================================================================

constexpr int CATEGORY_ALIGNMENT_RADIUS =
    140;

constexpr std::size_t
    MIN_CATEGORY_NEIGHBOURS_FOR_STRONG_ALIGNMENT =
        2;

// =============================================================================
// CATEGORY SEQUENCE MODEL
// =============================================================================
//
// Real x-axis labels form a spatial sequence.
//
// We use:
//
//     baseline consistency
//     glyph-height consistency
//     horizontal spacing consistency
//
// as an additional structural prior.
//
// OCR text itself is intentionally not used here.
//
// =============================================================================

constexpr double CATEGORY_SEQUENCE_BASELINE_TOLERANCE =
    8.0;

constexpr double CATEGORY_SEQUENCE_HEIGHT_TOLERANCE =
    0.45;

constexpr double CATEGORY_SEQUENCE_SPACING_TOLERANCE =
    0.60;

constexpr int CATEGORY_SEQUENCE_NEIGHBOUR_RADIUS =
    180;

constexpr std::size_t MIN_CATEGORY_SEQUENCE_SIZE =
    3;

constexpr double MIN_CATEGORY_SEQUENCE_SCORE =
    0.52;

constexpr double CATEGORY_SEQUENCE_BASELINE_WEIGHT =
    0.40;

constexpr double CATEGORY_SEQUENCE_HEIGHT_WEIGHT =
    0.25;

constexpr double CATEGORY_SEQUENCE_SPACING_WEIGHT =
    0.35;

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================

constexpr bool CHART_LABEL_DEBUG =
    true;

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
// ASCII CHARACTER HELPERS
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
// REPEATED GLYPH REJECTION
// =============================================================================

[[nodiscard]]
bool is_repeated_glyph_noise(
    const std::string& text
) noexcept {

    const std::size_t glyph_count =
        count_glyphs(
            text
        );

    if (
        glyph_count < 3
    ) {

        return false;
    }

    char first =
        '\0';

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

        if (
            first == '\0'
        ) {

            first =
                c;

            continue;
        }

        if (
            c != first
        ) {

            return false;
        }
    }

    return true;
}

// =============================================================================
// TEXT QUALITY
// =============================================================================

[[nodiscard]]
bool acceptable_label_text(
    const std::string& text,
    std::size_t glyph_count
) noexcept {

    if (
        text.empty() ||
        glyph_count == 0
    ) {

        return false;
    }

    std::size_t alphabetic =
        0;

    std::size_t digits =
        0;

    std::size_t punctuation =
        0;

    std::size_t whitespace =
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

            ++whitespace;

        } else if (
            is_ascii_alpha(c)
        ) {

            ++alphabetic;

        } else if (
            is_ascii_digit(c)
        ) {

            ++digits;

        } else if (
            is_ascii_punctuation(c)
        ) {

            ++punctuation;
        }
    }

    if (
        alphabetic == 0 &&
        digits == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Single glyph
    // -------------------------------------------------------------------------

    if (
        glyph_count == 1
    ) {

        if (
            digits == 1 ||
            punctuation == 1
        ) {

            return false;
        }

        return
            alphabetic == 1;
    }

    // -------------------------------------------------------------------------
    // Two glyphs
    // -------------------------------------------------------------------------

    if (
        glyph_count == 2
    ) {

        if (
            alphabetic >= 1
        ) {

            return true;
        }

        if (
            digits ==
            glyph_count
        ) {

            return false;
        }

        return
            punctuation == 0;
    }

    // -------------------------------------------------------------------------
    // Numeric/punctuation-only fragments
    // -------------------------------------------------------------------------

    if (
        alphabetic == 0 &&
        punctuation > 0 &&
        digits <= 2
    ) {

        return false;
    }

    if (
        punctuation > alphabetic + 2 &&
        alphabetic == 0
    ) {

        return false;
    }

    // -------------------------------------------------------------------------
    // Alphabetic labels
    // -------------------------------------------------------------------------

    if (
        alphabetic > 0
    ) {

        return true;
    }

    // -------------------------------------------------------------------------
    // Numeric axis values
    // -------------------------------------------------------------------------

    return
        digits >= 3 &&
        punctuation <= 1 &&
        whitespace <
            glyph_count;
}

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_candidate(
    const TextCandidate& candidate
) noexcept {

    return
        candidate.min_x >= 0 &&
        candidate.min_y >= 0 &&
        candidate.max_x >= candidate.min_x &&
        candidate.max_y >= candidate.min_y &&
        candidate.active_pixels > 0;
}

// =============================================================================
// RECTANGLE OVERLAP
// =============================================================================

[[nodiscard]]
bool vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept {

    return
        a_min_y <= b_max_y &&
        b_min_y <= a_max_y;
}

// =============================================================================
// HORIZONTAL OVERLAP
// =============================================================================

[[nodiscard]]
bool horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept {

    return
        a_min_x <= b_max_x &&
        b_min_x <= a_max_x;
}

// =============================================================================
// LABEL CONFIDENCE
// =============================================================================

[[nodiscard]]
float label_confidence(
    float density,
    int width,
    int height,
    std::size_t glyph_count
) noexcept {

    if (
        width <= 0 ||
        height <= 0 ||
        glyph_count == 0
    ) {

        return 0.0f;
    }

    const double density_score =
        std::clamp(
            static_cast<double>(
                density
            ) * 18.0,
            0.0,
            1.0
        );

    const double width_score =
        std::clamp(
            static_cast<double>(
                width
            ) / 32.0,
            0.0,
            1.0
        );

    const double height_score =
        std::clamp(
            static_cast<double>(
                height
            ) / 12.0,
            0.0,
            1.0
        );

    const double glyph_score =
        std::clamp(
            static_cast<double>(
                glyph_count
            ) / 8.0,
            0.0,
            1.0
        );

    return static_cast<float>(
        std::clamp(
            density_score * 0.45 +
            width_score * 0.15 +
            height_score * 0.15 +
            glyph_score * 0.25,
            0.0,
            1.0
        )
    );
}

// =============================================================================
// DUPLICATE LABEL DETECTION
// =============================================================================

[[nodiscard]]
bool duplicate_label(
    const DetectedLabel& candidate,
    const std::vector<DetectedLabel>& existing
) noexcept {

    for (
        const DetectedLabel& current :
        existing
    ) {

        if (
            current.label.text !=
            candidate.label.text
        ) {

            continue;
        }

        if (
            !vertical_overlap(
                current.label.min_y,
                current.label.max_y,
                candidate.label.min_y,
                candidate.label.max_y
            )
        ) {

            continue;
        }

        if (
            horizontal_overlap(
                current.label.min_x,
                current.label.max_x,
                candidate.label.min_x,
                candidate.label.max_x
            )
        ) {

            return true;
        }
    }

    return false;
}

// =============================================================================
// CHANNEL-3 FOREGROUND ACCESSOR
// =============================================================================
//
// The chart preprocessing contract uses:
//
//     channel 0 = supporting chart information
//     channel 1 = OCR foreground
//     channel 2 = supporting chart information
//
// This function intentionally reads channel 1.
//
// =============================================================================

[[nodiscard]]
inline std::uint8_t chart_foreground(
    const std::uint8_t* chart_buffer,
    int width,
    int x,
    int y
) noexcept {

    const std::size_t index =
        (
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)
        ) *
        3u +
        1u;

    return chart_buffer[index];
}

// =============================================================================
// ZONE CLASSIFICATION
// =============================================================================

[[nodiscard]]
LabelZone classify_label_zone(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept {

    if (
        !valid_candidate(candidate) ||
        image_width <= 0 ||
        image_height <= 0
    ) {

        return LabelZone::UNKNOWN;
    }

    const double center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) /
        2.0;

    const double y_ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    if (
        y_ratio >=
            CATEGORY_ZONE_MIN_Y_RATIO &&
        y_ratio <
            CATEGORY_ZONE_MAX_Y_RATIO
    ) {

        return LabelZone::CATEGORY_AXIS;
    }

    if (
        y_ratio >=
        TITLE_ZONE_MIN_Y_RATIO
    ) {

        if (
            y_ratio >=
            BOTTOM_EXCLUSION_RATIO
        ) {

            return LabelZone::OUTSIDE;
        }

        return LabelZone::TITLE;
    }

    return LabelZone::PLOT_INTERIOR;
}

// =============================================================================
// CATEGORY ZONE GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_category_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept {

    if (
        classify_label_zone(
            candidate,
            image_width,
            image_height
        ) !=
        LabelZone::CATEGORY_AXIS
    ) {

        return false;
    }

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        width < MIN_LABEL_WIDTH ||
        height < MIN_LABEL_HEIGHT
    ) {

        return false;
    }

    const double height_ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio <
            MIN_CATEGORY_HEIGHT_RATIO ||
        height_ratio >
            MAX_CATEGORY_HEIGHT_RATIO
    ) {

        return false;
    }

    const double width_ratio =
        static_cast<double>(
            width
        ) /
        static_cast<double>(
            image_width
        );

    if (
        width_ratio >
        MAX_CANDIDATE_WIDTH_RATIO
    ) {

        return false;
    }

    const std::size_t area =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            height
        );

    if (
        area == 0
    ) {

        return false;
    }

    const double density =
        static_cast<double>(
            candidate.active_pixels
        ) /
        static_cast<double>(
            area
        );

    if (
        !std::isfinite(
            density
        ) ||
        density <
            MIN_LABEL_DENSITY
    ) {

        return false;
    }

    return true;
}

// =============================================================================
// CATEGORY ZONE SCORE
// =============================================================================

[[nodiscard]]
double category_center_score(
    const TextCandidate& candidate,
    int image_height
) noexcept {

    if (
        image_height <= 0
    ) {

        return 0.0;
    }

    const double center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) /
        2.0;

    const double y_ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    const double center =
        (
            CATEGORY_ZONE_MIN_Y_RATIO +
            CATEGORY_ZONE_MAX_Y_RATIO
        ) /
        2.0;

    const double half_range =
        (
            CATEGORY_ZONE_MAX_Y_RATIO -
            CATEGORY_ZONE_MIN_Y_RATIO
        ) /
        2.0;

    if (
        half_range <= 0.0
    ) {

        return 0.0;
    }

    const double distance =
        std::abs(
            y_ratio -
            center
        );

    return std::clamp(
        1.0 -
        (
            distance /
            half_range
        ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double category_height_score(
    const TextCandidate& candidate,
    int image_height
) noexcept {

    if (
        image_height <= 0
    ) {

        return 0.0;
    }

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    const double ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    const double target =
        (
            MIN_CATEGORY_HEIGHT_RATIO +
            MAX_CATEGORY_HEIGHT_RATIO
        ) /
        2.0;

    const double range =
        (
            MAX_CATEGORY_HEIGHT_RATIO -
            MIN_CATEGORY_HEIGHT_RATIO
        ) /
        2.0;

    if (
        range <= 0.0
    ) {

        return 0.0;
    }

    return std::clamp(
        1.0 -
        std::abs(
            ratio -
            target
        ) /
        range,
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY WIDTH SCORE
// =============================================================================

[[nodiscard]]
double category_width_score(
    const TextCandidate& candidate,
    int image_width
) noexcept {

    if (
        image_width <= 0
    ) {

        return 0.0;
    }

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const double ratio =
        static_cast<double>(
            width
        ) /
        static_cast<double>(
            image_width
        );

    if (
        ratio <= 0.0
    ) {

        return 0.0;
    }

    if (
        ratio >= 0.12
    ) {

        return 0.0;
    }

    return std::clamp(
        1.0 -
        ratio /
        0.12,
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY DENSITY SCORE
// =============================================================================

[[nodiscard]]
double category_density_score(
    const TextCandidate& candidate
) noexcept {

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        width <= 0 ||
        height <= 0
    ) {

        return 0.0;
    }

    const std::size_t area =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            height
        );

    if (
        area == 0
    ) {

        return 0.0;
    }

    const double density =
        static_cast<double>(
            candidate.active_pixels
        ) /
        static_cast<double>(
            area
        );

    return std::clamp(
        (
            density -
            0.015
        ) /
        0.20,
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY HORIZONTAL ALIGNMENT SCORE
// =============================================================================
//
// IMPORTANT:
//
// image_width is deliberately part of this function's signature.
//
// The previous implementation passed:
//
//     classify_label_zone(other, 1, image_height)
//
// which was incorrect because classify_label_zone() validates image_width.
//
// =============================================================================

[[nodiscard]]
double category_alignment_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept {

    if (
        image_width <= 0 ||
        image_height <= 0
    ) {

        return 0.0;
    }

    const double candidate_center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) /
        2.0;

    std::size_t neighbours =
        0;

    double accumulated_y_distance =
        0.0;

    for (
        const TextCandidate& other :
        candidates
    ) {

        if (
            &other ==
            &candidate
        ) {

            continue;
        }

        if (
            classify_label_zone(
                other,
                image_width,
                image_height
            ) !=
            LabelZone::CATEGORY_AXIS
        ) {

            continue;
        }

        const int horizontal_distance =
            other.min_x >
                candidate.max_x
                ? other.min_x -
                  candidate.max_x -
                  1
                : candidate.min_x >
                    other.max_x
                    ? candidate.min_x -
                      other.max_x -
                      1
                    : 0;

        if (
            horizontal_distance >
            CATEGORY_ALIGNMENT_RADIUS
        ) {

            continue;
        }

        const double other_center_y =
            (
                static_cast<double>(
                    other.min_y
                ) +
                static_cast<double>(
                    other.max_y
                )
            ) /
            2.0;

        const double y_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            y_distance >
            10.0
        ) {

            continue;
        }

        ++neighbours;

        accumulated_y_distance +=
            y_distance;
    }

    if (
        neighbours >=
        MIN_CATEGORY_NEIGHBOURS_FOR_STRONG_ALIGNMENT
    ) {

        const double average_distance =
            accumulated_y_distance /
            static_cast<double>(
                neighbours
            );

        return std::clamp(
            1.0 -
            (
                average_distance /
                10.0
            ),
            0.0,
            1.0
        );
    }

    if (
        neighbours == 1
    ) {

        return 0.55;
    }

    return 0.35;
}

// =============================================================================
// CATEGORY CANDIDATE SCORE
// =============================================================================

[[nodiscard]]
double category_candidate_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept {

    if (
        !acceptable_category_geometry(
            candidate,
            image_width,
            image_height
        )
    ) {

        return 0.0;
    }

    const double center_score =
        category_center_score(
            candidate,
            image_height
        );

    const double height_score =
        category_height_score(
            candidate,
            image_height
        );

    const double width_score =
        category_width_score(
            candidate,
            image_width
        );

    const double density_score =
        category_density_score(
            candidate
        );

    const double alignment_score =
        category_alignment_score(
            candidate,
            candidates,
            image_width,
            image_height
        );

    return
        center_score *
            CATEGORY_CENTER_SCORE_WEIGHT +
        height_score *
            CATEGORY_HEIGHT_SCORE_WEIGHT +
        width_score *
            CATEGORY_WIDTH_SCORE_WEIGHT +
        density_score *
            CATEGORY_DENSITY_SCORE_WEIGHT +
        alignment_score *
            CATEGORY_ALIGNMENT_SCORE_WEIGHT;
}

// =============================================================================
// CATEGORY SEQUENCE STATISTICS
// =============================================================================

struct CategorySequenceStats {

    double median_center_y = 0.0;

    double median_height = 0.0;

    double median_width = 0.0;

    double median_center_spacing = 0.0;

    std::size_t candidate_count = 0;

    std::size_t coherent_count = 0;
};

// =============================================================================
// MEDIAN
// =============================================================================

[[nodiscard]]
double median_value(
    std::vector<double> values
) noexcept {

    if (
        values.empty()
    ) {

        return 0.0;
    }

    const std::size_t middle =
        values.size() / 2u;

    std::nth_element(
        values.begin(),
        values.begin() +
            middle,
        values.end()
    );

    const double upper =
        values[middle];

    if (
        values.size() % 2u != 0u
    ) {

        return upper;
    }

    std::nth_element(
        values.begin(),
        values.begin() +
            middle -
            1u,
        values.end()
    );

    const double lower =
        values[
            middle -
            1u
        ];

    return
        (lower + upper) *
        0.5;
}

// =============================================================================
// BUILD CATEGORY SEQUENCE MODEL
// =============================================================================

[[nodiscard]]
CategorySequenceStats
build_category_sequence_stats(
    const std::vector<TextCandidate>& category_candidates
) noexcept {

    CategorySequenceStats stats{};

    if (
        category_candidates.empty()
    ) {

        return stats;
    }

    stats.candidate_count =
        category_candidates.size();

    std::vector<double> center_y;
    std::vector<double> heights;
    std::vector<double> widths;

    center_y.reserve(
        category_candidates.size()
    );

    heights.reserve(
        category_candidates.size()
    );

    widths.reserve(
        category_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        const double center =
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5;

        const double candidate_height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double candidate_width =
            static_cast<double>(
                candidate.max_x -
                candidate.min_x +
                1
            );

        center_y.push_back(
            center
        );

        heights.push_back(
            candidate_height
        );

        widths.push_back(
            candidate_width
        );
    }

    stats.median_center_y =
        median_value(
            std::move(
                center_y
            )
        );

    stats.median_height =
        median_value(
            std::move(
                heights
            )
        );

    stats.median_width =
        median_value(
            std::move(
                widths
            )
        );

    // =========================================================================
    // HORIZONTAL CENTER SPACING
    // =========================================================================

    std::vector<double> center_x;

    center_x.reserve(
        category_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        center_x.push_back(
            (
                static_cast<double>(
                    candidate.min_x
                ) +
                static_cast<double>(
                    candidate.max_x
                )
            ) *
            0.5
        );
    }

    std::sort(
        center_x.begin(),
        center_x.end()
    );

    std::vector<double> spacings;

    if (
        center_x.size() >= 2u
    ) {

        spacings.reserve(
            center_x.size() -
            1u
        );

        for (
            std::size_t i = 1;
            i < center_x.size();
            ++i
        ) {

            const double spacing =
                center_x[i] -
                center_x[i - 1u];

            if (
                spacing > 0.0
            ) {

                spacings.push_back(
                    spacing
                );
            }
        }
    }

    stats.median_center_spacing =
        median_value(
            std::move(
                spacings
            )
        );

    // =========================================================================
    // COHERENT CANDIDATES
    // =========================================================================

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        const double center =
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5;

        const double height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double baseline_distance =
            std::abs(
                center -
                stats.median_center_y
            );

        const double height_error =
            stats.median_height > 0.0
                ? std::abs(
                      height -
                      stats.median_height
                  ) /
                  stats.median_height
                : 1.0;

        if (
            baseline_distance <=
                CATEGORY_SEQUENCE_BASELINE_TOLERANCE &&
            height_error <=
                CATEGORY_SEQUENCE_HEIGHT_TOLERANCE
        ) {

            ++stats.coherent_count;
        }
    }

    return stats;
}

// =============================================================================
// CATEGORY SEQUENCE BASELINE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_baseline_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
) noexcept {

    if (
        stats.median_center_y <= 0.0
    ) {

        return 0.0;
    }

    const double center =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    const double distance =
        std::abs(
            center -
            stats.median_center_y
        );

    return std::clamp(
        1.0 -
        (
            distance /
            CATEGORY_SEQUENCE_BASELINE_TOLERANCE
        ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY SEQUENCE HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_height_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
) noexcept {

    if (
        stats.median_height <= 0.0
    ) {

        return 0.0;
    }

    const double height =
        static_cast<double>(
            candidate.max_y -
            candidate.min_y +
            1
        );

    const double relative_error =
        std::abs(
            height -
            stats.median_height
        ) /
        stats.median_height;

    return std::clamp(
        1.0 -
        (
            relative_error /
            CATEGORY_SEQUENCE_HEIGHT_TOLERANCE
        ),
        0.0,
        1.0
    );
}

// =============================================================================
// CATEGORY SEQUENCE SPACING SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept {

    if (
        stats.median_center_spacing <= 0.0
    ) {

        return 0.50;
    }

    const double candidate_center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    const double candidate_center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    double best_error =
        std::numeric_limits<double>::max();

    std::size_t neighbours =
        0;

    for (
        const TextCandidate& other :
        category_candidates
    ) {

        if (
            &other ==
            &candidate
        ) {

            continue;
        }

        const double other_center_x =
            (
                static_cast<double>(
                    other.min_x
                ) +
                static_cast<double>(
                    other.max_x
                )
            ) *
            0.5;

        const double other_center_y =
            (
                static_cast<double>(
                    other.min_y
                ) +
                static_cast<double>(
                    other.max_y
                )
            ) *
            0.5;

        const double y_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            y_distance >
            CATEGORY_SEQUENCE_BASELINE_TOLERANCE
        ) {

            continue;
        }

        const double horizontal_distance =
            std::abs(
                candidate_center_x -
                other_center_x
            );

        if (
            horizontal_distance <= 0.0 ||
            horizontal_distance >
                CATEGORY_SEQUENCE_NEIGHBOUR_RADIUS
        ) {

            continue;
        }

        ++neighbours;

        const double spacing_error =
            std::abs(
                horizontal_distance -
                stats.median_center_spacing
            ) /
            stats.median_center_spacing;

        best_error =
            std::min(
                best_error,
                spacing_error
            );
    }

    if (
        neighbours == 0u ||
        !std::isfinite(
            best_error
        )
    ) {

        return 0.40;
    }

    return std::clamp(
        1.0 -
        (
            best_error /
            CATEGORY_SEQUENCE_SPACING_TOLERANCE
        ),
        0.0,
        1.0
    );
}

// =============================================================================
// FINAL CATEGORY SEQUENCE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept {

    const double baseline_score =
        category_sequence_baseline_score(
            candidate,
            stats
        );

    const double height_score =
        category_sequence_height_score(
            candidate,
            stats
        );

    const double spacing_score =
        category_sequence_spacing_score(
            candidate,
            category_candidates,
            stats
        );

    return
        baseline_score *
            CATEGORY_SEQUENCE_BASELINE_WEIGHT +
        height_score *
            CATEGORY_SEQUENCE_HEIGHT_WEIGHT +
        spacing_score *
            CATEGORY_SEQUENCE_SPACING_WEIGHT;
}

// =============================================================================
// ASCII TEXT WORD-LIKE VALIDATION
// =============================================================================

[[nodiscard]]
bool reject_geometry_like_text(
    const std::string& text
) noexcept {

    if (
        text.empty()
    ) {

        return true;
    }

    const std::size_t glyph_count =
        count_glyphs(
            text
        );

    if (
        glyph_count == 0
    ) {

        return true;
    }

    std::size_t alpha =
        0;

    std::size_t digits =
        0;

    std::size_t punctuation =
        0;

    std::size_t other =
        0;

    std::size_t spaces =
        0;

    std::size_t longest_alpha_run =
        0;

    std::size_t current_alpha_run =
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

            ++spaces;

            current_alpha_run =
                0;

            continue;
        }

        if (
            is_ascii_alpha(c)
        ) {

            ++alpha;
            ++current_alpha_run;

            longest_alpha_run =
                std::max(
                    longest_alpha_run,
                    current_alpha_run
                );

        } else {

            current_alpha_run =
                0;
        }

        if (
            is_ascii_digit(c)
        ) {

            ++digits;

        } else if (
            is_ascii_punctuation(c)
        ) {

            ++punctuation;

        } else if (
            !is_ascii_alpha(c)
        ) {

            ++other;
        }
    }

    // =========================================================================
    // MOST COMMON CHARACTER
    // =========================================================================

    std::size_t most_common_count =
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

        std::size_t frequency =
            0;

        for (
            const char candidate :
            text
        ) {

            if (
                candidate ==
                c
            ) {

                ++frequency;
            }
        }

        most_common_count =
            std::max(
                most_common_count,
                frequency
            );
    }

    if (
        glyph_count >= 4 &&
        most_common_count * 100u >=
            55u *
            glyph_count
    ) {

        return true;
    }

    // =========================================================================
    // NUMERIC-ONLY GEOMETRY
    // =========================================================================

    if (
        alpha == 0 &&
        digits > 0
    ) {

        if (
            glyph_count > 8
        ) {

            return true;
        }

        if (
            digits * 100u >=
                75u *
                glyph_count &&
            glyph_count >= 4
        ) {

            return true;
        }
    }

    // =========================================================================
    // PUNCTUATION
    // =========================================================================

    if (
        punctuation >=
        alpha +
        digits
    ) {

        return true;
    }

    // =========================================================================
    // SYMBOL CONTAMINATION
    // =========================================================================

    if (
        other >
        alpha +
        digits
    ) {

        return true;
    }

    // =========================================================================
    // FRAGMENTED ALPHABETIC GARBAGE
    // =========================================================================

    if (
        alpha > 0 &&
        digits == 0 &&
        longest_alpha_run < 2 &&
        glyph_count >= 4
    ) {

        return true;
    }

    // =========================================================================
    // SHORT-TOKEN FRAGMENTATION
    // =========================================================================

    if (
        spaces > 0 &&
        glyph_count <= 12
    ) {

        std::size_t token_count =
            0;

        std::size_t short_token_count =
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

                    if (
                        token_length <= 2
                    ) {

                        ++short_token_count;
                    }

                    token_length =
                        0;
                }

            } else {

                ++token_length;
            }
        }

        if (
            token_count >= 3 &&
            short_token_count ==
                token_count
        ) {

            return true;
        }
    }

    return false;
}

// =============================================================================
// BUILD TEXT CANDIDATES FROM ENTIRE IMAGE REGION
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate>
build_band_candidates(
    const std::uint8_t* chart_buffer,
    int width,
    int y0,
    int y1,
    std::uint8_t minimum_foreground,
    int minimum_component_width
) {

    std::vector<TextCandidate> candidates;

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        y0 < 0 ||
        y1 <= y0 ||
        minimum_component_width <= 0
    ) {

        return candidates;
    }

    const int region_height =
        y1 -
        y0;

    if (
        region_height <= 1
    ) {

        return candidates;
    }

    // =========================================================================
    // LOCAL BINARY MASK
    // =========================================================================

    const std::size_t mask_size =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            region_height
        );

    std::vector<std::uint8_t> mask(
        mask_size,
        std::uint8_t{0}
    );

    for (
        int local_y = 0;
        local_y < region_height;
        ++local_y
    ) {

        const int image_y =
            y0 +
            local_y;

        const std::size_t row_offset =
            static_cast<std::size_t>(
                local_y
            ) *
            static_cast<std::size_t>(
                width
            );

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            if (
                chart_foreground(
                    chart_buffer,
                    width,
                    x,
                    image_y
                ) >=
                minimum_foreground
            ) {

                mask[
                    row_offset +
                    static_cast<std::size_t>(
                        x
                    )
                ] =
                    1;
            }
        }
    }

    // =========================================================================
    // RAW COMPONENT
    // =========================================================================

    struct RawComponent {

        int min_x = 0;
        int min_y = 0;

        int max_x = 0;
        int max_y = 0;

        std::size_t pixels = 0;

        [[nodiscard]]
        int width() const noexcept {

            return
                max_x -
                min_x +
                1;
        }

        [[nodiscard]]
        int height() const noexcept {

            return
                max_y -
                min_y +
                1;
        }
    };

    // =========================================================================
    // VISITED
    // =========================================================================

    std::vector<std::uint8_t> visited(
        mask_size,
        std::uint8_t{0}
    );

    const auto local_index =
        [width](
            int x,
            int y
        ) noexcept -> std::size_t {

        return
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                width
            ) +
            static_cast<std::size_t>(
                x
            );
    };

    // =========================================================================
    // BFS
    // =========================================================================

    std::vector<int> queue;

    queue.reserve(
        256
    );

    std::vector<RawComponent> components;

    components.reserve(
        std::min(
            static_cast<std::size_t>(
                width * 4
            ),
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS * 4
            )
        )
    );

    // =========================================================================
    // 8-CONNECTED COMPONENT EXTRACTION
    // =========================================================================

    for (
        int local_y = 0;
        local_y < region_height;
        ++local_y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t start =
                local_index(
                    x,
                    local_y
                );

            if (
                visited[start] != 0 ||
                mask[start] == 0
            ) {

                continue;
            }

            visited[start] =
                1;

            RawComponent component{
                x,
                y0 + local_y,
                x,
                y0 + local_y,
                0
            };

            queue.clear();

            queue.push_back(
                local_y *
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

                const int image_y =
                    y0 +
                    cy;

                ++component.pixels;

                component.min_x =
                    std::min(
                        component.min_x,
                        cx
                    );

                component.max_x =
                    std::max(
                        component.max_x,
                        cx
                    );

                component.min_y =
                    std::min(
                        component.min_y,
                        image_y
                    );

                component.max_y =
                    std::max(
                        component.max_y,
                        image_y
                    );

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

                        if (
                            dx == 0 &&
                            dy == 0
                        ) {

                            continue;
                        }

                        const int nx =
                            cx +
                            dx;

                        const int ny =
                            cy +
                            dy;

                        if (
                            nx < 0 ||
                            nx >= width ||
                            ny < 0 ||
                            ny >= region_height
                        ) {

                            continue;
                        }

                        const std::size_t neighbour =
                            local_index(
                                nx,
                                ny
                            );

                        if (
                            visited[neighbour] != 0 ||
                            mask[neighbour] == 0
                        ) {

                            continue;
                        }

                        visited[neighbour] =
                            1;

                        queue.push_back(
                            ny *
                            width +
                            nx
                        );
                    }
                }
            }

            // =========================================================================
            // RAW COMPONENT FILTER
            // =========================================================================

            const int component_width =
                component.width();

            const int component_height =
                component.height();

            if (
                component.pixels <
                MIN_COMPONENT_PIXELS
            ) {

                continue;
            }

            if (
                component_height >
                MAX_GLYPH_HEIGHT
            ) {

                continue;
            }

            if (
                component_width >
                MAX_GLYPH_WIDTH
            ) {

                continue;
            }

            // Horizontal line / tick / bar edge.
            if (
                component_width >= 8 &&
                component_height <= 3
            ) {

                continue;
            }

            // Vertical line / axis edge.
            if (
                component_height >= 8 &&
                component_width <= 2
            ) {

                continue;
            }

            // Large connected chart geometry.
            if (
                component_width >= 32 &&
                component_height >= 20
            ) {

                continue;
            }

            const std::size_t component_area =
                static_cast<std::size_t>(
                    component_width
                ) *
                static_cast<std::size_t>(
                    component_height
                );

            if (
                component_area == 0
            ) {

                continue;
            }

            const double density =
                static_cast<double>(
                    component.pixels
                ) /
                static_cast<double>(
                    component_area
                );

            if (
                !std::isfinite(
                    density
                )
            ) {

                continue;
            }

            if (
                density <
                MIN_COMPONENT_DENSITY
            ) {

                continue;
            }

            if (
                density >
                MAX_COMPONENT_DENSITY &&
                component_width > 24 &&
                component_height > 16
            ) {

                continue;
            }

            components.push_back(
                component
            );

            if (
                components.size() >=
                static_cast<std::size_t>(
                    MAX_CANDIDATE_COMPONENTS
                )
            ) {

                break;
            }
        }

        if (
            components.size() >=
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        ) {

            break;
        }
    }

    if (
        components.empty()
    ) {

        return candidates;
    }

    // =========================================================================
    // SORT COMPONENTS
    // =========================================================================

    std::sort(
        components.begin(),
        components.end(),
        [](
            const RawComponent& a,
            const RawComponent& b
        ) noexcept {

            if (
                a.min_y !=
                b.min_y
            ) {

                return
                    a.min_y <
                    b.min_y;
            }

            return
                a.min_x <
                b.min_x;
        }
    );

    // =========================================================================
    // COMPONENT GROUP
    // =========================================================================

    struct ComponentGroup {

        int min_x = 0;
        int min_y = 0;

        int max_x = 0;
        int max_y = 0;

        std::size_t pixels = 0;
        std::size_t count = 0;

        int reference_height = 0;
        int reference_baseline = 0;

        [[nodiscard]]
        int width() const noexcept {

            return
                max_x -
                min_x +
                1;
        }

        [[nodiscard]]
        int height() const noexcept {

            return
                max_y -
                min_y +
                1;
        }
    };

    std::vector<ComponentGroup> groups;

    groups.reserve(
        components.size()
    );

    // =========================================================================
    // GROUP COMPONENTS
    // =========================================================================

    constexpr std::size_t GROUP_SEARCH_LIMIT =
        32;

    for (
        const RawComponent& component :
        components
    ) {

        const int component_width =
            component.width();

        const int component_height =
            component.height();

        if (
            component_height <
            MIN_GLYPH_HEIGHT ||
            component_height >
            MAX_GLYPH_HEIGHT
        ) {

            continue;
        }

        if (
            component_width >
            MAX_GLYPH_WIDTH
        ) {

            continue;
        }

        bool attached =
            false;

        const std::size_t search_begin =
            groups.size() >
                GROUP_SEARCH_LIMIT
                ? groups.size() -
                  GROUP_SEARCH_LIMIT
                : 0;

        for (
            std::size_t i =
                groups.size();
            i-- > search_begin;
        ) {

            ComponentGroup& group =
                groups[i];

            const int reference_height =
                std::max(
                    1,
                    group.reference_height
                );

            const double height_ratio =
                component_height >
                    reference_height
                    ? static_cast<double>(
                          component_height
                      ) /
                      static_cast<double>(
                          reference_height
                      )
                    : static_cast<double>(
                          reference_height
                      ) /
                      static_cast<double>(
                          component_height
                      );

            if (
                height_ratio >
                MAX_GLYPH_HEIGHT_RATIO
            ) {

                continue;
            }

            const int baseline_delta =
                std::abs(
                    component.max_y -
                    group.reference_baseline
                );

            if (
                baseline_delta >
                MAX_BASELINE_DELTA
            ) {

                continue;
            }

            const int component_center_y =
                component.min_y +
                component_height /
                    2;

            const int group_center_y =
                group.min_y +
                group.height() /
                    2;

            if (
                std::abs(
                    component_center_y -
                    group_center_y
                ) >
                MAX_CENTER_Y_DELTA
            ) {

                continue;
            }

            const int horizontal_gap =
                component.min_x >
                    group.max_x
                    ? component.min_x -
                      group.max_x -
                      1
                    : group.min_x >
                            component.max_x
                        ? group.min_x -
                          component.max_x -
                          1
                        : 0;

            if (
                horizontal_gap >
                MAX_GLYPH_HORIZONTAL_GAP
            ) {

                continue;
            }

            const int merged_width =
                std::max(
                    group.max_x,
                    component.max_x
                ) -
                std::min(
                    group.min_x,
                    component.min_x
                ) +
                1;

            if (
                merged_width >
                MAX_CANDIDATE_WIDTH
            ) {

                continue;
            }

            group.min_x =
                std::min(
                    group.min_x,
                    component.min_x
                );

            group.min_y =
                std::min(
                    group.min_y,
                    component.min_y
                );

            group.max_x =
                std::max(
                    group.max_x,
                    component.max_x
                );

            group.max_y =
                std::max(
                    group.max_y,
                    component.max_y
                );

            group.pixels +=
                component.pixels;

            ++group.count;

            group.reference_height =
                static_cast<int>(
                    (
                        static_cast<std::size_t>(
                            group.reference_height
                        ) *
                        (
                            group.count -
                            1
                        )
                    +
                    static_cast<std::size_t>(
                        component_height
                    )
                    ) /
                    group.count
                );

            group.reference_baseline =
                static_cast<int>(
                    (
                        static_cast<std::size_t>(
                            group.reference_baseline
                        ) *
                        (
                            group.count -
                            1
                        )
                    +
                    static_cast<std::size_t>(
                        component.max_y
                    )
                    ) /
                    group.count
                );

            attached =
                true;

            break;
        }

        if (
            !attached
        ) {

            groups.push_back({
                component.min_x,
                component.min_y,
                component.max_x,
                component.max_y,
                component.pixels,
                1,
                component_height,
                component.max_y
            });
        }
    }

    // =========================================================================
    // GROUP -> CANDIDATE
    // =========================================================================

    for (
        const ComponentGroup& group :
        groups
    ) {

        if (
            candidates.size() >=
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        ) {

            break;
        }

        const int group_width =
            group.width();

        const int group_height =
            group.height();

        if (
            group.count == 0 ||
            group_width <
                minimum_component_width ||
            group_height <
                MIN_LABEL_HEIGHT
        ) {

            continue;
        }

        if (
            group_width >
            MAX_CANDIDATE_WIDTH ||
            group_height >
            MAX_CANDIDATE_HEIGHT
        ) {

            continue;
        }

        const std::size_t group_area =
            static_cast<std::size_t>(
                group_width
            ) *
            static_cast<std::size_t>(
                group_height
            );

        if (
            group_area == 0
        ) {

            continue;
        }

        const double density =
            static_cast<double>(
                group.pixels
            ) /
            static_cast<double>(
                group_area
            );

        if (
            !std::isfinite(
                density
            ) ||
            density <
                MIN_LABEL_DENSITY
        ) {

            continue;
        }

        const double width_ratio =
            static_cast<double>(
                group_width
            ) /
            static_cast<double>(
                width
            );

        if (
            width_ratio >
            MAX_CANDIDATE_WIDTH_RATIO
        ) {

            continue;
        }

        const double height_ratio =
            static_cast<double>(
                group_height
            ) /
            static_cast<double>(
                region_height
            );

        if (
            height_ratio >
            MAX_CANDIDATE_HEIGHT_RATIO
        ) {

            continue;
        }

        if (
            group_width >= 48 &&
            group_height <= 4
        ) {

            continue;
        }

        if (
            group_width >= 96 &&
            group_height <= 8
        ) {

            continue;
        }

        if (
            width_ratio >=
                MAX_WIDE_DENSE_RATIO &&
            density >=
                MAX_WIDE_DENSE_DENSITY
        ) {

            continue;
        }

        candidates.push_back({
            group.min_x,
            group.min_y,
            group.max_x,
            group.max_y,
            group.pixels
        });
    }

    // =========================================================================
    // FINAL CANDIDATE ORDER
    // =========================================================================

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            if (
                a.min_y !=
                b.min_y
            ) {

                return
                    a.min_y <
                    b.min_y;
            }

            if (
                a.min_x !=
                b.min_x
            ) {

                return
                    a.min_x <
                    b.min_x;
            }

            const int a_width =
                a.max_x -
                a.min_x +
                1;

            const int b_width =
                b.max_x -
                b.min_x +
                1;

            return
                a_width <
                b_width;
        }
    );

    return candidates;
}

// =============================================================================
// CANDIDATE GEOMETRY FILTER
// =============================================================================

[[nodiscard]]
bool acceptable_candidate_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    int max_band_height
) noexcept {

    if (
        !valid_candidate(candidate) ||
        image_width <= 0 ||
        image_height <= 0
    ) {

        return false;
    }

    const int width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        width <
        MIN_LABEL_WIDTH ||
        height <
        MIN_LABEL_HEIGHT
    ) {

        return false;
    }

    if (
        height >
        max_band_height ||
        height >
        MAX_CANDIDATE_HEIGHT
    ) {

        return false;
    }

    const double width_ratio =
        static_cast<double>(
            width
        ) /
        static_cast<double>(
            image_width
        );

    if (
        width_ratio >
        MAX_CANDIDATE_WIDTH_RATIO
    ) {

        return false;
    }

    const double height_ratio =
        static_cast<double>(
            height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio >
        MAX_CANDIDATE_HEIGHT_RATIO
    ) {

        return false;
    }

    const std::size_t area =
        static_cast<std::size_t>(
            width
        ) *
        static_cast<std::size_t>(
            height
        );

    if (
        area == 0
    ) {

        return false;
    }

    const double density =
        static_cast<double>(
            candidate.active_pixels
        ) /
        static_cast<double>(
            area
        );

    if (
        !std::isfinite(
            density
        ) ||
        density <
            MIN_LABEL_DENSITY
    ) {

        return false;
    }

    if (
        width_ratio >=
            MAX_WIDE_DENSE_RATIO &&
        density >=
            MAX_WIDE_DENSE_DENSITY
    ) {

        return false;
    }

    return true;
}

// =============================================================================
// BUILD PADDED GRAYSCALE OCR BUFFER
// =============================================================================
//
// Detection is performed using the isolated binary threshold.
//
// OCR receives:
//
//     channel-1 grayscale
//     + horizontal padding
//     + vertical padding
//
// The original grayscale values are intentionally preserved.
//
// This lets LineRecognizer perform its own normalization / thresholding and
// prevents anti-aliased glyph edges from being destroyed before OCR.
//
// =============================================================================

[[nodiscard]]
OcrBuffer
build_candidate_buffer(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate
) {

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        !valid_candidate(candidate)
    ) {

        return {};
    }

    constexpr int OCR_PADDING_X =
        4;

    constexpr int OCR_PADDING_Y =
        3;

    const int crop_min_x =
        std::max(
            0,
            candidate.min_x -
                OCR_PADDING_X
        );

    const int crop_max_x =
        std::min(
            image_width - 1,
            candidate.max_x +
                OCR_PADDING_X
        );

    const int crop_min_y =
        std::max(
            0,
            candidate.min_y -
                OCR_PADDING_Y
        );

    const int crop_max_y =
        std::min(
            image_height - 1,
            candidate.max_y +
                OCR_PADDING_Y
        );

    if (
        crop_min_x > crop_max_x ||
        crop_min_y > crop_max_y
    ) {

        return {};
    }

    const int crop_width =
        crop_max_x -
        crop_min_x +
        1;

    const int crop_height =
        crop_max_y -
        crop_min_y +
        1;

    if (
        crop_width <= 0 ||
        crop_height <= 0
    ) {

        return {};
    }

    const std::size_t crop_pixels =
        static_cast<std::size_t>(
            crop_width
        ) *
        static_cast<std::size_t>(
            crop_height
        );

    if (
        crop_pixels == 0
    ) {

        return {};
    }

    std::vector<std::uint8_t> buffer(
        crop_pixels,
        std::uint8_t{0}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(
                y
            ) *
            static_cast<std::size_t>(
                image_width
            ) *
            3u;

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y -
                crop_min_y
            ) *
            static_cast<std::size_t>(
                crop_width
            );

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            ++x
        ) {

            const std::size_t source_index =
                source_row +
                static_cast<std::size_t>(
                    x
                ) *
                3u +
                1u;

            const std::size_t destination_index =
                destination_row +
                static_cast<std::size_t>(
                    x -
                    crop_min_x
                );

            buffer[
                destination_index
            ] =
                chart_buffer[
                    source_index
                ];
        }
    }

    return OcrBuffer{
        std::move(
            buffer
        ),
        crop_width,
        crop_height
    };
}

// =============================================================================
// CATEGORY LABEL CANONICALIZATION
// =============================================================================
//
// Financial x-axis labels frequently contain short month names.
//
// OCR errors on three-character month abbreviations are predictable:
//
//     Apt -> Apr
//     Seo -> Sep
//     Now -> Nov
//     Bec -> Dec
//
// These corrections are deliberately constrained to the known month vocabulary.
// No general-purpose fuzzy correction is performed.
//
// =============================================================================

[[nodiscard]]
const char* canonical_month_label(
    const std::string& text
) noexcept {

    if (
        text == "Jan"
    ) {

        return "Jan";
    }

    if (
        text == "Feb"
    ) {

        return "Feb";
    }

    if (
        text == "Mar"
    ) {

        return "Mar";
    }

    if (
        text == "Apt"
    ) {

        return "Apr";
    }

    if (
        text == "Apr"
    ) {

        return "Apr";
    }

    if (
        text == "May"
    ) {

        return "May";
    }

    if (
        text == "Jun"
    ) {

        return "Jun";
    }

    if (
        text == "Jul"
    ) {

        return "Jul";
    }

    if (
        text == "Seo"
    ) {

        return "Sep";
    }

    if (
        text == "Sep"
    ) {

        return "Sep";
    }

    if (
        text == "Oct"
    ) {

        return "Oct";
    }

    if (
        text == "Now"
    ) {

        return "Nov";
    }

    if (
        text == "Nov"
    ) {

        return "Nov";
    }

    if (
        text == "Bec"
    ) {

        return "Dec";
    }

    if (
        text == "Dec"
    ) {

        return "Dec";
    }

    return nullptr;
}

// =============================================================================
// CANONICALIZE CATEGORY LABEL
// =============================================================================
//
// Returns true when the OCR text belongs to the constrained month vocabulary
// and was canonicalized.
//
// =============================================================================

[[nodiscard]]
bool canonicalize_category_label(
    std::string& text
) noexcept {

    const char* canonical =
        canonical_month_label(
            text
        );

    if (
        canonical == nullptr
    ) {

        return false;
    }

    const bool changed =
        text != canonical;

    if (
        changed
    ) {

        text =
            canonical;
    }

    return changed;
}

// =============================================================================
// RECOGNIZE CANDIDATE
// =============================================================================

[[nodiscard]]
bool recognize_candidate(
    const LineRecognizer& line_recognizer,
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate,
    std::uint8_t minimum_foreground,
    int max_band_height,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& sequence_stats,
    DetectedLabel& output,
    CandidateRejectReason& reject_reason
) {

    reject_reason =
        CandidateRejectReason::NONE;

    // =========================================================================
    // GEOMETRY
    // =========================================================================

    if (
        !acceptable_candidate_geometry(
            candidate,
            image_width,
            image_height,
            max_band_height
        )
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY;

        return false;
    }

    // =========================================================================
    // SPATIAL ZONE GATE
    // =========================================================================

    const LabelZone zone =
        classify_label_zone(
            candidate,
            image_width,
            image_height
        );

    if (
        zone !=
        LabelZone::CATEGORY_AXIS
    ) {

        reject_reason =
            CandidateRejectReason::ZONE;

        return false;
    }

    const double zone_score =
        category_candidate_score(
            candidate,
            category_candidates,
            image_width,
            image_height
        );

    if (
        zone_score <
        MIN_CATEGORY_ZONE_SCORE
    ) {

        reject_reason =
            CandidateRejectReason::ZONE_SCORE;

        return false;
    }

    // =========================================================================
    // CATEGORY SEQUENCE GATE
    // =========================================================================

    const double sequence_score =
        category_sequence_score(
            candidate,
            category_candidates,
            sequence_stats
        );

    if (
        sequence_stats.coherent_count >=
            MIN_CATEGORY_SEQUENCE_SIZE &&
        sequence_score <
            MIN_CATEGORY_SEQUENCE_SCORE
    ) {

        reject_reason =
            CandidateRejectReason::SEQUENCE;

        return false;
    }

    // =========================================================================
    // DETECTED CANDIDATE DIMENSIONS
    // =========================================================================

    const int candidate_width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int candidate_height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        candidate_width <= 0 ||
        candidate_height <= 1
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY;

        return false;
    }

    // =========================================================================
    // BUILD PADDED OCR BUFFER
    // =========================================================================

    const OcrBuffer ocr_buffer =
        build_candidate_buffer(
            chart_buffer,
            image_width,
            image_height,
            candidate
        );

    if (
        ocr_buffer.data.empty() ||
        ocr_buffer.width <= 0 ||
        ocr_buffer.height <= 1
    ) {

        reject_reason =
            CandidateRejectReason::EMPTY_CROP;

        return false;
    }

    // =========================================================================
    // OCR
    // =========================================================================

    std::string text =
        line_recognizer.recognize(
            ocr_buffer.data.data(),
            ocr_buffer.width,
            0,
            ocr_buffer.height,
            1
        );

    trim_text(
        text
    );

    if (
        text.empty()
    ) {

        reject_reason =
            CandidateRejectReason::OCR_EMPTY;

        return false;
    }

    // =========================================================================
    // CATEGORY LABEL CANONICALIZATION
    // =========================================================================

    const std::string original_ocr_text =
        text;

    const bool canonicalized =
        canonicalize_category_label(
            text
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        if (
            canonicalized
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "category_label_canonicalized: "
                "\"%s\" -> \"%s\"\n",
                original_ocr_text.c_str(),
                text.c_str()
            );
        }
    }

    // =========================================================================
    // BASIC TEXT QUALITY
    // =========================================================================

    const std::size_t glyph_count =
        count_glyphs(
            text
        );

    if (
        glyph_count == 0
    ) {

        reject_reason =
            CandidateRejectReason::TEXT_QUALITY;

        return false;
    }

    if (
        is_repeated_glyph_noise(
            text
        )
    ) {

        reject_reason =
            CandidateRejectReason::REPEATED_NOISE;

        return false;
    }

    if (
        !acceptable_label_text(
            text,
            glyph_count
        )
    ) {

        reject_reason =
            CandidateRejectReason::TEXT_QUALITY;

        return false;
    }

    if (
        reject_geometry_like_text(
            text
        )
    ) {

        reject_reason =
            CandidateRejectReason::GEOMETRY_TEXT;

        return false;
    }

    // =========================================================================
    // CANDIDATE DENSITY
    // =========================================================================

    //
    // Density remains based on the actual detected candidate rather than the
    // padded OCR buffer. Padding must not artificially lower confidence.
    //
    const std::size_t candidate_area =
        static_cast<std::size_t>(
            candidate_width
        ) *
        static_cast<std::size_t>(
            candidate_height
        );

    if (
        candidate_area == 0
    ) {

        reject_reason =
            CandidateRejectReason::DENSITY;

        return false;
    }

    const float density =
        static_cast<float>(
            static_cast<double>(
                candidate.active_pixels
            ) /
            static_cast<double>(
                candidate_area
            )
        );

    if (
        !std::isfinite(
            static_cast<double>(
                density
            )
        ) ||
        density <= 0.0f
    ) {

        reject_reason =
            CandidateRejectReason::DENSITY;

        return false;
    }

    // =========================================================================
    // OCR CONFIDENCE
    // =========================================================================

    const float confidence =
        label_confidence(
            density,
            candidate_width,
            candidate_height,
            glyph_count
        );

    if (
        !std::isfinite(
            static_cast<double>(
                confidence
            )
        ) ||
        confidence <= 0.0f
    ) {

        reject_reason =
            CandidateRejectReason::CONFIDENCE;

        return false;
    }

    // =========================================================================
    // CHARACTER COMPOSITION
    // =========================================================================

    std::size_t alphabetic_count =
        0;

    std::size_t digit_count =
        0;

    std::size_t punctuation_count =
        0;

    std::size_t other_count =
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

        if (
            is_ascii_alpha(c)
        ) {

            ++alphabetic_count;

        } else if (
            is_ascii_digit(c)
        ) {

            ++digit_count;

        } else if (
            is_ascii_punctuation(c)
        ) {

            ++punctuation_count;

        } else {

            ++other_count;
        }
    }

    const std::size_t classified_count =
        alphabetic_count +
        digit_count +
        punctuation_count +
        other_count;

    if (
        classified_count == 0
    ) {

        reject_reason =
            CandidateRejectReason::COMPOSITION;

        return false;
    }

    // =========================================================================
    // SINGLE GLYPH
    // =========================================================================

    if (
        glyph_count == 1
    ) {

        if (
            alphabetic_count != 1 ||
            digit_count != 0 ||
            punctuation_count != 0 ||
            other_count != 0
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        if (
            confidence <
            MIN_SINGLE_ALPHA_CONFIDENCE
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        const double aspect_ratio =
            static_cast<double>(
                candidate_width
            ) /
            static_cast<double>(
                candidate_height
            );

        if (
            aspect_ratio > 3.5
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }

        if (
            static_cast<double>(
                candidate.active_pixels
            ) < 4.0
        ) {

            reject_reason =
                CandidateRejectReason::SINGLE_GLYPH;

            return false;
        }
    }

    // =========================================================================
    // TWO GLYPHS
    // =========================================================================
    //
    // Short two-glyph OCR is the highest-risk case for confident hallucination.
    //
    // The stricter threshold specifically protects against outputs such as:
    //
    //     Ju
    //     2s
    //
    // when the underlying pixels belong to chart geometry or a damaged glyph.
    //
    // =========================================================================

    if (
        glyph_count == 2
    ) {

        const bool alpha_alpha =
            alphabetic_count == 2 &&
            digit_count == 0 &&
            punctuation_count == 0 &&
            other_count == 0;

        const bool alpha_numeric =
            alphabetic_count == 1 &&
            digit_count == 1 &&
            punctuation_count == 0 &&
            other_count == 0;

        if (
            !alpha_alpha &&
            !alpha_numeric
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        if (
            confidence <
            MIN_TWO_GLYPH_CONFIDENCE
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        const double aspect_ratio =
            static_cast<double>(
                candidate_width
            ) /
            static_cast<double>(
                candidate_height
            );

        if (
            aspect_ratio > 6.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        const double pixels_per_glyph =
            static_cast<double>(
                candidate.active_pixels
            ) /
            2.0;

        if (
            pixels_per_glyph < 3.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        // Mixed alpha/numeric two-glyph labels need additional occupancy.
        //
        // This is intentionally stricter because OCR commonly converts small
        // chart fragments into combinations such as "2s".
        if (
            alpha_numeric &&
            pixels_per_glyph < 8.0
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }

        if (
            alpha_numeric &&
            candidate_width < 14
        ) {

            reject_reason =
                CandidateRejectReason::TWO_GLYPH;

            return false;
        }
    }

    // =========================================================================
    // THREE GLYPHS
    // =========================================================================

    if (
        glyph_count == 3
    ) {

        const bool all_alpha =
            alphabetic_count == 3 &&
            digit_count == 0 &&
            punctuation_count == 0;

        const bool mixed_alpha_numeric =
            alphabetic_count > 0 &&
            digit_count > 0;

        if (
            digit_count == 3
        ) {

            reject_reason =
                CandidateRejectReason::THREE_GLYPH;

            return false;
        }

        if (
            punctuation_count >= 2
        ) {

            reject_reason =
                CandidateRejectReason::THREE_GLYPH;

            return false;
        }

        if (
            all_alpha
        ) {

            if (
                candidate_width < 14 ||
                candidate_height < 7
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            const double aspect_ratio =
                static_cast<double>(
                    candidate_width
                ) /
                static_cast<double>(
                    candidate_height
                );

            if (
                aspect_ratio > 7.0
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            const double pixels_per_glyph =
                static_cast<double>(
                    candidate.active_pixels
                ) /
                3.0;

            if (
                pixels_per_glyph < 4.0
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                confidence < 0.70f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                density < 0.08f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }
        }

        if (
            mixed_alpha_numeric
        ) {

            if (
                confidence < 0.60f
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }

            if (
                candidate_width < 12 ||
                candidate_height < 6
            ) {

                reject_reason =
                    CandidateRejectReason::THREE_GLYPH;

                return false;
            }
        }
    }

    // =========================================================================
    // PUNCTUATION
    // =========================================================================

    if (
        punctuation_count > 0 &&
        punctuation_count >=
            alphabetic_count +
            digit_count
    ) {

        reject_reason =
            CandidateRejectReason::PUNCTUATION;

        return false;
    }

    // =========================================================================
    // OTHER CHARACTERS
    // =========================================================================

    if (
        other_count > 0
    ) {

        const std::size_t normal_text_count =
            alphabetic_count +
            digit_count;

        if (
            normal_text_count == 0 ||
            other_count >
                normal_text_count
        ) {

            reject_reason =
                CandidateRejectReason::OTHER_CHARS;

            return false;
        }
    }

    // =========================================================================
    // NUMERIC ONLY
    // =========================================================================

    if (
        alphabetic_count == 0 &&
        digit_count > 0
    ) {

        if (
            glyph_count <= 3
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }

        if (
            candidate_width < 18
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }

        if (
            punctuation_count > 1
        ) {

            reject_reason =
                CandidateRejectReason::NUMERIC_ONLY;

            return false;
        }
    }

    // =========================================================================
    // LONG GEOMETRY
    // =========================================================================

    const double width_ratio =
        image_width > 0
            ? static_cast<double>(
                  candidate_width
              ) /
              static_cast<double>(
                  image_width
              )
            : 1.0;

    if (
        width_ratio >= 0.20 &&
        density >= 0.06 &&
        glyph_count > 8
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    if (
        width_ratio >= 0.35 &&
        glyph_count <= 6
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    const double aspect_ratio =
        candidate_height > 0
            ? static_cast<double>(
                  candidate_width
              ) /
              static_cast<double>(
                  candidate_height
              )
            : 0.0;

    if (
        aspect_ratio > 45.0 &&
        glyph_count <= 6
    ) {

        reject_reason =
            CandidateRejectReason::LONG_GEOMETRY;

        return false;
    }

    // =========================================================================
    // SHORT-TEXT OCCUPANCY
    // =========================================================================

    if (
        glyph_count <= 3
    ) {

        const double pixels_per_glyph =
            static_cast<double>(
                candidate.active_pixels
            ) /
            static_cast<double>(
                glyph_count
            );

        if (
            pixels_per_glyph < 3.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }

        if (
            glyph_count == 3 &&
            pixels_per_glyph < 4.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }

        if (
            aspect_ratio > 8.0
        ) {

            reject_reason =
                CandidateRejectReason::OCCUPANCY;

            return false;
        }
    }

    // =========================================================================
    // FINAL LABEL
    // =========================================================================

    chart::ChartLabel label{};

    label.text =
        std::move(
            text
        );

    label.min_x =
        candidate.min_x;

    label.min_y =
        candidate.min_y;

    label.max_x =
        candidate.max_x;

    label.max_y =
        candidate.max_y;

    label.kind =
        chart::ChartLabelKind::UNKNOWN;

    label.series_index =
        -1;

    label.category_index =
        -1;

    // Combine OCR/text confidence with the structural priors.
    //
    // OCR remains the dominant signal.
    //
    // The sequence and spatial priors cannot fabricate text, but they can
    // reduce the effective confidence of geometrically suspicious candidates.
    label.confidence =
        static_cast<float>(
            std::clamp(
                (
                    static_cast<double>(
                        confidence
                    ) *
                    0.65
                ) +
                (
                    zone_score *
                    0.15
                ) +
                (
                    sequence_score *
                    0.20
                ),
                0.0,
                1.0
            )
        );

    output.label =
        std::move(
            label
        );

    output.density =
        density;

    output.glyph_count =
        glyph_count;

    return true;
}

} // namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ChartLabelRecognizer::ChartLabelRecognizer(
    const LineRecognizer& line_recognizer
) noexcept
    : line_recognizer_(
          line_recognizer
      )
{
}

// =============================================================================
// STRUCTURED CHART LABEL RECOGNITION
// =============================================================================

std::vector<chart::ChartLabel>
ChartLabelRecognizer::recognize_labels(
    const std::uint8_t* chart_buffer,
    int width,
    int height
) const {

    std::vector<chart::ChartLabel> labels;

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        height <= 0
    ) {

        return labels;
    }

    constexpr std::uint8_t MIN_FOREGROUND =
        LABEL_FOREGROUND_THRESHOLD;

    constexpr int MIN_COMPONENT_WIDTH =
        MIN_LABEL_WIDTH;

    constexpr std::size_t MAX_LABELS =
        config::CHART_MAX_LABELS;

    // =========================================================================
    // STEP 1: FULL-IMAGE COMPONENT SEGMENTATION
    // =========================================================================

    std::vector<TextCandidate> candidates =
        build_band_candidates(
            chart_buffer,
            width,
            0,
            height,
            MIN_FOREGROUND,
            MIN_COMPONENT_WIDTH
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "full_image_component_segmentation: "
            "width=%d height=%d candidates=%zu\n",
            width,
            height,
            candidates.size()
        );
    }

    if (
        candidates.empty()
    ) {

        return labels;
    }

    // =========================================================================
    // STEP 2: GLOBAL GEOMETRY FILTER
    // =========================================================================

    std::vector<TextCandidate> filtered_candidates;

    filtered_candidates.reserve(
        std::min(
            candidates.size(),
            static_cast<std::size_t>(
                MAX_CANDIDATE_COMPONENTS
            )
        )
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        if (
            !acceptable_candidate_geometry(
                candidate,
                width,
                height,
                MAX_CANDIDATE_HEIGHT
            )
        ) {

            continue;
        }

        filtered_candidates.push_back(
            candidate
        );
    }

    candidates.swap(
        filtered_candidates
    );

    if (
        candidates.empty()
    ) {

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "no_candidates_after_global_filter\n"
            );
        }

        return labels;
    }

    // =========================================================================
    // STEP 3: SPATIAL ZONE DIAGNOSTICS
    // =========================================================================

    std::size_t category_zone_candidates =
        0;

    std::size_t title_zone_candidates =
        0;

    std::size_t plot_zone_candidates =
        0;

    std::size_t outside_zone_candidates =
        0;

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        const LabelZone zone =
            classify_label_zone(
                candidate,
                width,
                height
            );

        switch (
            zone
        ) {

            case LabelZone::CATEGORY_AXIS:
                ++category_zone_candidates;
                break;

            case LabelZone::TITLE:
                ++title_zone_candidates;
                break;

            case LabelZone::PLOT_INTERIOR:
                ++plot_zone_candidates;
                break;

            case LabelZone::OUTSIDE:
                ++outside_zone_candidates;
                break;

            default:
                break;
        }
    }

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "zone_summary: "
            "category=%zu "
            "title=%zu "
            "plot=%zu "
            "outside=%zu\n",
            category_zone_candidates,
            title_zone_candidates,
            plot_zone_candidates,
            outside_zone_candidates
        );
    }

    if (
        category_zone_candidates == 0
    ) {

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "no_category_zone_candidates\n"
            );
        }

        return labels;
    }

    // =========================================================================
    // STEP 4: CATEGORY CANDIDATE PRE-FILTER
    // =========================================================================

    std::vector<TextCandidate> category_candidates;

    category_candidates.reserve(
        std::min(
            category_zone_candidates,
            MAX_LABELS * 4u
        )
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        if (
            classify_label_zone(
                candidate,
                width,
                height
            ) !=
            LabelZone::CATEGORY_AXIS
        ) {

            continue;
        }

        // ---------------------------------------------------------------------
        // category alignment is intentionally weak before the category pool is
        // assembled. Use the complete candidate set for initial admission.
        // ---------------------------------------------------------------------

        const double initial_score =
            category_candidate_score(
                candidate,
                candidates,
                width,
                height
            );

        if (
            initial_score <
            MIN_CATEGORY_ZONE_SCORE
        ) {

            continue;
        }

        category_candidates.push_back(
            candidate
        );
    }

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "category_candidates=%zu\n",
            category_candidates.size()
        );
    }

    if (
        category_candidates.empty()
    ) {

        return labels;
    }

    // =========================================================================
    // STEP 4B: CATEGORY SEQUENCE MODEL
    // =========================================================================

    const CategorySequenceStats sequence_stats =
        build_category_sequence_stats(
            category_candidates
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "category_sequence_stats: "
            "count=%zu "
            "coherent=%zu "
            "median_y=%.2f "
            "median_height=%.2f "
            "median_width=%.2f "
            "median_spacing=%.2f\n",
            sequence_stats.candidate_count,
            sequence_stats.coherent_count,
            sequence_stats.median_center_y,
            sequence_stats.median_height,
            sequence_stats.median_width,
            sequence_stats.median_center_spacing
        );
    }

    // =========================================================================
    // STEP 4C: RE-SCORE CATEGORY CANDIDATES AGAINST CATEGORY POOL
    // =========================================================================

    std::vector<TextCandidate> sequence_candidates;

    sequence_candidates.reserve(
        category_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        const double zone_score =
            category_candidate_score(
                candidate,
                category_candidates,
                width,
                height
            );

        const double sequence_score =
            category_sequence_score(
                candidate,
                category_candidates,
                sequence_stats
            );

        if (
            sequence_stats.coherent_count >=
                MIN_CATEGORY_SEQUENCE_SIZE &&
            sequence_score <
                MIN_CATEGORY_SEQUENCE_SCORE
        ) {

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "sequence_candidate_rejected: "
                    "x=%d..%d y=%d..%d "
                    "zone=%.4f sequence=%.4f "
                    "reason=sequence\n",
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y,
                    zone_score,
                    sequence_score
                );
            }

            continue;
        }

        sequence_candidates.push_back(
            candidate
        );
    }

    category_candidates.swap(
        sequence_candidates
    );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "category_candidates_after_sequence=%zu\n",
            category_candidates.size()
        );
    }

    if (
        category_candidates.empty()
    ) {

        return labels;
    }

    // =========================================================================
    // REBUILD SEQUENCE MODEL AFTER SEQUENCE GATING
    // =========================================================================

    const CategorySequenceStats final_sequence_stats =
        build_category_sequence_stats(
            category_candidates
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "final_category_sequence_stats: "
            "count=%zu "
            "coherent=%zu "
            "median_y=%.2f "
            "median_height=%.2f "
            "median_width=%.2f "
            "median_spacing=%.2f\n",
            final_sequence_stats.candidate_count,
            final_sequence_stats.coherent_count,
            final_sequence_stats.median_center_y,
            final_sequence_stats.median_height,
            final_sequence_stats.median_width,
            final_sequence_stats.median_center_spacing
        );
    }

    // =========================================================================
    // STEP 5: SORT CATEGORY CANDIDATES
    // =========================================================================

    std::sort(
        category_candidates.begin(),
        category_candidates.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            if (
                a.min_x !=
                b.min_x
            ) {

                return
                    a.min_x <
                    b.min_x;
            }

            if (
                a.min_y !=
                b.min_y
            ) {

                return
                    a.min_y <
                    b.min_y;
            }

            const int a_width =
                a.max_x -
                a.min_x +
                1;

            const int b_width =
                b.max_x -
                b.min_x +
                1;

            return
                a_width <
                b_width;
        }
    );

    // =========================================================================
    // DEBUG: CATEGORY CANDIDATES
    // =========================================================================

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        for (
            std::size_t i = 0;
            i < category_candidates.size() &&
            i < 64;
            ++i
        ) {

            const TextCandidate& candidate =
                category_candidates[i];

            const double zone_score =
                category_candidate_score(
                    candidate,
                    category_candidates,
                    width,
                    height
                );

            const double sequence_score =
                category_sequence_score(
                    candidate,
                    category_candidates,
                    final_sequence_stats
                );

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "category_candidate[%zu]: "
                "x=%d..%d y=%d..%d "
                "w=%d h=%d pixels=%zu "
                "zone=%.4f "
                "sequence=%.4f\n",
                i,
                candidate.min_x,
                candidate.max_x,
                candidate.min_y,
                candidate.max_y,
                candidate.max_x -
                    candidate.min_x +
                    1,
                candidate.max_y -
                    candidate.min_y +
                    1,
                candidate.active_pixels,
                zone_score,
                sequence_score
            );
        }
    }

    // =========================================================================
    // STEP 6: OCR CATEGORY CANDIDATES
    // =========================================================================

    std::vector<DetectedLabel> detected;

    detected.reserve(
        std::min(
            category_candidates.size(),
            MAX_LABELS
        )
    );

    std::size_t rejected_by_ocr =
        0;

    std::size_t rejected_by_duplicate =
        0;

    for (
        const TextCandidate& candidate :
        category_candidates
    ) {

        if (
            detected.size() >=
            MAX_LABELS
        ) {

            break;
        }

        DetectedLabel recognized{};

        CandidateRejectReason reject_reason =
            CandidateRejectReason::NONE;

        if (
            !recognize_candidate(
                line_recognizer_,
                chart_buffer,
                width,
                height,
                candidate,
                MIN_FOREGROUND,
                MAX_CANDIDATE_HEIGHT,
                category_candidates,
                final_sequence_stats,
                recognized,
                reject_reason
            )
        ) {

            ++rejected_by_ocr;

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                const double zone_score =
                    category_candidate_score(
                        candidate,
                        category_candidates,
                        width,
                        height
                    );

                const double sequence_score =
                    category_sequence_score(
                        candidate,
                        category_candidates,
                        final_sequence_stats
                    );

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "category_candidate_rejected: "
                    "x=%d..%d y=%d..%d "
                    "zone=%.4f "
                    "sequence=%.4f "
                    "reason=%s\n",
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y,
                    zone_score,
                    sequence_score,
                    candidate_reject_reason_string(
                        reject_reason
                    )
                );
            }

            continue;
        }

        if (
            duplicate_label(
                recognized,
                detected
            )
        ) {

            ++rejected_by_duplicate;

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "category_candidate_rejected: "
                    "x=%d..%d y=%d..%d "
                    "reason=duplicate\n",
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y
                );
            }

            continue;
        }

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            const double zone_score =
                category_candidate_score(
                    candidate,
                    category_candidates,
                    width,
                    height
                );

            const double sequence_score =
                category_sequence_score(
                    candidate,
                    category_candidates,
                    final_sequence_stats
                );

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "category_OCR_accepted: "
                "x=%d..%d y=%d..%d "
                "text=\"%s\" "
                "confidence=%.4f "
                "density=%.4f "
                "glyphs=%zu "
                "zone=%.4f "
                "sequence=%.4f\n",
                candidate.min_x,
                candidate.max_x,
                candidate.min_y,
                candidate.max_y,
                recognized.label.text.c_str(),
                static_cast<double>(
                    recognized.label.confidence
                ),
                static_cast<double>(
                    recognized.density
                ),
                recognized.glyph_count,
                zone_score,
                sequence_score
            );
        }

        detected.push_back(
            std::move(
                recognized
            )
        );
    }

    // =========================================================================
    // STEP 7: SORT FINAL LABELS
    // =========================================================================

    std::sort(
        detected.begin(),
        detected.end(),
        [](
            const DetectedLabel& a,
            const DetectedLabel& b
        ) noexcept {

            if (
                a.label.min_x !=
                b.label.min_x
            ) {

                return
                    a.label.min_x <
                    b.label.min_x;
            }

            if (
                a.label.min_y !=
                b.label.min_y
            ) {

                return
                    a.label.min_y <
                    b.label.min_y;
            }

            return
                a.label.text <
                b.label.text;
        }
    );

    // =========================================================================
    // STEP 8: PUBLIC OUTPUT
    // =========================================================================

    labels.reserve(
        std::min(
            detected.size(),
            MAX_LABELS
        )
    );

    for (
        std::size_t i = 0;
        i < detected.size() &&
        labels.size() <
            MAX_LABELS;
        ++i
    ) {

        labels.push_back(
            std::move(
                detected[i].label
            )
        );
    }

    // =========================================================================
    // DEBUG SUMMARY
    // =========================================================================

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "recognition_summary: "
            "all_candidates=%zu "
            "category_candidates=%zu "
            "detected=%zu "
            "rejected_by_ocr=%zu "
            "rejected_by_duplicate=%zu\n",
            candidates.size(),
            category_candidates.size(),
            detected.size(),
            rejected_by_ocr,
            rejected_by_duplicate
        );

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "final_labels=%zu\n",
            labels.size()
        );
    }

    return labels;
}

// =============================================================================
// EXISTING TEXT OUTPUT / COMPATIBILITY API
// =============================================================================

std::string ChartLabelRecognizer::recognize(
    const std::uint8_t* chart_buffer,
    int width,
    int height
) const {

    std::string result;

    result.reserve(
        256
    );

    result +=
        "[CHART_TEXT_DATA]\n";

    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        height <= 0
    ) {

        result +=
            "  - Line 1 [Y:0-0, X:0-0]: "
            "[INVALID_CHART_BUFFER] "
            "[confidence=0.000000]\n";

        return result;
    }

    const std::vector<chart::ChartLabel> labels =
        recognize_labels(
            chart_buffer,
            width,
            height
        );

    if (
        labels.empty()
    ) {

        result +=
            "  - Line 1 [Y:0-0, X:0-0]: "
            "[NO_RECOGNIZED_CHART_LABELS] "
            "[confidence=0.000000]\n";

        return result;
    }

    result.reserve(
        64 +
        labels.size() * 96
    );

    for (
        std::size_t i = 0;
        i < labels.size();
        ++i
    ) {

        const chart::ChartLabel& label =
            labels[i];

        result +=
            "  - Line " +
            std::to_string(
                i + 1
            );

        result +=
            " [Y:" +
            std::to_string(
                label.min_y
            );

        result +=
            "-" +
            std::to_string(
                label.max_y + 1
            );

        result +=
            ", X:" +
            std::to_string(
                label.min_x
            );

        result +=
            "-" +
            std::to_string(
                label.max_x
            );

        result +=
            "]: " +
            label.text;

        result +=
            " [confidence=" +
            std::to_string(
                label.confidence
            ) +
            "]\n";
    }

    return result;
}

} // namespace fin_ocr
