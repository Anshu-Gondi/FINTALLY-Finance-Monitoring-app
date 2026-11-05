#include "fin_ocr/chart/object/line_detector.hpp"

#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace fin_ocr::chart::object::line {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr double MIN_LINE_DENSITY =
    config::CHART_OBJECT_MIN_LINE_DENSITY;

// =============================================================================
// PATH DISCONTINUITY
// =============================================================================
//
// A large vertical jump over a non-trivial horizontal gap is treated as a
// discontinuity between independent visual structures.
//
// These values preserve the thresholds from the original implementation.
//
// =============================================================================

constexpr int MIN_DISCONTINUITY_DX = 4;

constexpr int MIN_DISCONTINUITY_DY = 24;

constexpr int PLOT_HEIGHT_DIVISOR = 6;

// =============================================================================
// POINT CONFIDENCE SCALE
// =============================================================================

constexpr double POINT_CONFIDENCE_SCALE = 8.0;

// =============================================================================
// PATH CONFIDENCE SCALE
// =============================================================================

constexpr double PATH_CONFIDENCE_SCALE = 8.0;

} // namespace

// =============================================================================
// LINE PATHS
// =============================================================================
//
// For each X coordinate inside the relevant plot region:
//
//   1. scan the Y range
//   2. collect object pixels
//   3. determine the minimum and maximum Y
//   4. use their midpoint as the representative path position
//
// This approach intentionally does not rely on connected components, allowing
// the detector to tolerate antialiasing gaps and partially disconnected line
// segments.
//
// =============================================================================

void detect_line_paths(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
)
{
    // =========================================================================
    // IMAGE VALIDATION
    // =========================================================================

    if (
        !::fin_ocr::chart::object::valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return;
    }

    // =========================================================================
    // DETECTION REGION
    // =========================================================================
    //
    // With a valid coordinate system, restrict detection to the detected plot
    // area. Otherwise fall back to the complete image.
    //
    // =========================================================================

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

    // =========================================================================
    // REPRESENTATIVE POINT EXTRACTION
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::ChartPathPoint
    > points;

    points.reserve(
        static_cast<std::size_t>(
            x_end -
            x_start +
            1
        )
    );

    std::size_t active_samples =
        0;

    // =========================================================================
    // SCAN COLUMNS
    // =========================================================================

    for (
        int x = x_start;
        x <= x_end;
        ++x
    ) {
        int min_y =
            y_end + 1;

        int max_y =
            y_start - 1;

        std::size_t count =
            0;

        for (
            int y = y_start;
            y <= y_end;
            ++y
        ) {
            if (
                !::fin_ocr::chart::object::is_object_pixel(
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

        // ---------------------------------------------------------------------
        // Representative Y.
        // ---------------------------------------------------------------------

        const int representative_y =
            (
                min_y +
                max_y
            ) / 2;

        // ---------------------------------------------------------------------
        // Point confidence.
        //
        // More signal in a column increases confidence, capped at 1.
        // ---------------------------------------------------------------------

        const float point_confidence =
            static_cast<float>(
                std::clamp(
                    static_cast<double>(count) /
                        POINT_CONFIDENCE_SCALE,
                    0.0,
                    1.0
                )
            );

        points.push_back({
            x,
            representative_y,
            point_confidence
        });

        active_samples +=
            count;
    }

    // =========================================================================
    // REGION AREA
    // =========================================================================

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

    // =========================================================================
    // GEOMETRIC DENSITY
    // =========================================================================

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

    // =========================================================================
    // MINIMUM PATH SIZE
    // =========================================================================

    if (
        points.size() < 3
    ) {
        return;
    }

    // =========================================================================
    // PATH SEGMENTATION
    // =========================================================================

    ::fin_ocr::chart::object::ChartPath current{};

    current.series_index =
        -1;

    current.points.reserve(
        points.size()
    );

    const int discontinuity_height =
        std::max(
            MIN_DISCONTINUITY_DY,
            (
                y_end -
                y_start
            ) /
            PLOT_HEIGHT_DIVISOR
        );

    // =========================================================================
    // BUILD PATHS
    // =========================================================================

    for (
        const ::fin_ocr::chart::object::ChartPathPoint& point :
        points
    ) {
        if (
            !current.points.empty()
        ) {
            const ::fin_ocr::chart::object::ChartPathPoint& previous =
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
                dx >
                    MIN_DISCONTINUITY_DX &&
                dy >
                    discontinuity_height;

            if (
                discontinuity &&
                current.points.size() >= 3
            ) {
                current.confidence =
                    static_cast<float>(
                        std::clamp(
                            density *
                            PATH_CONFIDENCE_SCALE,
                            0.0,
                            1.0
                        )
                    );

                if (
                    result.paths.size() <
                    MAX_OBJECTS
                ) {
                    result.paths.push_back(
                        std::move(
                            current
                        )
                    );
                }

                current =
                    ::fin_ocr::chart::object::ChartPath{};

                current.series_index =
                    -1;

                current.points.reserve(
                    points.size()
                );
            }
        }

        current.points.push_back(
            point
        );
    }

    // =========================================================================
    // FLUSH FINAL PATH
    // =========================================================================

    if (
        current.points.size() >= 3 &&
        result.paths.size() <
            MAX_OBJECTS
    ) {
        current.confidence =
            static_cast<float>(
                std::clamp(
                    density *
                    PATH_CONFIDENCE_SCALE,
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

} // namespace fin_ocr::chart::object::line
