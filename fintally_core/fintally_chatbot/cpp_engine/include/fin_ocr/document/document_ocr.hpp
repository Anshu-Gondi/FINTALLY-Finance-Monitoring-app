#pragma once

#include <cstdint>
#include <string>

#include "ffi_bridge.h"

namespace fin_ocr {

class DocumentOcr {
public:
    [[nodiscard]]
    std::string recognize(
        const uint8_t* grayscale,
        const uint8_t* binary_mask,
        int width,
        int height,
        FinInputType input_type
    ) const;
};

} // namespace fin_ocr
