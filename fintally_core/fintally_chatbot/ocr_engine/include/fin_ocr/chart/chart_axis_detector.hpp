#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart {

// =============================================================================
// CHART AXIS TYPES
// =============================================================================

enum class AxisKind : std::uint8_t {
    UNKNOWN = 0,
    X_AXIS,
    Y_AXIS,
    SECONDARY_X_AXIS,
    SECONDARY_Y_AXIS
};

// =============================================================================
// AXIS TICK
// =============================================================================
//
// A tick may optionally have a recognized text label and/or numeric value.
//
// The detector itself should not assume OCR succeeded.
// =============================================================================

struct AxisTick {

    int pixel_position = 0;

    int label_min_x = -1;
    int label_max_x = -1;
    int label_min_y = -1;
    int label_max_y = -1;

    double numeric_value = 0.0;

    bool has_numeric_value = false;

    float confidence = 0.0f;
};

// =============================================================================
// AXIS
// =============================================================================

struct ChartAxis {

    AxisKind kind = AxisKind::UNKNOWN;

    int start_x = 0;
    int start_y = 0;

    int end_x = 0;
    int end_y = 0;

    int thickness = 0;

    bool horizontal = false;

    bool vertical = false;

    float confidence = 0.0f;

    std::vector<AxisTick> ticks;
};

// =============================================================================
// PLOT AREA
// =============================================================================

struct PlotArea {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    float confidence = 0.0f;
};

// =============================================================================
// COORDINATE SYSTEM
// =============================================================================

struct ChartCoordinateSystem {

    ChartAxis x_axis;
    ChartAxis y_axis;

    ChartAxis secondary_x_axis;
    ChartAxis secondary_y_axis;

    PlotArea plot_area;

    bool has_secondary_x_axis = false;
    bool has_secondary_y_axis = false;

    bool valid = false;

    float confidence = 0.0f;
};

// =============================================================================
// AXIS DETECTOR
// =============================================================================

class ChartAxisDetector {
public:

    [[nodiscard]]
    ChartCoordinateSystem detect(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;

private:

    [[nodiscard]]
    ChartAxis detect_horizontal_axis(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;

    [[nodiscard]]
    ChartAxis detect_vertical_axis(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;

    [[nodiscard]]
    PlotArea detect_plot_area(
        const ChartAxis& x_axis,
        const ChartAxis& y_axis,
        int width,
        int height
    ) const;

    void detect_ticks(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        ChartAxis& axis
    ) const;
};

} // namespace fin_ocr::chart
