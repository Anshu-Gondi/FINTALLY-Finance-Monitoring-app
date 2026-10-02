#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/interpreter/interpreter_types.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::data::radial {

[[nodiscard]]
std::vector<ChartDataPoint> build_radial_data(
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
);

} // namespace fin_ocr::chart::interpreter::data::radial
