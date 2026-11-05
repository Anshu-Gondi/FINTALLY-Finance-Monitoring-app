#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr {

class ChartColorIsolator {
public:
    [[nodiscard]]
    static bool isolate(
        const uint8_t* rgb,
        uint8_t* output,
        std::size_t num_pixels
    ) noexcept;
};

} // namespace fin_ocr
