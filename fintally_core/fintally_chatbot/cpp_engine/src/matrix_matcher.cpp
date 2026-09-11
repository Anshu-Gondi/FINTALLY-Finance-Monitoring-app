#include "fin_ocr/matrix_matcher.hpp"

#include "fin_ocr/chart/chart_label_recognizer.hpp"
#include "fin_ocr/line/line_recognizer.hpp"
#include "fin_ocr/matrix/glyph_matcher.hpp"
#include "fin_ocr/matrix/glyph_templates.hpp"
#include "fin_ocr/tesseract/tesseract_recognizer.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr {

// =============================================================================
// MATRIXMATCHER IMPLEMENTATION
// =============================================================================
//
// MatrixMatcher is the public facade.
//
// Actual OCR responsibilities are delegated to:
//
//     GlyphMatcher
//     TesseractRecognizer
//     LineRecognizer
//     ChartLabelRecognizer
//
// The canonical glyph database is:
//
//     GlyphTemplateTable
//
// No compatibility std::vector is required anymore.
// =============================================================================

struct MatrixMatcher::Impl {

    // =========================================================================
    // CANONICAL TEMPLATE DATABASE
    // =========================================================================

    const GlyphTemplateTable& templates;

    // =========================================================================
    // OCR SUBSYSTEMS
    // =========================================================================

    GlyphMatcher glyph_matcher;

    TesseractRecognizer tesseract_recognizer;

    LineRecognizer line_recognizer;

    ChartLabelRecognizer chart_label_recognizer;

    // =========================================================================
    // CONSTRUCTOR
    // =========================================================================

    Impl()
        : templates(
              default_glyph_templates()
          )
        , glyph_matcher(
              templates
          )
        , line_recognizer(
              glyph_matcher,
              tesseract_recognizer
          )
        , chart_label_recognizer(
              line_recognizer
          )
    {
    }

    // =========================================================================
    // COPY CONSTRUCTOR
    // =========================================================================
    //
    // The template database is immutable canonical storage, so the copied
    // facade can safely refer to the same GlyphTemplateTable.
    //
    // Every subsystem is reconstructed so its internal references point to
    // this Impl's members.
    // =========================================================================

    Impl(
        const Impl&
    )
        : templates(
              default_glyph_templates()
          )
        , glyph_matcher(
              templates
          )
        , line_recognizer(
              glyph_matcher,
              tesseract_recognizer
          )
        , chart_label_recognizer(
              line_recognizer
          )
    {
    }

    Impl& operator=(
        const Impl&
    ) = delete;
};

// =============================================================================
// CONSTRUCTOR
// =============================================================================

MatrixMatcher::MatrixMatcher()
    : impl_(
          std::make_unique<Impl>()
      )
{
}

// =============================================================================
// DESTRUCTOR
// =============================================================================

MatrixMatcher::~MatrixMatcher() = default;

// =============================================================================
// COPY CONSTRUCTOR
// =============================================================================

MatrixMatcher::MatrixMatcher(
    const MatrixMatcher& other
)
    : impl_(
          other.impl_
              ? std::make_unique<Impl>(
                    *other.impl_
                )
              : nullptr
      )
{
}

// =============================================================================
// COPY ASSIGNMENT
// =============================================================================

MatrixMatcher& MatrixMatcher::operator=(
    const MatrixMatcher& other
)
{
    if (
        this == &other
    ) {
        return *this;
    }

    MatrixMatcher temporary(
        other
    );

    impl_.swap(
        temporary.impl_
    );

    return *this;
}

// =============================================================================
// MOVE CONSTRUCTOR
// =============================================================================

MatrixMatcher::MatrixMatcher(
    MatrixMatcher&&
) noexcept = default;

// =============================================================================
// MOVE ASSIGNMENT
// =============================================================================

MatrixMatcher& MatrixMatcher::operator=(
    MatrixMatcher&&
) noexcept = default;

// =============================================================================
// SINGLE GLYPH MATCHING
// =============================================================================

char MatrixMatcher::match_glyph(
    const std::vector<uint8_t>& cropped_patch,
    int patch_w,
    int patch_h
) const
{
    if (
        !impl_
    ) {
        return '?';
    }

    return impl_->glyph_matcher.match(
        cropped_patch,
        patch_w,
        patch_h
    );
}

// =============================================================================
// LINE RECOGNITION
// =============================================================================

std::string MatrixMatcher::recognize_line(
    const uint8_t* image,
    int img_width,
    int line_y_start,
    int line_y_end,
    int channels
) const
{
    if (
        !impl_
    ) {
        return {};
    }

    return impl_->line_recognizer.recognize(
        image,
        img_width,
        line_y_start,
        line_y_end,
        channels
    );
}

// =============================================================================
// CHART LABEL RECOGNITION
// =============================================================================

std::string MatrixMatcher::recognize_chart_labels(
    const uint8_t* hsv_buffer,
    int width,
    int height
) const
{
    if (
        !impl_
    ) {
        return {};
    }

    return impl_->chart_label_recognizer.recognize(
        hsv_buffer,
        width,
        height
    );
}

} // namespace fin_ocr
