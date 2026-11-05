#include "fin_ocr/core/buffer_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fin_ocr::core {

// =============================================================================
// RAW-BUFFER FALLBACK
//
// Copies as much input as fits into the destination buffer.
//
// Behavior:
//
//     input_len < output_len
//         -> copy input
//         -> zero-fill remaining bytes
//
//     input_len >= output_len
//         -> copy exactly output_len bytes
//
// Invalid/null buffers are ignored.
//
// This preserves the behavior of the legacy vision pipeline.
// =============================================================================

void copy_raw_fallback(
    const uint8_t* input,
    std::size_t input_len,
    uint8_t* output,
    std::size_t output_len
) noexcept {

    if (
        input == nullptr ||
        output == nullptr ||
        output_len == 0
    ) {
        return;
    }

    const std::size_t copy_size =
        std::min(
            input_len,
            output_len
        );

    // =========================================================================
    // COPY AVAILABLE INPUT
    // =========================================================================

    if (
        copy_size > 0
    ) {

        std::memcpy(
            output,
            input,
            copy_size
        );
    }

    // =========================================================================
    // ZERO-FILL REMAINING OUTPUT
    // =========================================================================

    if (
        copy_size < output_len
    ) {

        std::memset(
            output + copy_size,
            0,
            output_len - copy_size
        );
    }
}

} // namespace fin_ocr::core
