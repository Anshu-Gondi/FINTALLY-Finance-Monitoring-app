#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/interpreter/interpreter_types.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::data::bar {

[[nodiscard]]
std::vector<ChartDataPoint> build_bar_data(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
);

} // namespace fin_ocr::chart::interpreter::data::bar
