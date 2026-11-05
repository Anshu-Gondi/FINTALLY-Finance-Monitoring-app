#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr {

struct PdfPageDimensions {
    double width_points = 0.0;
    double height_points = 0.0;
};

class PdfPage {
public:
    [[nodiscard]]
    static bool get_dimensions(
        const uint8_t* pdf_bytes,
        std::size_t pdf_len,
        PdfPageDimensions& dimensions
    );
};

} // namespace fin_ocr
