#include "fin_ocr/chart/label/y_axis/y_axis_parser.hpp"

#include "fin_ocr/chart/label/text/text_utils.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// Y-AXIS TEXT LIMIT
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
//
// =============================================================================

constexpr std::size_t MAX_Y_AXIS_TEXT_LENGTH =
    20u;

} // namespace

// =============================================================================
// Y-AXIS OCR TEXT COMPACTION
// =============================================================================

void compact_axis_text(
    std::string& text
)
{
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

// =============================================================================
// Y-AXIS TEXT NORMALIZATION / VALIDATION
// =============================================================================
//
// Normalizes OCR output into the constrained representation expected by the
// downstream numeric parser.
//
// LEFT:
//
//     $123
//     $12.5
//     $12.5k
//
// RIGHT:
//
//     12%
//     12.5%
//
// OCR substitutions are deliberately conservative and only applied in numeric
// contexts.
//
// =============================================================================

[[nodiscard]]
bool normalize_y_axis_text(
    std::string& text,
    YAxisKind axis
) noexcept
{
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

    // =========================================================================
    // CONSERVATIVE OCR SUBSTITUTIONS
    // =========================================================================

    for (
        std::size_t i = 0;
        i < text.size();
        ++i
    ) {

        char& c =
            text[i];

        // ---------------------------------------------------------------------
        // O / o -> 0
        // ---------------------------------------------------------------------

        if (
            c == 'O' ||
            c == 'o'
        ) {

            const bool numeric_context =
                (
                    i > 0u &&
                    (
                        ::fin_ocr::chart::label::text::
                            is_ascii_digit(
                                text[i - 1u]
                            ) ||
                        text[i - 1u] == '.'
                    )
                ) ||
                (
                    i + 1u < text.size() &&
                    (
                        ::fin_ocr::chart::label::text::
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

        // ---------------------------------------------------------------------
        // I / l -> 1
        // ---------------------------------------------------------------------

        if (
            c == 'I' ||
            c == 'l'
        ) {

            const bool numeric_context =
                (
                    i > 0u &&
                    (
                        ::fin_ocr::chart::label::text::
                            is_ascii_digit(
                                text[i - 1u]
                            ) ||
                        text[i - 1u] == '.'
                    )
                ) ||
                (
                    i + 1u < text.size() &&
                    (
                        ::fin_ocr::chart::label::text::
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

        // ---------------------------------------------------------------------
        // K -> k
        // ---------------------------------------------------------------------

        if (
            c == 'K'
        ) {
            c =
                'k';
        }
    }

    // =========================================================================
    // LEFT-AXIS CURRENCY OCR RECOVERY
    // =========================================================================
    //
    // A leading S/s is a common OCR substitution for '$'.
    //
    // Apply only to the left currency axis and only when followed by a digit.
    // =========================================================================

    if (
        axis == YAxisKind::LEFT &&
        text.size() >= 2u &&
        (
            text[0] == 'S' ||
            text[0] == 's'
        ) &&
        ::fin_ocr::chart::label::text::
            is_ascii_digit(
                text[1]
            )
    ) {

        text[0] =
            '$';
    }

    // =========================================================================
    // LEFT AXIS
    // =========================================================================

    if (
        axis == YAxisKind::LEFT
    ) {

        std::size_t i =
            0u;

        // Optional currency marker.
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
                ::fin_ocr::chart::label::text::
                    is_ascii_digit(
                        c
                    )
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

                // k is only valid as the final suffix.
                if (
                    i + 1u !=
                    text.size()
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

        // =========================================================================
        // EXPLICIT LEFT CURRENCY NORMALIZATION
        // =========================================================================
        //
        // The left-axis contract is currency/numeric. Preserve the legacy
        // behavior of making the '$' marker explicit.
        // =========================================================================

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

    // =========================================================================
    // RIGHT AXIS
    // =========================================================================

    if (
        axis == YAxisKind::RIGHT
    ) {

        if (
            text.empty()
        ) {
            return false;
        }

        // Accept either raw numeric OCR or an already recognized '%' suffix.
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
                ::fin_ocr::chart::label::text::
                    is_ascii_digit(
                        c
                    )
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
// PARSE NORMALIZED Y-AXIS VALUE
// =============================================================================
//
// Converts:
//
//     $123
//     $123.5
//     $12.5k
//     55%
//     55.5%
//
// into numeric values.
//
// =============================================================================

[[nodiscard]]
bool parse_y_axis_numeric_value(
    const std::string& text,
    YAxisKind axis,
    double& value
) noexcept
{
    if (
        text.empty()
    ) {
        return false;
    }

    std::size_t begin =
        0u;

    std::size_t end =
        text.size();

    // =========================================================================
    // LEFT AXIS
    // =========================================================================

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

    // =========================================================================
    // RIGHT AXIS
    // =========================================================================

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
            ::fin_ocr::chart::label::text::
                is_ascii_digit(
                    c
                )
        ) {

            const int digit =
                c -
                '0';

            if (
                seen_decimal
            ) {

                fractional_part +=
                    static_cast<double>(
                        digit
                    ) *
                    fractional_scale;

                fractional_scale *=
                    0.1;

            } else {

                integer_part =
                    integer_part *
                    10.0 +
                    static_cast<double>(
                        digit
                    );
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

    // =========================================================================
    // THOUSANDS SUFFIX
    // =========================================================================

    if (
        axis == YAxisKind::LEFT &&
        end < text.size() &&
        text[end] == 'k'
    ) {

        value *=
            1000.0;
    }

    return
        std::isfinite(
            value
        );
}

} // namespace fin_ocr::chart::label::y_axis
