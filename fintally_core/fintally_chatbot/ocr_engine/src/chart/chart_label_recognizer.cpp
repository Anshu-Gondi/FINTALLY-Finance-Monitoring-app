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

    LEFT_Y_AXIS,

    RIGHT_Y_AXIS,

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
// DUAL Y-AXIS DETECTION
// =============================================================================
//
// Financial charts may expose two independent numeric axes:
//
//     left  -> currency / revenue
//     right -> percent / margin
//
// These labels live in the horizontal edge regions and must not participate in
// the x-axis category sequence model.
//
// The coordinates are relative to the complete image so the detector remains
// resolution-independent.
//
// =============================================================================

constexpr double LEFT_Y_AXIS_MAX_X_RATIO =
    0.18;

constexpr double RIGHT_Y_AXIS_MIN_X_RATIO =
    0.82;

constexpr double Y_AXIS_MIN_Y_RATIO =
    0.055;

constexpr double Y_AXIS_MAX_Y_RATIO =
    0.82;

constexpr double Y_AXIS_TARGET_LEFT_X_RATIO =
    0.14;

constexpr double Y_AXIS_TARGET_RIGHT_X_RATIO =
    0.86;

constexpr double Y_AXIS_X_SCORE_TOLERANCE =
    0.10;

constexpr double MIN_Y_AXIS_SCORE =
    0.30;

constexpr int MIN_Y_AXIS_LABEL_WIDTH =
    4;

constexpr int MAX_Y_AXIS_LABEL_WIDTH =
    96;

constexpr int MAX_Y_AXIS_LABEL_HEIGHT =
    28;

constexpr double MIN_Y_AXIS_HEIGHT_RATIO =
    0.008;

constexpr double MAX_Y_AXIS_HEIGHT_RATIO =
    0.050;

constexpr double Y_AXIS_SEQUENCE_X_TOLERANCE =
    28.0;

constexpr double Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE =
    0.55;

constexpr double Y_AXIS_SEQUENCE_SPACING_TOLERANCE =
    0.65;

constexpr int Y_AXIS_SEQUENCE_NEIGHBOUR_RADIUS =
    180;

constexpr std::size_t MIN_Y_AXIS_SEQUENCE_SIZE =
    3;

constexpr double MIN_Y_AXIS_SEQUENCE_SCORE =
    0.45;

constexpr double Y_AXIS_SEQUENCE_X_WEIGHT =
    0.35;

constexpr double Y_AXIS_SEQUENCE_HEIGHT_WEIGHT =
    0.20;

constexpr double Y_AXIS_SEQUENCE_SPACING_WEIGHT =
    0.45;

constexpr std::size_t MAX_Y_AXIS_TEXT_LENGTH =
    20;

// =============================================================================
// Y-AXIS OCR RECOVERY
// =============================================================================
//
// Recover decimal-point loss in small percentage ticks from the established
// neighbouring axis sequence. For example:
//
//     16.5%
//     11.0%
//      5.5%  <- OCR may return 55%
//
// The target value is inferred from spatially adjacent ticks rather than
// hardcoded to the sample chart.
//
// =============================================================================

constexpr double RIGHT_Y_DECIMAL_RECOVERY_RELATIVE_TOLERANCE =
    0.20;

constexpr double RIGHT_Y_DECIMAL_RECOVERY_ABSOLUTE_TOLERANCE =
    0.75;

constexpr double RIGHT_Y_MAX_PERCENT_VALUE =
    100.0;

// An isolated left-axis numeric OCR result is not sufficient to publish a
// Y-axis value. This suppresses hallucinations such as an isolated "$645"
// when the surrounding left-axis ticks failed OCR.
constexpr std::size_t MIN_LEFT_Y_NUMERIC_SUPPORT =
    2u;

// Minimum independently recognized anchors required before spatial value
// recovery is allowed.
constexpr std::size_t MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS =
    3u;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO =
    0.35;

constexpr double Y_AXIS_VALUE_RECOVERY_MIN_SPATIAL_SUPPORT =
    0.50;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_RIGHT =
    3.0;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_LEFT =
    750.0;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT =
    100.0;

// Bounded upscale used by the Y-axis OCR retry path.
constexpr int Y_AXIS_OCR_SCALE_FACTOR =
    2;

// =============================================================================
// DUAL Y-AXIS MODEL
// =============================================================================

enum class YAxisKind : std::uint8_t {

    NONE = 0,

    LEFT,

    RIGHT
};

struct YAxisSequenceStats {

    double median_center_x = 0.0;

    double median_height = 0.0;

    double median_center_spacing = 0.0;

    std::size_t candidate_count = 0;

    std::size_t coherent_count = 0;
};

// median_value() is defined later with the category sequence helpers.
[[nodiscard]]
double median_value(
    std::vector<double> values
) noexcept;

// =============================================================================
// CATEGORY GAP RECOVERY
// =============================================================================
//
// Component segmentation can legitimately lose one label when one or more
// glyphs become connected to chart geometry, for example a grid line or a
// plotted stroke.
//
// Do not synthesize text from sequence position alone. Instead, when an
// internal category gap is approximately an integer multiple of the learned
// median spacing, inspect the missing slot locally, suppress only dominant
// horizontal/vertical chart strokes, and recover a real foreground bounding
// box for OCR.
//
// This keeps recovery evidence-driven:
//
//     regular spatial gap
//          +
//     real local foreground
//          +
//     plausible text geometry
//          -> OCR recovery
//
// =============================================================================

constexpr double CATEGORY_GAP_RECOVERY_MIN_RATIO =
    1.45;

constexpr double CATEGORY_GAP_RECOVERY_MAX_RELATIVE_ERROR =
    0.45;

constexpr int CATEGORY_GAP_RECOVERY_MAX_MISSING_SLOTS =
    3;

constexpr double CATEGORY_GAP_RECOVERY_X_RADIUS_FACTOR =
    0.95;

constexpr double CATEGORY_GAP_RECOVERY_Y_RADIUS_FACTOR =
    1.00;

constexpr int CATEGORY_GAP_RECOVERY_MIN_X_RADIUS =
    18;

constexpr int CATEGORY_GAP_RECOVERY_MAX_X_RADIUS =
    32;

constexpr int CATEGORY_GAP_RECOVERY_MIN_Y_RADIUS =
    12;

constexpr int CATEGORY_GAP_RECOVERY_MAX_Y_RADIUS =
    18;

constexpr double CATEGORY_GAP_RECOVERY_DOMINANT_ROW_RATIO =
    0.85;

constexpr double CATEGORY_GAP_RECOVERY_DOMINANT_COLUMN_RATIO =
    0.85;

constexpr std::size_t CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS =
    20;

constexpr double CATEGORY_GAP_RECOVERY_MIN_DENSITY =
    0.04;

constexpr double CATEGORY_GAP_RECOVERY_MAX_DENSITY =
    0.75;

constexpr double CATEGORY_GAP_RECOVERY_MAX_CENTER_OFFSET_FACTOR =
    0.50;

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

    const double center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    const double center_y =
        (
            static_cast<double>(
                candidate.min_y
            ) +
            static_cast<double>(
                candidate.max_y
            )
        ) *
        0.5;

    const double x_ratio =
        center_x /
        static_cast<double>(
            image_width
        );

    const double y_ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    // -------------------------------------------------------------------------
    // LEFT Y AXIS
    // -------------------------------------------------------------------------

    if (
        x_ratio <=
            LEFT_Y_AXIS_MAX_X_RATIO &&
        y_ratio >=
            Y_AXIS_MIN_Y_RATIO &&
        y_ratio <=
            Y_AXIS_MAX_Y_RATIO
    ) {

        return LabelZone::LEFT_Y_AXIS;
    }

    // -------------------------------------------------------------------------
    // RIGHT Y AXIS
    // -------------------------------------------------------------------------

    if (
        x_ratio >=
            RIGHT_Y_AXIS_MIN_X_RATIO &&
        y_ratio >=
            Y_AXIS_MIN_Y_RATIO &&
        y_ratio <=
            Y_AXIS_MAX_Y_RATIO
    ) {

        return LabelZone::RIGHT_Y_AXIS;
    }

    // -------------------------------------------------------------------------
    // CATEGORY AXIS
    // -------------------------------------------------------------------------

    if (
        y_ratio >=
            CATEGORY_ZONE_MIN_Y_RATIO &&
        y_ratio <
            CATEGORY_ZONE_MAX_Y_RATIO
    ) {

        return LabelZone::CATEGORY_AXIS;
    }

    // -------------------------------------------------------------------------
    // TITLE / FOOTNOTE
    // -------------------------------------------------------------------------

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
// Y-AXIS GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_y_axis_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept {

    if (
        axis ==
        YAxisKind::NONE
    ) {

        return false;
    }

    const LabelZone expected_zone =
        axis == YAxisKind::LEFT
            ? LabelZone::LEFT_Y_AXIS
            : LabelZone::RIGHT_Y_AXIS;

    if (
        classify_label_zone(
            candidate,
            image_width,
            image_height
        ) !=
        expected_zone
    ) {

        return false;
    }

    const int candidate_width =
        candidate.max_x -
        candidate.min_x +
        1;

    const int candidate_height =
        candidate.max_y -
        candidate.min_y +
        1;

    if (
        candidate_width <
            MIN_Y_AXIS_LABEL_WIDTH ||
        candidate_width >
            MAX_Y_AXIS_LABEL_WIDTH ||
        candidate_height <
            MIN_LABEL_HEIGHT ||
        candidate_height >
            MAX_Y_AXIS_LABEL_HEIGHT
    ) {

        return false;
    }

    const double height_ratio =
        static_cast<double>(
            candidate_height
        ) /
        static_cast<double>(
            image_height
        );

    if (
        height_ratio <
            MIN_Y_AXIS_HEIGHT_RATIO ||
        height_ratio >
            MAX_Y_AXIS_HEIGHT_RATIO
    ) {

        return false;
    }

    const std::size_t area =
        static_cast<std::size_t>(
            candidate_width
        ) *
        static_cast<std::size_t>(
            candidate_height
        );

    if (
        area == 0u
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
        density < MIN_LABEL_DENSITY ||
        density > 0.95
    ) {

        return false;
    }

    return true;
}

// =============================================================================
// Y-AXIS SPATIAL SCORE
// =============================================================================

[[nodiscard]]
double y_axis_x_score(
    const TextCandidate& candidate,
    int image_width,
    YAxisKind axis
) noexcept {

    if (
        image_width <= 0 ||
        axis == YAxisKind::NONE
    ) {

        return 0.0;
    }

    const double center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    const double x_ratio =
        center_x /
        static_cast<double>(
            image_width
        );

    const double target =
        axis == YAxisKind::LEFT
            ? Y_AXIS_TARGET_LEFT_X_RATIO
            : Y_AXIS_TARGET_RIGHT_X_RATIO;

    const double distance =
        std::abs(
            x_ratio -
            target
        );

    return std::clamp(
        1.0 -
        (
            distance /
            Y_AXIS_X_SCORE_TOLERANCE
        ),
        0.0,
        1.0
    );
}

[[nodiscard]]
double y_axis_height_score(
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
            MIN_Y_AXIS_HEIGHT_RATIO +
            MAX_Y_AXIS_HEIGHT_RATIO
        ) *
        0.5;

    const double range =
        (
            MAX_Y_AXIS_HEIGHT_RATIO -
            MIN_Y_AXIS_HEIGHT_RATIO
        ) *
        0.5;

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

[[nodiscard]]
double y_axis_vertical_position_score(
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
        ) *
        0.5;

    const double ratio =
        center_y /
        static_cast<double>(
            image_height
        );

    if (
        ratio < Y_AXIS_MIN_Y_RATIO ||
        ratio > Y_AXIS_MAX_Y_RATIO
    ) {

        return 0.0;
    }

    // Y-axis ticks naturally occupy most of the chart height. Do not strongly
    // prefer the center because top and bottom ticks are equally valid.
    return 1.0;
}

[[nodiscard]]
double y_axis_candidate_score(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept {

    if (
        !acceptable_y_axis_geometry(
            candidate,
            image_width,
            image_height,
            axis
        )
    ) {

        return 0.0;
    }

    const double x_score =
        y_axis_x_score(
            candidate,
            image_width,
            axis
        );

    const double height_score =
        y_axis_height_score(
            candidate,
            image_height
        );

    const double vertical_score =
        y_axis_vertical_position_score(
            candidate,
            image_height
        );

    const double width =
        static_cast<double>(
            candidate.max_x -
            candidate.min_x +
            1
        );

    const double height =
        static_cast<double>(
            candidate.max_y -
            candidate.min_y +
            1
        );

    const double density =
        width > 0.0 &&
        height > 0.0
            ? static_cast<double>(
                  candidate.active_pixels
              ) /
              (width * height)
            : 0.0;

    const double density_score =
        std::clamp(
            (
                density -
                0.02
            ) /
            0.45,
            0.0,
            1.0
        );

    return
        x_score * 0.55 +
        height_score * 0.15 +
        vertical_score * 0.10 +
        density_score * 0.20;
}

// =============================================================================
// Y-AXIS SEQUENCE MODEL
// =============================================================================

[[nodiscard]]
double y_axis_median_spacing(
    const std::vector<TextCandidate>& candidates
) noexcept {

    if (
        candidates.size() < 2u
    ) {

        return 0.0;
    }

    std::vector<double> centers;
    centers.reserve(
        candidates.size()
    );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        centers.push_back(
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5
        );
    }

    std::sort(
        centers.begin(),
        centers.end()
    );

    std::vector<double> spacings;
    spacings.reserve(
        centers.size() -
        1u
    );

    for (
        std::size_t i = 1u;
        i < centers.size();
        ++i
    ) {

        const double spacing =
            centers[i] -
            centers[i - 1u];

        if (
            spacing > 0.0
        ) {

            spacings.push_back(
                spacing
            );
        }
    }

    if (
        spacings.empty()
    ) {

        return 0.0;
    }

    return median_value(
        std::move(
            spacings
        )
    );
}

// =============================================================================
// Y-AXIS FRAGMENT CONSOLIDATION
// =============================================================================
//
// The component extractor is intentionally strict. On financial charts that
// can split a single Y-axis tick label into multiple components, for example:
//
//     "$5.00k" -> "$5" + ".00k"
//     "16.5%"   -> "16" + ".5%"
//
// Those fragments must be consolidated BEFORE learning the vertical sequence.
// Otherwise the sequence model measures intra-label fragment spacing instead
// of true tick-to-tick spacing and rejects every real Y-axis label.
//
// Consolidation is geometry-only and bounded to a single horizontal tick row.
// It never fabricates text.
//
// =============================================================================

constexpr double Y_AXIS_FRAGMENT_MAX_CENTER_Y_DELTA =
    10.0;

constexpr int Y_AXIS_FRAGMENT_MAX_HORIZONTAL_GAP =
    18;

constexpr double Y_AXIS_FRAGMENT_MIN_VERTICAL_OVERLAP_RATIO =
    0.25;

[[nodiscard]]
std::vector<TextCandidate>
consolidate_y_axis_fragments(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& fragments,
    std::uint8_t minimum_foreground
) {

    std::vector<TextCandidate> consolidated;

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        fragments.empty()
    ) {

        return consolidated;
    }

    std::vector<TextCandidate> ordered =
        fragments;

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            const double a_center_y =
                (
                    static_cast<double>(a.min_y) +
                    static_cast<double>(a.max_y)
                ) *
                0.5;

            const double b_center_y =
                (
                    static_cast<double>(b.min_y) +
                    static_cast<double>(b.max_y)
                ) *
                0.5;

            if (
                a_center_y != b_center_y
            ) {

                return a_center_y < b_center_y;
            }

            if (
                a.min_x != b.min_x
            ) {

                return a.min_x < b.min_x;
            }

            return a.max_x < b.max_x;
        }
    );

    for (
        const TextCandidate& fragment :
        ordered
    ) {

        bool merged = false;

        const double fragment_center_y =
            (
                static_cast<double>(fragment.min_y) +
                static_cast<double>(fragment.max_y)
            ) *
            0.5;

        for (
            TextCandidate& group :
            consolidated
        ) {

            const double group_center_y =
                (
                    static_cast<double>(group.min_y) +
                    static_cast<double>(group.max_y)
                ) *
                0.5;

            if (
                std::abs(
                    fragment_center_y - group_center_y
                ) >
                Y_AXIS_FRAGMENT_MAX_CENTER_Y_DELTA
            ) {

                continue;
            }

            const int overlap_min_y =
                std::max(
                    fragment.min_y,
                    group.min_y
                );

            const int overlap_max_y =
                std::min(
                    fragment.max_y,
                    group.max_y
                );

            const int fragment_height =
                fragment.max_y -
                fragment.min_y +
                1;

            const int group_height =
                group.max_y -
                group.min_y +
                1;

            const int overlap_height =
                overlap_max_y >= overlap_min_y
                    ? overlap_max_y -
                      overlap_min_y +
                      1
                    : 0;

            const double min_height =
                static_cast<double>(
                    std::min(
                        fragment_height,
                        group_height
                    )
                );

            const double overlap_ratio =
                min_height > 0.0
                    ? static_cast<double>(
                          overlap_height
                      ) /
                      min_height
                    : 0.0;

            const int horizontal_gap =
                fragment.min_x > group.max_x
                    ? fragment.min_x -
                      group.max_x -
                      1
                    : group.min_x > fragment.max_x
                        ? group.min_x -
                          fragment.max_x -
                          1
                        : 0;

            if (
                overlap_ratio <
                Y_AXIS_FRAGMENT_MIN_VERTICAL_OVERLAP_RATIO &&
                horizontal_gap >
                Y_AXIS_FRAGMENT_MAX_HORIZONTAL_GAP
            ) {

                continue;
            }

            const int merged_min_x =
                std::min(
                    group.min_x,
                    fragment.min_x
                );

            const int merged_max_x =
                std::max(
                    group.max_x,
                    fragment.max_x
                );

            const int merged_min_y =
                std::min(
                    group.min_y,
                    fragment.min_y
                );

            const int merged_max_y =
                std::max(
                    group.max_y,
                    fragment.max_y
                );

            const int merged_width =
                merged_max_x -
                merged_min_x +
                1;

            const int merged_height =
                merged_max_y -
                merged_min_y +
                1;

            if (
                merged_width > MAX_Y_AXIS_LABEL_WIDTH ||
                merged_height > MAX_Y_AXIS_LABEL_HEIGHT
            ) {

                continue;
            }

            group.min_x = merged_min_x;
            group.min_y = merged_min_y;
            group.max_x = merged_max_x;
            group.max_y = merged_max_y;

            merged = true;

            break;
        }

        if (
            !merged
        ) {

            consolidated.push_back(
                fragment
            );
        }
    }

    // Recalculate foreground occupancy from the source image so merged
    // overlapping fragments do not double-count pixels.
    for (
        TextCandidate& candidate :
        consolidated
    ) {

        std::size_t active_pixels =
            0u;

        for (
            int y = candidate.min_y;
            y <= candidate.max_y;
            ++y
        ) {

            for (
                int x = candidate.min_x;
                x <= candidate.max_x;
                ++x
            ) {

                if (
                    chart_foreground(
                        chart_buffer,
                        image_width,
                        x,
                        y
                    ) >=
                    minimum_foreground
                ) {

                    ++active_pixels;
                }
            }
        }

        candidate.active_pixels =
            active_pixels;
    }

    std::sort(
        consolidated.begin(),
        consolidated.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            if (
                a.min_y != b.min_y
            ) {

                return a.min_y < b.min_y;
            }

            return a.min_x < b.min_x;
        }
    );

    return consolidated;
}

// =============================================================================
// Y-AXIS SPACING STATS
// =============================================================================

[[nodiscard]]
YAxisSequenceStats
build_y_axis_sequence_stats(
    const std::vector<TextCandidate>& candidates
) noexcept {

    YAxisSequenceStats stats{};

    if (
        candidates.empty()
    ) {

        return stats;
    }

    stats.candidate_count =
        candidates.size();

    std::vector<double> center_x;
    std::vector<double> heights;

    center_x.reserve(
        candidates.size()
    );

    heights.reserve(
        candidates.size()
    );

    for (
        const TextCandidate& candidate :
        candidates
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

        heights.push_back(
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            )
        );
    }

    stats.median_center_x =
        median_value(
            std::move(
                center_x
            )
        );

    stats.median_height =
        median_value(
            std::move(
                heights
            )
        );

    stats.median_center_spacing =
        y_axis_median_spacing(
            candidates
        );

    for (
        const TextCandidate& candidate :
        candidates
    ) {

        const double center_x =
            (
                static_cast<double>(
                    candidate.min_x
                ) +
                static_cast<double>(
                    candidate.max_x
                )
            ) *
            0.5;

        const double height =
            static_cast<double>(
                candidate.max_y -
                candidate.min_y +
                1
            );

        const double x_distance =
            std::abs(
                center_x -
                stats.median_center_x
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
            x_distance <=
                Y_AXIS_SEQUENCE_X_TOLERANCE &&
            height_error <=
                Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE
        ) {

            ++stats.coherent_count;
        }
    }

    return stats;
}

// =============================================================================
// Y-AXIS SEQUENCE SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_x_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
) noexcept {

    if (
        stats.median_center_x <= 0.0
    ) {

        return 0.0;
    }

    const double center_x =
        (
            static_cast<double>(
                candidate.min_x
            ) +
            static_cast<double>(
                candidate.max_x
            )
        ) *
        0.5;

    return std::clamp(
        1.0 -
        std::abs(
            center_x -
            stats.median_center_x
        ) /
        Y_AXIS_SEQUENCE_X_TOLERANCE,
        0.0,
        1.0
    );
}

[[nodiscard]]
double y_axis_sequence_height_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
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

    const double error =
        std::abs(
            height -
            stats.median_height
        ) /
        stats.median_height;

    return std::clamp(
        1.0 -
        (
            error /
            Y_AXIS_SEQUENCE_HEIGHT_TOLERANCE
        ),
        0.0,
        1.0
    );
}

[[nodiscard]]
double y_axis_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept {

    if (
        stats.median_center_spacing <= 0.0
    ) {

        return 0.50;
    }

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

    double best_error =
        std::numeric_limits<double>::max();

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

        if (
            std::abs(
                candidate_center_x -
                other_center_x
            ) >
            Y_AXIS_SEQUENCE_X_TOLERANCE
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
            ) *
            0.5;

        const double vertical_distance =
            std::abs(
                candidate_center_y -
                other_center_y
            );

        if (
            vertical_distance <= 0.0 ||
            vertical_distance >
                Y_AXIS_SEQUENCE_NEIGHBOUR_RADIUS
        ) {

            continue;
        }

        const double spacing_error =
            std::abs(
                vertical_distance -
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
            Y_AXIS_SEQUENCE_SPACING_TOLERANCE
        ),
        0.0,
        1.0
    );
}

[[nodiscard]]
double y_axis_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept {

    return
        y_axis_sequence_x_score(
            candidate,
            stats
        ) *
            Y_AXIS_SEQUENCE_X_WEIGHT +
        y_axis_sequence_height_score(
            candidate,
            stats
        ) *
            Y_AXIS_SEQUENCE_HEIGHT_WEIGHT +
        y_axis_sequence_spacing_score(
            candidate,
            candidates,
            stats
        ) *
            Y_AXIS_SEQUENCE_SPACING_WEIGHT;
}

// =============================================================================
// Y-AXIS OCR NORMALIZATION / VALIDATION
// =============================================================================

void compact_axis_text(
    std::string& text
) {

    std::string compact;
    compact.reserve(
        text.size()
    );

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

        compact.push_back(
            c
        );
    }

    text.swap(
        compact
    );
}

[[nodiscard]]
bool normalize_y_axis_text(
    std::string& text,
    YAxisKind axis
) noexcept {

    compact_axis_text(
        text
    );

    if (
        text.empty() ||
        text.size() >
            MAX_Y_AXIS_TEXT_LENGTH
    ) {

        return false;
    }

    // Conservative OCR substitutions inside an otherwise numeric token.
    for (
        std::size_t i = 0;
        i < text.size();
        ++i
    ) {

        char& c =
            text[i];

        if (
            c == 'O' ||
            c == 'o'
        ) {

            const bool numeric_context =
                (
                    i > 0u &&
                    (
                        is_ascii_digit(
                            text[i - 1u]
                        ) ||
                        text[i - 1u] == '.'
                    )
                ) ||
                (
                    i + 1u < text.size() &&
                    (
                        is_ascii_digit(
                            text[i + 1u]
                        ) ||
                        text[i + 1u] == '.'
                    )
                );

            if (
                numeric_context
            ) {

                c =
                    '0';
            }
        }

        if (
            c == 'I' ||
            c == 'l'
        ) {

            const bool numeric_context =
                (
                    i > 0u &&
                    (
                        is_ascii_digit(
                            text[i - 1u]
                        ) ||
                        text[i - 1u] == '.'
                    )
                ) ||
                (
                    i + 1u < text.size() &&
                    (
                        is_ascii_digit(
                            text[i + 1u]
                        ) ||
                        text[i + 1u] == '.'
                    )
                );

            if (
                numeric_context
            ) {

                c =
                    '1';
            }
        }

        if (
            c == 'K'
        ) {

            c =
                'k';
        }
    }

    // A leading S/s is a common OCR substitution for '$'. Only apply it on
    // the left currency axis and only when followed by a digit.
    if (
        axis == YAxisKind::LEFT &&
        text.size() >= 2u &&
        (text[0] == 'S' || text[0] == 's') &&
        is_ascii_digit(text[1])
    ) {

        text[0] =
            '$';
    }

    // -------------------------------------------------------------------------
    // LEFT: currency/numeric tick
    // -------------------------------------------------------------------------

    if (
        axis == YAxisKind::LEFT
    ) {

        std::size_t i =
            0u;

        if (
            text[i] == '$'
        ) {

            ++i;
        }

        if (
            i >= text.size()
        ) {

            return false;
        }

        std::size_t digit_count =
            0u;

        std::size_t decimal_count =
            0u;

        while (
            i < text.size()
        ) {

            const char c =
                text[i];

            if (
                is_ascii_digit(c)
            ) {

                ++digit_count;
                ++i;
                continue;
            }

            if (
                c == '.'
            ) {

                ++decimal_count;

                if (
                    decimal_count > 1u
                ) {

                    return false;
                }

                ++i;
                continue;
            }

            if (
                c == 'k'
            ) {

                if (
                    i + 1u != text.size()
                ) {

                    return false;
                }

                ++i;
                break;
            }

            return false;
        }

        if (
            digit_count == 0u
        ) {

            return false;
        }

        // Make the currency marker explicit when OCR dropped it. This is a
        // constrained normalization because the candidate already lives on the
        // left numeric axis.
        if (
            text[0] != '$'
        ) {

            text.insert(
                text.begin(),
                '$'
            );
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // RIGHT: percentage tick
    // -------------------------------------------------------------------------

    if (
        axis == YAxisKind::RIGHT
    ) {

        if (
            text.empty()
        ) {

            return false;
        }

        if (
            text.back() == '%'
        ) {

            text.pop_back();
        }

        if (
            text.empty()
        ) {

            return false;
        }

        std::size_t digit_count =
            0u;

        std::size_t decimal_count =
            0u;

        for (
            const char c :
            text
        ) {

            if (
                is_ascii_digit(c)
            ) {

                ++digit_count;
                continue;
            }

            if (
                c == '.'
            ) {

                ++decimal_count;

                if (
                    decimal_count > 1u
                ) {

                    return false;
                }

                continue;
            }

            return false;
        }

        if (
            digit_count == 0u
        ) {

            return false;
        }

        text.push_back(
            '%'
        );

        return true;
    }

    return false;
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
//     Mav -> May
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
        text == "Mav"
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
// RECOVER MISSING CATEGORY CANDIDATES
// =============================================================================
//
// The primary component detector is intentionally conservative. That is
// correct for rejecting chart geometry, but a real axis label can disappear
// when one glyph touches a chart stroke and the combined component is rejected.
//
// We therefore perform a bounded recovery pass only inside an internal spatial
// gap that matches the learned category spacing. The pass does NOT invent a
// label string. It only creates a real foreground-backed candidate which is
// subsequently sent through the normal OCR and validation pipeline.
//
// Dominant rows/columns are removed locally because horizontal grid lines and
// vertical chart strokes are the main structures that can contaminate the slot.
// The suppression is deliberately local and conservative.
//
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate>
recover_missing_category_candidates(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& sequence_stats,
    std::uint8_t minimum_foreground
) {

    std::vector<TextCandidate> recovered;

    if (
        chart_buffer == nullptr ||
        image_width <= 0 ||
        image_height <= 0 ||
        category_candidates.size() < 2u ||
        sequence_stats.median_center_spacing <= 0.0 ||
        sequence_stats.median_width <= 0.0 ||
        sequence_stats.median_height <= 0.0
    ) {

        return recovered;
    }

    // -------------------------------------------------------------------------
    // Work in horizontal reading order.
    // -------------------------------------------------------------------------

    std::vector<TextCandidate> ordered =
        category_candidates;

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](
            const TextCandidate& a,
            const TextCandidate& b
        ) noexcept {

            const double a_center =
                (
                    static_cast<double>(
                        a.min_x
                    ) +
                    static_cast<double>(
                        a.max_x
                    )
                ) *
                0.5;

            const double b_center =
                (
                    static_cast<double>(
                        b.min_x
                    ) +
                    static_cast<double>(
                        b.max_x
                    )
                ) *
                0.5;

            if (
                a_center !=
                b_center
            ) {

                return
                    a_center <
                    b_center;
            }

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

    const double median_spacing =
        sequence_stats.median_center_spacing;

    const double median_width =
        sequence_stats.median_width;

    const double median_height =
        sequence_stats.median_height;

    const double median_center_y =
        sequence_stats.median_center_y;

    // -------------------------------------------------------------------------
    // Inspect only internal gaps.
    // -------------------------------------------------------------------------

    for (
        std::size_t i = 1u;
        i < ordered.size();
        ++i
    ) {

        const TextCandidate& left =
            ordered[i - 1u];

        const TextCandidate& right =
            ordered[i];

        const double left_center_x =
            (
                static_cast<double>(
                    left.min_x
                ) +
                static_cast<double>(
                    left.max_x
                )
            ) *
            0.5;

        const double right_center_x =
            (
                static_cast<double>(
                    right.min_x
                ) +
                static_cast<double>(
                    right.max_x
                )
            ) *
            0.5;

        const double gap =
            right_center_x -
            left_center_x;

        if (
            gap <=
            median_spacing *
                CATEGORY_GAP_RECOVERY_MIN_RATIO
        ) {

            continue;
        }

        const long long estimated_slot_count =
            static_cast<long long>(
                std::llround(
                    gap /
                    median_spacing
                )
            ) -
            1LL;

        if (
            estimated_slot_count <= 0LL ||
            estimated_slot_count >
                static_cast<long long>(
                    CATEGORY_GAP_RECOVERY_MAX_MISSING_SLOTS
                )
        ) {

            continue;
        }

        const double modeled_gap =
            median_spacing *
            static_cast<double>(
                estimated_slot_count +
                1LL
            );

        if (
            modeled_gap <= 0.0
        ) {

            continue;
        }

        const double relative_gap_error =
            std::abs(
                gap -
                modeled_gap
            ) /
            modeled_gap;

        if (
            relative_gap_error >
            CATEGORY_GAP_RECOVERY_MAX_RELATIVE_ERROR
        ) {

            continue;
        }

        for (
            long long slot = 1LL;
            slot <= estimated_slot_count;
            ++slot
        ) {

            const double expected_center_x =
                left_center_x +
                median_spacing *
                    static_cast<double>(
                        slot
                    );

            // -----------------------------------------------------------------
            // Do not recover a slot already occupied by an existing candidate.
            // -----------------------------------------------------------------

            bool occupied =
                false;

            const double occupancy_radius =
                median_spacing *
                0.35;

            for (
                const TextCandidate& existing :
                ordered
            ) {

                const double existing_center_x =
                    (
                        static_cast<double>(
                            existing.min_x
                        ) +
                        static_cast<double>(
                            existing.max_x
                        )
                    ) *
                    0.5;

                if (
                    std::abs(
                        existing_center_x -
                        expected_center_x
                    ) <=
                    occupancy_radius
                ) {

                    occupied =
                        true;

                    break;
                }
            }

            if (
                occupied
            ) {

                continue;
            }

            // -----------------------------------------------------------------
            // Bounded local inspection window.
            // -----------------------------------------------------------------

            const int x_radius =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            median_width *
                            CATEGORY_GAP_RECOVERY_X_RADIUS_FACTOR
                        )
                    ),
                    CATEGORY_GAP_RECOVERY_MIN_X_RADIUS,
                    CATEGORY_GAP_RECOVERY_MAX_X_RADIUS
                );

            const int y_radius =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            median_height *
                            CATEGORY_GAP_RECOVERY_Y_RADIUS_FACTOR
                        )
                    ),
                    CATEGORY_GAP_RECOVERY_MIN_Y_RADIUS,
                    CATEGORY_GAP_RECOVERY_MAX_Y_RADIUS
                );

            const int roi_center_x =
                static_cast<int>(
                    std::llround(
                        expected_center_x
                    )
                );

            const int roi_center_y =
                static_cast<int>(
                    std::llround(
                        median_center_y
                    )
                );

            const int roi_min_x =
                std::max(
                    0,
                    roi_center_x -
                        x_radius
                );

            const int roi_max_x =
                std::min(
                    image_width - 1,
                    roi_center_x +
                        x_radius
                );

            const int roi_min_y =
                std::max(
                    0,
                    roi_center_y -
                        y_radius
                );

            const int roi_max_y =
                std::min(
                    image_height - 1,
                    roi_center_y +
                        y_radius
                );

            if (
                roi_min_x > roi_max_x ||
                roi_min_y > roi_max_y
            ) {

                continue;
            }

            const int roi_width =
                roi_max_x -
                roi_min_x +
                1;

            const int roi_height =
                roi_max_y -
                roi_min_y +
                1;

            if (
                roi_width <= 0 ||
                roi_height <= 0
            ) {

                continue;
            }

            const std::size_t roi_pixels =
                static_cast<std::size_t>(
                    roi_width
                ) *
                static_cast<std::size_t>(
                    roi_height
                );

            std::vector<std::uint8_t> mask(
                roi_pixels,
                std::uint8_t{0}
            );

            std::vector<std::uint16_t> row_counts(
                static_cast<std::size_t>(
                    roi_height
                ),
                std::uint16_t{0}
            );

            std::vector<std::uint16_t> column_counts(
                static_cast<std::size_t>(
                    roi_width
                ),
                std::uint16_t{0}
            );

            std::size_t raw_active_pixels =
                0u;

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                const int image_y =
                    roi_min_y +
                    local_y;

                const std::size_t row_offset =
                    static_cast<std::size_t>(
                        local_y
                    ) *
                    static_cast<std::size_t>(
                        roi_width
                    );

                for (
                    int local_x = 0;
                    local_x < roi_width;
                    ++local_x
                ) {

                    const int image_x =
                        roi_min_x +
                        local_x;

                    const bool active =
                        chart_foreground(
                            chart_buffer,
                            image_width,
                            image_x,
                            image_y
                        ) >=
                        minimum_foreground;

                    if (
                        !active
                    ) {

                        continue;
                    }

                    mask[
                        row_offset +
                        static_cast<std::size_t>(
                            local_x
                        )
                    ] =
                        1u;

                    ++row_counts[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ];

                    ++column_counts[
                        static_cast<std::size_t>(
                            local_x
                        )
                    ];

                    ++raw_active_pixels;
                }
            }

            if (
                raw_active_pixels <
                CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS
            ) {

                continue;
            }

            // -----------------------------------------------------------------
            // Remove dominant chart strokes.
            // -----------------------------------------------------------------

            std::vector<std::uint8_t> dominant_rows(
                static_cast<std::size_t>(
                    roi_height
                ),
                std::uint8_t{0}
            );

            std::vector<std::uint8_t> dominant_columns(
                static_cast<std::size_t>(
                    roi_width
                ),
                std::uint8_t{0}
            );

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                const double occupancy =
                    static_cast<double>(
                        row_counts[
                            static_cast<std::size_t>(
                                local_y
                            )
                        ]
                    ) /
                    static_cast<double>(
                        roi_width
                    );

                if (
                    occupancy >=
                    CATEGORY_GAP_RECOVERY_DOMINANT_ROW_RATIO
                ) {

                    dominant_rows[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ] =
                        1u;
                }
            }

            for (
                int local_x = 0;
                local_x < roi_width;
                ++local_x
            ) {

                const double occupancy =
                    static_cast<double>(
                        column_counts[
                            static_cast<std::size_t>(
                                local_x
                            )
                        ]
                    ) /
                    static_cast<double>(
                        roi_height
                    );

                if (
                    occupancy >=
                    CATEGORY_GAP_RECOVERY_DOMINANT_COLUMN_RATIO
                ) {

                    dominant_columns[
                        static_cast<std::size_t>(
                            local_x
                        )
                    ] =
                        1u;
                }
            }

            int min_x =
                roi_max_x +
                1;

            int min_y =
                roi_max_y +
                1;

            int max_x =
                roi_min_x -
                1;

            int max_y =
                roi_min_y -
                1;

            std::size_t clean_active_pixels =
                0u;

            for (
                int local_y = 0;
                local_y < roi_height;
                ++local_y
            ) {

                if (
                    dominant_rows[
                        static_cast<std::size_t>(
                            local_y
                        )
                    ] != 0u
                ) {

                    continue;
                }

                const std::size_t row_offset =
                    static_cast<std::size_t>(
                        local_y
                    ) *
                    static_cast<std::size_t>(
                        roi_width
                    );

                for (
                    int local_x = 0;
                    local_x < roi_width;
                    ++local_x
                ) {

                    if (
                        dominant_columns[
                            static_cast<std::size_t>(
                                local_x
                            )
                        ] != 0u
                    ) {

                        continue;
                    }

                    if (
                        mask[
                            row_offset +
                            static_cast<std::size_t>(
                                local_x
                            )
                        ] == 0u
                    ) {

                        continue;
                    }

                    const int image_x =
                        roi_min_x +
                        local_x;

                    const int image_y =
                        roi_min_y +
                        local_y;

                    min_x =
                        std::min(
                            min_x,
                            image_x
                        );

                    min_y =
                        std::min(
                            min_y,
                            image_y
                        );

                    max_x =
                        std::max(
                            max_x,
                            image_x
                        );

                    max_y =
                        std::max(
                            max_y,
                            image_y
                        );

                    ++clean_active_pixels;
                }
            }

            if (
                clean_active_pixels <
                CATEGORY_GAP_RECOVERY_MIN_ACTIVE_PIXELS ||
                max_x < min_x ||
                max_y < min_y
            ) {

                continue;
            }

            const int candidate_width =
                max_x -
                min_x +
                1;

            const int candidate_height =
                max_y -
                min_y +
                1;

            if (
                candidate_width <
                    MIN_LABEL_WIDTH ||
                candidate_height <
                    MIN_LABEL_HEIGHT ||
                candidate_width >
                    MAX_CANDIDATE_WIDTH ||
                candidate_height >
                    MAX_CANDIDATE_HEIGHT
            ) {

                continue;
            }

            const double candidate_center_x =
                (
                    static_cast<double>(
                        min_x
                    ) +
                    static_cast<double>(
                        max_x
                    )
                ) *
                0.5;

            const double center_offset =
                std::abs(
                    candidate_center_x -
                    expected_center_x
                );

            if (
                center_offset >
                static_cast<double>(
                    x_radius
                ) *
                CATEGORY_GAP_RECOVERY_MAX_CENTER_OFFSET_FACTOR
            ) {

                continue;
            }

            const std::size_t area =
                static_cast<std::size_t>(
                    candidate_width
                ) *
                static_cast<std::size_t>(
                    candidate_height
                );

            if (
                area == 0u
            ) {

                continue;
            }

            const double density =
                static_cast<double>(
                    clean_active_pixels
                ) /
                static_cast<double>(
                    area
                );

            if (
                !std::isfinite(
                    density
                ) ||
                density <
                    CATEGORY_GAP_RECOVERY_MIN_DENSITY ||
                density >
                    CATEGORY_GAP_RECOVERY_MAX_DENSITY
            ) {

                continue;
            }

            TextCandidate candidate{
                min_x,
                min_y,
                max_x,
                max_y,
                clean_active_pixels
            };

            if (
                !acceptable_category_geometry(
                    candidate,
                    image_width,
                    image_height
                )
            ) {

                continue;
            }

            recovered.push_back(
                candidate
            );

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "category_gap_recovered: "
                    "slot=%lld/%lld "
                    "expected_center_x=%.2f "
                    "x=%d..%d y=%d..%d "
                    "w=%d h=%d pixels=%zu "
                    "density=%.4f\n",
                    slot,
                    estimated_slot_count,
                    expected_center_x,
                    candidate.min_x,
                    candidate.max_x,
                    candidate.min_y,
                    candidate.max_y,
                    candidate_width,
                    candidate_height,
                    candidate.active_pixels,
                    density
                );
            }
        }
    }

    return recovered;
}

// =============================================================================
// BUILD COLOR-AWARE Y-AXIS OCR BUFFER
// =============================================================================
//
// The chart-label detector intentionally uses channel 1 because that channel
// is the existing OCR foreground/mask channel in the preprocessing pipeline.
//
// The Y axes are different: real financial charts commonly render the left
// numeric axis in blue and the right numeric axis in orange. A single-channel
// green extraction can therefore preserve the component geometry while still
// giving LineRecognizer a weak or unusable glyph image.
//
// We build a dedicated one-channel OCR representation for Y-axis candidates.
// The background polarity is estimated from the candidate crop:
//
//     light background -> 255 - min(R,G,B)
//     dark background  -> max(R,G,B)
//
// This keeps both saturated colored text and ordinary grayscale text visible
// to the downstream 1-channel OCR path without changing the global detector.
//
// =============================================================================

[[nodiscard]]
OcrBuffer
build_y_axis_candidate_buffer(
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
        5;

    constexpr int OCR_PADDING_Y =
        3;

    const int crop_min_x =
        std::max(
            0,
            candidate.min_x - OCR_PADDING_X
        );

    const int crop_max_x =
        std::min(
            image_width - 1,
            candidate.max_x + OCR_PADDING_X
        );

    const int crop_min_y =
        std::max(
            0,
            candidate.min_y - OCR_PADDING_Y
        );

    const int crop_max_y =
        std::min(
            image_height - 1,
            candidate.max_y + OCR_PADDING_Y
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
        static_cast<std::size_t>(crop_width) *
        static_cast<std::size_t>(crop_height);

    if (
        crop_pixels == 0u
    ) {

        return {};
    }

    // -------------------------------------------------------------------------
    // Estimate background polarity from a bounded sample.
    // -------------------------------------------------------------------------

    std::size_t bright_samples =
        0u;

    std::size_t sample_count =
        0u;

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        y += 2
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_width) *
            3u;

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            x += 2
        ) {

            const std::size_t index =
                source_row +
                static_cast<std::size_t>(x) *
                    3u;

            const std::uint8_t r =
                chart_buffer[index + 0u];

            const std::uint8_t g =
                chart_buffer[index + 1u];

            const std::uint8_t b =
                chart_buffer[index + 2u];

            const std::uint8_t luminance =
                static_cast<std::uint8_t>(
                    (
                        77u * static_cast<unsigned>(r) +
                        150u * static_cast<unsigned>(g) +
                        29u * static_cast<unsigned>(b)
                    ) >> 8u
                );

            if (
                luminance >= 128u
            ) {

                ++bright_samples;
            }

            ++sample_count;
        }
    }

    const bool light_background =
        sample_count > 0u &&
        bright_samples * 2u >= sample_count;

    OcrBuffer output;

    output.width =
        crop_width;

    output.height =
        crop_height;

    output.data.assign(
        crop_pixels,
        std::uint8_t{0}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_width) *
            3u;

        const std::size_t destination_row =
            static_cast<std::size_t>(
                y - crop_min_y
            ) *
            static_cast<std::size_t>(crop_width);

        for (
            int x = crop_min_x;
            x <= crop_max_x;
            ++x
        ) {

            const std::size_t source_index =
                source_row +
                static_cast<std::size_t>(x) *
                    3u;

            const std::uint8_t r =
                chart_buffer[source_index + 0u];

            const std::uint8_t g =
                chart_buffer[source_index + 1u];

            const std::uint8_t b =
                chart_buffer[source_index + 2u];

            const std::uint8_t cmin =
                std::min(
                    r,
                    std::min(g, b)
                );

            const std::uint8_t cmax =
                std::max(
                    r,
                    std::max(g, b)
                );

            const std::uint8_t strength =
                light_background
                    ? static_cast<std::uint8_t>(
                          255u -
                          static_cast<unsigned>(cmin)
                      )
                    : cmax;

            output.data[
                destination_row +
                static_cast<std::size_t>(
                    x - crop_min_x
                )
            ] =
                strength;
        }
    }

    return output;
}

// =============================================================================
// OCR BUFFER TRANSFORMS
// =============================================================================

[[nodiscard]]
OcrBuffer invert_ocr_buffer(
    const OcrBuffer& source
) {

    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0
    ) {

        return {};
    }

    OcrBuffer output;

    output.width =
        source.width;

    output.height =
        source.height;

    output.data.resize(
        source.data.size()
    );

    for (
        std::size_t i = 0u;
        i < source.data.size();
        ++i
    ) {

        output.data[i] =
            static_cast<std::uint8_t>(
                255u -
                static_cast<unsigned>(
                    source.data[i]
                )
            );
    }

    return output;
}

// =============================================================================
// SCALE OCR BUFFER
// =============================================================================

[[nodiscard]]
OcrBuffer scale_ocr_buffer(
    const OcrBuffer& source,
    int scale
) {

    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0 ||
        scale <= 1
    ) {

        return source;
    }

    const int scaled_width =
        source.width *
        scale;

    const int scaled_height =
        source.height *
        scale;

    if (
        scaled_width <= 0 ||
        scaled_height <= 0
    ) {

        return {};
    }

    const std::size_t scaled_pixels =
        static_cast<std::size_t>(
            scaled_width
        ) *
        static_cast<std::size_t>(
            scaled_height
        );

    OcrBuffer output;

    output.width =
        scaled_width;

    output.height =
        scaled_height;

    output.data.assign(
        scaled_pixels,
        std::uint8_t{0}
    );

    for (
        int y = 0;
        y < scaled_height;
        ++y
    ) {

        const int source_y =
            y /
            scale;

        const std::size_t source_row =
            static_cast<std::size_t>(
                source_y
            ) *
            static_cast<std::size_t>(
                source.width
            );

        const std::size_t destination_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(
                scaled_width
            );

        for (
            int x = 0;
            x < scaled_width;
            ++x
        ) {

            const int source_x =
                x /
                scale;

            output.data[
                destination_row +
                static_cast<std::size_t>(x)
            ] =
                source.data[
                    source_row +
                    static_cast<std::size_t>(
                        source_x
                    )
                ];
        }
    }

    return output;
}

// =============================================================================
// LUMINANCE Y-AXIS OCR BUFFER
// =============================================================================
//
// Conventional dark-text-on-light-background representation. This is useful
// for the blue left currency axis, where the existing color-minimum strength
// representation can be too weak for the OCR model.

[[nodiscard]]
OcrBuffer build_y_axis_luminance_buffer(
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
        5;

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

    OcrBuffer output;

    output.width =
        crop_width;

    output.height =
        crop_height;

    output.data.assign(
        crop_pixels,
        std::uint8_t{255}
    );

    for (
        int y = crop_min_y;
        y <= crop_max_y;
        ++y
    ) {

        const std::size_t source_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_width) *
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
                static_cast<std::size_t>(x) *
                    3u;

            const std::uint8_t r =
                chart_buffer[
                    source_index + 0u
                ];

            const std::uint8_t g =
                chart_buffer[
                    source_index + 1u
                ];

            const std::uint8_t b =
                chart_buffer[
                    source_index + 2u
                ];

            const std::uint8_t luminance =
                static_cast<std::uint8_t>(
                    (
                        77u *
                            static_cast<unsigned>(r) +
                        150u *
                            static_cast<unsigned>(g) +
                        29u *
                            static_cast<unsigned>(b)
                    ) >>
                    8u
                );

            output.data[
                destination_row +
                static_cast<std::size_t>(
                    x -
                    crop_min_x
                )
            ] =
                luminance;
        }
    }

    return output;
}

// =============================================================================
// THRESHOLD OCR BUFFER
// =============================================================================

[[nodiscard]]
OcrBuffer threshold_ocr_buffer(
    const OcrBuffer& source,
    std::uint8_t threshold,
    bool dark_foreground
) {

    if (
        source.data.empty() ||
        source.width <= 0 ||
        source.height <= 0
    ) {

        return {};
    }

    OcrBuffer output;

    output.width =
        source.width;

    output.height =
        source.height;

    output.data.resize(
        source.data.size()
    );

    for (
        std::size_t i = 0u;
        i < source.data.size();
        ++i
    ) {

        const bool foreground =
            dark_foreground
                ? source.data[i] <= threshold
                : source.data[i] >= threshold;

        output.data[i] =
            foreground
                ? std::uint8_t{0}
                : std::uint8_t{255};
    }

    return output;
}

// =============================================================================
// MULTI-PASS Y-AXIS OCR
// =============================================================================

[[nodiscard]]
bool recognize_y_axis_text(
    const LineRecognizer& line_recognizer,
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const TextCandidate& candidate,
    YAxisKind axis,
    std::string& raw_text,
    std::string& normalized_text
) {

    raw_text.clear();
    normalized_text.clear();

    const OcrBuffer adaptive =
        build_y_axis_candidate_buffer(
            chart_buffer,
            image_width,
            image_height,
            candidate
        );

    const OcrBuffer luminance =
        build_y_axis_luminance_buffer(
            chart_buffer,
            image_width,
            image_height,
            candidate
        );

    auto try_buffer =
        [&](
            const OcrBuffer& buffer
        ) -> bool {

        if (
            buffer.data.empty() ||
            buffer.width <= 0 ||
            buffer.height <= 1
        ) {

            return false;
        }

        std::string raw =
            line_recognizer.recognize(
                buffer.data.data(),
                buffer.width,
                0,
                buffer.height,
                1
            );

        trim_text(
            raw
        );

        if (
            raw.empty()
        ) {

            return false;
        }

        std::string normalized =
            raw;

        if (
            !normalize_y_axis_text(
                normalized,
                axis
            )
        ) {

            return false;
        }

        raw_text =
            std::move(raw);

        normalized_text =
            std::move(normalized);

        return true;
    };

    // Preserve the current representation first.
    if (
        try_buffer(
            adaptive
        )
    ) {

        return true;
    }

    const OcrBuffer adaptive_scaled =
        scale_ocr_buffer(
            adaptive,
            Y_AXIS_OCR_SCALE_FACTOR
        );

    if (
        try_buffer(
            adaptive_scaled
        )
    ) {

        return true;
    }

    const OcrBuffer adaptive_inverted =
        invert_ocr_buffer(
            adaptive
        );

    if (
        try_buffer(
            adaptive_inverted
        )
    ) {

        return true;
    }

    const OcrBuffer luminance_scaled =
        scale_ocr_buffer(
            luminance,
            Y_AXIS_OCR_SCALE_FACTOR
        );

    if (
        try_buffer(
            luminance_scaled
        )
    ) {

        return true;
    }

    if (
        try_buffer(
            luminance
        )
    ) {

        return true;
    }

    const OcrBuffer luminance_inverted =
        invert_ocr_buffer(
            luminance
        );

    if (
        try_buffer(
            luminance_inverted
        )
    ) {

        return true;
    }

    const OcrBuffer luminance_thresholded =
        threshold_ocr_buffer(
            luminance,
            220u,
            true
        );

    const OcrBuffer luminance_thresholded_scaled =
        scale_ocr_buffer(
            luminance_thresholded,
            Y_AXIS_OCR_SCALE_FACTOR
        );

    if (
        try_buffer(
            luminance_thresholded_scaled
        )
    ) {

        return true;
    }

    const OcrBuffer legacy =
        build_candidate_buffer(
            chart_buffer,
            image_width,
            image_height,
            candidate
        );

    if (
        try_buffer(
            legacy
        )
    ) {

        return true;
    }

    return false;
}

// =============================================================================
// PARSE NORMALIZED Y-AXIS VALUE
// =============================================================================

[[nodiscard]]
bool parse_y_axis_numeric_value(
    const std::string& text,
    YAxisKind axis,
    double& value
) noexcept {

    if (
        text.empty()
    ) {

        return false;
    }

    std::size_t begin =
        0u;

    std::size_t end =
        text.size();

    if (
        axis == YAxisKind::LEFT
    ) {

        if (
            begin < end &&
            text[begin] == '$'
        ) {

            ++begin;
        }

        if (
            end > begin &&
            text[end - 1u] == 'k'
        ) {

            --end;
        }
    } else if (
        axis == YAxisKind::RIGHT
    ) {

        if (
            end > begin &&
            text[end - 1u] == '%'
        ) {

            --end;
        }
    } else {

        return false;
    }

    if (
        begin >= end
    ) {

        return false;
    }

    double integer_part =
        0.0;

    double fractional_part =
        0.0;

    double fractional_scale =
        0.1;

    bool seen_decimal =
        false;

    std::size_t digit_count =
        0u;

    for (
        std::size_t i = begin;
        i < end;
        ++i
    ) {

        const char c =
            text[i];

        if (
            is_ascii_digit(c)
        ) {

            const int digit =
                c -
                '0';

            if (
                seen_decimal
            ) {

                fractional_part +=
                    static_cast<double>(digit) *
                    fractional_scale;

                fractional_scale *=
                    0.1;

            } else {

                integer_part =
                    integer_part *
                    10.0 +
                    static_cast<double>(digit);
            }

            ++digit_count;
            continue;
        }

        if (
            c == '.' &&
            !seen_decimal
        ) {

            seen_decimal =
                true;

            continue;
        }

        return false;
    }

    if (
        digit_count == 0u
    ) {

        return false;
    }

    value =
        integer_part +
        fractional_part;

    if (
        axis == YAxisKind::LEFT &&
        end < text.size() &&
        text[end] == 'k'
    ) {

        value *=
            1000.0;
    }

    return std::isfinite(value);
}

// =============================================================================
// RIGHT Y-AXIS DECIMAL RECOVERY
// =============================================================================

[[nodiscard]]
bool recover_right_y_axis_decimal(
    std::string& text,
    const std::vector<DetectedLabel>& accepted_labels,
    int candidate_min_y,
    int candidate_max_y,
    double& original_value,
    double& recovered_value
) noexcept {

    original_value =
        0.0;

    recovered_value =
        0.0;

    if (
        text.empty() ||
        accepted_labels.size() < 2u
    ) {

        return false;
    }

    double current_value =
        0.0;

    if (
        !parse_y_axis_numeric_value(
            text,
            YAxisKind::RIGHT,
            current_value
        )
    ) {

        return false;
    }

    if (
        !std::isfinite(current_value) ||
        current_value < 0.0 ||
        current_value >
            RIGHT_Y_MAX_PERCENT_VALUE
    ) {

        return false;
    }

    std::vector<std::pair<double, double>> points;
    points.reserve(
        accepted_labels.size()
    );

    for (
        const DetectedLabel& accepted :
        accepted_labels
    ) {

        double value =
            0.0;

        if (
            !parse_y_axis_numeric_value(
                accepted.label.text,
                YAxisKind::RIGHT,
                value
            )
        ) {

            continue;
        }

        if (
            !std::isfinite(value) ||
            value < 0.0 ||
            value >
                RIGHT_Y_MAX_PERCENT_VALUE
        ) {

            continue;
        }

        const double center_y =
            (
                static_cast<double>(
                    accepted.label.min_y
                ) +
                static_cast<double>(
                    accepted.label.max_y
                )
            ) *
            0.5;

        points.push_back({
            center_y,
            value
        });
    }

    if (
        points.size() < 2u
    ) {

        return false;
    }

    std::sort(
        points.begin(),
        points.end(),
        [](
            const auto& a,
            const auto& b
        ) noexcept {

            return a.first < b.first;
        }
    );

    const double current_center_y =
        (
            static_cast<double>(candidate_min_y) +
            static_cast<double>(candidate_max_y)
        ) *
        0.5;

    double best_pair_distance =
        std::numeric_limits<double>::max();

    double best_slope =
        0.0;

    double best_intercept =
        0.0;

    for (
        std::size_t i = 0u;
        i < points.size();
        ++i
    ) {

        for (
            std::size_t j = i + 1u;
            j < points.size();
            ++j
        ) {

            const double y0 =
                points[i].first;

            const double y1 =
                points[j].first;

            const double dy =
                y1 - y0;

            if (
                std::abs(dy) <
                1.0e-9
            ) {

                continue;
            }

            const double pair_center =
                (y0 + y1) *
                0.5;

            const double pair_distance =
                std::abs(
                    current_center_y -
                    pair_center
                );

            const double slope =
                (
                    points[j].second -
                    points[i].second
                ) /
                dy;

            if (
                !std::isfinite(slope)
            ) {

                continue;
            }

            const double intercept =
                points[i].second -
                slope *
                points[i].first;

            if (
                !std::isfinite(intercept)
            ) {

                continue;
            }

            if (
                pair_distance <
                best_pair_distance
            ) {

                best_pair_distance =
                    pair_distance;

                best_slope =
                    slope;

                best_intercept =
                    intercept;
            }
        }
    }

    if (
        !std::isfinite(best_pair_distance)
    ) {

        return false;
    }

    const double predicted =
        best_slope *
        current_center_y +
        best_intercept;

    if (
        !std::isfinite(predicted) ||
        predicted < 0.0 ||
        predicted >
            RIGHT_Y_MAX_PERCENT_VALUE
    ) {

        return false;
    }

    constexpr double scales[] = {
        0.1,
        0.01,
        10.0
    };

    double best_scale =
        1.0;

    double best_error =
        std::numeric_limits<double>::max();

    for (
        const double scale :
        scales
    ) {

        const double candidate =
            current_value *
            scale;

        if (
            !std::isfinite(candidate) ||
            candidate < 0.0 ||
            candidate >
                RIGHT_Y_MAX_PERCENT_VALUE
        ) {

            continue;
        }

        const double absolute_error =
            std::abs(
                candidate -
                predicted
            );

        const double relative_error =
            absolute_error /
            std::max(
                1.0,
                std::abs(predicted)
            );

        const bool within_tolerance =
            absolute_error <=
                RIGHT_Y_DECIMAL_RECOVERY_ABSOLUTE_TOLERANCE ||
            relative_error <=
                RIGHT_Y_DECIMAL_RECOVERY_RELATIVE_TOLERANCE;

        if (
            !within_tolerance
        ) {

            continue;
        }

        if (
            absolute_error <
            best_error
        ) {

            best_error =
                absolute_error;

            best_scale =
                scale;
        }
    }

    if (
        best_scale ==
        1.0
    ) {

        return false;
    }

    const double canonical_value =
        current_value *
        best_scale;

    if (
        !std::isfinite(canonical_value) ||
        canonical_value < 0.0 ||
        canonical_value >
            RIGHT_Y_MAX_PERCENT_VALUE
    ) {

        return false;
    }

    if (
        std::abs(
            canonical_value -
            current_value
        ) <
        0.05
    ) {

        return false;
    }

    char formatted[64] = {};

    const int written =
        std::snprintf(
            formatted,
            sizeof(formatted),
            "%.1f%%",
            canonical_value
        );

    if (
        written <= 0 ||
        static_cast<std::size_t>(written) >=
            sizeof(formatted)
    ) {

        return false;
    }

    original_value =
        current_value;

    recovered_value =
        canonical_value;

    text.assign(
        formatted,
        static_cast<std::size_t>(written)
    );

    return true;
}

// =============================================================================
// ROBUST Y-AXIS SEQUENCE OUTLIER FILTER
// =============================================================================
//
// A valid linear axis has approximately linear numeric values as a function
// of image Y. OCR hallucinations such as "55554%" violate that relationship
// by orders of magnitude.
//
// The fit uses the median of all pairwise slopes (Theil-Sen style), which is
// robust to a minority of bad OCR values and does not assume a particular
// axis range.
//
// =============================================================================

[[nodiscard]]
std::vector<bool>
y_axis_sequence_inlier_mask(
    const std::vector<DetectedLabel>& labels,
    YAxisKind axis
) noexcept {

    std::vector<bool> keep(
        labels.size(),
        true
    );

    if (
        labels.size() < 4u
    ) {

        return keep;
    }

    struct Point {

        double y = 0.0;
        double value = 0.0;
    };

    std::vector<Point> points;
    points.reserve(
        labels.size()
    );

    std::vector<std::size_t> point_indices;
    point_indices.reserve(
        labels.size()
    );

    for (
        std::size_t i = 0u;
        i < labels.size();
        ++i
    ) {

        double value =
            0.0;

        if (
            !parse_y_axis_numeric_value(
                labels[i].label.text,
                axis,
                value
            )
        ) {

            keep[i] =
                false;

            continue;
        }

        const double center_y =
            (
                static_cast<double>(
                    labels[i].label.min_y
                ) +
                static_cast<double>(
                    labels[i].label.max_y
                )
            ) *
            0.5;

        points.push_back({
            center_y,
            value
        });

        point_indices.push_back(
            i
        );
    }

    if (
        points.size() < 4u
    ) {

        return keep;
    }

    std::vector<double> slopes;

    slopes.reserve(
        points.size() *
        (points.size() - 1u) /
        2u
    );

    for (
        std::size_t i = 0u;
        i < points.size();
        ++i
    ) {

        for (
            std::size_t j = i + 1u;
            j < points.size();
            ++j
        ) {

            const double dy =
                points[j].y -
                points[i].y;

            if (
                std::abs(dy) <
                1.0e-9
            ) {

                continue;
            }

            const double slope =
                (
                    points[j].value -
                    points[i].value
                ) /
                dy;

            if (
                std::isfinite(slope)
            ) {

                slopes.push_back(
                    slope
                );
            }
        }
    }

    if (
        slopes.empty()
    ) {

        return keep;
    }

    const double slope =
        median_value(
            std::move(slopes)
        );

    if (
        !std::isfinite(slope)
    ) {

        return keep;
    }

    std::vector<double> intercepts;
    intercepts.reserve(
        points.size()
    );

    for (
        const Point& point :
        points
    ) {

        intercepts.push_back(
            point.value -
            slope *
            point.y
        );
    }

    const double intercept =
        median_value(
            std::move(intercepts)
        );

    if (
        !std::isfinite(intercept)
    ) {

        return keep;
    }

    std::vector<double> residuals;
    residuals.reserve(
        points.size()
    );

    double min_value =
        std::numeric_limits<double>::max();

    double max_value =
        std::numeric_limits<double>::lowest();

    for (
        const Point& point :
        points
    ) {

        min_value =
            std::min(
                min_value,
                point.value
            );

        max_value =
            std::max(
                max_value,
                point.value
            );

        const double predicted =
            slope *
            point.y +
            intercept;

        residuals.push_back(
            std::abs(
                point.value -
                predicted
            )
        );
    }

    const double median_residual =
        median_value(
            residuals
        );

    const double value_range =
        max_value -
        min_value;

    if (
        !std::isfinite(median_residual) ||
        !std::isfinite(value_range)
    ) {

        return keep;
    }

    const double residual_threshold =
        std::max(
            1.0,
            std::max(
                median_residual * 8.0,
                value_range * 0.02
            )
        );

    for (
        std::size_t i = 0u;
        i < points.size();
        ++i
    ) {

        const double predicted =
            slope *
            points[i].y +
            intercept;

        const double residual =
            std::abs(
                points[i].value -
                predicted
            );

        if (
            residual >
            residual_threshold
        ) {

            keep[
                point_indices[i]
            ] =
                false;
        }
    }

    return keep;
}

// =============================================================================
// Y-AXIS SPATIAL VALUE FIT
// =============================================================================

struct YAxisValueFit {

    bool valid = false;

    double slope = 0.0;

    double intercept = 0.0;

    double tick_spacing = 0.0;

    double tick_delta = 0.0;

    std::size_t anchors = 0u;
};

// =============================================================================
// ESTIMATE Y-AXIS VALUE FIT
// =============================================================================
//
// Estimate the value change per spatial tick instead of fitting directly to
// every OCR result. This makes the model resistant to a single bad OCR value.
//
// For the sample right axis:
//
//     16.5
//     11.0
//      5.5
//
// establish a -5.5 tick delta. The top tick at the corresponding spatial slot
// therefore becomes 22.0 even when OCR returns "55".
//

[[nodiscard]]
YAxisValueFit
estimate_y_axis_value_fit(
    const std::vector<TextCandidate>& axis_candidates,
    const std::vector<DetectedLabel>& axis_results,
    YAxisKind axis
) noexcept {

    YAxisValueFit fit{};

    if (
        axis_candidates.size() < 2u ||
        axis_results.size() <
            MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS
    ) {

        return fit;
    }

    std::vector<double> centers;
    centers.reserve(
        axis_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        axis_candidates
    ) {

        centers.push_back(
            (
                static_cast<double>(
                    candidate.min_y
                ) +
                static_cast<double>(
                    candidate.max_y
                )
            ) *
            0.5
        );
    }

    std::sort(
        centers.begin(),
        centers.end()
    );

    std::vector<double> spacings;
    spacings.reserve(
        centers.size() - 1u
    );

    for (
        std::size_t i = 1u;
        i < centers.size();
        ++i
    ) {

        const double spacing =
            centers[i] -
            centers[i - 1u];

        if (
            spacing > 1.0
        ) {

            spacings.push_back(
                spacing
            );
        }
    }

    if (
        spacings.empty()
    ) {

        return fit;
    }

    fit.tick_spacing =
        median_value(
            std::move(
                spacings
            )
        );

    if (
        !std::isfinite(
            fit.tick_spacing
        ) ||
        fit.tick_spacing <= 1.0
    ) {

        return fit;
    }

    struct Point {

        double y = 0.0;

        double value = 0.0;
    };

    std::vector<Point> points;
    points.reserve(
        axis_results.size()
    );

    for (
        const DetectedLabel& result :
        axis_results
    ) {

        double value =
            0.0;

        if (
            !parse_y_axis_numeric_value(
                result.label.text,
                axis,
                value
            )
        ) {

            continue;
        }

        points.push_back({
            (
                static_cast<double>(
                    result.label.min_y
                ) +
                static_cast<double>(
                    result.label.max_y
                )
            ) *
                0.5,
            value
        });
    }

    if (
        points.size() <
        MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS
    ) {

        return fit;
    }

    std::sort(
        points.begin(),
        points.end(),
        [](
            const Point& a,
            const Point& b
        ) noexcept {

            return a.y < b.y;
        }
    );

    std::vector<double> tick_deltas;

    tick_deltas.reserve(
        points.size() > 1u
            ? points.size() - 1u
            : 0u
    );

    // Use spatially adjacent OCR anchors. A single bad OCR result should only
    // contaminate one local interval rather than half of all pairwise slopes.
    //
    // Example:
    //
    //     55%   <- bad
    //     16.5% <- good
    //     11.0% <- good
    //      5.5% <- good
    //
    // Adjacent deltas are:
    //
    //     -38.5, -5.5, -5.5
    //
    // and their median remains -5.5.
    for (
        std::size_t i = 1u;
        i < points.size();
        ++i
    ) {

        const double spatial_distance =
            points[i].y -
            points[i - 1u].y;

        if (
            spatial_distance <
            fit.tick_spacing *
                0.50
        ) {

            continue;
        }

        const double spatial_ticks =
            spatial_distance /
            fit.tick_spacing;

        const long long rounded_ticks =
            std::llround(
                spatial_ticks
            );

        if (
            rounded_ticks <= 0
        ) {

            continue;
        }

        if (
            std::abs(
                spatial_ticks -
                static_cast<double>(
                    rounded_ticks
                )
            ) >
            Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO
        ) {

            continue;
        }

        const double delta =
            (
                points[i].value -
                points[i - 1u].value
            ) /
            static_cast<double>(
                rounded_ticks
            );

        if (
            std::isfinite(delta)
        ) {

            tick_deltas.push_back(
                delta
            );
        }
    }

    if (
        tick_deltas.empty()
    ) {

        return fit;
    }

    fit.tick_delta =
        median_value(
            std::move(
                tick_deltas
            )
        );

    if (
        !std::isfinite(
            fit.tick_delta
        ) ||
        std::abs(
            fit.tick_delta
        ) < 1.0e-9
    ) {

        return fit;
    }

    fit.slope =
        fit.tick_delta /
        fit.tick_spacing;

    std::vector<double> intercepts;
    intercepts.reserve(
        points.size()
    );

    for (
        const Point& point :
        points
    ) {

        intercepts.push_back(
            point.value -
            fit.slope *
                point.y
        );
    }

    fit.intercept =
        median_value(
            std::move(
                intercepts
            )
        );

    if (
        !std::isfinite(
            fit.slope
        ) ||
        !std::isfinite(
            fit.intercept
        )
    ) {

        return fit;
    }

    if (
        axis == YAxisKind::RIGHT
    ) {

        const double first =
            fit.slope *
                centers.front() +
            fit.intercept;

        const double last =
            fit.slope *
                centers.back() +
            fit.intercept;

        if (
            first <
                -5.0 ||
            first >
                Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT + 5.0 ||
            last <
                -5.0 ||
            last >
                Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT + 5.0
        ) {

            return fit;
        }
    }

    fit.anchors =
        points.size();

    fit.valid =
        true;

    return fit;
}

// =============================================================================
// FORMAT RECOVERED Y-AXIS VALUE
// =============================================================================

[[nodiscard]]
std::string
format_recovered_y_axis_value(
    double value,
    YAxisKind axis,
    const std::vector<DetectedLabel>& existing_results
) {

    if (
        !std::isfinite(value)
    ) {

        return {};
    }

    if (
        std::abs(value) < 0.05
    ) {

        value =
            0.0;
    }

    char formatted[64] = {};

    if (
        axis == YAxisKind::RIGHT
    ) {

        if (
            std::abs(value) < 0.05
        ) {

            return "0%";
        }

        const int written =
            std::snprintf(
                formatted,
                sizeof(formatted),
                "%.1f%%",
                value
            );

        if (
            written <= 0 ||
            static_cast<std::size_t>(
                written
            ) >=
                sizeof(formatted)
        ) {

            return {};
        }

        return std::string(
            formatted,
            static_cast<std::size_t>(
                written
            )
        );
    }

    if (
        std::abs(value) < 0.5
    ) {

        return "$0";
    }

    bool use_k =
        false;

    for (
        const DetectedLabel& result :
        existing_results
    ) {

        if (
            result.label.text.find(
                'k'
            ) !=
            std::string::npos
        ) {

            use_k =
                true;

            break;
        }
    }

    if (
        use_k ||
        std::abs(value) >= 1000.0
    ) {

        const double thousands =
            value /
            1000.0;

        const int written =
            std::snprintf(
                formatted,
                sizeof(formatted),
                "$%.2fk",
                thousands
            );

        if (
            written <= 0 ||
            static_cast<std::size_t>(
                written
            ) >=
                sizeof(formatted)
        ) {

            return {};
        }

        return std::string(
            formatted,
            static_cast<std::size_t>(
                written
            )
        );
    }

    const int written =
        std::snprintf(
            formatted,
            sizeof(formatted),
            "$%.0f",
            value
        );

    if (
        written <= 0 ||
        static_cast<std::size_t>(
            written
        ) >=
            sizeof(formatted)
    ) {

        return {};
    }

    return std::string(
        formatted,
        static_cast<std::size_t>(
            written
        )
    );
}

// =============================================================================
// RECONCILE Y-AXIS VALUES AGAINST SPATIAL GRID
// =============================================================================
//
// Repairs OCR values that contradict the established spatial tick sequence
// and fills candidate ticks where OCR returned nothing.

void reconcile_y_axis_values(
    const std::vector<TextCandidate>& axis_candidates,
    YAxisKind axis,
    std::vector<DetectedLabel>& axis_results
) {

    const YAxisValueFit fit =
        estimate_y_axis_value_fit(
            axis_candidates,
            axis_results,
            axis
        );

    if (
        !fit.valid ||
        fit.anchors <
            MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS
    ) {

        return;
    }

    // -------------------------------------------------------------------------
    // Repair accepted OCR outliers.
    // -------------------------------------------------------------------------

    for (
        DetectedLabel& result :
        axis_results
    ) {

        double current_value =
            0.0;

        if (
            !parse_y_axis_numeric_value(
                result.label.text,
                axis,
                current_value
            )
        ) {

            continue;
        }

        const double center_y =
            (
                static_cast<double>(
                    result.label.min_y
                ) +
                static_cast<double>(
                    result.label.max_y
                )
            ) *
            0.5;

        double predicted =
            fit.slope *
                center_y +
            fit.intercept;

        if (
            axis == YAxisKind::RIGHT
        ) {

            predicted =
                std::clamp(
                    predicted,
                    0.0,
                    Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT
                );
        }

        const double error =
            std::abs(
                current_value -
                predicted
            );

        const double max_error =
            axis == YAxisKind::RIGHT
                ? std::max(
                      Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_RIGHT,
                      std::abs(
                          fit.tick_delta
                      ) *
                          0.55
                  )
                : std::max(
                      Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_LEFT,
                      std::abs(
                          fit.tick_delta
                      ) *
                          0.55
                  );

        if (
            error <=
            max_error
        ) {

            continue;
        }

        const std::string canonical =
            format_recovered_y_axis_value(
                predicted,
                axis,
                axis_results
            );

        if (
            canonical.empty()
        ) {

            continue;
        }

        const std::string original =
            result.label.text;

        result.label.text =
            canonical;

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "y_axis_spatial_value_recovered: "
                "axis=%s "
                "\"%s\" -> \"%s\" "
                "predicted=%.4f "
                "tick_delta=%.4f\n",
                axis == YAxisKind::LEFT
                    ? "left"
                    : "right",
                original.c_str(),
                canonical.c_str(),
                predicted,
                fit.tick_delta
            );
        }
    }

    // -------------------------------------------------------------------------
    // Recover missing candidate values.
    // -------------------------------------------------------------------------

    std::vector<DetectedLabel> recovered;
    recovered.reserve(
        axis_candidates.size()
    );

    for (
        const TextCandidate& candidate :
        axis_candidates
    ) {

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

        bool already_present =
            false;

        for (
            const DetectedLabel& result :
            axis_results
        ) {

            const double result_center_y =
                (
                    static_cast<double>(
                        result.label.min_y
                    ) +
                    static_cast<double>(
                        result.label.max_y
                    )
                ) *
                0.5;

            if (
                std::abs(
                    result_center_y -
                    candidate_center_y
                ) <=
                fit.tick_spacing * 0.35
            ) {

                already_present =
                    true;

                break;
            }
        }

        if (
            already_present
        ) {

            continue;
        }

        double predicted =
            fit.slope *
                candidate_center_y +
            fit.intercept;

        if (
            axis == YAxisKind::RIGHT
        ) {

            if (
                predicted < -1.0 ||
                predicted >
                    Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT +
                    1.0
            ) {

                continue;
            }

            predicted =
                std::clamp(
                    predicted,
                    0.0,
                    Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT
                );
        }

        const double tick_units =
            std::abs(
                fit.tick_delta
            ) > 1.0e-9
                ? predicted /
                  std::abs(
                      fit.tick_delta
                  )
                : 0.0;

        const double nearest_tick =
            std::round(
                tick_units
            ) *
            std::abs(
                fit.tick_delta
            );

        if (
            std::abs(
                predicted -
                nearest_tick
            ) >
            std::max(
                0.75,
                std::abs(
                    fit.tick_delta
                ) *
                    Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO
            )
        ) {

            continue;
        }

        const std::string recovered_text =
            format_recovered_y_axis_value(
                predicted,
                axis,
                axis_results
            );

        if (
            recovered_text.empty()
        ) {

            continue;
        }

        DetectedLabel recovered_label{};

        recovered_label.label.text =
            recovered_text;

        recovered_label.label.min_x =
            candidate.min_x;

        recovered_label.label.min_y =
            candidate.min_y;

        recovered_label.label.max_x =
            candidate.max_x;

        recovered_label.label.max_y =
            candidate.max_y;

        recovered_label.label.kind =
            chart::ChartLabelKind::UNKNOWN;

        recovered_label.label.series_index =
            -1;

        recovered_label.label.category_index =
            -1;

        recovered_label.label.confidence =
            static_cast<float>(
                std::clamp(
                    Y_AXIS_VALUE_RECOVERY_MIN_SPATIAL_SUPPORT,
                    0.0,
                    1.0
                )
            );

        const int candidate_width =
            candidate.max_x -
            candidate.min_x +
            1;

        const int candidate_height =
            candidate.max_y -
            candidate.min_y +
            1;

        const std::size_t area =
            candidate_width > 0 &&
            candidate_height > 0
                ? static_cast<std::size_t>(
                      candidate_width
                  ) *
                  static_cast<std::size_t>(
                      candidate_height
                  )
                : 0u;

        recovered_label.density =
            area > 0u
                ? static_cast<float>(
                      static_cast<double>(
                          candidate.active_pixels
                      ) /
                      static_cast<double>(
                          area
                      )
                  )
                : 0.0f;

        recovered_label.glyph_count =
            count_glyphs(
                recovered_text
            );

        recovered.push_back(
            std::move(
                recovered_label
            )
        );

        if constexpr (
            CHART_LABEL_DEBUG
        ) {

            std::fprintf(
                stderr,
                "[ChartLabelRecognizer] "
                "y_axis_missing_value_recovered: "
                "axis=%s "
                "x=%d..%d y=%d..%d "
                "text=\"%s\" "
                "predicted=%.4f\n",
                axis == YAxisKind::LEFT
                    ? "left"
                    : "right",
                candidate.min_x,
                candidate.max_x,
                candidate.min_y,
                candidate.max_y,
                recovered_text.c_str(),
                predicted
            );
        }
    }

    for (
        DetectedLabel& item :
        recovered
    ) {

        axis_results.push_back(
            std::move(
                item
            )
        );
    }

    std::sort(
        axis_results.begin(),
        axis_results.end(),
        [](
            const DetectedLabel& a,
            const DetectedLabel& b
        ) noexcept {

            if (
                a.label.min_y !=
                b.label.min_y
            ) {

                return
                    a.label.min_y <
                    b.label.min_y;
            }

            return
                a.label.min_x <
                b.label.min_x;
        }
    );
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

    std::size_t left_y_axis_zone_candidates =
        0u;

    std::size_t right_y_axis_zone_candidates =
        0u;

    std::size_t category_zone_candidates =
        0u;

    std::size_t title_zone_candidates =
        0u;

    std::size_t plot_zone_candidates =
        0u;

    std::size_t outside_zone_candidates =
        0u;

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

            case LabelZone::LEFT_Y_AXIS:
                ++left_y_axis_zone_candidates;
                break;

            case LabelZone::RIGHT_Y_AXIS:
                ++right_y_axis_zone_candidates;
                break;

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
            "left_y=%zu "
            "right_y=%zu "
            "category=%zu "
            "title=%zu "
            "plot=%zu "
            "outside=%zu\n",
            left_y_axis_zone_candidates,
            right_y_axis_zone_candidates,
            category_zone_candidates,
            title_zone_candidates,
            plot_zone_candidates,
            outside_zone_candidates
        );
    }

    std::vector<DetectedLabel> detected;

    detected.reserve(
        std::min(
            MAX_LABELS,
            candidates.size()
        )
    );

    // =========================================================================
    // STEP 4: CATEGORY PIPELINE
    // =========================================================================

    std::vector<TextCandidate> category_candidates;

    std::size_t recovered_count =
        0u;

    CategorySequenceStats final_sequence_stats{};

    if (
        category_zone_candidates > 0u
    ) {

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
            !category_candidates.empty()
        ) {

            const CategorySequenceStats initial_sequence_stats =
                build_category_sequence_stats(
                    category_candidates
                );

            // -----------------------------------------------------------------
            // Gap recovery remains category-specific.
            // -----------------------------------------------------------------

            const std::vector<TextCandidate> recovered_category_candidates =
                recover_missing_category_candidates(
                    chart_buffer,
                    width,
                    height,
                    category_candidates,
                    initial_sequence_stats,
                    MIN_FOREGROUND
                );

            for (
                const TextCandidate& recovered_candidate :
                recovered_category_candidates
            ) {

                if (
                    category_candidates.size() >=
                    static_cast<std::size_t>(
                        MAX_CANDIDATE_COMPONENTS
                    )
                ) {

                    break;
                }

                bool duplicate_geometry =
                    false;

                const double recovered_center_x =
                    (
                        static_cast<double>(
                            recovered_candidate.min_x
                        ) +
                        static_cast<double>(
                            recovered_candidate.max_x
                        )
                    ) *
                    0.5;

                for (
                    const TextCandidate& existing :
                    category_candidates
                ) {

                    const double existing_center_x =
                        (
                            static_cast<double>(
                                existing.min_x
                            ) +
                            static_cast<double>(
                                existing.max_x
                            )
                        ) *
                        0.5;

                    if (
                        std::abs(
                            existing_center_x -
                            recovered_center_x
                        ) <
                        initial_sequence_stats.median_center_spacing *
                        0.35
                    ) {

                        duplicate_geometry =
                            true;

                        break;
                    }
                }

                if (
                    duplicate_geometry
                ) {

                    continue;
                }

                category_candidates.push_back(
                    recovered_candidate
                );

                ++recovered_count;
            }

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "category_gap_recovery: "
                    "recovered=%zu "
                    "candidates_after_recovery=%zu\n",
                    recovered_count,
                    category_candidates.size()
                );
            }

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
                    "median_spacing=%.2f "
                    "recovered=%zu\n",
                    sequence_stats.candidate_count,
                    sequence_stats.coherent_count,
                    sequence_stats.median_center_y,
                    sequence_stats.median_height,
                    sequence_stats.median_width,
                    sequence_stats.median_center_spacing,
                    recovered_count
                );
            }

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
                !category_candidates.empty()
            ) {

                final_sequence_stats =
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

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    for (
                        std::size_t i = 0;
                        i < category_candidates.size() &&
                        i < 64u;
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
                            "zone=%.4f sequence=%.4f\n",
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

                std::size_t rejected_by_ocr =
                    0u;

                std::size_t rejected_by_duplicate =
                    0u;

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
                                "zone=%.4f sequence=%.4f "
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
                            "confidence=%.4f density=%.4f "
                            "glyphs=%zu zone=%.4f sequence=%.4f\n",
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

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    std::fprintf(
                        stderr,
                        "[ChartLabelRecognizer] "
                        "category_recognition_summary: "
                        "candidates=%zu detected=%zu "
                        "rejected_by_ocr=%zu "
                        "rejected_by_duplicate=%zu\n",
                        category_candidates.size(),
                        detected.size(),
                        rejected_by_ocr,
                        rejected_by_duplicate
                    );
                }
            }
        }
    }

    // =========================================================================
    // STEP 5: LEFT / RIGHT Y-AXIS CANDIDATE POOLS
    // =========================================================================

    std::vector<TextCandidate> left_y_axis_candidates;
    std::vector<TextCandidate> right_y_axis_candidates;

    left_y_axis_candidates.reserve(
        left_y_axis_zone_candidates
    );

    right_y_axis_candidates.reserve(
        right_y_axis_zone_candidates
    );

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

        if (
            zone ==
            LabelZone::LEFT_Y_AXIS
        ) {

            const double score =
                y_axis_candidate_score(
                    candidate,
                    width,
                    height,
                    YAxisKind::LEFT
                );

            if (
                score >=
                MIN_Y_AXIS_SCORE
            ) {

                left_y_axis_candidates.push_back(
                    candidate
                );
            }
        }

        if (
            zone ==
            LabelZone::RIGHT_Y_AXIS
        ) {

            const double score =
                y_axis_candidate_score(
                    candidate,
                    width,
                    height,
                    YAxisKind::RIGHT
                );

            if (
                score >=
                MIN_Y_AXIS_SCORE
            ) {

                right_y_axis_candidates.push_back(
                    candidate
                );
            }
        }
    }

    // =========================================================================
    // STEP 5A: CONSOLIDATE Y-AXIS FRAGMENTS BEFORE SEQUENCE MODELING
    // =========================================================================
    //
    // This is the critical fix for labels that were previously visible as
    // multiple fragments at the same Y position. Sequence statistics must see
    // one candidate per tick, not one candidate per glyph fragment.
    //
    // =========================================================================

    left_y_axis_candidates =
        consolidate_y_axis_fragments(
            chart_buffer,
            width,
            height,
            left_y_axis_candidates,
            MIN_FOREGROUND
        );

    right_y_axis_candidates =
        consolidate_y_axis_fragments(
            chart_buffer,
            width,
            height,
            right_y_axis_candidates,
            MIN_FOREGROUND
        );

    if constexpr (
        CHART_LABEL_DEBUG
    ) {

        std::fprintf(
            stderr,
            "[ChartLabelRecognizer] "
            "y_axis_fragment_consolidation: "
            "left=%zu right=%zu\n",
            left_y_axis_candidates.size(),
            right_y_axis_candidates.size()
        );
    }

    auto process_y_axis =
        [
            &
        ](
            std::vector<TextCandidate>& axis_candidates,
            YAxisKind axis,
            const char* axis_name
        ) {

            if (
                axis_candidates.empty() ||
                detected.size() >=
                    MAX_LABELS
            ) {

                return;
            }

            const YAxisSequenceStats sequence_stats =
                build_y_axis_sequence_stats(
                    axis_candidates
                );

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "%s_y_axis_sequence_stats: "
                    "count=%zu coherent=%zu "
                    "median_x=%.2f "
                    "median_height=%.2f "
                    "median_spacing=%.2f\n",
                    axis_name,
                    sequence_stats.candidate_count,
                    sequence_stats.coherent_count,
                    sequence_stats.median_center_x,
                    sequence_stats.median_height,
                    sequence_stats.median_center_spacing
                );
            }

            std::sort(
                axis_candidates.begin(),
                axis_candidates.end(),
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

                    return
                        a.min_x <
                        b.min_x;
                }
            );

            std::size_t axis_detected =
                0u;

            std::vector<DetectedLabel> axis_results;

            axis_results.reserve(
                axis_candidates.size()
            );

            for (
                const TextCandidate& candidate :
                axis_candidates
            ) {

                if (
                    detected.size() >=
                    MAX_LABELS
                ) {

                    break;
                }

                const double spatial_score =
                    y_axis_candidate_score(
                        candidate,
                        width,
                        height,
                        axis
                    );

                const double sequence_score =
                    y_axis_sequence_score(
                        candidate,
                        axis_candidates,
                        sequence_stats
                    );

                if (
                    sequence_stats.coherent_count >=
                        MIN_Y_AXIS_SEQUENCE_SIZE &&
                    sequence_score <
                        MIN_Y_AXIS_SEQUENCE_SCORE
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        std::fprintf(
                            stderr,
                            "[ChartLabelRecognizer] "
                            "%s_y_axis_candidate_rejected: "
                            "x=%d..%d y=%d..%d "
                            "spatial=%.4f sequence=%.4f "
                            "reason=sequence\n",
                            axis_name,
                            candidate.min_x,
                            candidate.max_x,
                            candidate.min_y,
                            candidate.max_y,
                            spatial_score,
                            sequence_score
                        );
                    }

                    continue;
                }

                // =====================================================================
                // MULTI-PASS Y-AXIS OCR
                // =====================================================================

                std::string adaptive_original_text;
                std::string text;

                if (
                    !recognize_y_axis_text(
                        line_recognizer_,
                        chart_buffer,
                        width,
                        height,
                        candidate,
                        axis,
                        adaptive_original_text,
                        text
                    )
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        std::fprintf(
                            stderr,
                            "[ChartLabelRecognizer] "
                            "%s_y_axis_candidate_rejected: "
                            "x=%d..%d y=%d..%d "
                            "reason=ocr_empty_or_invalid\n",
                            axis_name,
                            candidate.min_x,
                            candidate.max_x,
                            candidate.min_y,
                            candidate.max_y
                        );
                    }

                    continue;
                }


                // =====================================================================
                // RIGHT-AXIS DECIMAL RECOVERY
                // =====================================================================
                //
                // OCR can collapse decimal points on small percentage labels.
                // Example: 5.5% -> 55%.  Use the already accepted neighbouring
                // ticks to decide whether a power-of-ten shift is justified.
                // =====================================================================

                if (
                    axis == YAxisKind::RIGHT
                ) {

                    double original_numeric_value =
                        0.0;

                    double recovered_numeric_value =
                        0.0;

                    if (
                        recover_right_y_axis_decimal(
                            text,
                            axis_results,
                            candidate.min_y,
                            candidate.max_y,
                            original_numeric_value,
                            recovered_numeric_value
                        )
                    ) {

                        if constexpr (
                            CHART_LABEL_DEBUG
                        ) {

                            std::fprintf(
                                stderr,
                                "[ChartLabelRecognizer] "
                                "right_y_axis_decimal_recovered: "
                                "\"%.10g%%\" -> \"%s\" "
                                "predicted=%.6f\n",
                                original_numeric_value,
                                text.c_str(),
                                recovered_numeric_value
                            );
                        }
                    }
                }

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    if (
                        text !=
                        adaptive_original_text
                    ) {

                        std::fprintf(
                            stderr,
                            "[ChartLabelRecognizer] "
                            "%s_y_axis_canonicalized: "
                            "\"%s\" -> \"%s\"\n",
                            axis_name,
                            adaptive_original_text.c_str(),
                            text.c_str()
                        );
                    }
                }

                const std::size_t glyph_count =
                    count_glyphs(
                        text
                    );

                if (
                    glyph_count == 0u ||
                    glyph_count >
                        MAX_Y_AXIS_TEXT_LENGTH
                ) {

                    continue;
                }

                // Y-axis ticks are numeric. Do not apply the category-axis
                // three-glyph alpha rules here; '$0' and '0%' are legitimate.
                if (
                    !acceptable_label_text(
                        text,
                        glyph_count
                    )
                ) {

                    // acceptable_label_text() intentionally rejects some pure
                    // numeric fragments, so the dedicated validator above is
                    // the authoritative Y-axis gate.
                }

                const int candidate_width =
                    candidate.max_x -
                    candidate.min_x +
                    1;

                const int candidate_height =
                    candidate.max_y -
                    candidate.min_y +
                    1;

                const std::size_t candidate_area =
                    static_cast<std::size_t>(
                        candidate_width
                    ) *
                    static_cast<std::size_t>(
                        candidate_height
                    );

                if (
                    candidate_area == 0u
                ) {

                    continue;
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

                    continue;
                }

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

                    continue;
                }

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

                label.confidence =
                    static_cast<float>(
                        std::clamp(
                            static_cast<double>(
                                confidence
                            ) *
                                0.70 +
                            spatial_score *
                                0.15 +
                            sequence_score *
                                0.15,
                            0.0,
                            1.0
                        )
                    );

                DetectedLabel y_label{};

                y_label.label =
                    std::move(
                        label
                    );

                y_label.density =
                    density;

                y_label.glyph_count =
                    glyph_count;

                bool duplicate =
                    false;

                for (
                    const DetectedLabel& existing :
                    detected
                ) {

                    if (
                        existing.label.text ==
                        y_label.label.text &&
                        vertical_overlap(
                            existing.label.min_y,
                            existing.label.max_y,
                            y_label.label.min_y,
                            y_label.label.max_y
                        ) &&
                        horizontal_overlap(
                            existing.label.min_x,
                            existing.label.max_x,
                            y_label.label.min_x,
                            y_label.label.max_x
                        )
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

                if constexpr (
                    CHART_LABEL_DEBUG
                ) {

                    std::fprintf(
                        stderr,
                        "[ChartLabelRecognizer] "
                        "%s_y_axis_OCR_accepted: "
                        "x=%d..%d y=%d..%d "
                        "text=\"%s\" "
                        "confidence=%.4f density=%.4f "
                        "glyphs=%zu spatial=%.4f "
                        "sequence=%.4f\n",
                        axis_name,
                        candidate.min_x,
                        candidate.max_x,
                        candidate.min_y,
                        candidate.max_y,
                        y_label.label.text.c_str(),
                        static_cast<double>(
                            y_label.label.confidence
                        ),
                        static_cast<double>(
                            y_label.density
                        ),
                        y_label.glyph_count,
                        spatial_score,
                        sequence_score
                    );
                }

                axis_results.push_back(
                    std::move(
                        y_label
                    )
                );
            }

            // =====================================================================
            // LEFT-AXIS ISOLATED NUMERIC GUARD
            // =====================================================================
            //
            // Require at least two independently recognized numeric left-axis
            // ticks before publishing any left-axis result. This removes an
            // isolated OCR hallucination such as "$645" when the surrounding
            // left-axis candidates did not produce valid values.
            // =====================================================================

            if (
                axis == YAxisKind::LEFT
            ) {

                std::size_t numeric_support =
                    0u;

                for (
                    const DetectedLabel& axis_result :
                    axis_results
                ) {

                    double value =
                        0.0;

                    if (
                        parse_y_axis_numeric_value(
                            axis_result.label.text,
                            axis,
                            value
                        )
                    ) {

                        ++numeric_support;
                    }
                }

                if (
                    numeric_support <
                    MIN_LEFT_Y_NUMERIC_SUPPORT
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        for (
                            const DetectedLabel& axis_result :
                            axis_results
                        ) {

                            std::fprintf(
                                stderr,
                                "[ChartLabelRecognizer] "
                                "%s_y_axis_isolated_numeric_rejected: "
                                "x=%d..%d y=%d..%d "
                                "text=\"%s\" "
                                "numeric_support=%zu "
                                "required=%zu "
                                "reason=insufficient_sequence_support\n",
                                axis_name,
                                axis_result.label.min_x,
                                axis_result.label.max_x,
                                axis_result.label.min_y,
                                axis_result.label.max_y,
                                axis_result.label.text.c_str(),
                                numeric_support,
                                MIN_LEFT_Y_NUMERIC_SUPPORT
                            );
                        }
                    }

                    axis_results.clear();
                }
            }

            // =====================================================================
            // =========================================================================
            // SPATIAL Y-AXIS VALUE RECONCILIATION
            // =========================================================================
            //
            // Use the established tick grid before final outlier filtering.
            // This fills OCR gaps, including zero ticks, and repairs spatially
            // inconsistent values such as right-axis "55%" -> "22.0%".
            // =========================================================================

            reconcile_y_axis_values(
                axis_candidates,
                axis,
                axis_results
            );

            // ROBUST NUMERIC SEQUENCE VALIDATION
            // =====================================================================

            const std::vector<bool> inlier_mask =
                y_axis_sequence_inlier_mask(
                    axis_results,
                    axis
                );

            for (
                std::size_t i = 0u;
                i < axis_results.size();
                ++i
            ) {

                if (
                    !inlier_mask[i]
                ) {

                    if constexpr (
                        CHART_LABEL_DEBUG
                    ) {

                        double value =
                            0.0;

                        if (
                            parse_y_axis_numeric_value(
                                axis_results[i].label.text,
                                axis,
                                value
                            )
                        ) {

                            std::fprintf(
                                stderr,
                                "[ChartLabelRecognizer] "
                                "%s_y_axis_outlier_rejected: "
                                "x=%d..%d y=%d..%d "
                                "text=\"%s\" value=%.6f "
                                "reason=sequence_outlier\n",
                                axis_name,
                                axis_results[i].label.min_x,
                                axis_results[i].label.max_x,
                                axis_results[i].label.min_y,
                                axis_results[i].label.max_y,
                                axis_results[i].label.text.c_str(),
                                value
                            );
                        }
                    }

                    continue;
                }

                detected.push_back(
                    std::move(
                        axis_results[i]
                    )
                );

                ++axis_detected;
            }

            if constexpr (
                CHART_LABEL_DEBUG
            ) {

                std::fprintf(
                    stderr,
                    "[ChartLabelRecognizer] "
                    "%s_y_axis_summary: "
                    "candidates=%zu detected=%zu\n",
                    axis_name,
                    axis_candidates.size(),
                    axis_detected
                );
            }
        };

    process_y_axis(
        left_y_axis_candidates,
        YAxisKind::LEFT,
        "left"
    );

    process_y_axis(
        right_y_axis_candidates,
        YAxisKind::RIGHT,
        "right"
    );

    // =========================================================================
    // STEP 6: SORT FINAL LABELS
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
    // STEP 7: PUBLIC OUTPUT
    // =========================================================================

    labels.reserve(
        std::min(
            detected.size(),
            MAX_LABELS
        )
    );

    for (
        std::size_t i = 0u;
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
            "left_y_candidates=%zu "
            "right_y_candidates=%zu "
            "category_candidates=%zu "
            "detected=%zu "
            "final_labels=%zu\n",
            candidates.size(),
            left_y_axis_candidates.size(),
            right_y_axis_candidates.size(),
            category_candidates.size(),
            detected.size(),
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
