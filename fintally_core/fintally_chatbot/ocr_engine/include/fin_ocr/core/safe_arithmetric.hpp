#pragma once

#include <cstddef>

namespace fin_ocr::core {

[[nodiscard]]
bool safe_mul(
    std::size_t a,
    std::size_t b,
    std::size_t& out
) noexcept;

[[nodiscard]]
bool safe_add(
    std::size_t a,
    std::size_t b,
    std::size_t& out
) noexcept;

} // namespace fin_ocr::core
