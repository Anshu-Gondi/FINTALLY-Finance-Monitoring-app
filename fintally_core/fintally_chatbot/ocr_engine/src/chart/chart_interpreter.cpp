#include "fin_ocr/chart/chart_interpreter.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr::chart {

namespace {

// =============================================================================
// CONSTANTS
// =============================================================================

constexpr double PI =
    config::CHART_INTERPRETER_PI;

constexpr double MIN_SCALE_PIXELS =
    config::CHART_INTERPRETER_MIN_SCALE_PIXELS;

constexpr double MIN_NUMERIC_AXIS_TICKS =
    config::CHART_INTERPRETER_MIN_NUMERIC_AXIS_TICKS;

constexpr double STACK_X_TOLERANCE =
    config::CHART_INTERPRETER_STACK_X_TOLERANCE;

constexpr double STACK_Y_TOLERANCE =
    config::CHART_INTERPRETER_STACK_Y_TOLERANCE;

constexpr double CLUSTER_X_TOLERANCE =
    config::CHART_INTERPRETER_CLUSTER_X_TOLERANCE;

constexpr double CLUSTER_Y_TOLERANCE =
    config::CHART_INTERPRETER_CLUSTER_Y_TOLERANCE;

constexpr double COMBO_MIN_LINE_POINTS =
    config::CHART_INTERPRETER_COMBO_MIN_LINE_POINTS;

constexpr double MIN_CONFIDENCE =
    config::CHART_INTERPRETER_MIN_CONFIDENCE;

// =============================================================================
// VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_rect(
    const ChartRect& rect
) noexcept {

    return
        rect.max_x >= rect.min_x &&
        rect.max_y >= rect.min_y;
}

[[nodiscard]]
int rect_width(
    const ChartRect& rect
) noexcept {

    if (!valid_rect(rect)) {
        return 0;
    }

    return
        rect.max_x -
        rect.min_x +
        1;
}

[[nodiscard]]
int rect_height(
    const ChartRect& rect
) noexcept {

    if (!valid_rect(rect)) {
        return 0;
    }

    return
        rect.max_y -
        rect.min_y +
        1;
}

[[nodiscard]]
std::pair<int, int> rect_center(
    const ChartRect& rect
) noexcept {

    return {
        rect.min_x +
            (rect.max_x - rect.min_x) / 2,

        rect.min_y +
            (rect.max_y - rect.min_y) / 2
    };
}

[[nodiscard]]
bool finite_value(
    double value
) noexcept {

    return std::isfinite(value);
}

// =============================================================================
// LABEL LOOKUP
// =============================================================================

[[nodiscard]]
const ChartLabel*
find_label(
    const ChartAssociationResult& associations,
    int label_index
) noexcept {

    if (
        label_index < 0 ||
        static_cast<std::size_t>(label_index) >=
            associations.labels.size()
    ) {
        return nullptr;
    }

    return
        &associations.labels[
            static_cast<std::size_t>(
                label_index
            )
        ];
}

// =============================================================================
// CATEGORY LOOKUP
// =============================================================================

[[nodiscard]]
const ChartCategory*
find_category(
    const ChartAssociationResult& associations,
    int category_index
) noexcept {

    if (
        category_index < 0 ||
        static_cast<std::size_t>(category_index) >=
            associations.categories.size()
    ) {
        return nullptr;
    }

    return
        &associations.categories[
            static_cast<std::size_t>(
                category_index
            )
        ];
}

// =============================================================================
// SERIES LOOKUP
// =============================================================================

[[nodiscard]]
const ChartSeries*
find_series(
    const ChartAssociationResult& associations,
    int series_index
) noexcept {

    if (
        series_index < 0 ||
        static_cast<std::size_t>(series_index) >=
            associations.series.size()
    ) {
        return nullptr;
    }

    return
        &associations.series[
            static_cast<std::size_t>(
                series_index
            )
        ];
}

// =============================================================================
// ASSOCIATION LOOKUP
// =============================================================================

[[nodiscard]]
const ChartAssociation*
find_object_association(
    const ChartAssociationResult& associations,
    AssociatedObjectKind object_kind,
    int object_index
) noexcept {

    const ChartAssociation* best =
        nullptr;

    for (
        const ChartAssociation& association :
        associations.associations
    ) {

        if (
            association.object_kind !=
            object_kind
        ) {
            continue;
        }

        if (
            association.object_index !=
            object_index
        ) {
            continue;
        }

        if (
            best == nullptr ||
            association.confidence >
                best->confidence
        ) {
            best =
                &association;
        }
    }

    return best;
}

// =============================================================================
// CATEGORY NAME
// =============================================================================

[[nodiscard]]
std::string category_name(
    const ChartAssociationResult& associations,
    int category_index
) {

    const ChartCategory* category =
        find_category(
            associations,
            category_index
        );

    if (
        category == nullptr
    ) {
        return {};
    }

    return category->name;
}

// =============================================================================
// SERIES NAME
// =============================================================================

[[nodiscard]]
std::string series_name(
    const ChartAssociationResult& associations,
    int series_index
) {

    const ChartSeries* series =
        find_series(
            associations,
            series_index
        );

    if (
        series == nullptr
    ) {
        return {};
    }

    return series->name;
}

// =============================================================================
// NUMERIC TICK VALIDATION
// =============================================================================

[[nodiscard]]
bool has_numeric_ticks(
    const ChartAxis& axis
) noexcept {

    std::size_t count = 0;

    for (
        const AxisTick& tick :
        axis.ticks
    ) {

        if (
            tick.has_numeric_value &&
            finite_value(
                tick.numeric_value
            )
        ) {

            ++count;
        }
    }

    return
        count >=
        static_cast<std::size_t>(
            MIN_NUMERIC_AXIS_TICKS
        );
}

// =============================================================================
// AXIS VALUE INTERPOLATION
// =============================================================================
//
// Interpolates only between actual numeric ticks surrounding the requested
// pixel. No extrapolation and no fabricated zero baseline.
//
// =============================================================================

[[nodiscard]]
bool interpolate_axis_value(
    const ChartAxis& axis,
    int pixel,
    double& value
) noexcept {

    if (
        !axis.horizontal &&
        !axis.vertical
    ) {
        return false;
    }

    const AxisTick* lower =
        nullptr;

    const AxisTick* upper =
        nullptr;

    for (
        const AxisTick& tick :
        axis.ticks
    ) {

        if (
            !tick.has_numeric_value ||
            !finite_value(
                tick.numeric_value
            )
        ) {
            continue;
        }

        if (
            tick.pixel_position <=
            pixel
        ) {

            if (
                lower == nullptr ||
                tick.pixel_position >
                    lower->pixel_position
            ) {

                lower =
                    &tick;
            }
        }

        if (
            tick.pixel_position >=
            pixel
        ) {

            if (
                upper == nullptr ||
                tick.pixel_position <
                    upper->pixel_position
            ) {

                upper =
                    &tick;
            }
        }
    }

    if (
        lower == nullptr ||
        upper == nullptr ||
        lower == upper
    ) {
        return false;
    }

    const int pixel_delta =
        upper->pixel_position -
        lower->pixel_position;

    if (
        pixel_delta == 0
    ) {
        return false;
    }

    const double alpha =
        static_cast<double>(
            pixel -
            lower->pixel_position
        ) /
        static_cast<double>(
            pixel_delta
        );

    if (
        !finite_value(alpha)
    ) {
        return false;
    }

    value =
        lower->numeric_value +
        (
            upper->numeric_value -
            lower->numeric_value
        ) *
        alpha;

    return finite_value(value);
}

// =============================================================================
// PATH POINT VALUE
// =============================================================================

[[nodiscard]]
bool path_point_values(
    const ChartCoordinateSystem& coordinates,
    const ChartPathPoint& point,
    double& x_value,
    double& y_value
) noexcept {

    if (
        !coordinates.valid ||
        !has_numeric_ticks(
            coordinates.x_axis
        ) ||
        !has_numeric_ticks(
            coordinates.y_axis
        )
    ) {
        return false;
    }

    if (
        !interpolate_axis_value(
            coordinates.x_axis,
            point.x,
            x_value
        )
    ) {
        return false;
    }

    if (
        !interpolate_axis_value(
            coordinates.y_axis,
            point.y,
            y_value
        )
    ) {
        return false;
    }

    return
        finite_value(x_value) &&
        finite_value(y_value);
}

// =============================================================================
// POINT INSIDE PLOT
// =============================================================================

[[nodiscard]]
bool inside_plot(
    const ChartCoordinateSystem& coordinates,
    int x,
    int y
) noexcept {

    if (
        !coordinates.valid
    ) {
        return true;
    }

    return
        x >= coordinates.plot_area.min_x &&
        x <= coordinates.plot_area.max_x &&
        y >= coordinates.plot_area.min_y &&
        y <= coordinates.plot_area.max_y;
}

// =============================================================================
// BOUNDARY BASELINE
// =============================================================================

[[nodiscard]]
bool near_same_x(
    const ChartRect& a,
    const ChartRect& b
) noexcept {

    const auto [ax, ay] =
        rect_center(a);

    const auto [bx, by] =
        rect_center(b);

    (void)ay;
    (void)by;

    return
        std::abs(
            static_cast<double>(ax - bx)
        ) <=
        STACK_X_TOLERANCE;
}

[[nodiscard]]
bool near_same_y(
    const ChartRect& a,
    const ChartRect& b
) noexcept {

    const auto [ax, ay] =
        rect_center(a);

    const auto [bx, by] =
        rect_center(b);

    (void)ax;
    (void)bx;

    return
        std::abs(
            static_cast<double>(ay - by)
        ) <=
        STACK_Y_TOLERANCE;
}

// =============================================================================
// BAR ORIENTATION
// =============================================================================

[[nodiscard]]
bool is_vertical_bar(
    const BarSegment& bar
) noexcept {

    return
        rect_height(
            bar.bounds
        ) >
        rect_width(
            bar.bounds
        );
}

// =============================================================================
// COMPATIBLE STACK RELATIONSHIP
// =============================================================================

[[nodiscard]]
bool stack_compatible(
    const BarSegment& a,
    const BarSegment& b
) noexcept {

    const ChartRect& ar =
        a.bounds;

    const ChartRect& br =
        b.bounds;

    const int aw =
        rect_width(ar);

    const int bw =
        rect_width(br);

    const int ah =
        rect_height(ar);

    const int bh =
        rect_height(br);

    if (
        aw <= 0 ||
        bw <= 0 ||
        ah <= 0 ||
        bh <= 0
    ) {
        return false;
    }

    const bool a_vertical =
        is_vertical_bar(a);

    const bool b_vertical =
        is_vertical_bar(b);

    if (
        a_vertical !=
        b_vertical
    ) {
        return false;
    }

    if (
        a_vertical
    ) {

        if (
            !near_same_x(
                ar,
                br
            )
        ) {
            return false;
        }

        const bool touching =
            std::abs(
                ar.max_y -
                br.min_y
            ) <= 2 ||
            std::abs(
                br.max_y -
                ar.min_y
            ) <= 2;

        return touching;
    }

    if (
        !near_same_y(
            ar,
            br
        )
    ) {
        return false;
    }

    const bool touching =
        std::abs(
            ar.max_x -
            br.min_x
        ) <= 2 ||
        std::abs(
            br.max_x -
            ar.min_x
        ) <= 2;

    return touching;
}

// =============================================================================
// CHART TYPE PRIORITY
// =============================================================================

[[nodiscard]]
ChartType classify_radial_type(
    const ChartObjectSet& objects
) noexcept {

    if (
        objects.slices.empty()
    ) {
        return ChartType::UNKNOWN;
    }

    bool has_donut =
        false;

    for (
        const RadialSlice& slice :
        objects.slices
    ) {

        if (
            slice.inner_radius > 0
        ) {
            has_donut =
                true;

            break;
        }
    }

    return
        has_donut
            ? ChartType::DONUT
            : ChartType::PIE;
}

// =============================================================================
// TYPE NAME HELPERS
// =============================================================================

[[nodiscard]]
ChartType classify_cartesian_type(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects
) noexcept {

    const bool has_bars =
        !objects.bars.empty();

    const bool has_paths =
        !objects.paths.empty();

    const bool has_waterfall =
        !objects.waterfall_steps.empty();

    const bool has_funnel =
        !objects.funnel_stages.empty();

    const bool has_scatter =
        !objects.points.empty();

    const bool has_area_objects =
        std::any_of(
            objects.generic_objects.begin(),
            objects.generic_objects.end(),
            [](
                const ChartObject& object
            ) noexcept {

                return
                    object.kind ==
                    ChartObjectKind::AREA_REGION ||
                    object.kind ==
                    ChartObjectKind::STACKED_AREA_REGION;
            }
        );

    if (
        has_waterfall
    ) {
        return ChartType::WATERFALL;
    }

    if (
        has_funnel
    ) {
        return ChartType::FUNNEL;
    }

    if (
        has_bars &&
        has_paths
    ) {

        return
            coordinates.has_secondary_y_axis
                ? ChartType::COMBO_LINE_COLUMN
                : ChartType::COMBO_LINE_COLUMN;
    }

    if (
        has_scatter &&
        !has_bars &&
        !has_paths
    ) {

        const bool bubble =
            std::any_of(
                objects.points.begin(),
                objects.points.end(),
                [](
                    const ScatterPoint& point
                ) noexcept {

                    return point.is_bubble;
                }
            );

        return
            bubble
                ? ChartType::BUBBLE
                : ChartType::SCATTER;
    }

    if (
        has_area_objects &&
        has_paths
    ) {

        return ChartType::AREA;
    }

    if (
        has_paths
    ) {

        return ChartType::LINE;
    }

    if (
        has_bars
    ) {
        return ChartType::COLUMN;
    }

    return ChartType::UNKNOWN;
}

} // namespace

// =============================================================================
// PIXEL -> VALUE
// =============================================================================

double ChartInterpreter::pixel_to_value(
    const ChartAxis& axis,
    int pixel
) const noexcept {

    double value =
        0.0;

    if (
        interpolate_axis_value(
            axis,
            pixel,
            value
        )
    ) {
        return value;
    }

    return
        std::numeric_limits<double>::quiet_NaN();
}

// =============================================================================
// SCALE ESTIMATION
// =============================================================================
//
// This is intentionally a local unit-per-pixel estimate between the first and
// last numeric ticks.
//
// It does NOT assume that the axis starts at zero.
//
// =============================================================================

double ChartInterpreter::estimate_scale(
    const ChartAxis& axis
) const noexcept {

    if (
        !has_numeric_ticks(axis)
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const AxisTick* first =
        nullptr;

    const AxisTick* last =
        nullptr;

    for (
        const AxisTick& tick :
        axis.ticks
    ) {

        if (
            !tick.has_numeric_value ||
            !finite_value(
                tick.numeric_value
            )
        ) {
            continue;
        }

        if (
            first == nullptr ||
            tick.pixel_position <
                first->pixel_position
        ) {
            first =
                &tick;
        }

        if (
            last == nullptr ||
            tick.pixel_position >
                last->pixel_position
        ) {
            last =
                &tick;
        }
    }

    if (
        first == nullptr ||
        last == nullptr ||
        first == last
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const int pixel_delta =
        last->pixel_position -
        first->pixel_position;

    if (
        std::abs(pixel_delta) <
        MIN_SCALE_PIXELS
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const double value_delta =
        last->numeric_value -
        first->numeric_value;

    return
        value_delta /
        static_cast<double>(
            pixel_delta
        );
}

// =============================================================================
// CHART TYPE CLASSIFICATION
// =============================================================================

ChartType ChartInterpreter::classify_chart_type(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects
) const {

    if (
        !objects.slices.empty()
    ) {
        return classify_radial_type(objects);
    }

    if (
        objects.treemap_nodes.size() >= 2
    ) {
        return ChartType::TREEMAP;
    }

    if (
        objects.funnel_stages.size() >= 2
    ) {
        return ChartType::FUNNEL;
    }

    return
        classify_cartesian_type(
            coordinates,
            objects
        );
}

// =============================================================================
// STACKED DETECTION
// =============================================================================

bool ChartInterpreter::is_stacked(
    const ChartObjectSet& objects
) const noexcept {

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
                stack_compatible(
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
// This remains conservative because absolute percentage semantics cannot be
// inferred safely from geometry alone unless normalized values are available.
// Here we only identify explicit inferred values that form approximately equal
// stack totals.
//
// =============================================================================

bool ChartInterpreter::is_percent_stacked(
    const ChartObjectSet& objects
) const noexcept {

    if (
        objects.bars.size() < 2 ||
        !is_stacked(objects)
    ) {
        return false;
    }

    std::vector<double> totals;

    for (
        const BarSegment& bar :
        objects.bars
    ) {

        if (
            !bar.has_inferred_value ||
            !finite_value(
                bar.inferred_value
            )
        ) {
            continue;
        }

        const ChartRect& bounds =
            bar.bounds;

        const int center =
            is_vertical_bar(bar)
                ? (
                    bounds.min_x +
                    bounds.max_x
                ) / 2
                : (
                    bounds.min_y +
                    bounds.max_y
                ) / 2;

        bool merged =
            false;

        for (
            double& total :
            totals
        ) {

            (void)center;

            total +=
                std::abs(
                    bar.inferred_value
                );

            merged =
                true;

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

    double mean =
        0.0;

    for (
        double value :
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

    double max_deviation =
        0.0;

    for (
        double value :
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

bool ChartInterpreter::is_clustered(
    const ChartObjectSet& objects
) const noexcept {

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

            const ChartRect& a =
                objects.bars[i].bounds;

            const ChartRect& b =
                objects.bars[j].bounds;

            if (
                stack_compatible(
                    objects.bars[i],
                    objects.bars[j]
                )
            ) {
                continue;
            }

            if (
                is_vertical_bar(
                    objects.bars[i]
                ) !=
                is_vertical_bar(
                    objects.bars[j]
                )
            ) {
                continue;
            }

            if (
                is_vertical_bar(
                    objects.bars[i]
                )
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
                        ax - bx
                    ) <=
                    CLUSTER_X_TOLERANCE
                ) {
                    return true;
                }

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
                        ay - by
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

bool ChartInterpreter::is_combo(
    const ChartObjectSet& objects
) const noexcept {

    if (
        objects.bars.empty() ||
        objects.paths.empty()
    ) {
        return false;
    }

    for (
        const ChartPath& path :
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

// =============================================================================
// BAR DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_bar_data(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        std::min<std::size_t>(
            objects.bars.size(),
            4096
        )
    );

    for (
        std::size_t index = 0;
        index < objects.bars.size();
        ++index
    ) {

        const BarSegment& bar =
            objects.bars[index];

        ChartDataPoint point{};

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            bar.category_index;

        point.series_index =
            bar.series_index;

        point.category =
            category_name(
                associations,
                bar.category_index
            );

        point.series =
            series_name(
                associations,
                bar.series_index
            );

        double value =
            std::numeric_limits<double>::quiet_NaN();

        bool value_valid =
            false;

        if (
            bar.has_inferred_value &&
            finite_value(
                bar.inferred_value
            )
        ) {

            value =
                bar.inferred_value;

            value_valid =
                true;

        } else if (
            coordinates.y_axis.vertical
        ) {

            const ChartRect& bounds =
                bar.bounds;

            const int pixel =
                bar.negative
                    ? bounds.max_y
                    : bounds.min_y;

            value_valid =
                interpolate_axis_value(
                    coordinates.y_axis,
                    pixel,
                    value
                );
        }

        point.value.value =
            value;

        point.value.valid =
            value_valid;

        point.value.confidence =
            value_valid
                ? (
                    bar.confidence *
                    0.85f
                )
                : 0.0f;

        point.confidence =
            std::max(
                point.value.confidence,
                bar.confidence
            );

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

// =============================================================================
// LINE DATA
// =============================================================================
//
// One ChartDataPoint is emitted per sampled path point. This preserves the
// underlying geometry instead of collapsing a line into a single statistic.
//
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_line_data(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    for (
        std::size_t path_index = 0;
        path_index < objects.paths.size();
        ++path_index
    ) {

        const ChartPath& path =
            objects.paths[path_index];

        for (
            const ChartPathPoint& point :
            path.points
        ) {

            ChartDataPoint value{};

            value.object_index =
                static_cast<int>(
                    path_index
                );

            value.series_index =
                path.series_index;

            value.series =
                series_name(
                    associations,
                    path.series_index
                );

            double x_value =
                std::numeric_limits<double>::quiet_NaN();

            double y_value =
                std::numeric_limits<double>::quiet_NaN();

            const bool mapped =
                path_point_values(
                    coordinates,
                    point,
                    x_value,
                    y_value
                );

            value.category =
                mapped
                    ? std::to_string(
                          x_value
                      )
                    : std::string{};

            value.value.value =
                y_value;

            value.value.valid =
                mapped;

            value.value.confidence =
                mapped
                    ? (
                        point.confidence *
                        path.confidence
                    )
                    : 0.0f;

            value.confidence =
                value.value.confidence;

            data.push_back(
                std::move(value)
            );
        }
    }

    return data;
}

// =============================================================================
// RADIAL DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_radial_data(
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.slices.size()
    );

    for (
        std::size_t index = 0;
        index < objects.slices.size();
        ++index
    ) {

        const RadialSlice& slice =
            objects.slices[index];

        ChartDataPoint point{};

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            slice.category_index;

        point.category =
            category_name(
                associations,
                slice.category_index
            );

        /*
         * fraction is a geometrically valid value, but it is not automatically
         * an accounting amount.
         */
        point.value.value =
            slice.fraction;

        point.value.valid =
            finite_value(
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

// =============================================================================
// SCATTER DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_scatter_data(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.points.size()
    );

    for (
        std::size_t index = 0;
        index < objects.points.size();
        ++index
    ) {

        const ScatterPoint& point =
            objects.points[index];

        ChartDataPoint value{};

        value.object_index =
            static_cast<int>(
                index
            );

        value.series_index =
            point.series_index;

        value.series =
            series_name(
                associations,
                point.series_index
            );

        if (
            point.has_x_value &&
            finite_value(
                point.x_value
            )
        ) {

            value.category =
                std::to_string(
                    point.x_value
                );
        } else {

            double x_value =
                std::numeric_limits<double>::quiet_NaN();

            if (
                interpolate_axis_value(
                    coordinates.x_axis,
                    point.x,
                    x_value
                )
            ) {

                value.category =
                    std::to_string(
                        x_value
                    );
            }
        }

        double y_value =
            std::numeric_limits<double>::quiet_NaN();

        bool y_valid =
            point.has_y_value &&
            finite_value(
                point.y_value
            );

        if (
            !y_valid &&
            interpolate_axis_value(
                coordinates.y_axis,
                point.y,
                y_value
            )
        ) {

            y_valid =
                true;
        }

        if (
            point.has_y_value &&
            finite_value(
                point.y_value
            )
        ) {

            y_value =
                point.y_value;
        }

        const bool x_valid =
            !value.category.empty();

        value.value.value =
            y_value;

        value.value.valid =
            x_valid &&
            y_valid;

        value.value.confidence =
            value.value.valid
                ? point.confidence
                : 0.0f;

        value.confidence =
            point.confidence;

        data.push_back(
            std::move(value)
        );
    }

    return data;
}

// =============================================================================
// WATERFALL DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_waterfall_data(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.waterfall_steps.size()
    );

    for (
        std::size_t index = 0;
        index < objects.waterfall_steps.size();
        ++index
    ) {

        const WaterfallStep& step =
            objects.waterfall_steps[index];

        ChartDataPoint point{};

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            step.category_index;

        point.category =
            category_name(
                associations,
                step.category_index
            );

        double value =
            std::numeric_limits<double>::quiet_NaN();

        bool valid =
            false;

        if (
            step.has_delta &&
            finite_value(
                step.delta
            )
        ) {

            value =
                step.delta;

            valid =
                true;

        } else if (
            coordinates.y_axis.vertical
        ) {

            const int y =
                (
                    step.bounds.min_y +
                    step.bounds.max_y
                ) / 2;

            valid =
                interpolate_axis_value(
                    coordinates.y_axis,
                    y,
                    value
                );
        }

        point.value.value =
            value;

        point.value.valid =
            valid;

        point.value.confidence =
            valid
                ? step.confidence
                : 0.0f;

        point.confidence =
            step.confidence;

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

// =============================================================================
// FUNNEL DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_funnel_data(
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.funnel_stages.size()
    );

    for (
        std::size_t index = 0;
        index < objects.funnel_stages.size();
        ++index
    ) {

        const FunnelStage& stage =
            objects.funnel_stages[index];

        ChartDataPoint point{};

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            stage.stage_index;

        point.category =
            category_name(
                associations,
                stage.stage_index
            );

        /*
         * The relative width is a valid geometry-derived quantity. It is not
         * silently converted into an absolute business value.
         */
        point.value.value =
            stage.has_inferred_value &&
            finite_value(
                stage.inferred_value
            )
                ? stage.inferred_value
                : stage.relative_width;

        point.value.valid =
            stage.has_inferred_value
                ? finite_value(
                      stage.inferred_value
                  )
                : finite_value(
                      stage.relative_width
                  );

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

// =============================================================================
// TREEMAP DATA
// =============================================================================

std::vector<ChartDataPoint>
ChartInterpreter::build_treemap_data(
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.treemap_nodes.size()
    );

    for (
        std::size_t index = 0;
        index < objects.treemap_nodes.size();
        ++index
    ) {

        const TreemapNode& node =
            objects.treemap_nodes[index];

        ChartDataPoint point{};

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            node.label_index;

        point.category =
            category_name(
                associations,
                node.label_index
            );

        point.value.value =
            node.inferred_value;

        point.value.valid =
            node.has_inferred_value &&
            finite_value(
                node.inferred_value
            );

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

// =============================================================================
// MAIN INTERPRETER
// =============================================================================

ChartAnalysis ChartInterpreter::interpret(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const ChartAssociationResult& associations
) const {

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
    // TYPE CLASSIFICATION
    // =========================================================================

    ChartType type =
        classify_chart_type(
            coordinates,
            objects
        );

    const bool stacked =
        is_stacked(objects);

    const bool clustered =
        is_clustered(objects);

    const bool percent_stacked =
        is_percent_stacked(objects);

    const bool combo =
        is_combo(objects);

    // =========================================================================
    // REFINE BAR CLASSIFICATION
    // =========================================================================

    if (
        !objects.bars.empty()
    ) {

        bool vertical =
            false;

        bool horizontal =
            false;

        for (
            const BarSegment& bar :
            objects.bars
        ) {

            if (
                is_vertical_bar(bar)
            ) {
                vertical =
                    true;
            } else {
                horizontal =
                    true;
            }
        }

        if (
            combo
        ) {

            type =
                vertical
                    ? ChartType::COMBO_LINE_COLUMN
                    : ChartType::COMBO_LINE_BAR;

        } else if (
            percent_stacked
        ) {

            type =
                vertical
                    ? ChartType::HUNDRED_PERCENT_STACKED_COLUMN
                    : ChartType::HUNDRED_PERCENT_STACKED_BAR;

        } else if (
            stacked
        ) {

            type =
                vertical
                    ? ChartType::STACKED_COLUMN
                    : ChartType::STACKED_BAR;

        } else if (
            clustered
        ) {

            type =
                vertical
                    ? ChartType::CLUSTERED_COLUMN
                    : ChartType::CLUSTERED_BAR;

        } else if (
            horizontal
        ) {

            type =
                ChartType::BAR;

        } else {

            type =
                ChartType::COLUMN;
        }
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

    for (
        const ChartLabel& label :
        associations.labels
    ) {

        if (
            label.kind ==
            ChartLabelKind::TITLE
        ) {

            if (
                result.metadata.title.empty() ||
                label.confidence >
                    result.associations.labels[
                        static_cast<std::size_t>(
                            std::find_if(
                                result.associations.labels.begin(),
                                result.associations.labels.end(),
                                [](
                                    const ChartLabel& candidate
                                ) noexcept {
                                    return
                                        candidate.kind ==
                                        ChartLabelKind::TITLE;
                                }
                            ) -
                            result.associations.labels.begin()
                        )
                    ].confidence
            ) {

                result.metadata.title =
                    label.text;
            }
        }
    }

    // =========================================================================
    // DATA GENERATION
    // =========================================================================

    switch (
        type
    ) {

        case ChartType::BAR:
        case ChartType::COLUMN:
        case ChartType::STACKED_BAR:
        case ChartType::STACKED_COLUMN:
        case ChartType::HUNDRED_PERCENT_STACKED_BAR:
        case ChartType::HUNDRED_PERCENT_STACKED_COLUMN:
        case ChartType::CLUSTERED_BAR:
        case ChartType::CLUSTERED_COLUMN:
        case ChartType::WATERFALL:

            result.data_points =
                build_bar_data(
                    coordinates,
                    objects,
                    associations
                );

            if (
                type ==
                ChartType::WATERFALL
            ) {

                result.data_points =
                    build_waterfall_data(
                        coordinates,
                        objects,
                        associations
                    );
            }

            break;

        case ChartType::LINE:
        case ChartType::AREA:
        case ChartType::STACKED_AREA:
        case ChartType::COMBO_LINE_BAR:
        case ChartType::COMBO_LINE_COLUMN:

            result.data_points =
                build_line_data(
                    coordinates,
                    objects,
                    associations
                );

            if (
                !objects.bars.empty()
            ) {

                std::vector<ChartDataPoint> bar_data =
                    build_bar_data(
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

        case ChartType::PIE:
        case ChartType::DONUT:

            result.data_points =
                build_radial_data(
                    objects,
                    associations
                );

            break;

        case ChartType::SCATTER:
        case ChartType::BUBBLE:

            result.data_points =
                build_scatter_data(
                    coordinates,
                    objects,
                    associations
                );

            break;

        case ChartType::FUNNEL:

            result.data_points =
                build_funnel_data(
                    objects,
                    associations
                );

            break;

        case ChartType::TREEMAP:

            result.data_points =
                build_treemap_data(
                    objects,
                    associations
                );

            break;

        default:
            break;
    }

    // =========================================================================
    // AXIS TITLES / LABEL DERIVATION
    // =========================================================================
    //
    // The current ChartLabel model does not carry an explicit "axis title"
    // classification separate from X/Y labels. Therefore only explicitly
    // classified labels are used here.
    //
    // Do not promote arbitrary labels into axis titles.
    // =========================================================================

    // =========================================================================
    // VALIDITY
    // =========================================================================

    const bool has_type =
        type != ChartType::UNKNOWN;

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
    // CONFIDENCE
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    confidence_sum +=
        static_cast<double>(
            objects.confidence
        );

    ++confidence_count;

    confidence_sum +=
        static_cast<double>(
            associations.confidence
        );

    ++confidence_count;

    if (
        type != ChartType::UNKNOWN
    ) {

        confidence_sum +=
            0.50;

        ++confidence_count;
    }

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

} // namespace fin_ocr::chart
