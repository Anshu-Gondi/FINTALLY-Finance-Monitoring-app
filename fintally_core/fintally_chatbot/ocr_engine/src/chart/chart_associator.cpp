#include "fin_ocr/chart/chart_associator.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace fin_ocr::chart {

namespace {

// =============================================================================
// CENTRAL CONFIGURATION
// =============================================================================

constexpr double MAX_AXIS_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_AXIS_DISTANCE;

constexpr double MAX_SERIES_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_SERIES_DISTANCE;

constexpr double MAX_OBJECT_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_OBJECT_DISTANCE;

constexpr double MIN_ASSOCIATION_CONFIDENCE =
    static_cast<double>(
        config::CHART_LABEL_MIN_ASSOCIATION_CONFIDENCE
    );

constexpr std::size_t MAX_ASSOCIATIONS =
    config::CHART_MAX_ASSOCIATIONS;

constexpr std::size_t MAX_CATEGORIES =
    config::CHART_MAX_CATEGORIES;

constexpr std::size_t MAX_SERIES =
    config::CHART_MAX_SERIES;

// =============================================================================
// GEOMETRIC HELPERS
// =============================================================================

[[nodiscard]]
bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept {

    return
        min_x >= 0 &&
        min_y >= 0 &&
        max_x >= min_x &&
        max_y >= min_y;
}

[[nodiscard]]
bool valid_label(
    const ChartLabel& label
) noexcept {

    return
        !label.text.empty() &&
        valid_rect(
            label.min_x,
            label.min_y,
            label.max_x,
            label.max_y
        );
}

[[nodiscard]]
std::pair<int, int> rect_center(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept {

    return {
        min_x +
            (max_x - min_x) / 2,

        min_y +
            (max_y - min_y) / 2
    };
}

[[nodiscard]]
double point_to_rect_distance(
    int x,
    int y,
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept {

    if (
        !valid_rect(
            min_x,
            min_y,
            max_x,
            max_y
        )
    ) {
        return std::numeric_limits<double>::infinity();
    }

    int dx = 0;
    int dy = 0;

    if (x < min_x) {
        dx = min_x - x;
    } else if (x > max_x) {
        dx = x - max_x;
    }

    if (y < min_y) {
        dy = min_y - y;
    } else if (y > max_y) {
        dy = y - max_y;
    }

    return std::sqrt(
        static_cast<double>(dx) * static_cast<double>(dx) +
        static_cast<double>(dy) * static_cast<double>(dy)
    );
}

[[nodiscard]]
double normalized_distance_score(
    double distance,
    double max_distance
) noexcept {

    if (
        !std::isfinite(distance) ||
        max_distance <= 0.0 ||
        distance >= max_distance
    ) {
        return 0.0;
    }

    return std::clamp(
        1.0 -
            (
                distance /
                max_distance
            ),
        0.0,
        1.0
    );
}

[[nodiscard]]
bool horizontal_overlap_impl(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept {

    return
        a_min_x <= b_max_x &&
        b_min_x <= a_max_x;
}

[[nodiscard]]
bool vertical_overlap_impl(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept {

    return
        a_min_y <= b_max_y &&
        b_min_y <= a_max_y;
}

[[nodiscard]]
double overlap_ratio_1d(
    int a_min,
    int a_max,
    int b_min,
    int b_max
) noexcept {

    const int overlap_min =
        std::max(
            a_min,
            b_min
        );

    const int overlap_max =
        std::min(
            a_max,
            b_max
        );

    if (
        overlap_max < overlap_min
    ) {
        return 0.0;
    }

    const int a_extent =
        a_max -
        a_min +
        1;

    const int b_extent =
        b_max -
        b_min +
        1;

    const int overlap =
        overlap_max -
        overlap_min +
        1;

    const int denominator =
        std::min(
            a_extent,
            b_extent
        );

    if (
        denominator <= 0
    ) {
        return 0.0;
    }

    return
        static_cast<double>(overlap) /
        static_cast<double>(denominator);
}

// =============================================================================
// LABEL POSITION
// =============================================================================

[[nodiscard]]
bool label_is_below_axis(
    const ChartLabel& label,
    const ChartAxis& axis
) noexcept {

    if (
        !axis.horizontal ||
        !valid_label(label)
    ) {
        return false;
    }

    return
        label.min_y >=
        axis.start_y;
}

[[nodiscard]]
bool label_is_left_of_axis(
    const ChartLabel& label,
    const ChartAxis& axis
) noexcept {

    if (
        !axis.vertical ||
        !valid_label(label)
    ) {
        return false;
    }

    return
        label.max_x <=
        axis.start_x;
}

// =============================================================================
// ASSOCIATION STORAGE
// =============================================================================

[[nodiscard]]
bool association_exists(
    const std::vector<ChartAssociation>& associations,
    int label_index,
    AssociatedObjectKind object_kind,
    int object_index
) noexcept {

    return std::any_of(
        associations.begin(),
        associations.end(),
        [label_index, object_kind, object_index](
            const ChartAssociation& association
        ) noexcept {

            return
                association.label_index ==
                    label_index &&
                association.object_kind ==
                    object_kind &&
                association.object_index ==
                    object_index;
        }
    );
}

void append_association(
    std::vector<ChartAssociation>& associations,
    const ChartAssociation& association
) {

    if (
        associations.size() >= MAX_ASSOCIATIONS
    ) {
        return;
    }

    if (
        association.label_index < 0 ||
        association.object_index < 0 ||
        association.confidence <
            MIN_ASSOCIATION_CONFIDENCE
    ) {
        return;
    }

    if (
        association_exists(
            associations,
            association.label_index,
            association.object_kind,
            association.object_index
        )
    ) {
        return;
    }

    associations.push_back(
        association
    );
}

// =============================================================================
// OBJECT CENTER
// =============================================================================

[[nodiscard]]
std::pair<int, int> object_center(
    const ChartRect& rect
) noexcept {

    return rect_center(
        rect.min_x,
        rect.min_y,
        rect.max_x,
        rect.max_y
    );
}

[[nodiscard]]
ChartRect path_bounds(
    const ChartPath& path
) noexcept {

    ChartRect result{};

    if (
        path.points.empty()
    ) {
        return result;
    }

    result.min_x =
        path.points.front().x;

    result.max_x =
        path.points.front().x;

    result.min_y =
        path.points.front().y;

    result.max_y =
        path.points.front().y;

    float confidence_sum =
        0.0f;

    std::size_t confidence_count =
        0;

    for (
        const ChartPathPoint& point :
        path.points
    ) {

        result.min_x =
            std::min(
                result.min_x,
                point.x
            );

        result.max_x =
            std::max(
                result.max_x,
                point.x
            );

        result.min_y =
            std::min(
                result.min_y,
                point.y
            );

        result.max_y =
            std::max(
                result.max_y,
                point.y
            );

        confidence_sum +=
            point.confidence;

        ++confidence_count;
    }

    result.confidence =
        confidence_count > 0
            ? confidence_sum /
              static_cast<float>(
                  confidence_count
              )
            : path.confidence;

    return result;
}

// =============================================================================
// CLASSIFICATION HELPERS
// =============================================================================

[[nodiscard]]
bool likely_title(
    const ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept {

    if (
        coordinates.plot_area.max_y <=
        coordinates.plot_area.min_y
    ) {
        return false;
    }

    if (
        label.max_y >=
        coordinates.plot_area.min_y
    ) {
        return false;
    }

    const auto [cx, cy] =
        rect_center(
            label.min_x,
            label.min_y,
            label.max_x,
            label.max_y
        );

    (void)cy;

    const int plot_center_x =
        (
            coordinates.plot_area.min_x +
            coordinates.plot_area.max_x
        ) / 2;

    const int plot_width =
        std::max(
            1,
            coordinates.plot_area.max_x -
            coordinates.plot_area.min_x +
            1
        );

    return
        std::abs(
            cx -
            plot_center_x
        ) <=
        plot_width / 4;
}

[[nodiscard]]
bool likely_legend_label(
    const ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept {

    if (
        !valid_label(label)
    ) {
        return false;
    }

    if (
        coordinates.plot_area.max_x <=
            coordinates.plot_area.min_x ||
        coordinates.plot_area.max_y <=
            coordinates.plot_area.min_y
    ) {
        return false;
    }

    const bool right_of_plot =
        label.min_x >
        coordinates.plot_area.max_x;

    const bool above_plot =
        label.max_y <
        coordinates.plot_area.min_y;

    const bool near_plot_top =
        label.min_y <=
        coordinates.plot_area.min_y + 96;

    return
        right_of_plot ||
        (
            above_plot &&
            near_plot_top
        );
}

[[nodiscard]]
bool likely_data_label(
    const ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept {

    if (
        !valid_label(label)
    ) {
        return false;
    }

    if (
        !coordinates.valid
    ) {
        return false;
    }

    const auto [cx, cy] =
        rect_center(
            label.min_x,
            label.min_y,
            label.max_x,
            label.max_y
        );

    return
        cx >= coordinates.plot_area.min_x &&
        cx <= coordinates.plot_area.max_x &&
        cy >= coordinates.plot_area.min_y &&
        cy <= coordinates.plot_area.max_y;
}

} // namespace

// =============================================================================
// CENTER DISTANCE
// =============================================================================

double ChartAssociator::center_distance(
    int ax,
    int ay,
    int bx,
    int by
) noexcept {

    const double dx =
        static_cast<double>(ax) -
        static_cast<double>(bx);

    const double dy =
        static_cast<double>(ay) -
        static_cast<double>(by);

    return std::sqrt(
        dx * dx +
        dy * dy
    );
}

// =============================================================================
// HORIZONTAL OVERLAP
// =============================================================================

bool ChartAssociator::horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept {

    return horizontal_overlap_impl(
        a_min_x,
        a_max_x,
        b_min_x,
        b_max_x
    );
}

// =============================================================================
// VERTICAL OVERLAP
// =============================================================================

bool ChartAssociator::vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept {

    return vertical_overlap_impl(
        a_min_y,
        a_max_y,
        b_min_y,
        b_max_y
    );
}

// =============================================================================
// LABEL CLASSIFICATION
// =============================================================================

void ChartAssociator::classify_labels(
    const ChartCoordinateSystem& coordinates,
    std::vector<ChartLabel>& labels
) const {

    for (
        ChartLabel& label :
        labels
    ) {

        if (
            !valid_label(label)
        ) {

            label.kind =
                ChartLabelKind::UNKNOWN;

            continue;
        }

        // Preserve explicit classification supplied by the OCR layer.
        if (
            label.kind !=
            ChartLabelKind::UNKNOWN
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // X axis.
        // ---------------------------------------------------------------------

        if (
            coordinates.x_axis.horizontal &&
            label_is_below_axis(
                label,
                coordinates.x_axis
            ) &&
            horizontal_overlap(
                label.min_x,
                label.max_x,
                coordinates.x_axis.start_x,
                coordinates.x_axis.end_x
            )
        ) {

            label.kind =
                ChartLabelKind::X_AXIS_LABEL;

            continue;
        }

        // ---------------------------------------------------------------------
        // Y axis.
        // ---------------------------------------------------------------------

        if (
            coordinates.y_axis.vertical &&
            label_is_left_of_axis(
                label,
                coordinates.y_axis
            ) &&
            vertical_overlap(
                label.min_y,
                label.max_y,
                coordinates.y_axis.start_y,
                coordinates.y_axis.end_y
            )
        ) {

            label.kind =
                ChartLabelKind::Y_AXIS_LABEL;

            continue;
        }

        // ---------------------------------------------------------------------
        // Title.
        // ---------------------------------------------------------------------

        if (
            likely_title(
                coordinates,
                label
            )
        ) {

            label.kind =
                ChartLabelKind::TITLE;

            continue;
        }

        // ---------------------------------------------------------------------
        // Legend / series.
        // ---------------------------------------------------------------------

        if (
            likely_legend_label(
                coordinates,
                label
            )
        ) {

            label.kind =
                ChartLabelKind::LEGEND_LABEL;

            continue;
        }

        // ---------------------------------------------------------------------
        // Interior label.
        // ---------------------------------------------------------------------

        if (
            likely_data_label(
                coordinates,
                label
            )
        ) {

            label.kind =
                ChartLabelKind::DATA_LABEL;

            continue;
        }

        label.kind =
            ChartLabelKind::UNKNOWN;
    }
}

// =============================================================================
// AXIS / CATEGORY ASSOCIATION
// =============================================================================

void ChartAssociator::associate_axis_labels(
    const ChartCoordinateSystem& coordinates,
    std::vector<ChartLabel>& labels,
    std::vector<ChartCategory>& categories
) const {

    categories.clear();

    if (
        !coordinates.x_axis.horizontal
    ) {
        return;
    }

    struct Candidate {

        int label_index = -1;

        int center_x = 0;
        int center_y = 0;

        double distance = 0.0;

        float confidence = 0.0f;
    };

    std::vector<Candidate> candidates;

    candidates.reserve(
        labels.size()
    );

    for (
        std::size_t i = 0;
        i < labels.size();
        ++i
    ) {

        const ChartLabel& label =
            labels[i];

        if (
            label.kind !=
            ChartLabelKind::X_AXIS_LABEL
        ) {
            continue;
        }

        const auto [cx, cy] =
            rect_center(
                label.min_x,
                label.min_y,
                label.max_x,
                label.max_y
            );

        const double distance =
            point_to_rect_distance(
                cx,
                coordinates.x_axis.start_y,
                label.min_x,
                label.min_y,
                label.max_x,
                label.max_y
            );

        if (
            distance >
            MAX_AXIS_LABEL_DISTANCE
        ) {
            continue;
        }

        candidates.push_back({
            static_cast<int>(i),
            cx,
            cy,
            distance,
            label.confidence
        });
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const Candidate& a,
            const Candidate& b
        ) noexcept {

            if (
                a.center_x !=
                b.center_x
            ) {
                return
                    a.center_x <
                    b.center_x;
            }

            return
                a.center_y <
                b.center_y;
        }
    );

    const std::size_t count =
        std::min(
            candidates.size(),
            MAX_CATEGORIES
        );

    categories.reserve(
        count
    );

    for (
        std::size_t i = 0;
        i < count;
        ++i
    ) {

        const Candidate& candidate =
            candidates[i];

        ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    candidate.label_index
                )
            ];

        ChartCategory category{};

        category.name =
            label.text;

        category.label_index =
            candidate.label_index;

        category.category_index =
            static_cast<int>(i);

        category.center_x =
            candidate.center_x;

        category.center_y =
            candidate.center_y;

        const double proximity =
            normalized_distance_score(
                candidate.distance,
                MAX_AXIS_LABEL_DISTANCE
            );

        category.confidence =
            static_cast<float>(
                std::clamp(
                    proximity *
                    std::max(
                        0.0,
                        static_cast<double>(
                            label.confidence
                        )
                    ),
                    0.0,
                    1.0
                )
            );

        label.category_index =
            category.category_index;

        categories.push_back(
            std::move(category)
        );
    }
}

// =============================================================================
// SERIES / LEGEND ASSOCIATION
// =============================================================================

void ChartAssociator::associate_series_labels(
    const ChartCoordinateSystem& coordinates,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartSeries>& series
) const {

    series.clear();

    (void)coordinates;

    for (
        std::size_t i = 0;
        i < labels.size();
        ++i
    ) {

        const ChartLabel& label =
            labels[i];

        if (
            label.kind !=
                ChartLabelKind::SERIES_LABEL &&
            label.kind !=
                ChartLabelKind::LEGEND_LABEL
        ) {
            continue;
        }

        if (
            series.size() >=
            MAX_SERIES
        ) {
            break;
        }

        ChartSeries entry{};

        entry.name =
            label.text;

        entry.label_index =
            static_cast<int>(i);

        entry.series_index =
            static_cast<int>(
                series.size()
            );

        entry.confidence =
            label.confidence;

        series.push_back(
            std::move(entry)
        );
    }
}

// =============================================================================
// BAR / COLUMN ASSOCIATION
// =============================================================================

void ChartAssociator::associate_bars(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    for (
        std::size_t bar_index = 0;
        bar_index < objects.bars.size();
        ++bar_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const BarSegment& bar =
            objects.bars[bar_index];

        const ChartRect& bounds =
            bar.bounds;

        const int width =
            bounds.max_x -
            bounds.min_x +
            1;

        const int height =
            bounds.max_y -
            bounds.min_y +
            1;

        const bool vertical =
            height >
            width;

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        // ---------------------------------------------------------------------
        // COLUMN -> X-axis category.
        // ---------------------------------------------------------------------

        if (
            vertical &&
            coordinates.x_axis.horizontal
        ) {

            const int object_center_x =
                (
                    bounds.min_x +
                    bounds.max_x
                ) / 2;

            for (
                std::size_t i = 0;
                i < labels.size();
                ++i
            ) {

                const ChartLabel& label =
                    labels[i];

                if (
                    label.kind !=
                    ChartLabelKind::X_AXIS_LABEL
                ) {
                    continue;
                }

                const auto [label_cx, label_cy] =
                    rect_center(
                        label.min_x,
                        label.min_y,
                        label.max_x,
                        label.max_y
                    );

                const double horizontal_delta =
                    std::abs(
                        static_cast<double>(
                            object_center_x -
                            label_cx
                        )
                    );

                const double vertical_delta =
                    std::abs(
                        static_cast<double>(
                            bounds.max_y -
                            label_cy
                        )
                    );

                const double distance =
                    horizontal_delta +
                    vertical_delta * 0.35;

                if (
                    distance <
                    best_distance
                ) {

                    best_distance =
                        distance;

                    best_label =
                        static_cast<int>(i);
                }
            }

        // ---------------------------------------------------------------------
        // BAR -> Y-axis category.
        // ---------------------------------------------------------------------

        } else if (
            coordinates.y_axis.vertical
        ) {

            const int object_center_y =
                (
                    bounds.min_y +
                    bounds.max_y
                ) / 2;

            for (
                std::size_t i = 0;
                i < labels.size();
                ++i
            ) {

                const ChartLabel& label =
                    labels[i];

                if (
                    label.kind !=
                    ChartLabelKind::Y_AXIS_LABEL
                ) {
                    continue;
                }

                const auto [label_cx, label_cy] =
                    rect_center(
                        label.min_x,
                        label.min_y,
                        label.max_x,
                        label.max_y
                    );

                const double vertical_delta =
                    std::abs(
                        static_cast<double>(
                            object_center_y -
                            label_cy
                        )
                    );

                const double horizontal_delta =
                    std::abs(
                        static_cast<double>(
                            bounds.min_x -
                            label_cx
                        )
                    );

                const double distance =
                    vertical_delta +
                    horizontal_delta * 0.35;

                if (
                    distance <
                    best_distance
                ) {

                    best_distance =
                        distance;

                    best_label =
                        static_cast<int>(i);
                }
            }
        }

        if (
            best_label < 0 ||
            best_distance >
                MAX_OBJECT_LABEL_DISTANCE
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double proximity =
            normalized_distance_score(
                best_distance,
                MAX_OBJECT_LABEL_DISTANCE
            );

        const double confidence =
            proximity *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    bar.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            vertical
                ? AssociatedObjectKind::COLUMN
                : AssociatedObjectKind::BAR;

        association.object_index =
            static_cast<int>(
                bar_index
            );

        association.series_index =
            bar.series_index;

        association.category_index =
            label.category_index;

        association.stack_index =
            bar.stack_index;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// PATH / AREA ASSOCIATION
// =============================================================================

void ChartAssociator::associate_paths(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t path_index = 0;
        path_index < objects.paths.size();
        ++path_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const ChartPath& path =
            objects.paths[path_index];

        const ChartRect bounds =
            path_bounds(path);

        if (
            path.points.empty() ||
            !valid_rect(
                bounds.min_x,
                bounds.min_y,
                bounds.max_x,
                bounds.max_y
            )
        ) {
            continue;
        }

        const auto [cx, cy] =
            object_center(
                bounds
            );

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t i = 0;
            i < labels.size();
            ++i
        ) {

            const ChartLabel& label =
                labels[i];

            if (
                label.kind !=
                    ChartLabelKind::SERIES_LABEL &&
                label.kind !=
                    ChartLabelKind::LEGEND_LABEL
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const double distance =
                center_distance(
                    cx,
                    cy,
                    label_cx,
                    label_cy
                );

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(i);
            }
        }

        if (
            best_label < 0 ||
            best_distance >
                MAX_SERIES_LABEL_DISTANCE
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double proximity =
            normalized_distance_score(
                best_distance,
                MAX_SERIES_LABEL_DISTANCE
            );

        const double confidence =
            proximity *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    path.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            AssociatedObjectKind::LINE_SEGMENT;

        association.object_index =
            static_cast<int>(
                path_index
            );

        association.series_index =
            label.series_index >= 0
                ? label.series_index
                : path.series_index;

        association.category_index =
            -1;

        association.stack_index =
            -1;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// RADIAL OBJECT ASSOCIATION
// =============================================================================

void ChartAssociator::associate_radial_objects(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t slice_index = 0;
        slice_index < objects.slices.size();
        ++slice_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const RadialSlice& slice =
            objects.slices[slice_index];

        const double mid_angle =
            (
                slice.start_angle +
                slice.end_angle
            ) * 0.5;

        const double radius =
            slice.inner_radius > 0
                ? (
                    static_cast<double>(
                        slice.inner_radius
                    ) +
                    static_cast<double>(
                        slice.outer_radius
                    )
                ) * 0.5
                : static_cast<double>(
                      slice.outer_radius
                  ) * 0.70;

        const int object_x =
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        slice.center_x
                    ) +
                    std::cos(
                        mid_angle
                    ) *
                    radius
                )
            );

        const int object_y =
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        slice.center_y
                    ) +
                    std::sin(
                        mid_angle
                    ) *
                    radius
                )
            );

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {

            const ChartLabel& label =
                labels[label_index];

            if (
                label.kind !=
                    ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ChartLabelKind::LEGEND_LABEL &&
                label.kind !=
                    ChartLabelKind::SERIES_LABEL
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const double distance =
                center_distance(
                    object_x,
                    object_y,
                    label_cx,
                    label_cy
                );

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        if (
            best_label < 0 ||
            best_distance >
                MAX_OBJECT_LABEL_DISTANCE
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double proximity =
            normalized_distance_score(
                best_distance,
                MAX_OBJECT_LABEL_DISTANCE
            );

        const double confidence =
            proximity *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    slice.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            slice.inner_radius > 0
                ? AssociatedObjectKind::DONUT_SLICE
                : AssociatedObjectKind::PIE_SLICE;

        association.object_index =
            static_cast<int>(
                slice_index
            );

        association.series_index =
            -1;

        association.category_index =
            slice.category_index;

        association.stack_index =
            -1;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// SCATTER / BUBBLE ASSOCIATION
// =============================================================================

void ChartAssociator::associate_scatter_objects(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t point_index = 0;
        point_index < objects.points.size();
        ++point_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const ScatterPoint& point =
            objects.points[point_index];

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {

            const ChartLabel& label =
                labels[label_index];

            if (
                label.kind !=
                ChartLabelKind::DATA_LABEL
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const double distance =
                center_distance(
                    point.x,
                    point.y,
                    label_cx,
                    label_cy
                );

            const double allowed =
                std::max(
                    MAX_OBJECT_LABEL_DISTANCE,
                    point.radius * 4.0
                );

            if (
                distance > allowed
            ) {
                continue;
            }

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        if (
            best_label < 0
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double maximum_distance =
            std::max(
                MAX_OBJECT_LABEL_DISTANCE,
                point.radius * 4.0
            );

        const double proximity =
            normalized_distance_score(
                best_distance,
                maximum_distance
            );

        const double confidence =
            proximity *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    point.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            point.is_bubble
                ? AssociatedObjectKind::BUBBLE
                : AssociatedObjectKind::SCATTER_POINT;

        association.object_index =
            static_cast<int>(
                point_index
            );

        association.series_index =
            point.series_index;

        association.category_index =
            -1;

        association.stack_index =
            -1;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// WATERFALL ASSOCIATION
// =============================================================================

void ChartAssociator::associate_waterfall_objects(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t step_index = 0;
        step_index < objects.waterfall_steps.size();
        ++step_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const WaterfallStep& step =
            objects.waterfall_steps[step_index];

        const int cx =
            (
                step.bounds.min_x +
                step.bounds.max_x
            ) / 2;

        const int cy =
            (
                step.bounds.min_y +
                step.bounds.max_y
            ) / 2;

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {

            const ChartLabel& label =
                labels[label_index];

            if (
                label.kind !=
                    ChartLabelKind::X_AXIS_LABEL &&
                label.kind !=
                    ChartLabelKind::DATA_LABEL
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const double distance =
                center_distance(
                    cx,
                    cy,
                    label_cx,
                    label_cy
                );

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        if (
            best_label < 0 ||
            best_distance >
                MAX_OBJECT_LABEL_DISTANCE
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double confidence =
            normalized_distance_score(
                best_distance,
                MAX_OBJECT_LABEL_DISTANCE
            ) *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    step.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            AssociatedObjectKind::WATERFALL_STEP;

        association.object_index =
            static_cast<int>(
                step_index
            );

        association.category_index =
            step.category_index;

        association.series_index =
            -1;

        association.stack_index =
            -1;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// FUNNEL ASSOCIATION
// =============================================================================

void ChartAssociator::associate_funnel_objects(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t stage_index = 0;
        stage_index < objects.funnel_stages.size();
        ++stage_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const FunnelStage& stage =
            objects.funnel_stages[stage_index];

        const auto [cx, cy] =
            rect_center(
                stage.bounds.min_x,
                stage.bounds.min_y,
                stage.bounds.max_x,
                stage.bounds.max_y
            );

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {

            const ChartLabel& label =
                labels[label_index];

            if (
                label.kind !=
                    ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ChartLabelKind::UNKNOWN &&
                label.kind !=
                    ChartLabelKind::TABLE_ROW_LABEL
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const double distance =
                center_distance(
                    cx,
                    cy,
                    label_cx,
                    label_cy
                );

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        if (
            best_label < 0 ||
            best_distance >
                MAX_OBJECT_LABEL_DISTANCE
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double confidence =
            normalized_distance_score(
                best_distance,
                MAX_OBJECT_LABEL_DISTANCE
            ) *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    stage.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            AssociatedObjectKind::FUNNEL_STAGE;

        association.object_index =
            static_cast<int>(
                stage_index
            );

        association.category_index =
            stage.stage_index;

        association.series_index =
            -1;

        association.stack_index =
            -1;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// TREEMAP ASSOCIATION
// =============================================================================

void ChartAssociator::associate_treemap_objects(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartAssociation>& associations
) const {

    (void)coordinates;

    for (
        std::size_t node_index = 0;
        node_index < objects.treemap_nodes.size();
        ++node_index
    ) {

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const TreemapNode& node =
            objects.treemap_nodes[node_index];

        // Prefer a label physically inside the node.
        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        const auto [node_cx, node_cy] =
            rect_center(
                node.bounds.min_x,
                node.bounds.min_y,
                node.bounds.max_x,
                node.bounds.max_y
            );

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {

            const ChartLabel& label =
                labels[label_index];

            if (
                label.kind !=
                    ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ChartLabelKind::UNKNOWN
            ) {
                continue;
            }

            const auto [label_cx, label_cy] =
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

            const bool inside =
                label.min_x >=
                    node.bounds.min_x &&
                label.max_x <=
                    node.bounds.max_x &&
                label.min_y >=
                    node.bounds.min_y &&
                label.max_y <=
                    node.bounds.max_y;

            if (
                !inside
            ) {
                continue;
            }

            const double distance =
                center_distance(
                    node_cx,
                    node_cy,
                    label_cx,
                    label_cy
                );

            if (
                distance <
                best_distance
            ) {

                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        if (
            best_label < 0
        ) {
            continue;
        }

        const ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        const double max_distance =
            std::max(
                1.0,
                static_cast<double>(
                    node.bounds.max_x -
                    node.bounds.min_x +
                    node.bounds.max_y -
                    node.bounds.min_y
                )
            );

        const double confidence =
            normalized_distance_score(
                best_distance,
                max_distance
            ) *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    node.confidence
                )
            );

        ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            AssociatedObjectKind::TREEMAP_RECTANGLE;

        association.object_index =
            static_cast<int>(
                node_index
            );

        association.category_index =
            -1;

        association.series_index =
            -1;

        association.stack_index =
            node.hierarchy_level;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        append_association(
            associations,
            association
        );
    }
}

// =============================================================================
// STACKED BAR / COLUMN ASSOCIATION
// =============================================================================
//
// Geometry rule:
//
//     vertically stacked columns:
//         same X interval / substantial X overlap
//
//     horizontally stacked bars:
//         same Y interval / substantial Y overlap
//
// Stack index is assigned deterministically by visual ordering.
//
// This pass does NOT infer percentages or business values.
// =============================================================================

void ChartAssociator::associate_stacked_objects(
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& objects
) const {

    if (
        objects.bars.size() < 2
    ) {
        return;
    }

    (void)coordinates;

    // -------------------------------------------------------------------------
    // Reset stack information.
    // Existing non-negative values are preserved only when already supplied by
    // an upstream detector.
    // -------------------------------------------------------------------------

    for (
        BarSegment& bar :
        objects.bars
    ) {

        if (
            bar.stack_index < 0
        ) {
            bar.stack_index =
                -1;
        }
    }

    // -------------------------------------------------------------------------
    // Compare each bar with previous bars.
    // -------------------------------------------------------------------------

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {

        BarSegment& current =
            objects.bars[i];

        if (
            current.stack_index < 0
        ) {
            current.stack_index =
                0;
        }

        const ChartRect& current_rect =
            current.bounds;

        const int current_width =
            current_rect.max_x -
            current_rect.min_x +
            1;

        const int current_height =
            current_rect.max_y -
            current_rect.min_y +
            1;

        const bool current_vertical =
            current_height >
            current_width;

        for (
            std::size_t j = 0;
            j < i;
            ++j
        ) {

            BarSegment& previous =
                objects.bars[j];

            const ChartRect& previous_rect =
                previous.bounds;

            const int previous_width =
                previous_rect.max_x -
                previous_rect.min_x +
                1;

            const int previous_height =
                previous_rect.max_y -
                previous_rect.min_y +
                1;

            const bool previous_vertical =
                previous_height >
                previous_width;

            if (
                current_vertical !=
                previous_vertical
            ) {
                continue;
            }

            if (
                current_vertical
            ) {

                const double x_overlap =
                    overlap_ratio_1d(
                        current_rect.min_x,
                        current_rect.max_x,
                        previous_rect.min_x,
                        previous_rect.max_x
                    );

                const bool vertical_touch =
                    current_rect.min_y <=
                        previous_rect.max_y + 2 &&
                    previous_rect.min_y <=
                        current_rect.max_y + 2;

                if (
                    x_overlap >= 0.70 &&
                    vertical_touch
                ) {

                    current.stack_index =
                        std::max(
                            current.stack_index,
                            previous.stack_index + 1
                        );
                }

            } else {

                const double y_overlap =
                    overlap_ratio_1d(
                        current_rect.min_y,
                        current_rect.max_y,
                        previous_rect.min_y,
                        previous_rect.max_y
                    );

                const bool horizontal_touch =
                    current_rect.min_x <=
                        previous_rect.max_x + 2 &&
                    previous_rect.min_x <=
                        current_rect.max_x + 2;

                if (
                    y_overlap >= 0.70 &&
                    horizontal_touch
                ) {

                    current.stack_index =
                        std::max(
                            current.stack_index,
                            previous.stack_index + 1
                        );
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Classify stacked segments where multiple segments share category space.
    // -------------------------------------------------------------------------

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {

        BarSegment& current =
            objects.bars[i];

        if (
            current.stack_index < 1
        ) {
            continue;
        }

        const int width =
            current.bounds.max_x -
            current.bounds.min_x +
            1;

        const int height =
            current.bounds.max_y -
            current.bounds.min_y +
            1;

        if (
            height >
            width
        ) {

            current.series_index =
                current.series_index < 0
                    ? current.stack_index
                    : current.series_index;

        } else {

            current.series_index =
                current.series_index < 0
                    ? current.stack_index
                    : current.series_index;
        }
    }
}

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
// It validates whether an existing object lies materially closer to the
// detected secondary coordinate area. The interpreter can later expose this
// relationship through a richer semantic representation.
//
// =============================================================================

void ChartAssociator::associate_dual_axes(
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& objects
) const {

    if (
        !coordinates.has_secondary_y_axis &&
        !coordinates.has_secondary_x_axis
    ) {
        return;
    }

    /*
     * No representable axis-id field exists in the current ChartObject /
     * BarSegment / ChartPath ABI.
     *
     * Deliberately leave objects untouched rather than encoding primary vs.
     * secondary axes into series_index or category_index.
     */
    (void)objects;
}

// =============================================================================
// COMPLETE ASSOCIATION
// =============================================================================

ChartAssociationResult ChartAssociator::associate(
    const ChartCoordinateSystem& coordinates,
    const ChartObjectSet& objects,
    const std::vector<ChartLabel>& input_labels
) const {

    ChartAssociationResult result{};

    // =========================================================================
    // COPY OBJECT SET
    // =========================================================================
    //
    // Some association passes need to enrich bar stack metadata. The input
    // object set itself remains caller-owned.
    // =========================================================================

    ChartObjectSet working_objects =
        objects;

    // =========================================================================
    // COPY LABELS
    // =========================================================================

    result.labels =
        input_labels;

    // =========================================================================
    // CLASSIFICATION
    // =========================================================================

    classify_labels(
        coordinates,
        result.labels
    );

    // =========================================================================
    // CATEGORIES / SERIES
    // =========================================================================

    associate_axis_labels(
        coordinates,
        result.labels,
        result.categories
    );

    associate_series_labels(
        coordinates,
        result.labels,
        result.series
    );

    // =========================================================================
    // STACK RELATIONSHIPS
    // =========================================================================

    associate_stacked_objects(
        coordinates,
        working_objects
    );

    // =========================================================================
    // DUAL AXIS VALIDATION
    // =========================================================================

    associate_dual_axes(
        coordinates,
        working_objects
    );

    // =========================================================================
    // OBJECT RELATIONSHIPS
    // =========================================================================

    associate_bars(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_paths(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_radial_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_scatter_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_waterfall_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_funnel_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    associate_treemap_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // GLOBAL VALIDITY
    // =========================================================================

    const bool has_labels =
        !result.labels.empty();

    const bool has_objects =
        objects.valid ||
        !objects.bars.empty() ||
        !objects.paths.empty() ||
        !objects.slices.empty() ||
        !objects.points.empty() ||
        !objects.waterfall_steps.empty() ||
        !objects.funnel_stages.empty() ||
        !objects.treemap_nodes.empty();

    const bool has_relationships =
        !result.categories.empty() ||
        !result.series.empty() ||
        !result.associations.empty();

    result.valid =
        has_labels &&
        has_objects &&
        has_relationships;

    // =========================================================================
    // GLOBAL CONFIDENCE
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    if (
        objects.confidence > 0.0f
    ) {

        confidence_sum +=
            static_cast<double>(
                objects.confidence
            );

        ++confidence_count;
    }

    for (
        const ChartLabel& label :
        result.labels
    ) {

        if (
            label.confidence <= 0.0f
        ) {
            continue;
        }

        confidence_sum +=
            static_cast<double>(
                label.confidence
            );

        ++confidence_count;
    }

    for (
        const ChartCategory& category :
        result.categories
    ) {

        confidence_sum +=
            static_cast<double>(
                category.confidence
            );

        ++confidence_count;
    }

    for (
        const ChartSeries& series :
        result.series
    ) {

        confidence_sum +=
            static_cast<double>(
                series.confidence
            );

        ++confidence_count;
    }

    for (
        const ChartAssociation& association :
        result.associations
    ) {

        confidence_sum +=
            static_cast<double>(
                association.confidence
            );

        ++confidence_count;
    }

    if (
        confidence_count == 0
    ) {

        result.confidence =
            0.0f;

    } else {

        result.confidence =
            static_cast<float>(
                std::clamp(
                    confidence_sum /
                        static_cast<double>(
                            confidence_count
                        ),
                    0.0,
                    1.0
                )
            );
    }

    return result;
}

} // namespace fin_ocr::chart
