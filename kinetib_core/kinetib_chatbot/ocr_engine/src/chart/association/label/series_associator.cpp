#include "fin_ocr/chart/association/label/series_associator.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace fin_ocr::chart::association::label::series {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_SERIES =
    config::CHART_MAX_SERIES;

} // namespace

// =============================================================================
// SERIES / LEGEND ASSOCIATION
// =============================================================================
//
// Converts SERIES_LABEL / LEGEND_LABEL instances into ordered ChartSeries
// records.
//
// Ordering is the order in which eligible labels appear in the supplied
// label vector.
//
// No geometric reordering is performed here.
// No series names are fabricated.
//
// =============================================================================

void associate_series_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels,
    std::vector<
        ::fin_ocr::chart::association::ChartSeries
    >& series
)
{
    // =========================================================================
    // RESET RESULT
    // =========================================================================

    series.clear();

    // =========================================================================
    // CURRENT IMPLEMENTATION DOES NOT USE COORDINATES
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // COLLECT SERIES LABELS
    // =========================================================================

    for (
        std::size_t i = 0;
        i < labels.size();
        ++i
    ) {
        const ::fin_ocr::chart::association::ChartLabel& label =
            labels[i];

        // ---------------------------------------------------------------------
        // Only explicit series/legend labels participate.
        // ---------------------------------------------------------------------

        if (
            label.kind !=
                ::fin_ocr::chart::association::
                    ChartLabelKind::SERIES_LABEL &&
            label.kind !=
                ::fin_ocr::chart::association::
                    ChartLabelKind::LEGEND_LABEL
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Enforce configured series limit.
        // ---------------------------------------------------------------------

        if (
            series.size() >=
            MAX_SERIES
        ) {
            break;
        }

        // =====================================================================
        // BUILD SERIES
        // =====================================================================

        ::fin_ocr::chart::association::ChartSeries entry{};

        entry.name =
            label.text;

        entry.label_index =
            static_cast<int>(i);

        entry.series_index =
            static_cast<int>(
                series.size()
            );

        entry.confidence =
            label.confidence;

        series.push_back(
            std::move(
                entry
            )
        );
    }
}

} // namespace fin_ocr::chart::association::label::series
