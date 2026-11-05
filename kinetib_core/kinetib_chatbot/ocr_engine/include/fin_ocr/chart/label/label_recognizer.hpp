#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/label/label_engine.hpp"

namespace fin_ocr {

class LineRecognizer;

namespace chart::label {

// =============================================================================
// CHART LABEL RECOGNIZER
// =============================================================================
//
// Public façade.
//
// Responsibilities:
//
//     - expose the stable public recognition API
//     - delegate structured recognition to LabelEngine
//     - delegate formatted compatibility output to label::output
//
// The recognizer itself does not perform candidate segmentation, OCR,
// category processing, Y-axis processing, recovery, filtering or merging.
//
// =============================================================================

class ChartLabelRecognizer {
public:

    explicit ChartLabelRecognizer(
        const ::fin_ocr::LineRecognizer& line_recognizer
    ) noexcept;

    ~ChartLabelRecognizer() = default;

    ChartLabelRecognizer(
        const ChartLabelRecognizer&
    ) = default;

    ChartLabelRecognizer& operator=(
        const ChartLabelRecognizer&
    ) = delete;

    ChartLabelRecognizer(
        ChartLabelRecognizer&&
    ) noexcept = default;

    ChartLabelRecognizer& operator=(
        ChartLabelRecognizer&&
    ) noexcept = delete;

    // =========================================================================
    // STRUCTURED OUTPUT
    // =========================================================================

    [[nodiscard]]
    std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >
    recognize_labels(
        const std::uint8_t* chart_buffer,
        int width,
        int height
    ) const;

    // =========================================================================
    // PRESENTATION / COMPATIBILITY OUTPUT
    // =========================================================================
    //
    // Returns the legacy-compatible [CHART_TEXT_DATA] representation.
    //
    // Formatting is handled by label::output.
    //
    // =========================================================================

    [[nodiscard]]
    std::string recognize(
        const std::uint8_t* chart_buffer,
        int width,
        int height
    ) const;

private:

    LabelEngine engine_;
};

} // namespace chart::label

} // namespace fin_ocr
