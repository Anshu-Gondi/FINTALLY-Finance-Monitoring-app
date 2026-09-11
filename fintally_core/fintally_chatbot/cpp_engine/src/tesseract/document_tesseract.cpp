#include "fin_ocr/tesseract/document_tesseract.hpp"

#include "fin_ocr/core/ocr_config.hpp"
#include "fin_ocr/core/safe_arithmetric.hpp"
#include "fin_ocr/tesseract/tesseract_engine.hpp"
#include "fin_ocr/tesseract/tesseract_preprocess.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

namespace {

// =============================================================================
// DOCUMENT PAGE-SEGMENTATION MODES
// =============================================================================
//
// These values correspond to Tesseract PageSegMode values:
//
//     3  = PSM_AUTO
//     4  = PSM_SINGLE_COLUMN
//     6  = PSM_SINGLE_BLOCK
//    11  = PSM_SPARSE_TEXT
//    12  = PSM_SPARSE_TEXT_OSD
//    13  = PSM_RAW_LINE
//
// Kept local here so DocumentTesseract does not expose the Tesseract C++ API
// through its public header.
//
// These should eventually move into ocr_config.hpp if you want all OCR policy
// constants centralized.
// =============================================================================

constexpr int PSM_AUTO =
    3;

constexpr int PSM_SINGLE_COLUMN =
    4;

constexpr int PSM_SINGLE_BLOCK =
    6;

constexpr int PSM_SPARSE_TEXT =
    11;

constexpr int PSM_SPARSE_TEXT_OSD =
    12;

constexpr int PSM_RAW_LINE =
    13;

// =============================================================================
// DOCUMENT TESSERACT PASS RESULT
// =============================================================================

struct DocumentPassResult {

    std::string text;

    int confidence =
        0;

    int psm =
        0;

    bool valid =
        false;
};

// =============================================================================
// THREAD-LOCAL DOCUMENT TESSERACT ENGINE
// =============================================================================
//
// TessBaseAPI is stateful.
//
// TesseractEngine owns the actual engine and this layer reuses one engine per
// worker thread instead of constructing/loading Tesseract for every layout
// hypothesis.
//
// This preserves the thread-safety property while avoiding repeated
// initialization costs.
// =============================================================================

TesseractEngine& document_tesseract_engine() {

    static thread_local TesseractEngine engine;

    return engine;
}

// =============================================================================
// SINGLE DOCUMENT TESSERACT PASS
// =============================================================================
//
// DocumentTesseract owns:
//
//     - which PSM hypotheses to evaluate
//     - document candidate bookkeeping
//
// TesseractEngine owns:
//
//     - TessBaseAPI
//     - initialization
//     - SetImage()
//     - SetSourceResolution()
//     - Recognize()
//     - numeric whitelist handling
//     - engine cleanup
//
// This is the important architectural separation.
// =============================================================================

[[nodiscard]]
DocumentPassResult recognize_document_pass(
    TesseractEngine& engine,
    const std::vector<uint8_t>& grayscale,
    int width,
    int height,
    int psm
) {

    DocumentPassResult result;

    result.psm =
        psm;

    if (
        grayscale.empty() ||
        width <= 0 ||
        height <= 0 ||
        !engine.initialized()
    ) {
        return result;
    }

    std::string text;

    float confidence =
        0.0f;

    // -------------------------------------------------------------------------
    // TesseractEngine performs the actual OCR operation.
    //
    // numeric_mode = false because document OCR uses normal semantic
    // recognition. Numeric-specialized logic belongs to line recognition.
    // -------------------------------------------------------------------------

    const bool ok =
        engine.recognize(
            grayscale,
            width,
            height,
            psm,
            false,
            text,
            confidence
        );

    if (
        !ok
    ) {
        return result;
    }

    // -------------------------------------------------------------------------
    // Keep text cleanup centralized in tesseract_preprocess.cpp.
    // -------------------------------------------------------------------------

    if (
        !text.empty()
    ) {

        text =
            clean_tesseract_text(
                text.c_str()
            );
    }

    if (
        text.empty()
    ) {
        return result;
    }

    result.text =
        std::move(text);

    result.confidence =
        std::clamp(
            static_cast<int>(
                confidence
            ),
            0,
            100
        );

    result.valid =
        true;

    return result;
}

// =============================================================================
// DOCUMENT PASS SCORING
// =============================================================================
//
// Confidence remains dominant.
//
// Text density prevents a tiny high-confidence fragment from defeating a more
// complete document result.
// =============================================================================

[[nodiscard]]
double score_document_pass(
    const DocumentPassResult& result
) noexcept {

    if (
        !result.valid ||
        result.text.empty()
    ) {
        return -1.0;
    }

    std::size_t non_whitespace =
        0;

    std::size_t total =
        0;

    for (
        const unsigned char c :
        result.text
    ) {

        ++total;

        if (
            c != ' ' &&
            c != '\n' &&
            c != '\t'
        ) {

            ++non_whitespace;
        }
    }

    if (
        total == 0
    ) {
        return -1.0;
    }

    const double density =
        static_cast<double>(
            non_whitespace
        ) /
        static_cast<double>(
            total
        );

    const double confidence =
        static_cast<double>(
            std::clamp(
                result.confidence,
                0,
                100
            )
        ) /
        100.0;

    return
        confidence * 0.78 +
        density * 0.22;
}

} // namespace

// =============================================================================
// DOCUMENT TESSERACT OCR
// =============================================================================
//
// PDF:
//
//     PSM_AUTO
//     PSM_SPARSE_TEXT
//     PSM_SINGLE_BLOCK
//     PSM_SINGLE_COLUMN
//     PSM_RAW_LINE
//     PSM_SPARSE_TEXT_OSD
//
// Normal image / receipt:
//
//     PSM_AUTO
//     PSM_SINGLE_BLOCK
//
// The best meaningful candidate wins.
// =============================================================================

std::string DocumentTesseract::recognize(
    const uint8_t* grayscale,
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
    // SAFE INPUT SIZE
    // =========================================================================

    std::size_t pixel_count =
        0;

    if (
        !core::safe_mul(
            static_cast<std::size_t>(
                width
            ),
            static_cast<std::size_t>(
                height
            ),
            pixel_count
        )
    ) {
        return {};
    }

    if (
        pixel_count == 0
    ) {
        return {};
    }

    // =========================================================================
    // TESSERACT ENGINE
    // =========================================================================

    TesseractEngine& engine =
        document_tesseract_engine();

    if (
        !engine.initialized()
    ) {
        return {};
    }

    // =========================================================================
    // ADAPT RAW BUFFER TO TESSERACT ENGINE CONTRACT
    // =========================================================================
    //
    // TesseractEngine currently accepts std::vector<uint8_t>.
    //
    // Copy once here.
    //
    // We intentionally do NOT copy once per PSM pass.
    // =========================================================================

    std::vector<uint8_t> grayscale_buffer(
        grayscale,
        grayscale + pixel_count
    );

    if (
        grayscale_buffer.empty()
    ) {
        return {};
    }

    // =========================================================================
    // CANDIDATE PASSES
    // =========================================================================

    std::vector<DocumentPassResult> passes;

    if (
        input_type ==
        FIN_INPUT_PDF_PAGE
    ) {

        passes.reserve(
            6
        );

        // ---------------------------------------------------------------------
        // 1. Automatic page segmentation.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_AUTO
            )
        );

        // ---------------------------------------------------------------------
        // 2. Sparse text.
        //
        // Useful when financial statements contain spatially separated
        // labels, values and table cells.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_SPARSE_TEXT
            )
        );

        // ---------------------------------------------------------------------
        // 3. Single block.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_SINGLE_BLOCK
            )
        );

        // ---------------------------------------------------------------------
        // 4. Single column.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_SINGLE_COLUMN
            )
        );

        // ---------------------------------------------------------------------
        // 5. Raw line.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_RAW_LINE
            )
        );

        // ---------------------------------------------------------------------
        // 6. Sparse text with orientation detection.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_SPARSE_TEXT_OSD
            )
        );

    } else {

        passes.reserve(
            2
        );

        // ---------------------------------------------------------------------
        // Normal image / receipt.
        // ---------------------------------------------------------------------

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_AUTO
            )
        );

        passes.push_back(
            recognize_document_pass(
                engine,
                grayscale_buffer,
                width,
                height,
                PSM_SINGLE_BLOCK
            )
        );
    }

    // =========================================================================
    // SELECT BEST RESULT
    // =========================================================================

    const DocumentPassResult* best =
        nullptr;

    double best_score =
        -1.0;

    for (
        const DocumentPassResult& pass :
        passes
    ) {

        const double score =
            score_document_pass(
                pass
            );

        if (
            score >
            best_score
        ) {

            best_score =
                score;

            best =
                &pass;
        }
    }

    // =========================================================================
    // FINAL RESULT
    // =========================================================================

    if (
        best == nullptr ||
        !best->valid ||
        best->text.empty()
    ) {
        return {};
    }

    return best->text;
}

} // namespace fin_ocr
