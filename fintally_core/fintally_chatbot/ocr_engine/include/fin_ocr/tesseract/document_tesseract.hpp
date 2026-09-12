#pragma once

#include <cstdint>
#include <string>

#include "ffi_bridge.h"

namespace fin_ocr {

class DocumentTesseract {
public:
    [[nodiscard]]
    std::string recognize(
        const uint8_t* grayscale,
        int width,
        int height,
        FinInputType input_type
    ) const;
};

} // namespace fin_ocr
