#include "fin_ocr/chart/chart_object_detector.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace fin_ocr::chart {

namespace {

// =============================================================================
// CENTRAL CONFIG ALIASES
// =============================================================================

constexpr int MIN_RECT_WIDTH =
    config::CHART_OBJECT_MIN_RECT_WIDTH;

constexpr int MIN_RECT_HEIGHT =
    config::CHART_OBJECT_MIN_RECT_HEIGHT;

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr double MIN_OBJECT_DENSITY =
    config::CHART_OBJECT_MIN_DENSITY;

constexpr double MIN_BAR_ASPECT =
    config::CHART_OBJECT_MIN_BAR_ASPECT;

constexpr double MAX_BAR_ASPECT =
    config::CHART_OBJECT_MAX_BAR_ASPECT;

constexpr double MIN_LINE_DENSITY =
    config::CHART_OBJECT_MIN_LINE_DENSITY;

constexpr int MAX_COMPONENTS =
    config::CHART_OBJECT_MAX_COMPONENTS;

constexpr int RADIAL_MIN_RADIUS =
    config::CHART_OBJECT_RADIAL_MIN_RADIUS;

constexpr int RADIAL_SAMPLE_COUNT =
    config::CHART_OBJECT_RADIAL_SAMPLE_COUNT;

constexpr double RADIAL_MIN_COVERAGE =
    config::CHART_OBJECT_RADIAL_MIN_COVERAGE;

constexpr int FUNNEL_MIN_STAGES =
    config::CHART_OBJECT_FUNNEL_MIN_STAGES;

constexpr int TREEMAP_MIN_RECT_SIZE =
    config::CHART_OBJECT_TREEMAP_MIN_RECT_SIZE;

// =============================================================================
// GENERAL VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_image(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) noexcept {

    return
        image != nullptr &&
        width > 0 &&
        height > 0 &&
        channels > 0;
}

// =============================================================================
// PIXEL ACCESS
// =============================================================================

[[nodiscard]]
std::size_t pixel_offset(
    int x,
    int y,
    int width,
    int channels
) noexcept {

    return
        (
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)
        ) *
        static_cast<std::size_t>(channels);
}

// =============================================================================
// OBJECT SIGNAL
// =============================================================================
//
// ChartColorIsolator output:
//
//     channel 0 = saturation
//     channel 1 = OCR foreground strength
//     channel 2 = chroma / RGB delta
//
// Text is primarily channel 1.
//
// Colored chart geometry may have weak channel 1 but strong chroma.
//
// Therefore object detection uses a broader geometry signal than OCR.
// =============================================================================

[[nodiscard]]
inline std::uint8_t geometry_signal(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept {

    if (
        image == nullptr ||
        x < 0 ||
        y < 0 ||
        x >= width ||
        channels <= 0
    ) {
        return 0;
    }

    const std::size_t index =
        pixel_offset(
            x,
            y,
            width,
            channels
        );

    if (channels == 1) {

        return image[index];
    }

    if (channels >= 3) {

        const std::uint8_t foreground =
            image[index + 1];

        const std::uint8_t chroma =
            image[index + 2];

        return std::max(
            foreground,
            chroma
        );
    }

    return image[index];
}

// =============================================================================
// OBJECT PIXEL PREDICATE
// =============================================================================

[[nodiscard]]
inline bool is_object_pixel(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept {

    /*
     * The threshold is deliberately slightly above the minimum OCR signal.
     *
     * This keeps very weak compression/noise from becoming chart objects,
     * while allowing colored chart primitives through channel 2.
     */
    constexpr std::uint8_t OBJECT_SIGNAL_THRESHOLD = 24;

    return
        geometry_signal(
            image,
            x,
            y,
            width,
            channels
        ) >= OBJECT_SIGNAL_THRESHOLD;
}

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept {

    return
        max_x >= min_x &&
        max_y >= min_y &&
        (
            max_x -
            min_x +
            1
        ) >= MIN_RECT_WIDTH &&
        (
            max_y -
            min_y +
            1
        ) >= MIN_RECT_HEIGHT;
}

// =============================================================================
// RECT OVERLAP
// =============================================================================

[[nodiscard]]
double intersection_over_union(
    const ChartRect& a,
    const ChartRect& b
) noexcept {

    const int min_x =
        std::max(
            a.min_x,
            b.min_x
        );

    const int min_y =
        std::max(
            a.min_y,
            b.min_y
        );

    const int max_x =
        std::min(
            a.max_x,
            b.max_x
        );

    const int max_y =
        std::min(
            a.max_y,
            b.max_y
        );

    if (
        max_x < min_x ||
        max_y < min_y
    ) {
        return 0.0;
    }

    const double intersection =
        static_cast<double>(
            max_x -
            min_x +
            1
        ) *
        static_cast<double>(
            max_y -
            min_y +
            1
        );

    const double area_a =
        static_cast<double>(
            a.max_x -
            a.min_x +
            1
        ) *
        static_cast<double>(
            a.max_y -
            a.min_y +
            1
        );

    const double area_b =
        static_cast<double>(
            b.max_x -
            b.min_x +
            1
        ) *
        static_cast<double>(
            b.max_y -
            b.min_y +
            1
        );

    const double union_area =
        area_a +
        area_b -
        intersection;

    if (
        union_area <= 0.0
    ) {
        return 0.0;
    }

    return
        intersection /
        union_area;
}

// =============================================================================
// COMPONENT
// =============================================================================

struct Component {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    std::size_t pixels = 0;

    double density = 0.0;

    [[nodiscard]]
    int width() const noexcept {
        return
            max_x -
            min_x +
            1;
    }

    [[nodiscard]]
    int height() const noexcept {
        return
            max_y -
            min_y +
            1;
    }

    [[nodiscard]]
    double aspect() const noexcept {

        const double h =
            static_cast<double>(
                std::max(
                    height(),
                    1
                )
            );

        return
            static_cast<double>(
                width()
            ) /
            h;
    }
};

// =============================================================================
// CONNECTED COMPONENT EXTRACTION
// =============================================================================
//
// This is intentionally local to the object detector.
//
// The dedicated segmentation subsystem can later replace this implementation
// without changing the ChartObjectDetector public API.
//
// 8-connected traversal is preferable here because line/area/bar edges often
// touch diagonally after rasterization.
// =============================================================================

[[nodiscard]]
std::vector<Component> find_components(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) {

    std::vector<Component> components;

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        )
    ) {
        return components;
    }

    const std::size_t pixel_count =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    if (
        pixel_count == 0
    ) {
        return components;
    }

    std::vector<std::uint8_t> visited(
        pixel_count,
        0
    );

    components.reserve(
        std::min<std::size_t>(
            MAX_COMPONENTS,
            256
        )
    );

    constexpr int DX[8] = {
        -1, 0, 1,
        -1,    1,
        -1, 0, 1
    };

    constexpr int DY[8] = {
        -1, -1, -1,
         0,     0,
         1,  1,  1
    };

    std::queue<std::pair<int, int>> queue;

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t root =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            if (
                visited[root] != 0 ||
                !is_object_pixel(
                    image,
                    x,
                    y,
                    width,
                    channels
                )
            ) {
                continue;
            }

            visited[root] = 1;

            queue.push({
                x,
                y
            });

            Component component{};

            component.min_x = x;
            component.max_x = x;
            component.min_y = y;
            component.max_y = y;

            while (
                !queue.empty()
            ) {

                const auto current =
                    queue.front();

                queue.pop();

                const int cx =
                    current.first;

                const int cy =
                    current.second;

                ++component.pixels;

                component.min_x =
                    std::min(
                        component.min_x,
                        cx
                    );

                component.max_x =
                    std::max(
                        component.max_x,
                        cx
                    );

                component.min_y =
                    std::min(
                        component.min_y,
                        cy
                    );

                component.max_y =
                    std::max(
                        component.max_y,
                        cy
                    );

                for (
                    int direction = 0;
                    direction < 8;
                    ++direction
                ) {

                    const int nx =
                        cx +
                        DX[direction];

                    const int ny =
                        cy +
                        DY[direction];

                    if (
                        nx < 0 ||
                        ny < 0 ||
                        nx >= width ||
                        ny >= height
                    ) {
                        continue;
                    }

                    const std::size_t index =
                        static_cast<std::size_t>(ny) *
                        static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(nx);

                    if (
                        visited[index] != 0
                    ) {
                        continue;
                    }

                    if (
                        !is_object_pixel(
                            image,
                            nx,
                            ny,
                            width,
                            channels
                        )
                    ) {
                        continue;
                    }

                    visited[index] = 1;

                    queue.push({
                        nx,
                        ny
                    });
                }
            }

            const int component_width =
                component.width();

            const int component_height =
                component.height();

            const std::size_t component_area =
                static_cast<std::size_t>(
                    component_width
                ) *
                static_cast<std::size_t>(
                    component_height
                );

            if (
                component_area > 0
            ) {

                component.density =
                    static_cast<double>(
                        component.pixels
                    ) /
                    static_cast<double>(
                        component_area
                    );
            }

            if (
                components.size() >=
                MAX_COMPONENTS
            ) {
                return components;
            }

            if (
                component_width >=
                    MIN_RECT_WIDTH &&
                component_height >=
                    MIN_RECT_HEIGHT &&
                component.density >=
                    MIN_OBJECT_DENSITY
            ) {

                components.push_back(
                    component
                );
            }
        }
    }

    return components;
}

// =============================================================================
// BAR COMPONENT CLASSIFICATION
// =============================================================================

[[nodiscard]]
bool classify_bar_component(
    const Component& component,
    const ChartCoordinateSystem& coordinates,
    bool& horizontal
) noexcept {

    if (
        !valid_rect(
            component.min_x,
            component.min_y,
            component.max_x,
            component.max_y
        )
    ) {
        return false;
    }

    const double aspect =
        component.aspect();

    const bool horizontal_bar =
        aspect >=
        1.0 /
        MAX_BAR_ASPECT;

    const bool vertical_bar =
        aspect <=
        MAX_BAR_ASPECT;

    if (
        !horizontal_bar &&
        !vertical_bar
    ) {
        return false;
    }

    if (
        coordinates.valid
    ) {

        const ChartRect& plot =
            ChartRect{
                coordinates.plot_area.min_x,
                coordinates.plot_area.min_y,
                coordinates.plot_area.max_x,
                coordinates.plot_area.max_y,
                coordinates.plot_area.confidence
            };

        const double plot_overlap =
            intersection_over_union(
                ChartRect{
                    component.min_x,
                    component.min_y,
                    component.max_x,
                    component.max_y,
                    static_cast<float>(
                        component.density
                    )
                },
                plot
            );

        /*
         * Objects primarily outside the Cartesian plot are more likely to be:
         *
         *     legend
         *     title
         *     border
         *
         * than actual bars.
         */
        if (
            plot_overlap < 0.05
        ) {
            return false;
        }
    }

    horizontal =
        horizontal_bar &&
        (
            aspect >
            1.0
        );

    return true;
}

// =============================================================================
// VALUE MAPPING
// =============================================================================
//
// Ticks currently carry optional numeric values.
//
// When numeric values are unavailable, this routine deliberately returns
// false. Geometry must never invent monetary values.
//
// =============================================================================

[[nodiscard]]
bool infer_axis_value(
    const ChartAxis& axis,
    int pixel_position,
    double& value
) noexcept {

    if (
        axis.ticks.size() < 2
    ) {
        return false;
    }

    const AxisTick* left =
        nullptr;

    const AxisTick* right =
        nullptr;

    for (
        const AxisTick& tick :
        axis.ticks
    ) {

        if (
            !tick.has_numeric_value
        ) {
            continue;
        }

        if (
            tick.pixel_position <=
            pixel_position
        ) {

            if (
                left == nullptr ||
                tick.pixel_position >
                    left->pixel_position
            ) {
                left = &tick;
            }
        }

        if (
            tick.pixel_position >=
            pixel_position
        ) {

            if (
                right == nullptr ||
                tick.pixel_position <
                    right->pixel_position
            ) {
                right = &tick;
            }
        }
    }

    if (
        left == nullptr ||
        right == nullptr ||
        left ==
            right ||
        right->pixel_position ==
            left->pixel_position
    ) {
        return false;
    }

    const double alpha =
        static_cast<double>(
            pixel_position -
            left->pixel_position
        ) /
        static_cast<double>(
            right->pixel_position -
            left->pixel_position
        );

    value =
        left->numeric_value +
        (
            right->numeric_value -
            left->numeric_value
        ) *
        alpha;

    return true;
}

// =============================================================================
// BAR VALUE INFERENCE
// =============================================================================

void infer_bar_value(
    BarSegment& bar,
    const ChartCoordinateSystem& coordinates
) noexcept {

    if (
        !coordinates.y_axis.vertical ||
        coordinates.y_axis.ticks.size() < 2
    ) {
        return;
    }

    const ChartRect& bounds =
        bar.bounds;

    const int anchor_y =
        bar.negative
            ? bounds.max_y
            : bounds.min_y;

    double value = 0.0;

    if (
        infer_axis_value(
            coordinates.y_axis,
            anchor_y,
            value
        )
    ) {

        bar.inferred_value =
            value;

        bar.has_inferred_value =
            true;
    }
}

// =============================================================================
// PLOT CONTAINMENT
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
// COLOR / EDGE SAMPLE
// =============================================================================

[[nodiscard]]
double radial_signal(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    double cx,
    double cy,
    double radius
) noexcept {

    if (
        radius < 1.0
    ) {
        return 0.0;
    }

    std::size_t active = 0;

    std::size_t total = 0;

    for (
        int sample = 0;
        sample < RADIAL_SAMPLE_COUNT;
        ++sample
    ) {

        const double angle =
            (
                2.0 *
                3.14159265358979323846 *
                static_cast<double>(sample)
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            );

        const int x =
            static_cast<int>(
                std::lround(
                    cx +
                    std::cos(angle) *
                    radius
                )
            );

        const int y =
            static_cast<int>(
                std::lround(
                    cy +
                    std::sin(angle) *
                    radius
                )
            );

        if (
            x < 0 ||
            y < 0 ||
            x >= width ||
            y >= height
        ) {
            continue;
        }

        ++total;

        if (
            is_object_pixel(
                image,
                x,
                y,
                width,
                channels
            )
        ) {
            ++active;
        }
    }

    if (total == 0) {
        return 0.0;
    }

    return
        static_cast<double>(active) /
        static_cast<double>(total);
}

// =============================================================================
// PUBLIC RECT CONVERSION
// =============================================================================

[[nodiscard]]
ChartRect to_rect(
    const Component& component
) noexcept {

    ChartRect rect{};

    rect.min_x =
        component.min_x;

    rect.min_y =
        component.min_y;

    rect.max_x =
        component.max_x;

    rect.max_y =
        component.max_y;

    rect.confidence =
        static_cast<float>(
            std::clamp(
                component.density,
                0.0,
                1.0
            )
        );

    return rect;
}

// =============================================================================
// DUPLICATE BAR CHECK
// =============================================================================

[[nodiscard]]
bool duplicate_bar(
    const BarSegment& candidate,
    const std::vector<BarSegment>& existing
) noexcept {

    for (
        const BarSegment& bar :
        existing
    ) {

        const double overlap =
            intersection_over_union(
                candidate.bounds,
                bar.bounds
            );

        if (
            overlap >= 0.80
        ) {
            return true;
        }
    }

    return false;
}

} // namespace

// =============================================================================
// BARS / COLUMNS
// =============================================================================

void ChartObjectDetector::detect_bars_and_columns(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& result
) const {

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    for (
        const Component& component :
        components
    ) {

        if (
            result.bars.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        bool horizontal =
            false;

        if (
            !classify_bar_component(
                component,
                coordinates,
                horizontal
            )
        ) {
            continue;
        }

        const int rect_width =
            component.width();

        const int rect_height =
            component.height();

        if (
            rect_width < MIN_RECT_WIDTH ||
            rect_height < MIN_RECT_HEIGHT
        ) {
            continue;
        }

        /*
         * Very dense roughly rectangular connected components are the strongest
         * deterministic bar candidates.
         */
        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        BarSegment bar{};

        bar.bounds =
            to_rect(
                component
            );

        bar.confidence =
            static_cast<float>(
                std::clamp(
                    (
                        component.density *
                        0.55 +
                        std::min(
                            component.aspect(),
                            MAX_BAR_ASPECT
                        ) /
                        MAX_BAR_ASPECT *
                        0.20 +
                        0.25
                    ),
                    0.0,
                    1.0
                )
            );

        bar.negative =
            coordinates.valid &&
            coordinates.y_axis.vertical &&
            component.max_y >
            coordinates.x_axis.start_y;

        /*
         * A wide rectangle crossing many X positions is a horizontal BAR.
         *
         * A tall rectangle with narrow width is a COLUMN.
         */
        if (
            horizontal &&
            rect_width >
                rect_height
        ) {

            result.bars.push_back(
                bar
            );

        } else if (
            rect_height >
            rect_width
        ) {

            result.bars.push_back(
                bar
            );

        } else {

            /*
             * Nearly square components are retained only when they sit inside
             * the Cartesian plotting area. This avoids turning text/legend
             * blocks into bars.
             */
            if (
                inside_plot(
                    coordinates,
                    (
                        component.min_x +
                        component.max_x
                    ) / 2,
                    (
                        component.min_y +
                        component.max_y
                    ) / 2
                )
            ) {

                result.bars.push_back(
                    bar
                );
            }
        }

        if (
            !result.bars.empty()
        ) {

            BarSegment& inserted =
                result.bars.back();

            infer_bar_value(
                inserted,
                coordinates
            );
        }
    }

    /*
     * Remove high-overlap duplicates.
     */
    std::vector<BarSegment> filtered;

    filtered.reserve(
        result.bars.size()
    );

    for (
        const BarSegment& bar :
        result.bars
    ) {

        if (
            duplicate_bar(
                bar,
                filtered
            )
        ) {
            continue;
        }

        filtered.push_back(
            bar
        );
    }

    result.bars.swap(
        filtered
    );
}

// =============================================================================
// LINE PATHS
// =============================================================================

void ChartObjectDetector::detect_line_paths(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& result
) const {

    if (
        !valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return;
    }

    const int x_start =
        coordinates.valid
            ? std::clamp(
                  coordinates.plot_area.min_x,
                  0,
                  width - 1
              )
            : 0;

    const int x_end =
        coordinates.valid
            ? std::clamp(
                  coordinates.plot_area.max_x,
                  x_start,
                  width - 1
              )
            : width - 1;

    const int y_start =
        coordinates.valid
            ? std::clamp(
                  coordinates.plot_area.min_y,
                  0,
                  height - 1
              )
            : 0;

    const int y_end =
        coordinates.valid
            ? std::clamp(
                  coordinates.plot_area.max_y,
                  y_start,
                  height - 1
              )
            : height - 1;

    if (
        x_end <= x_start ||
        y_end <= y_start
    ) {
        return;
    }

    /*
     * Build one representative Y coordinate per X column.
     *
     * Instead of treating every connected component as a line, this approach
     * tolerates antialiasing gaps and partially disconnected line segments.
     */
    std::vector<ChartPathPoint> points;

    points.reserve(
        static_cast<std::size_t>(
            x_end -
            x_start +
            1
        )
    );

    std::size_t active_samples = 0;

    for (
        int x = x_start;
        x <= x_end;
        ++x
    ) {

        int min_y =
            y_end + 1;

        int max_y =
            y_start - 1;

        std::size_t count = 0;

        for (
            int y = y_start;
            y <= y_end;
            ++y
        ) {

            if (
                !is_object_pixel(
                    chart_buffer,
                    x,
                    y,
                    width,
                    channels
                )
            ) {
                continue;
            }

            ++count;

            min_y =
                std::min(
                    min_y,
                    y
                );

            max_y =
                std::max(
                    max_y,
                    y
                );
        }

        if (
            count == 0
        ) {
            continue;
        }

        const int representative_y =
            (
                min_y +
                max_y
            ) / 2;

        points.push_back({
            x,
            representative_y,
            static_cast<float>(
                std::clamp(
                    static_cast<double>(count) /
                    8.0,
                    0.0,
                    1.0
                )
            )
        });

        active_samples +=
            count;
    }

    const std::size_t plot_area =
        static_cast<std::size_t>(
            x_end -
            x_start +
            1
        ) *
        static_cast<std::size_t>(
            y_end -
            y_start +
            1
        );

    if (
        plot_area == 0
    ) {
        return;
    }

    const double density =
        static_cast<double>(
            active_samples
        ) /
        static_cast<double>(
            plot_area
        );

    if (
        density <
        MIN_LINE_DENSITY
    ) {
        return;
    }

    if (
        points.size() < 3
    ) {
        return;
    }

    /*
     * Split a path when consecutive valid X samples have an excessively large
     * vertical jump. That usually indicates two independent visual structures.
     */
    ChartPath current{};

    current.series_index =
        -1;

    current.points.reserve(
        points.size()
    );

    for (
        const ChartPathPoint& point :
        points
    ) {

        if (
            !current.points.empty()
        ) {

            const ChartPathPoint& previous =
                current.points.back();

            const int dx =
                point.x -
                previous.x;

            const int dy =
                std::abs(
                    point.y -
                    previous.y
                );

            const bool discontinuity =
                dx > 4 &&
                dy >
                    std::max(
                        24,
                        (
                            y_end -
                            y_start
                        ) /
                        6
                    );

            if (
                discontinuity &&
                current.points.size() >= 3
            ) {

                current.confidence =
                    static_cast<float>(
                        std::clamp(
                            density *
                            8.0,
                            0.0,
                            1.0
                        )
                    );

                result.paths.push_back(
                    std::move(
                        current
                    )
                );

                current =
                    ChartPath{};

                current.points.reserve(
                    points.size()
                );
            }
        }

        current.points.push_back(
            point
        );
    }

    if (
        current.points.size() >= 3 &&
        result.paths.size() <
            MAX_OBJECTS
    ) {

        current.confidence =
            static_cast<float>(
                std::clamp(
                    density *
                    8.0,
                    0.0,
                    1.0
                )
            );

        result.paths.push_back(
            std::move(
                current
            )
        );
    }
}

// =============================================================================
// AREA REGIONS
// =============================================================================

void ChartObjectDetector::detect_area_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& result
) const {

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    for (
        const Component& component :
        components
    ) {

        if (
            result.generic_objects.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        if (
            component.width() <
            MIN_RECT_WIDTH ||
            component.height() <
            MIN_RECT_HEIGHT
        ) {
            continue;
        }

        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        const int cx =
            (
                component.min_x +
                component.max_x
            ) / 2;

        const int cy =
            (
                component.min_y +
                component.max_y
            ) / 2;

        if (
            !inside_plot(
                coordinates,
                cx,
                cy
            )
        ) {
            continue;
        }

        /*
         * Area regions tend to be materially larger than text/marker
         * components.
         */
        const std::size_t area =
            static_cast<std::size_t>(
                component.width()
            ) *
            static_cast<std::size_t>(
                component.height()
            );

        const std::size_t plot_area =
            coordinates.valid
                ? static_cast<std::size_t>(
                      std::max(
                          1,
                          coordinates.plot_area.max_x -
                          coordinates.plot_area.min_x +
                          1
                      )
                  ) *
                  static_cast<std::size_t>(
                      std::max(
                          1,
                          coordinates.plot_area.max_y -
                          coordinates.plot_area.min_y +
                          1
                      )
                  )
                : static_cast<std::size_t>(
                      width
                  ) *
                  static_cast<std::size_t>(
                      height
                  );

        const double relative_area =
            static_cast<double>(area) /
            static_cast<double>(
                std::max<std::size_t>(
                    plot_area,
                    1
                )
            );

        /*
         * Only large filled regions are promoted to area objects.
         */
        if (
            relative_area <
            0.015
        ) {
            continue;
        }

        ChartObject object{};

        object.kind =
            ChartObjectKind::AREA_REGION;

        object.bounds =
            to_rect(
                component
            );

        object.confidence =
            static_cast<float>(
                std::clamp(
                    component.density *
                    0.7 +
                    std::min(
                        relative_area *
                        8.0,
                        0.3
                    ),
                    0.0,
                    1.0
                )
            );

        result.generic_objects.push_back(
            object
        );
    }
}

// =============================================================================
// RADIAL SLICES
// =============================================================================

void ChartObjectDetector::detect_radial_slices(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ChartObjectSet& result
) const {

    if (
        !valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return;
    }

    const int center_x =
        width / 2;

    const int center_y =
        height / 2;

    const int max_radius =
        std::min(
            width,
            height
        ) /
        2;

    if (
        max_radius <
        RADIAL_MIN_RADIUS
    ) {
        return;
    }

    int best_radius =
        -1;

    double best_coverage =
        0.0;

    /*
     * Search several radii instead of assuming the pie/donut fills the image.
     */
    for (
        int radius = RADIAL_MIN_RADIUS;
        radius <= max_radius;
        radius += std::max(
            4,
            max_radius / 64
        )
    ) {

        const double coverage =
            radial_signal(
                chart_buffer,
                width,
                height,
                channels,
                static_cast<double>(center_x),
                static_cast<double>(center_y),
                static_cast<double>(radius)
            );

        if (
            coverage >
            best_coverage
        ) {

            best_coverage =
                coverage;

            best_radius =
                radius;
        }
    }

    if (
        best_radius < 0 ||
        best_coverage <
            RADIAL_MIN_COVERAGE
    ) {
        return;
    }

    /*
     * Measure angular occupancy around the detected circumference.
     */
    std::vector<std::uint8_t> active_angles(
        static_cast<std::size_t>(
            RADIAL_SAMPLE_COUNT
        ),
        0
    );

    for (
        int i = 0;
        i < RADIAL_SAMPLE_COUNT;
        ++i
    ) {

        const double angle =
            (
                2.0 *
                3.14159265358979323846 *
                static_cast<double>(i)
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            );

        const int x =
            static_cast<int>(
                std::lround(
                    static_cast<double>(center_x) +
                    std::cos(angle) *
                    static_cast<double>(best_radius)
                )
            );

        const int y =
            static_cast<int>(
                std::lround(
                    static_cast<double>(center_y) +
                    std::sin(angle) *
                    static_cast<double>(best_radius)
                )
            );

        if (
            x < 0 ||
            y < 0 ||
            x >= width ||
            y >= height
        ) {
            continue;
        }

        if (
            is_object_pixel(
                chart_buffer,
                x,
                y,
                width,
                channels
            )
        ) {

            active_angles[
                static_cast<std::size_t>(i)
            ] = 1;
        }
    }

    /*
     * Split angularly separated regions.
     */
    struct AngularRun {
        int start = -1;
        int end = -1;
        int count = 0;
    };

    std::vector<AngularRun> runs;

    AngularRun current{};

    for (
        int i = 0;
        i < RADIAL_SAMPLE_COUNT;
        ++i
    ) {

        if (
            active_angles[
                static_cast<std::size_t>(i)
            ]
        ) {

            if (
                current.start < 0
            ) {

                current.start =
                    i;

                current.end =
                    i;

                current.count =
                    1;

            } else {

                current.end =
                    i;

                ++current.count;
            }

        } else if (
            current.start >= 0
        ) {

            runs.push_back(
                current
            );

            current =
                AngularRun{};
        }
    }

    if (
        current.start >= 0
    ) {

        runs.push_back(
            current
        );
    }

    if (
        runs.size() < 2
    ) {
        return;
    }

    for (
        const AngularRun& run :
        runs
    ) {

        if (
            result.slices.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        if (
            run.count <= 0
        ) {
            continue;
        }

        RadialSlice slice{};

        slice.start_angle =
            (
                static_cast<double>(
                    run.start
                ) /
                static_cast<double>(
                    RADIAL_SAMPLE_COUNT
                )
            ) *
            2.0 *
            3.14159265358979323846;

        slice.end_angle =
            (
                static_cast<double>(
                    run.end + 1
                ) /
                static_cast<double>(
                    RADIAL_SAMPLE_COUNT
                )
            ) *
            2.0 *
            3.14159265358979323846;

        slice.fraction =
            static_cast<double>(
                run.count
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            );

        slice.center_x =
            center_x;

        slice.center_y =
            center_y;

        slice.inner_radius =
            0;

        slice.outer_radius =
            best_radius;

        slice.confidence =
            static_cast<float>(
                std::clamp(
                    best_coverage *
                    0.8 +
                    slice.fraction *
                    0.2,
                    0.0,
                    1.0
                )
            );

        result.slices.push_back(
            slice
        );
    }
}

// =============================================================================
// SCATTER / BUBBLE POINTS
// =============================================================================

void ChartObjectDetector::detect_scatter_points(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& result
) const {

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    for (
        const Component& component :
        components
    ) {

        if (
            result.points.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        if (
            component.width() <
            MIN_RECT_WIDTH ||
            component.height() <
            MIN_RECT_HEIGHT
        ) {
            continue;
        }

        const int cx =
            (
                component.min_x +
                component.max_x
            ) / 2;

        const int cy =
            (
                component.min_y +
                component.max_y
            ) / 2;

        if (
            !inside_plot(
                coordinates,
                cx,
                cy
            )
        ) {
            continue;
        }

        /*
         * Scatter markers are generally compact and reasonably dense.
         *
         * Very elongated components are more likely line segments, bars, or
         * chart geometry.
         */
        const double aspect =
            component.aspect();

        if (
            aspect < 0.25 ||
            aspect > 4.0
        ) {
            continue;
        }

        const double area =
            static_cast<double>(
                component.width()
            ) *
            static_cast<double>(
                component.height()
            );

        if (
            area <= 0.0
        ) {
            continue;
        }

        const double radius =
            0.25 *
            (
                static_cast<double>(
                    component.width()
                ) +
                static_cast<double>(
                    component.height()
                )
            );

        ScatterPoint point{};

        point.x =
            cx;

        point.y =
            cy;

        point.radius =
            std::max(
                1.0,
                radius
            );

        point.is_bubble =
            radius >= 8.0;

        point.series_index =
            -1;

        point.confidence =
            static_cast<float>(
                std::clamp(
                    component.density *
                    0.85 +
                    (
                        aspect >= 0.5 &&
                        aspect <= 2.0
                            ? 0.15
                            : 0.0
                    ),
                    0.0,
                    1.0
                )
            );

        result.points.push_back(
            point
        );
    }
}

// =============================================================================
// WATERFALL
// =============================================================================

void ChartObjectDetector::detect_waterfall_steps(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates,
    ChartObjectSet& result
) const {

    if (
        !coordinates.valid ||
        !coordinates.x_axis.horizontal ||
        !coordinates.y_axis.vertical
    ) {
        return;
    }

    /*
     * Reuse rectangular components but require an ordering along X.
     */
    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    std::vector<Component> candidates;

    candidates.reserve(
        components.size()
    );

    for (
        const Component& component :
        components
    ) {

        if (
            component.width() <
            MIN_RECT_WIDTH ||
            component.height() <
            MIN_RECT_HEIGHT
        ) {
            continue;
        }

        const double aspect =
            component.aspect();

        if (
            aspect < MIN_BAR_ASPECT ||
            aspect > MAX_BAR_ASPECT
        ) {
            continue;
        }

        const int cx =
            (
                component.min_x +
                component.max_x
            ) / 2;

        const int cy =
            (
                component.min_y +
                component.max_y
            ) / 2;

        if (
            !inside_plot(
                coordinates,
                cx,
                cy
            )
        ) {
            continue;
        }

        candidates.push_back(
            component
        );
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const Component& a,
            const Component& b
        ) noexcept {

            return
                a.min_x <
                b.min_x;
        }
    );

    if (
        candidates.size() <
        3
    ) {
        return;
    }

    /*
     * Waterfall bars normally share similar widths and have varying vertical
     * baselines. Detect that regularity before promoting them.
     */
    std::vector<WaterfallStep> steps;

    for (
        const Component& component :
        candidates
    ) {

        if (
            steps.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        WaterfallStep step{};

        step.bounds =
            to_rect(
                component
            );

        step.category_index =
            static_cast<int>(
                steps.size()
            );

        step.confidence =
            static_cast<float>(
                std::clamp(
                    component.density,
                    0.0,
                    1.0
                )
            );

        steps.push_back(
            step
        );
    }

    if (
        steps.size() < 3
    ) {
        return;
    }

    double width_mean = 0.0;

    for (
        const WaterfallStep& step :
        steps
    ) {

        width_mean +=
            static_cast<double>(
                step.bounds.max_x -
                step.bounds.min_x +
                1
            );
    }

    width_mean /=
        static_cast<double>(
            steps.size()
        );

    if (
        width_mean <= 0.0
    ) {
        return;
    }

    double width_variance = 0.0;

    for (
        const WaterfallStep& step :
        steps
    ) {

        const double value =
            static_cast<double>(
                step.bounds.max_x -
                step.bounds.min_x +
                1
            );

        const double diff =
            value -
            width_mean;

        width_variance +=
            diff *
            diff;
    }

    width_variance /=
        static_cast<double>(
            steps.size()
        );

    const double width_stddev =
        std::sqrt(
            width_variance
        );

    const double regularity =
        std::clamp(
            1.0 -
            width_stddev /
                std::max(
                    width_mean,
                    1.0
                ),
            0.0,
            1.0
        );

    if (
        regularity <
        0.50
    ) {
        return;
    }

    result.waterfall_steps =
        std::move(
            steps
        );

    for (
        WaterfallStep& step :
        result.waterfall_steps
    ) {

        step.confidence =
            static_cast<float>(
                std::clamp(
                    static_cast<double>(
                        step.confidence
                    ) *
                    0.6 +
                    regularity *
                    0.4,
                    0.0,
                    1.0
                )
            );
    }
}

// =============================================================================
// FUNNEL STAGES
// =============================================================================

void ChartObjectDetector::detect_funnel_stages(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ChartObjectSet& result
) const {

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    struct Candidate {
        Component component;
    };

    std::vector<Candidate> candidates;

    for (
        const Component& component :
        components
    ) {

        if (
            component.width() <
            MIN_RECT_WIDTH ||
            component.height() <
            MIN_RECT_HEIGHT
        ) {
            continue;
        }

        candidates.push_back({
            component
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
                a.component.min_y !=
                b.component.min_y
            ) {
                return
                    a.component.min_y <
                    b.component.min_y;
            }

            return
                a.component.min_x <
                b.component.min_x;
        }
    );

    if (
        candidates.size() <
        static_cast<std::size_t>(
            FUNNEL_MIN_STAGES
        )
    ) {
        return;
    }

    std::vector<FunnelStage> stages;

    for (
        const Candidate& candidate :
        candidates
    ) {

        if (
            stages.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        FunnelStage stage{};

        stage.bounds =
            to_rect(
                candidate.component
            );

        stage.stage_index =
            static_cast<int>(
                stages.size()
            );

        stage.relative_width =
            static_cast<double>(
                candidate.component.width()
            );

        stage.confidence =
            static_cast<float>(
                std::clamp(
                    candidate.component.density,
                    0.0,
                    1.0
                )
            );

        stages.push_back(
            stage
        );
    }

    if (
        stages.size() <
        static_cast<std::size_t>(
            FUNNEL_MIN_STAGES
        )
    ) {
        return;
    }

    double previous_width =
        std::numeric_limits<double>::infinity();

    bool monotonically_decreasing =
        true;

    for (
        FunnelStage& stage :
        stages
    ) {

        const double width_value =
            stage.relative_width;

        if (
            width_value >
            previous_width +
                2.0
        ) {

            monotonically_decreasing =
                false;

            break;
        }

        previous_width =
            width_value;
    }

    if (
        !monotonically_decreasing
    ) {
        return;
    }

    const double max_width =
        std::max(
            stages.front().relative_width,
            1.0
        );

    for (
        FunnelStage& stage :
        stages
    ) {

        stage.relative_width /=
            max_width;

        stage.confidence =
            static_cast<float>(
                std::clamp(
                    static_cast<double>(
                        stage.confidence
                    ) *
                    0.7 +
                    stage.relative_width *
                    0.3,
                    0.0,
                    1.0
                )
            );
    }

    result.funnel_stages =
        std::move(
            stages
        );
}

// =============================================================================
// TREEMAP
// =============================================================================

void ChartObjectDetector::detect_treemap_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ChartObjectSet& result
) const {

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    std::vector<ChartRect> rectangles;

    rectangles.reserve(
        components.size()
    );

    for (
        const Component& component :
        components
    ) {

        if (
            component.width() <
            TREEMAP_MIN_RECT_SIZE ||
            component.height() <
            TREEMAP_MIN_RECT_SIZE
        ) {
            continue;
        }

        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        rectangles.push_back(
            to_rect(
                component
            )
        );
    }

    if (
        rectangles.size() < 2
    ) {
        return;
    }

    for (
        std::size_t i = 0;
        i < rectangles.size() &&
            result.treemap_nodes.size() <
                MAX_OBJECTS;
        ++i
    ) {

        const ChartRect& rectangle =
            rectangles[i];

        TreemapNode node{};

        node.bounds =
            rectangle;

        node.hierarchy_level =
            0;

        node.parent_index =
            -1;

        node.confidence =
            rectangle.confidence;

        /*
         * Find a containing rectangle. The smallest containing rectangle is
         * treated as the immediate parent.
         */
        double smallest_parent_area =
            std::numeric_limits<double>::infinity();

        int parent_index =
            -1;

        const int center_x =
            (
                rectangle.min_x +
                rectangle.max_x
            ) / 2;

        const int center_y =
            (
                rectangle.min_y +
                rectangle.max_y
            ) / 2;

        for (
            std::size_t j = 0;
            j < rectangles.size();
            ++j
        ) {

            if (
                i == j
            ) {
                continue;
            }

            const ChartRect& parent =
                rectangles[j];

            if (
                center_x < parent.min_x ||
                center_x > parent.max_x ||
                center_y < parent.min_y ||
                center_y > parent.max_y
            ) {
                continue;
            }

            const double parent_area =
                static_cast<double>(
                    parent.max_x -
                    parent.min_x +
                    1
                ) *
                static_cast<double>(
                    parent.max_y -
                    parent.min_y +
                    1
                );

            const double child_area =
                static_cast<double>(
                    rectangle.max_x -
                    rectangle.min_x +
                    1
                ) *
                static_cast<double>(
                    rectangle.max_y -
                    rectangle.min_y +
                    1
                );

            if (
                parent_area <=
                child_area
            ) {
                continue;
            }

            if (
                parent_area <
                smallest_parent_area
            ) {

                smallest_parent_area =
                    parent_area;

                parent_index =
                    static_cast<int>(
                        j
                    );
            }
        }

        if (
            parent_index >= 0
        ) {

            node.parent_index =
                parent_index;

            node.hierarchy_level =
                1;
        }

        result.treemap_nodes.push_back(
            node
        );
    }
}

// =============================================================================
// COMPLETE DETECTOR
// =============================================================================

ChartObjectSet ChartObjectDetector::detect(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ChartCoordinateSystem& coordinates
) const {

    ChartObjectSet result{};

    if (
        !valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return result;
    }

    // =========================================================================
    // DETECTION ORDER
    // =========================================================================
    //
    // Cartesian primitives first:
    //
    //     bars
    //     lines
    //     areas
    //
    // followed by structures that do not require a Cartesian coordinate system:
    //
    //     radial
    //     scatter
    //     waterfall
    //     funnel
    //     treemap
    //
    // =========================================================================

    detect_bars_and_columns(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    detect_line_paths(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    detect_area_regions(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    detect_radial_slices(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    detect_scatter_points(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    detect_waterfall_steps(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    detect_funnel_stages(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    detect_treemap_regions(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    // =========================================================================
    // RESULT VALIDITY
    // =========================================================================

    const std::size_t object_count =
        result.bars.size() +
        result.paths.size() +
        result.slices.size() +
        result.points.size() +
        result.waterfall_steps.size() +
        result.funnel_stages.size() +
        result.treemap_nodes.size() +
        result.generic_objects.size();

    result.valid =
        object_count > 0;

    if (
        !result.valid
    ) {
        result.confidence =
            0.0f;

        return result;
    }

    // =========================================================================
    // GLOBAL CONFIDENCE
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    for (
        const BarSegment& object :
        result.bars
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ChartPath& object :
        result.paths
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const RadialSlice& object :
        result.slices
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ScatterPoint& object :
        result.points
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const WaterfallStep& object :
        result.waterfall_steps
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const FunnelStage& object :
        result.funnel_stages
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const TreemapNode& object :
        result.treemap_nodes
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ChartObject& object :
        result.generic_objects
    ) {

        confidence_sum +=
            object.confidence;

        ++confidence_count;
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

    return result;
}

} // namespace fin_ocr::chart
