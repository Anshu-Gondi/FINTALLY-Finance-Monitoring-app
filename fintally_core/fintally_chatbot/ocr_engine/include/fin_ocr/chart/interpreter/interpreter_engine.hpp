#pragma once

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/interpreter/interpreter_types.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter {

// =============================================================================
// INTERPRETER ENGINE
// =============================================================================
//
// High-level orchestration only.
//
// Delegates:
//     chart type classification
//     stack classification
//     axis/value mapping
//     chart-family data building
//     lookup
//     geometry
//
// =============================================================================

class InterpreterEngine {
public:

    InterpreterEngine() = default;

    ~InterpreterEngine() = default;

    InterpreterEngine(
        const InterpreterEngine&
    ) = default;

    InterpreterEngine& operator=(
        const InterpreterEngine&
    ) = default;

    InterpreterEngine(
        InterpreterEngine&&
    ) noexcept = default;

    InterpreterEngine& operator=(
        InterpreterEngine&&
    ) noexcept = default;

    // =========================================================================
    // INTERPRET
    // =========================================================================

    [[nodiscard]]
    ChartAnalysis interpret(
        const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
        const ::fin_ocr::chart::object::ChartObjectSet& objects,
        const ::fin_ocr::chart::association::ChartAssociationResult& associations
    ) const;
};

} // namespace fin_ocr::chart::interpreter
