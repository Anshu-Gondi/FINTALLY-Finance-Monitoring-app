#pragma once

#include <cstddef>
#include <string>

namespace fin_ocr::chart::label::text {

// =============================================================================
// NORMALIZATION
// =============================================================================

void trim_text(
    std::string& text
);

// =============================================================================
// ASCII HELPERS
// =============================================================================

[[nodiscard]]
bool is_ascii_alpha(
    char c
) noexcept;

[[nodiscard]]
bool is_ascii_digit(
    char c
) noexcept;

[[nodiscard]]
bool is_ascii_punctuation(
    char c
) noexcept;

// =============================================================================
// GLYPH COUNT
// =============================================================================

[[nodiscard]]
std::size_t count_glyphs(
    const std::string& text
) noexcept;

// =============================================================================
// NOISE
// =============================================================================

[[nodiscard]]
bool is_repeated_glyph_noise(
    const std::string& text
) noexcept;

} // namespace fin_ocr::chart::label::text
