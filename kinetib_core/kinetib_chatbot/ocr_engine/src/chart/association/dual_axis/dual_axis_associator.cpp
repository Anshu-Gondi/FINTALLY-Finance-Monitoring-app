#include "fin_ocr/chart/association/dual_axis/dual_axis_associator.hpp"

namespace fin_ocr::chart::association::dual_axis {

// =============================================================================
// DUAL AXIS ASSOCIATION
// =============================================================================
//
// The current object structures do not contain an explicit
// primary/secondary-axis field.
//
// Therefore this pass intentionally does NOT mutate values or invent a
// secondary-axis relationship.
//
// It only observes whether secondary axes were detected. A richer semantic
// representation can consume that information later without abusing fields
// such as series_index or category_index.
//
// =============================================================================

void associate_dual_axes(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& objects
)
{
    // =========================================================================
    // NO SECONDARY AXIS
    // =========================================================================

    if (
        !coordinates.has_secondary_y_axis &&
        !coordinates.has_secondary_x_axis
    ) {
        return;
    }

    // =========================================================================
    // CURRENT ABI LIMITATION
    // =========================================================================
    //
    // ChartObjectSet / ChartObject / BarSegment / ChartPath do not currently
    // contain a dedicated primary-vs-secondary axis identifier.
    //
    // Do not encode that relationship into unrelated semantic fields.
    //
    // =========================================================================

    (void)objects;
}

} // namespace fin_ocr::chart::association::dual_axis
