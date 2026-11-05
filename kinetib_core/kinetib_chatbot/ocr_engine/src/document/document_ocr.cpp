#include "fin_ocr/document/document_ocr.hpp"

#include "fin_ocr/matrix_matcher.hpp"
#include "fin_ocr/tesseract/document_tesseract.hpp"

#include <cstdint>
#include <string>

namespace fin_ocr {

// =============================================================================
// HYBRID DOCUMENT OCR
// =============================================================================
//
// PRIMARY:
//     DocumentTesseract on preserved grayscale.
//
// FALLBACK:
//     MatrixMatcher on binary mask.
//
// IMPORTANT:
//     Tesseract never receives the binary MatrixMatcher representation.
// =============================================================================

std::string DocumentOcr::recognize(
    const uint8_t* grayscale,
    const uint8_t* binary_mask,
    int width,
    int height,
    FinInputType input_type
) const {

    if (
        grayscale == nullptr ||
        width <= 0 ||
        height <= 0
    ) {
        return {};
    }

    // =========================================================================
    // PRIMARY: TESSERACT
    // =========================================================================

    DocumentTesseract tesseract;

    const std::string tesseract_text =
        tesseract.recognize(
            grayscale,
            width,
            height,
            input_type
        );

    if (
        !tesseract_text.empty()
    ) {
        return tesseract_text;
    }

    // =========================================================================
    // FALLBACK: MATRIX MATCHER
    // =========================================================================

    if (
        binary_mask == nullptr
    ) {
        return {};
    }

    MatrixMatcher matcher;

    return matcher.recognize_line(
        binary_mask,
        width,
        0,
        height,
        1
    );
}

} // namespace fin_ocr
