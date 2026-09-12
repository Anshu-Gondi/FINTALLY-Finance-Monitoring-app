#include "fin_ocr/pdf/pdf_rasterizer.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/safe_arithmetric.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fpdfview.h>

namespace fin_ocr {

// =============================================================================
// COMPUTE PDF OCR RASTER SIZE
//
// PDF points -> pixels:
//
//     pixels = points * (DPI / 72)
//
// The OCR raster is capped by PDF_MAX_OCR_PIXELS to prevent pathological
// documents from allocating excessive memory.
// =============================================================================

bool PdfRasterizer::compute_ocr_size(
    double page_width_points,
    double page_height_points,
    int public_width,
    int public_height,
    int& ocr_width,
    int& ocr_height
) noexcept {

    ocr_width =
        std::max(
            1,
            public_width
        );

    ocr_height =
        std::max(
            1,
            public_height
        );

    if (
        page_width_points <= 0.0 ||
        page_height_points <= 0.0
    ) {
        return false;
    }

    const double scale =
        config::PDF_OCR_DPI /
        72.0;

    const double requested_width =
        std::ceil(
            page_width_points *
            scale
        );

    const double requested_height =
        std::ceil(
            page_height_points *
            scale
        );

    if (
        !std::isfinite(requested_width) ||
        !std::isfinite(requested_height) ||
        requested_width <= 0.0 ||
        requested_height <= 0.0
    ) {
        return false;
    }

    if (
        requested_width >
        static_cast<double>(
            std::numeric_limits<int>::max()
        ) ||
        requested_height >
        static_cast<double>(
            std::numeric_limits<int>::max()
        )
    ) {
        return false;
    }

    const std::size_t requested_width_size =
        static_cast<std::size_t>(
            requested_width
        );

    const std::size_t requested_height_size =
        static_cast<std::size_t>(
            requested_height
        );

    std::size_t requested_pixels = 0;

    if (
        !core::safe_mul(
            requested_width_size,
            requested_height_size,
            requested_pixels
        )
    ) {
        return false;
    }

    double factor = 1.0;

    if (
        requested_pixels >
        config::PDF_MAX_OCR_PIXELS
    ) {

        factor =
            std::sqrt(
                static_cast<double>(
                    config::PDF_MAX_OCR_PIXELS
                ) /
                static_cast<double>(
                    requested_pixels
                )
            );
    }

    ocr_width =
        std::max(
            config::PDF_MIN_OCR_WIDTH,
            static_cast<int>(
                std::floor(
                    requested_width *
                    factor
                )
            )
        );

    ocr_height =
        std::max(
            config::PDF_MIN_OCR_HEIGHT,
            static_cast<int>(
                std::floor(
                    requested_height *
                    factor
                )
            )
        );

    std::size_t final_pixels = 0;

    if (
        !core::safe_mul(
            static_cast<std::size_t>(
                ocr_width
            ),
            static_cast<std::size_t>(
                ocr_height
            ),
            final_pixels
        )
    ) {
        return false;
    }

    if (
        final_pixels >
        config::PDF_MAX_OCR_PIXELS
    ) {

        const double reduce =
            std::sqrt(
                static_cast<double>(
                    config::PDF_MAX_OCR_PIXELS
                ) /
                static_cast<double>(
                    final_pixels
                )
            );

        ocr_width =
            std::max(
                1,
                static_cast<int>(
                    std::floor(
                        static_cast<double>(
                            ocr_width
                        ) *
                        reduce
                    )
                )
            );

        ocr_height =
            std::max(
                1,
                static_cast<int>(
                    std::floor(
                        static_cast<double>(
                            ocr_height
                        ) *
                        reduce
                    )
                )
            );
    }

    return
        ocr_width > 0 &&
        ocr_height > 0;
}

// =============================================================================
// PDF PAGE -> BGRA
// =============================================================================

bool PdfRasterizer::render_page_to_bgra(
    const uint8_t* pdf_bytes,
    std::size_t pdf_len,
    uint8_t* target,
    std::size_t max_bytes,
    int width,
    int height
) {

    if (
        pdf_bytes == nullptr ||
        target == nullptr ||
        pdf_len == 0 ||
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    if (
        pdf_len >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        )
    ) {
        return false;
    }

    std::size_t pixels = 0;
    std::size_t required_bytes = 0;

    if (
        !core::safe_mul(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            pixels
        )
    ) {
        return false;
    }

    if (
        !core::safe_mul(
            pixels,
            std::size_t{4},
            required_bytes
        )
    ) {
        return false;
    }

    if (
        required_bytes >
        max_bytes
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

    /*
     * Pdfium's BGRA bitmap uses a caller-provided buffer.
     *
     * stride = width * 4 bytes.
     */
    const int stride =
        width * 4;

    FPDF_BITMAP bitmap =
        FPDFBitmap_CreateEx(
            width,
            height,
            FPDFBitmap_BGRA,
            target,
            stride
        );

    if (
        bitmap == nullptr
    ) {

        FPDF_ClosePage(
            page
        );

        FPDF_CloseDocument(
            document
        );

        FPDF_DestroyLibrary();

        return false;
    }

    /*
     * Render onto an opaque white page background.
     */
    FPDFBitmap_FillRect(
        bitmap,
        0,
        0,
        width,
        height,
        0xFFFFFFFF
    );

    FPDF_RenderPageBitmap(
        bitmap,
        page,
        0,
        0,
        width,
        height,
        0,
        0
    );

    FPDFBitmap_Destroy(
        bitmap
    );

    FPDF_ClosePage(
        page
    );

    FPDF_CloseDocument(
        document
    );

    FPDF_DestroyLibrary();

    return true;
}

} // namespace fin_ocr
