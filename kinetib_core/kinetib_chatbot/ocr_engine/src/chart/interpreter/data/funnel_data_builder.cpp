#include "fin_ocr/chart/interpreter/data/funnel_data_builder.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <utility>

namespace fin_ocr::chart::interpreter::data::funnel {

// =============================================================================
// FUNNEL DATA
// =============================================================================
//
// Converts detected funnel stages into semantic ChartDataPoint records.
//
// Value resolution:
//
//     1. Explicit inferred value, when valid.
//     2. Relative stage width, when no absolute value is available.
//
// Relative width remains a geometry-derived quantity and is not presented as
// an absolute business value.
//
// =============================================================================

std::vector<ChartDataPoint> build_funnel_data(
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.funnel_stages.size()
    );

    for (
        std::size_t index = 0;
        index < objects.funnel_stages.size();
        ++index
    ) {
        const ::fin_ocr::chart::object::FunnelStage& stage =
            objects.funnel_stages[index];

        ChartDataPoint point{};

        // ---------------------------------------------------------------------
        // Object identity.
        // ---------------------------------------------------------------------

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            stage.stage_index;

        // ---------------------------------------------------------------------
        // Category name.
        // ---------------------------------------------------------------------

        point.category =
            lookup::category_name(
                associations,
                stage.stage_index
            );

        // ---------------------------------------------------------------------
        // Value.
        // ---------------------------------------------------------------------
        //
        // Preserve the legacy resolution order:
        //
        //     inferred absolute value
        //             ↓
        //     relative geometry width
        //
        // Geometry-derived relative width is intentionally not relabeled as an
        // absolute numeric business value.
        // ---------------------------------------------------------------------

        const bool inferred_value_valid =
            stage.has_inferred_value &&
            geometry::finite_value(
                stage.inferred_value
            );

        const bool relative_width_valid =
            geometry::finite_value(
                stage.relative_width
            );

        if (
            inferred_value_valid
        ) {
            point.value.value =
                stage.inferred_value;

            point.value.valid =
                true;

        } else {
            point.value.value =
                stage.relative_width;

            point.value.valid =
                relative_width_valid;
        }

        // ---------------------------------------------------------------------
        // Confidence.
        // ---------------------------------------------------------------------

        point.value.confidence =
            point.value.valid
                ? stage.confidence
                : 0.0f;

        point.confidence =
            stage.confidence;

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::funnel
