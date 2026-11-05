#include "fin_ocr/pdf/pdf_page.hpp"

#include <cstddef>
#include <cstdint>
#include <climits>

#include <fpdfview.h>

namespace fin_ocr {

// =============================================================================
// PDF PAGE DIMENSIONS
// =============================================================================

bool PdfPage::get_dimensions(
    const uint8_t* pdf_bytes,
    std::size_t pdf_len,
    PdfPageDimensions& dimensions
) {

    dimensions.width_points =
        0.0;

    dimensions.height_points =
        0.0;

    if (
        pdf_bytes == nullptr ||
        pdf_len == 0
    ) {
        return false;
    }

    /*
     * PDFium's in-memory loader takes an int length.
     * Do not narrow a size_t that cannot fit.
     */
    if (
        pdf_len >
        static_cast<std::size_t>(
            INT_MAX
        )
    ) {
        return false;
    }

    FPDF_InitLibrary();

    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(
            pdf_bytes,
            static_cast<int>(pdf_len),
            nullptr
        );

    if (
        document == nullptr
    ) {

        FPDF_DestroyLibrary();

        return false;
    }

    FPDF_PAGE page =
        FPDF_LoadPage(
            document,
            0
        );

    if (
        page == nullptr
    ) {

        FPDF_CloseDocument(
            document
        );

        FPDF_DestroyLibrary();

        return false;
    }

    dimensions.width_points =
        FPDF_GetPageWidth(
            page
        );

    dimensions.height_points =
        FPDF_GetPageHeight(
            page
        );

    FPDF_ClosePage(
        page
    );

    FPDF_CloseDocument(
        document
    );

    FPDF_DestroyLibrary();

    return
        dimensions.width_points > 0.0 &&
        dimensions.height_points > 0.0;
}

} // namespace fin_ocr
