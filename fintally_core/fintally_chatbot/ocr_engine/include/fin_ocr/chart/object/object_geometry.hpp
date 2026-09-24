#pragma once

#include <cstddef>
#include <cstdint>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object {

// =============================================================================
// IMAGE VALIDATION
// =============================================================================
//
// Validates the basic image descriptor.
//
// This does not validate the backing allocation size because the image API
// supplies only a raw pointer and dimensions.
//
// =============================================================================

[[nodiscard]]
bool valid_image(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) noexcept;

// =============================================================================
// PIXEL ACCESS
// =============================================================================
//
// Computes:
//
//     ((y * width) + x) * channels
//
// Callers are expected to provide valid coordinates.
//
// =============================================================================

[[nodiscard]]
std::size_t pixel_offset(
    int x,
    int y,
    int width,
    int channels
) noexcept;

// =============================================================================
// GEOMETRY SIGNAL
// =============================================================================
//
// ChartColorIsolator representation:
//
//     channel 0 = saturation
//     channel 1 = OCR foreground strength
//     channel 2 = chroma / RGB delta
//
// Object detection uses a broader geometry signal than OCR.
//
// For multi-channel images:
//
//     geometry_signal = max(foreground, chroma)
//
// For one-channel images:
//
//     geometry_signal = channel 0
//
// =============================================================================

[[nodiscard]]
std::uint8_t geometry_signal(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept;

// =============================================================================
// OBJECT PIXEL
// =============================================================================
//
// Determines whether a pixel contains sufficient chart-object geometry signal.
//
// =============================================================================

[[nodiscard]]
bool is_object_pixel(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept;

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept;

// =============================================================================
// RECTANGLE OVERLAP
// =============================================================================
//
// Returns intersection-over-union:
//
//     intersection / union
//
// Returns 0 when the rectangles do not overlap or the union area is invalid.
//
// =============================================================================

[[nodiscard]]
double intersection_over_union(
    const ChartRect& a,
    const ChartRect& b
) noexcept;

// =============================================================================
// PLOT CONTAINMENT
// =============================================================================
//
// When the coordinate system is invalid, containment is treated as true so
// object detection can still operate without a coordinate system.
//
// =============================================================================

[[nodiscard]]
bool inside_plot(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    int x,
    int y
) noexcept;

} // namespace fin_ocr::chart::object
