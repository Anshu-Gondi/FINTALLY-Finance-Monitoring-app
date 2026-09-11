#pragma once

#include <cstddef>
#include <cstdint>

#include "ffi_bridge.h"

namespace fin_ocr {

class VisionPipeline {
public:
    [[nodiscard]]
    static FinProcessedBuffer* execute(
        const uint8_t* input_bytes,
        std::size_t input_len,
        FinInputType input_type,
        std::size_t target_width,
        std::size_t target_height,
        std::size_t target_channels
    );
};

} // namespace fin_ocr
