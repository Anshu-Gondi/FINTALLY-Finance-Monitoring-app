#include "fin_ocr/chart/interpreter/interpreter_engine.hpp"

#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"
#include "fin_ocr/chart/interpreter/chart_type_classifier.hpp"
#include "fin_ocr/chart/interpreter/stack_classifier.hpp"

#include "fin_ocr/chart/interpreter/data/bar_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/funnel_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/line_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/radial_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/scatter_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/treemap_data_builder.hpp"
#include "fin_ocr/chart/interpreter/data/waterfall_data_builder.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace fin_ocr::chart::interpreter {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr float MIN_CONFIDENCE =
    config::CHART_INTERPRETER_MIN_CONFIDENCE;

} // namespace

// =============================================================================
// MAIN INTERPRETER
// =============================================================================
//
// High-level orchestration only.
//
// Responsibilities:
//
//     1. Copy validated upstream state.
//     2. Classify the chart.
//     3. Refine bar/column classification.
//     4. Build semantic metadata.
//     5. Dispatch to the appropriate data builder(s).
//     6. Calculate final validity and confidence.
//
// No chart-family detection algorithm belongs here.
//
// =============================================================================

ChartAnalysis InterpreterEngine::interpret(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
) const
{
    ChartAnalysis result{};

    // =========================================================================
    // COPY INPUT STATE
    // =========================================================================

    result.coordinates =
        coordinates;

    result.objects =
        objects;

    result.associations =
        associations;

    // =========================================================================
    // BASIC VALIDATION
    // =========================================================================

    if (
        !objects.valid
    ) {
        result.metadata.type =
            ChartType::UNKNOWN;

        result.valid =
            false;

        result.confidence =
            0.0f;

        return result;
    }

    // =========================================================================
    // INITIAL TYPE CLASSIFICATION
    // =========================================================================

    ChartType type =
        classifier::classify_chart_type(
            coordinates,
            objects
        );

    // =========================================================================
    // STRUCTURAL CLASSIFICATION
    // =========================================================================

    const bool stacked =
        stack::is_stacked(
            objects
        );

    const bool clustered =
        stack::is_clustered(
            objects
        );

    const bool percent_stacked =
        stack::is_percent_stacked(
            objects
        );

    const bool combo =
        stack::is_combo(
            objects
        );

    // =========================================================================
    // BAR / COLUMN REFINEMENT
    // =========================================================================
    //
    // Only refine when bar/column geometry actually exists.
    //
    // This extracts the old inline refinement logic from ChartInterpreter.
    // =========================================================================

    if (
        !objects.bars.empty()
    ) {
        type =
            classifier::refine_bar_chart_type(
                type,
                objects,
                stacked,
                clustered,
                percent_stacked,
                combo
            );
    }

    // =========================================================================
    // METADATA
    // =========================================================================

    result.metadata.type =
        type;

    result.metadata.has_x_axis =
        coordinates.x_axis.horizontal;

    result.metadata.has_y_axis =
        coordinates.y_axis.vertical;

    result.metadata.has_secondary_y_axis =
        coordinates.has_secondary_y_axis;

    // =========================================================================
    // TITLE
    // =========================================================================
    //
    // Select the highest-confidence explicitly classified TITLE label.
    //
    // We deliberately do not promote arbitrary OCR labels into titles.
    // =========================================================================

    const ::fin_ocr::chart::association::ChartLabel* best_title =
        nullptr;

    for (
        const ::fin_ocr::chart::association::ChartLabel& label :
        associations.labels
    ) {
        if (
            label.kind !=
            ::fin_ocr::chart::association::ChartLabelKind::TITLE
        ) {
            continue;
        }

        if (
            best_title == nullptr ||
            label.confidence >
                best_title->confidence
        ) {
            best_title =
                &label;
        }
    }

    if (
        best_title != nullptr
    ) {
        result.metadata.title =
            best_title->text;
    }

    // =========================================================================
    // DATA GENERATION
    // =========================================================================

    switch (
        type
    ) {

        // =====================================================================
        // BAR / COLUMN
        // =====================================================================

        case ChartType::BAR:
        case ChartType::COLUMN:
        case ChartType::STACKED_BAR:
        case ChartType::STACKED_COLUMN:
        case ChartType::HUNDRED_PERCENT_STACKED_BAR:
        case ChartType::HUNDRED_PERCENT_STACKED_COLUMN:
        case ChartType::CLUSTERED_BAR:
        case ChartType::CLUSTERED_COLUMN:

            result.data_points =
                data::bar::build_bar_data(
                    coordinates,
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // WATERFALL
        // =====================================================================

        case ChartType::WATERFALL:

            result.data_points =
                data::waterfall::build_waterfall_data(
                    coordinates,
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // LINE / AREA / COMBO
        // =====================================================================

        case ChartType::LINE:
        case ChartType::AREA:
        case ChartType::STACKED_AREA:
        case ChartType::COMBO_LINE_BAR:
        case ChartType::COMBO_LINE_COLUMN:

            result.data_points =
                data::line::build_line_data(
                    coordinates,
                    objects,
                    associations
                );

            // -----------------------------------------------------------------
            // Combo charts carry both line/path and bar/column geometry.
            // -----------------------------------------------------------------

            if (
                !objects.bars.empty()
            ) {
                std::vector<ChartDataPoint> bar_data =
                    data::bar::build_bar_data(
                        coordinates,
                        objects,
                        associations
                    );

                result.data_points.insert(
                    result.data_points.end(),
                    std::make_move_iterator(
                        bar_data.begin()
                    ),
                    std::make_move_iterator(
                        bar_data.end()
                    )
                );
            }

            break;

        // =====================================================================
        // PIE / DONUT
        // =====================================================================

        case ChartType::PIE:
        case ChartType::DONUT:

            result.data_points =
                data::radial::build_radial_data(
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // SCATTER / BUBBLE
        // =====================================================================

        case ChartType::SCATTER:
        case ChartType::BUBBLE:

            result.data_points =
                data::scatter::build_scatter_data(
                    coordinates,
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // FUNNEL
        // =====================================================================

        case ChartType::FUNNEL:

            result.data_points =
                data::funnel::build_funnel_data(
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // TREEMAP
        // =====================================================================

        case ChartType::TREEMAP:

            result.data_points =
                data::treemap::build_treemap_data(
                    objects,
                    associations
                );

            break;

        // =====================================================================
        // UNSUPPORTED / UNKNOWN
        // =====================================================================

        default:

            break;
    }

    // =========================================================================
    // AXIS TITLES
    // =========================================================================
    //
    // ChartLabel currently has no dedicated axis-title classification.
    //
    // Therefore we intentionally leave:
    //
    //     metadata.x_axis_title
    //     metadata.y_axis_title
    //
    // unchanged rather than guessing from arbitrary OCR labels.
    // =========================================================================

    // =========================================================================
    // VALIDITY
    // =========================================================================

    const bool has_type =
        type !=
        ChartType::UNKNOWN;

    const bool has_data =
        !result.data_points.empty();

    const bool has_association =
        result.associations.valid ||
        !result.associations.associations.empty() ||
        !result.associations.categories.empty() ||
        !result.associations.series.empty();

    result.valid =
        has_type &&
        has_data &&
        (
            has_association ||
            objects.valid
        );

    // =========================================================================
    // GLOBAL CONFIDENCE
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    // -------------------------------------------------------------------------
    // Object confidence.
    // -------------------------------------------------------------------------

    confidence_sum +=
        static_cast<double>(
            objects.confidence
        );

    ++confidence_count;

    // -------------------------------------------------------------------------
    // Association confidence.
    // -------------------------------------------------------------------------

    confidence_sum +=
        static_cast<double>(
            associations.confidence
        );

    ++confidence_count;

    // -------------------------------------------------------------------------
    // Type confidence contribution.
    //
    // Preserve legacy behavior.
    // -------------------------------------------------------------------------

    if (
        type !=
        ChartType::UNKNOWN
    ) {
        confidence_sum +=
            0.50;

        ++confidence_count;
    }

    // -------------------------------------------------------------------------
    // Data-point confidence.
    // -------------------------------------------------------------------------

    for (
        const ChartDataPoint& point :
        result.data_points
    ) {
        if (
            point.confidence >=
            MIN_CONFIDENCE
        ) {
            confidence_sum +=
                static_cast<double>(
                    point.confidence
                );

            ++confidence_count;
        }
    }

    // -------------------------------------------------------------------------
    // Final confidence.
    // -------------------------------------------------------------------------

    result.confidence =
        confidence_count == 0
            ? 0.0f
            : static_cast<float>(
                  std::clamp(
                      confidence_sum /
                          static_cast<double>(
                              confidence_count
                          ),
                      0.0,
                      1.0
                  )
              );

    result.metadata.confidence =
        result.confidence;

    return result;
}

} // namespace fin_ocr::chart::interpreter
