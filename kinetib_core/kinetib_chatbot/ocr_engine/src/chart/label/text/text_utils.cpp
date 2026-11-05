#include "fin_ocr/chart/label/text/text_utils.hpp"

#include <cstddef>
#include <string>

namespace fin_ocr::chart::label::text {

// =============================================================================
// TEXT NORMALIZATION
// =============================================================================

void trim_text(
    std::string& text
)
{
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
) noexcept
{
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
) noexcept
{
    return
        c >= '0' &&
        c <= '9';
}

[[nodiscard]]
bool is_ascii_punctuation(
    char c
) noexcept
{
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
) noexcept
{
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
) noexcept
{
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

} // namespace fin_ocr::chart::label::text
