#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr {

class PdfRasterizer {
public:
    [[nodiscard]]
    static bool compute_ocr_size(
        double page_width_points,
        double page_height_points,
        int public_width,
        int public_height,
        int& ocr_width,
        int& ocr_height
    ) noexcept;

    [[nodiscard]]
    static bool render_page_to_bgra(
        const uint8_t* pdf_bytes,
        std::size_t pdf_len,
        uint8_t* target,
        std::size_t max_bytes,
        int width,
        int height
    );
};

} // namespace fin_ocr
