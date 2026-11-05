#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::core {

void copy_raw_fallback(
    const uint8_t* input,
    std::size_t input_len,
    uint8_t* output,
    std::size_t output_len
) noexcept;

} // namespace fin_ocr::core
