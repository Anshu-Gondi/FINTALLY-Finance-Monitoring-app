#include "fin_ocr/chart/interpreter/data/radial_data_builder.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <utility>

namespace fin_ocr::chart::interpreter::data::radial {

// =============================================================================
// RADIAL DATA
// =============================================================================
//
// Converts detected PIE / DONUT slices into semantic ChartDataPoint records.
//
// `fraction` is a geometry-derived quantity. It is not automatically treated
// as an accounting amount.
//
// =============================================================================

std::vector<ChartDataPoint> build_radial_data(
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.slices.size()
    );

    for (
        std::size_t index = 0;
        index < objects.slices.size();
        ++index
    ) {
        const ::fin_ocr::chart::object::RadialSlice& slice =
            objects.slices[index];

        ChartDataPoint point{};

        // ---------------------------------------------------------------------
        // Object identity.
        // ---------------------------------------------------------------------

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            slice.category_index;

        // ---------------------------------------------------------------------
        // Category.
        // ---------------------------------------------------------------------

        point.category =
            lookup::category_name(
                associations,
                slice.category_index
            );

        // ---------------------------------------------------------------------
        // Fraction.
        //
        // A radial fraction is geometrically meaningful, but must not be
        // silently interpreted as currency or another business-unit amount.
        // ---------------------------------------------------------------------

        point.value.value =
            slice.fraction;

        point.value.valid =
            geometry::finite_value(
                slice.fraction
            );

        point.value.confidence =
            point.value.valid
                ? slice.confidence
                : 0.0f;

        point.confidence =
            point.value.confidence;

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::radial
