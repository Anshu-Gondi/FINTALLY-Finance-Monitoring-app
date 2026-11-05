#include "fin_ocr/chart/interpreter/stack_classifier.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fin_ocr::chart::interpreter::stack {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double CLUSTER_X_TOLERANCE =
    config::CHART_INTERPRETER_CLUSTER_X_TOLERANCE;

constexpr double CLUSTER_Y_TOLERANCE =
    config::CHART_INTERPRETER_CLUSTER_Y_TOLERANCE;

constexpr double COMBO_MIN_LINE_POINTS =
    config::CHART_INTERPRETER_COMBO_MIN_LINE_POINTS;

} // namespace

// =============================================================================
// STACKED DETECTION
// =============================================================================

bool is_stacked(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    if (
        objects.bars.size() < 2
    ) {
        return false;
    }

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {
        for (
            std::size_t j = i + 1;
            j < objects.bars.size();
            ++j
        ) {
            if (
                geometry::stack_compatible(
                    objects.bars[i],
                    objects.bars[j]
                )
            ) {
                return true;
            }
        }
    }

    return false;
}

// =============================================================================
// PERCENT-STACKED DETECTION
// =============================================================================
//
// Conservative by design.
//
// Percentage semantics are accepted only when inferred numeric values are
// available and stack totals are approximately consistent.
//
// Geometry alone must not fabricate percentage values.
//
// =============================================================================

bool is_percent_stacked(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    if (
        objects.bars.size() < 2 ||
        !is_stacked(objects)
    ) {
        return false;
    }

    std::vector<double> totals;

    for (
        const ::fin_ocr::chart::object::BarSegment& bar :
        objects.bars
    ) {
        if (
            !bar.has_inferred_value ||
            !geometry::finite_value(
                bar.inferred_value
            )
        ) {
            continue;
        }

        const ::fin_ocr::chart::object::ChartRect& bounds =
            bar.bounds;

        const int center =
            geometry::is_vertical_bar(bar)
                ? (
                    bounds.min_x +
                    bounds.max_x
                ) / 2
                : (
                    bounds.min_y +
                    bounds.max_y
                ) / 2;

        bool merged = false;

        // ---------------------------------------------------------------------
        // Preserve the legacy grouping behavior.
        //
        // The current legacy implementation merges the value into the first
        // available total. `center` is intentionally retained to preserve the
        // original structure and can be used when grouping is refined later.
        // ---------------------------------------------------------------------

        for (
            double& total :
            totals
        ) {
            (void)center;

            total +=
                std::abs(
                    bar.inferred_value
                );

            merged = true;

            break;
        }

        if (
            !merged
        ) {
            totals.push_back(
                std::abs(
                    bar.inferred_value
                )
            );
        }
    }

    if (
        totals.size() < 2
    ) {
        return false;
    }

    double mean = 0.0;

    for (
        const double value :
        totals
    ) {
        mean += value;
    }

    mean /=
        static_cast<double>(
            totals.size()
        );

    if (
        mean <= 0.0
    ) {
        return false;
    }

    double max_deviation = 0.0;

    for (
        const double value :
        totals
    ) {
        max_deviation =
            std::max(
                max_deviation,
                std::abs(
                    value -
                    mean
                ) /
                mean
            );
    }

    return
        max_deviation <= 0.05;
}

// =============================================================================
// CLUSTERED DETECTION
// =============================================================================

bool is_clustered(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    if (
        objects.bars.size() < 2
    ) {
        return false;
    }

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {
        for (
            std::size_t j = i + 1;
            j < objects.bars.size();
            ++j
        ) {
            const ::fin_ocr::chart::object::ChartRect& a =
                objects.bars[i].bounds;

            const ::fin_ocr::chart::object::ChartRect& b =
                objects.bars[j].bounds;

            // -----------------------------------------------------------------
            // Stacked bars are not clustered bars.
            // -----------------------------------------------------------------

            if (
                geometry::stack_compatible(
                    objects.bars[i],
                    objects.bars[j]
                )
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Only compare bars with the same orientation.
            // -----------------------------------------------------------------

            const bool vertical_a =
                geometry::is_vertical_bar(
                    objects.bars[i]
                );

            const bool vertical_b =
                geometry::is_vertical_bar(
                    objects.bars[j]
                );

            if (
                vertical_a !=
                vertical_b
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Vertical columns: compare X centers.
            // -----------------------------------------------------------------

            if (
                vertical_a
            ) {
                const int ax =
                    (
                        a.min_x +
                        a.max_x
                    ) / 2;

                const int bx =
                    (
                        b.min_x +
                        b.max_x
                    ) / 2;

                if (
                    std::abs(
                        static_cast<double>(
                            ax -
                            bx
                        )
                    ) <=
                    CLUSTER_X_TOLERANCE
                ) {
                    return true;
                }

            // -----------------------------------------------------------------
            // Horizontal bars: compare Y centers.
            // -----------------------------------------------------------------

            } else {
                const int ay =
                    (
                        a.min_y +
                        a.max_y
                    ) / 2;

                const int by =
                    (
                        b.min_y +
                        b.max_y
                    ) / 2;

                if (
                    std::abs(
                        static_cast<double>(
                            ay -
                            by
                        )
                    ) <=
                    CLUSTER_Y_TOLERANCE
                ) {
                    return true;
                }
            }
        }
    }

    return false;
}

// =============================================================================
// COMBO DETECTION
// =============================================================================

bool is_combo(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    if (
        objects.bars.empty() ||
        objects.paths.empty()
    ) {
        return false;
    }

    for (
        const ::fin_ocr::chart::object::ChartPath& path :
        objects.paths
    ) {
        if (
            path.points.size() >=
            static_cast<std::size_t>(
                COMBO_MIN_LINE_POINTS
            )
        ) {
            return true;
        }
    }

    return false;
}

} // namespace fin_ocr::chart::interpreter::stack
