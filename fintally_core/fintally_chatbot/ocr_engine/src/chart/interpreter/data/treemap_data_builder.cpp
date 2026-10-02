#include "fin_ocr/chart/interpreter/data/treemap_data_builder.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <utility>

namespace fin_ocr::chart::interpreter::data::treemap {

// =============================================================================
// TREEMAP DATA
// =============================================================================
//
// Converts detected treemap nodes into semantic ChartDataPoint records.
//
// The value is considered valid only when the treemap detector explicitly
// marked the inferred value as available and the value is finite.
//
// =============================================================================

std::vector<ChartDataPoint> build_treemap_data(
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.treemap_nodes.size()
    );

    for (
        std::size_t index = 0;
        index < objects.treemap_nodes.size();
        ++index
    ) {
        const ::fin_ocr::chart::object::TreemapNode& node =
            objects.treemap_nodes[index];

        ChartDataPoint point{};

        // ---------------------------------------------------------------------
        // Object identity.
        // ---------------------------------------------------------------------

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            node.label_index;

        // ---------------------------------------------------------------------
        // Category / label lookup.
        // ---------------------------------------------------------------------

        point.category =
            lookup::category_name(
                associations,
                node.label_index
            );

        // ---------------------------------------------------------------------
        // Inferred value.
        // ---------------------------------------------------------------------

        point.value.value =
            node.inferred_value;

        point.value.valid =
            node.has_inferred_value &&
            geometry::finite_value(
                node.inferred_value
            );

        // ---------------------------------------------------------------------
        // Confidence.
        // ---------------------------------------------------------------------

        point.value.confidence =
            point.value.valid
                ? node.confidence
                : 0.0f;

        point.confidence =
            node.confidence;

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::treemap
