#include "fin_ocr/chart/object/radial_detector.hpp"

#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::object::radial {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr int RADIAL_MIN_RADIUS =
    config::CHART_OBJECT_RADIAL_MIN_RADIUS;

constexpr int RADIAL_SAMPLE_COUNT =
    config::CHART_OBJECT_RADIAL_SAMPLE_COUNT;

constexpr double RADIAL_MIN_COVERAGE =
    config::CHART_OBJECT_RADIAL_MIN_COVERAGE;

// =============================================================================
// MATHEMATICAL CONSTANTS
// =============================================================================

constexpr double TWO_PI =
    2.0 *
    3.14159265358979323846;

// =============================================================================
// RADIAL SIGNAL
// =============================================================================
//
// Samples a circumference at RADIAL_SAMPLE_COUNT angular positions and returns
// the fraction of valid samples containing object geometry.
//
// This helper is private to the radial detector because its meaning is specific
// to radial/circumference analysis.
//
// =============================================================================

[[nodiscard]]
double radial_signal(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    double center_x,
    double center_y,
    double radius
) noexcept
{
    if (
        radius < 1.0
    ) {
        return 0.0;
    }

    std::size_t active =
        0;

    std::size_t total =
        0;

    for (
        int sample = 0;
        sample < RADIAL_SAMPLE_COUNT;
        ++sample
    ) {
        const double angle =
            TWO_PI *
            static_cast<double>(sample) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            );

        const int x =
            static_cast<int>(
                std::lround(
                    center_x +
                    std::cos(angle) *
                    radius
                )
            );

        const int y =
            static_cast<int>(
                std::lround(
                    center_y +
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
            ::fin_ocr::chart::object::is_object_pixel(
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

    if (
        total == 0
    ) {
        return 0.0;
    }

    return
        static_cast<double>(active) /
        static_cast<double>(total);
}

} // namespace

// =============================================================================
// RADIAL SLICES
// =============================================================================
//
// Detection flow:
//
//   1. Validate the image.
//   2. Use the image center as the initial radial center.
//   3. Search candidate radii.
//   4. Select the radius with the strongest circumference signal.
//   5. Reject weak circumferences.
//   6. Sample angular occupancy.
//   7. Split occupied angular runs.
//   8. Convert runs into RadialSlice objects.
//
// This preserves the behavior of the original monolithic implementation.
//
// =============================================================================

void detect_radial_slices(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
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
    // CENTER
    // =========================================================================

    const int center_x =
        width / 2;

    const int center_y =
        height / 2;

    // =========================================================================
    // MAXIMUM RADIUS
    // =========================================================================

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

    // =========================================================================
    // SEARCH RADIUS
    // =========================================================================

    int best_radius =
        -1;

    double best_coverage =
        0.0;

    const int radius_step =
        std::max(
            4,
            max_radius / 64
        );

    for (
        int radius = RADIAL_MIN_RADIUS;
        radius <= max_radius;
        radius += radius_step
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

    // =========================================================================
    // COVERAGE VALIDATION
    // =========================================================================

    if (
        best_radius < 0 ||
        best_coverage <
            RADIAL_MIN_COVERAGE
    ) {
        return;
    }

    // =========================================================================
    // ANGULAR OCCUPANCY
    // =========================================================================

    std::vector<std::uint8_t> active_angles(
        static_cast<std::size_t>(
            RADIAL_SAMPLE_COUNT
        ),
        0
    );

    for (
        int sample = 0;
        sample < RADIAL_SAMPLE_COUNT;
        ++sample
    ) {
        const double angle =
            TWO_PI *
            static_cast<double>(sample) /
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
            ::fin_ocr::chart::object::is_object_pixel(
                chart_buffer,
                x,
                y,
                width,
                channels
            )
        ) {
            active_angles[
                static_cast<std::size_t>(
                    sample
                )
            ] = 1;
        }
    }

    // =========================================================================
    // ANGULAR RUN
    // =========================================================================

    struct AngularRun {

        int start = -1;
        int end = -1;
        int count = 0;
    };

    std::vector<AngularRun> runs;

    AngularRun current{};

    for (
        int sample = 0;
        sample < RADIAL_SAMPLE_COUNT;
        ++sample
    ) {
        const bool active =
            active_angles[
                static_cast<std::size_t>(
                    sample
                )
            ] != 0;

        if (
            active
        ) {
            if (
                current.start < 0
            ) {
                current.start =
                    sample;

                current.end =
                    sample;

                current.count =
                    1;
            } else {
                current.end =
                    sample;

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

    // =========================================================================
    // FLUSH FINAL RUN
    // =========================================================================

    if (
        current.start >= 0
    ) {
        runs.push_back(
            current
        );
    }

    // =========================================================================
    // REQUIRE MULTIPLE RADIAL REGIONS
    // =========================================================================

    if (
        runs.size() < 2
    ) {
        return;
    }

    // =========================================================================
    // BUILD SLICES
    // =========================================================================

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

        ::fin_ocr::chart::object::RadialSlice slice{};

        // ---------------------------------------------------------------------
        // START ANGLE
        // ---------------------------------------------------------------------

        slice.start_angle =
            static_cast<double>(
                run.start
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            ) *
            TWO_PI;

        // ---------------------------------------------------------------------
        // END ANGLE
        // ---------------------------------------------------------------------

        slice.end_angle =
            static_cast<double>(
                run.end + 1
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            ) *
            TWO_PI;

        // ---------------------------------------------------------------------
        // ANGULAR FRACTION
        // ---------------------------------------------------------------------

        slice.fraction =
            static_cast<double>(
                run.count
            ) /
            static_cast<double>(
                RADIAL_SAMPLE_COUNT
            );

        // ---------------------------------------------------------------------
        // CENTER
        // ---------------------------------------------------------------------

        slice.center_x =
            center_x;

        slice.center_y =
            center_y;

        // ---------------------------------------------------------------------
        // RADIAL EXTENT
        // ---------------------------------------------------------------------

        slice.inner_radius =
            0;

        slice.outer_radius =
            best_radius;

        // ---------------------------------------------------------------------
        // CONFIDENCE
        // ---------------------------------------------------------------------

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

} // namespace fin_ocr::chart::object::radial
