#include "fin_ocr/chart/label/label_output.hpp"

#include <cstddef>
#include <string>

namespace fin_ocr::chart::label::output {

namespace {

// =============================================================================
// OUTPUT PREFIX
// =============================================================================

constexpr char CHART_TEXT_DATA_HEADER[] =
    "[CHART_TEXT_DATA]\n";

// =============================================================================
// EMPTY LABEL FALLBACK
// =============================================================================

constexpr char NO_RECOGNIZED_LABELS[] =
    "  - Line 1 [Y:0-0, X:0-0]: "
    "[NO_RECOGNIZED_CHART_LABELS] "
    "[confidence=0.000000]\n";

} // namespace

// =============================================================================
// INVALID CHART BUFFER
// =============================================================================
//
// Deterministic transport fallback for invalid / unusable chart input.
//
// No semantic information is fabricated here.
//
// =============================================================================

[[nodiscard]]
std::string format_invalid_chart_buffer()
{
    std::string output;

    output.reserve(
        sizeof(CHART_TEXT_DATA_HEADER) +
        sizeof(NO_RECOGNIZED_LABELS)
    );

    output +=
        CHART_TEXT_DATA_HEADER;

    output +=
        NO_RECOGNIZED_LABELS;

    return output;
}

// =============================================================================
// EMPTY LABEL RESULT
// =============================================================================
//
// Used when recognition completed successfully but produced no structured
// labels.
//
// =============================================================================

[[nodiscard]]
std::string format_empty_labels()
{
    std::string output;

    output.reserve(
        sizeof(CHART_TEXT_DATA_HEADER) +
        sizeof(NO_RECOGNIZED_LABELS)
    );

    output +=
        CHART_TEXT_DATA_HEADER;

    output +=
        NO_RECOGNIZED_LABELS;

    return output;
}

// =============================================================================
// FORMATTED LABEL OUTPUT
// =============================================================================
//
// Converts structured ChartLabel records into the legacy-compatible
// [CHART_TEXT_DATA] transport representation.
//
// Important:
//
//     This function does NOT perform recognition.
//     It does NOT classify labels.
//     It does NOT modify labels.
//
// It only serializes already-recognized structured labels.
//
// =============================================================================

[[nodiscard]]
std::string format_labels(
    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels
)
{
    std::string output;

    // -------------------------------------------------------------------------
    // Reserve enough room for the common case without attempting an exact
    // allocation calculation.
    // -------------------------------------------------------------------------

    output.reserve(
        256u +
        labels.size() * 96u
    );

    output +=
        CHART_TEXT_DATA_HEADER;

    if (
        labels.empty()
    ) {

        output +=
            NO_RECOGNIZED_LABELS;

        return output;
    }

    // =========================================================================
    // LABEL SERIALIZATION
    // =========================================================================

    for (
        std::size_t i = 0u;
        i < labels.size();
        ++i
    ) {

        const auto& label =
            labels[i];

        output +=
            "  - Line " +
            std::to_string(
                i + 1u
            );

        output +=
            " [Y:" +
            std::to_string(
                label.min_y
            );

        output +=
            "-" +
            std::to_string(
                label.max_y + 1
            );

        output +=
            ", X:" +
            std::to_string(
                label.min_x
            );

        output +=
            "-" +
            std::to_string(
                label.max_x
            );

        output +=
            "]: ";

        output +=
            label.text;

        output +=
            " [confidence=";

        output +=
            std::to_string(
                label.confidence
            );

        output +=
            "]\n";
    }

    return output;
}

} // namespace fin_ocr::chart::label::output
