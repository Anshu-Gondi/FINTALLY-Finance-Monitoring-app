#include "fin_ocr/chart/label/label_recognizer.hpp"

#include "fin_ocr/chart/label/label_output.hpp"

namespace fin_ocr::chart::label {

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ChartLabelRecognizer::ChartLabelRecognizer(
    const ::fin_ocr::LineRecognizer& line_recognizer
) noexcept
    : engine_(
          line_recognizer
      )
{
}

// =============================================================================
// STRUCTURED LABEL RECOGNITION
// =============================================================================
//
// Public façade → LabelEngine.
//
// No recognition logic belongs here.
//
// =============================================================================

std::vector<
    ::fin_ocr::chart::association::ChartLabel
>
ChartLabelRecognizer::recognize_labels(
    const std::uint8_t* chart_buffer,
    int width,
    int height
) const
{
    return
        engine_.recognize_labels(
            chart_buffer,
            width,
            height
        );
}

// =============================================================================
// PRESENTATION / COMPATIBILITY OUTPUT
// =============================================================================
//
// Structured recognition is performed first.
//
// Formatting is delegated entirely to label_output.cpp.
//
// =============================================================================

std::string ChartLabelRecognizer::recognize(
    const std::uint8_t* chart_buffer,
    int width,
    int height
) const
{
    if (
        chart_buffer == nullptr ||
        width <= 0 ||
        height <= 0
    ) {

        return
            output::format_invalid_chart_buffer();
    }

    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    > labels =
        recognize_labels(
            chart_buffer,
            width,
            height
        );

    if (
        labels.empty()
    ) {

        return
            output::format_empty_labels();
    }

    return
        output::format_labels(
            labels
        );
}

} // namespace fin_ocr::chart::label
