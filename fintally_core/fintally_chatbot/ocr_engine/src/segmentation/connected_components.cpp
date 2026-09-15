#include "fin_ocr/segmentation/connected_components.hpp"

#include "fin_ocr/core/pixel_access.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fin_ocr {

namespace {

// =============================================================================
// COMPONENT LIMITS
// =============================================================================

constexpr int MIN_COMPONENT_HEIGHT = 2;

constexpr int MAX_TEXT_COMPONENT_WIDTH = 96;

constexpr int MAX_TEXT_COMPONENT_HEIGHT = 32;

constexpr double MAX_COMPONENT_WIDTH_RATIO = 0.18;

constexpr double MAX_COMPONENT_HEIGHT_RATIO = 0.90;

constexpr double MAX_HORIZONTAL_ASPECT = 14.0;

constexpr double MAX_VERTICAL_ASPECT = 10.0;

constexpr double MIN_COMPONENT_DENSITY = 0.015;

constexpr double MAX_COMPONENT_DENSITY = 0.90;

// Components wider than this are treated as possible chart structures and
// require stronger evidence before entering glyph reconstruction.
constexpr int WIDE_COMPONENT_WIDTH = 48;

constexpr int WIDE_COMPONENT_MAX_HEIGHT = 8;

constexpr double WIDE_COMPONENT_MAX_DENSITY = 0.65;

// =============================================================================
// GEOMETRY HELPERS
// =============================================================================

[[nodiscard]]
bool is_horizontal_rule(
    int width,
    int height
) noexcept {

    if (
        width < 12 ||
        height <= 0
    ) {
        return false;
    }

    return
        width >= height * 6 &&
        height <= 3;
}

[[nodiscard]]
bool is_vertical_rule(
    int width,
    int height
) noexcept {

    if (
        height < 12 ||
        width <= 0
    ) {
        return false;
    }

    return
        height >= width * 6 &&
        width <= 3;
}

[[nodiscard]]
bool is_extreme_horizontal_geometry(
    int width,
    int height
) noexcept {

    if (
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    const double aspect =
        static_cast<double>(width) /
        static_cast<double>(height);

    return
        aspect > MAX_HORIZONTAL_ASPECT &&
        width >= 24;
}

[[nodiscard]]
bool is_extreme_vertical_geometry(
    int width,
    int height
) noexcept {

    if (
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    const double aspect =
        static_cast<double>(height) /
        static_cast<double>(width);

    return
        aspect > MAX_VERTICAL_ASPECT &&
        height >= 24;
}

[[nodiscard]]
bool is_large_chart_blob(
    int component_width,
    int component_height,
    int roi_width,
    int roi_height
) noexcept {

    if (
        component_width <= 0 ||
        component_height <= 0 ||
        roi_width <= 0 ||
        roi_height <= 0
    ) {
        return true;
    }

    const double width_ratio =
        static_cast<double>(component_width) /
        static_cast<double>(roi_width);

    const double height_ratio =
        static_cast<double>(component_height) /
        static_cast<double>(roi_height);

    if (
        width_ratio >=
            MAX_COMPONENT_WIDTH_RATIO &&
        component_width >
            MAX_TEXT_COMPONENT_WIDTH
    ) {
        return true;
    }

    return
        height_ratio >
        MAX_COMPONENT_HEIGHT_RATIO;
}

[[nodiscard]]
bool acceptable_component_geometry(
    const BoundingBox& box,
    int roi_width,
    int roi_height
) noexcept {

    const int width =
        box.width();

    const int height =
        box.height();

    const int area =
        box.area();

    if (
        width <= 0 ||
        height <= 0 ||
        area <= 0
    ) {
        return false;
    }

    if (
        height <
        MIN_COMPONENT_HEIGHT
    ) {
        return false;
    }

    if (
        width >
        MAX_TEXT_COMPONENT_WIDTH
    ) {
        return false;
    }

    if (
        height >
        MAX_TEXT_COMPONENT_HEIGHT
    ) {
        return false;
    }

    if (
        is_horizontal_rule(
            width,
            height
        )
    ) {
        return false;
    }

    if (
        is_vertical_rule(
            width,
            height
        )
    ) {
        return false;
    }

    if (
        is_extreme_horizontal_geometry(
            width,
            height
        )
    ) {
        return false;
    }

    if (
        is_extreme_vertical_geometry(
            width,
            height
        )
    ) {
        return false;
    }

    if (
        is_large_chart_blob(
            width,
            height,
            roi_width,
            roi_height
        )
    ) {
        return false;
    }

    const double width_ratio =
        static_cast<double>(width) /
        static_cast<double>(
            std::max(
                1,
                roi_width
            )
        );

    const double height_ratio =
        static_cast<double>(height) /
        static_cast<double>(
            std::max(
                1,
                roi_height
            )
        );

    if (
        width_ratio >
        MAX_COMPONENT_WIDTH_RATIO
    ) {
        return false;
    }

    if (
        height_ratio >
        MAX_COMPONENT_HEIGHT_RATIO
    ) {
        return false;
    }

    return true;
}

// =============================================================================
// COMPONENT OCCUPANCY
// =============================================================================
//
// ConnectedComponents::extract() already gives us only a bounding box.
// We do not have a per-component pixel count after BFS unless we track it.
//
// This helper therefore calculates a conservative geometric proxy.
// =============================================================================

[[nodiscard]]
double component_density_proxy(
    const BoundingBox& box
) noexcept {

    const int width =
        box.width();

    const int height =
        box.height();

    if (
        width <= 0 ||
        height <= 0
    ) {
        return 0.0;
    }

    /*
     * A bounding-box-only density proxy cannot recover exact occupancy.
     * For component filtering we therefore use a deliberately conservative
     * estimate based on the geometry itself.
     */
    const double aspect =
        static_cast<double>(
            std::max(
                width,
                height
            )
        ) /
        static_cast<double>(
            std::max(
                1,
                std::min(
                    width,
                    height
                )
            )
        );

    if (
        aspect <= 1.5
    ) {
        return 0.50;
    }

    if (
        aspect <= 4.0
    ) {
        return 0.25;
    }

    return 0.10;
}

} // namespace

// =============================================================================
// EXTRACT CONNECTED COMPONENTS + MERGE FRAGMENTS
// =============================================================================

std::vector<BoundingBox>
ConnectedComponents::extract(
    const uint8_t* image,
    int width,
    int min_y,
    int max_y,
    int channels
) {

    std::vector<BoundingBox> boxes;

    // =========================================================================
    // INPUT VALIDATION
    // =========================================================================

    if (
        image == nullptr ||
        width <= 0 ||
        min_y < 0 ||
        max_y <= min_y ||
        channels <= 0
    ) {
        return boxes;
    }

    const int roi_height =
        max_y -
        min_y;

    if (
        roi_height <= 1
    ) {
        return boxes;
    }

    // =========================================================================
    // VISITED STATE
    // =========================================================================

    const std::size_t visited_size =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(roi_height);

    if (
        visited_size == 0
    ) {
        return boxes;
    }

    std::vector<uint8_t> visited(
        visited_size,
        uint8_t{0}
    );

    // =========================================================================
    // BFS QUEUE
    // =========================================================================

    std::vector<int> queue;

    queue.reserve(
        256
    );

    // =========================================================================
    // VISITED INDEX
    // =========================================================================

    const auto visited_index =
        [width, min_y](
            int x,
            int y
        ) noexcept -> std::size_t {

            return
                static_cast<std::size_t>(
                    y - min_y
                ) *
                static_cast<std::size_t>(
                    width
                ) +
                static_cast<std::size_t>(
                    x
                );
        };

    // =========================================================================
    // STAGE 1: RAW 4-CONNECTED COMPONENTS
    // =========================================================================

    for (
        int y = min_y;
        y < max_y;
        ++y
    ) {

        for (
            int x = 0;
            x < width;
            ++x
        ) {

            const std::size_t start_index =
                visited_index(
                    x,
                    y
                );

            if (
                visited[start_index] != 0
            ) {
                continue;
            }

            visited[start_index] =
                1;

            const uint8_t pixel =
                ocr_pixel(
                    image,
                    width,
                    channels,
                    x,
                    y
                );

            if (
                !is_foreground_for_channels(
                    pixel,
                    channels
                )
            ) {
                continue;
            }

            BoundingBox box{
                x,
                y,
                x,
                y
            };

            queue.clear();

            queue.push_back(
                y * width + x
            );

            std::size_t head =
                0;

            while (
                head <
                queue.size()
            ) {

                const int encoded =
                    queue[head++];

                const int cx =
                    encoded %
                    width;

                const int cy =
                    encoded /
                    width;

                box.min_x =
                    std::min(
                        box.min_x,
                        cx
                    );

                box.max_x =
                    std::max(
                        box.max_x,
                        cx
                    );

                box.min_y =
                    std::min(
                        box.min_y,
                        cy
                    );

                box.max_y =
                    std::max(
                        box.max_y,
                        cy
                    );

                // =================================================================
                // LEFT
                // =================================================================

                if (
                    cx > 0
                ) {

                    const int nx =
                        cx - 1;

                    const int ny =
                        cy;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                ocr_pixel(
                                    image,
                                    width,
                                    channels,
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny * width + nx
                            );
                        }
                    }
                }

                // =================================================================
                // RIGHT
                // =================================================================

                if (
                    cx + 1 <
                    width
                ) {

                    const int nx =
                        cx + 1;

                    const int ny =
                        cy;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                ocr_pixel(
                                    image,
                                    width,
                                    channels,
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny * width + nx
                            );
                        }
                    }
                }

                // =================================================================
                // UP
                // =================================================================

                if (
                    cy > min_y
                ) {

                    const int nx =
                        cx;

                    const int ny =
                        cy - 1;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                ocr_pixel(
                                    image,
                                    width,
                                    channels,
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny * width + nx
                            );
                        }
                    }
                }

                // =================================================================
                // DOWN
                // =================================================================

                if (
                    cy + 1 <
                    max_y
                ) {

                    const int nx =
                        cx;

                    const int ny =
                        cy + 1;

                    const std::size_t vi =
                        visited_index(
                            nx,
                            ny
                        );

                    if (
                        visited[vi] == 0
                    ) {

                        visited[vi] =
                            1;

                        if (
                            is_foreground_for_channels(
                                ocr_pixel(
                                    image,
                                    width,
                                    channels,
                                    nx,
                                    ny
                                ),
                                channels
                            )
                        ) {

                            queue.push_back(
                                ny * width + nx
                            );
                        }
                    }
                }
            }

            // =========================================================================
            // COMPONENT GEOMETRY
            // =========================================================================

            const int component_width =
                box.width();

            const int component_height =
                box.height();

            const int component_area =
                box.area();

            if (
                !acceptable_component_geometry(
                    box,
                    width,
                    roi_height
                )
            ) {
                continue;
            }

            if (
                component_area < 2
            ) {
                continue;
            }

            // =========================================================================
            // WIDE COMPONENT PROTECTION
            // =========================================================================
            //
            // Small text components may be wide, but a wide/short connected
            // component is usually plot geometry. Do not send these directly
            // into MatrixMatcher.
            // =========================================================================

            if (
                component_width >=
                    WIDE_COMPONENT_WIDTH &&
                component_height <=
                    WIDE_COMPONENT_MAX_HEIGHT
            ) {

                const double density_proxy =
                    component_density_proxy(
                        box
                    );

                if (
                    density_proxy >=
                    WIDE_COMPONENT_MAX_DENSITY
                ) {
                    continue;
                }
            }

            // =========================================================================
            // FINAL COMPONENT
            // =========================================================================

            boxes.push_back(
                box
            );
        }
    }

    if (
        boxes.empty()
    ) {
        return boxes;
    }

    // =========================================================================
    // STAGE 2: SORT
    // =========================================================================

    std::sort(
        boxes.begin(),
        boxes.end(),
        [](
            const BoundingBox& a,
            const BoundingBox& b
        ) noexcept {

            if (
                a.min_x !=
                b.min_x
            ) {
                return
                    a.min_x <
                    b.min_x;
            }

            if (
                a.min_y !=
                b.min_y
            ) {
                return
                    a.min_y <
                    b.min_y;
            }

            if (
                a.max_x !=
                b.max_x
            ) {
                return
                    a.max_x <
                    b.max_x;
            }

            return
                a.max_y <
                b.max_y;
        }
    );

    // =========================================================================
    // STAGE 3: MERGE FRAGMENTS
    // =========================================================================
    //
    // Important rule:
    //
    //     only merge fragments which can plausibly belong to one glyph.
    //
    // Never allow a chain of weak merges to turn multiple chart structures
    // into one massive OCR component.
    //
    // =========================================================================

    std::vector<BoundingBox> merged;

    merged.reserve(
        boxes.size()
    );

    for (
        const BoundingBox& current :
        boxes
    ) {

        bool merged_current =
            false;

        const std::size_t search_begin =
            merged.size() > 6
                ? merged.size() - 6
                : 0;

        for (
            std::size_t i =
                merged.size();
            i-- >
                search_begin;
        ) {

            BoundingBox& previous =
                merged[i];

            const int pw =
                previous.width();

            const int ph =
                previous.height();

            const int cw =
                current.width();

            const int ch =
                current.height();

            if (
                pw <= 0 ||
                ph <= 0 ||
                cw <= 0 ||
                ch <= 0
            ) {
                continue;
            }

            const int horizontal_overlap =
                std::max(
                    0,
                    std::min(
                        previous.max_x,
                        current.max_x
                    ) -
                    std::max(
                        previous.min_x,
                        current.min_x
                    ) +
                    1
                );

            const int vertical_overlap =
                std::max(
                    0,
                    std::min(
                        previous.max_y,
                        current.max_y
                    ) -
                    std::max(
                        previous.min_y,
                        current.min_y
                    ) +
                    1
                );

            const int horizontal_gap =
                current.min_x >
                    previous.max_x
                    ? current.min_x -
                      previous.max_x -
                      1
                    : previous.min_x >
                        current.max_x
                        ? previous.min_x -
                          current.max_x -
                          1
                        : 0;

            const int vertical_gap =
                current.min_y >
                    previous.max_y
                    ? current.min_y -
                      previous.max_y -
                      1
                    : previous.min_y >
                        current.max_y
                        ? previous.min_y -
                          current.max_y -
                          1
                        : 0;

            // =========================================================================
            // GLYPH HEIGHT COMPATIBILITY
            // =========================================================================

            const int smaller_height =
                std::min(
                    ph,
                    ch
                );

            const int larger_height =
                std::max(
                    ph,
                    ch
                );

            const double height_ratio =
                larger_height > 0
                    ? static_cast<double>(
                          smaller_height
                      ) /
                      static_cast<double>(
                          larger_height
                      )
                    : 0.0;

            const bool compatible_height =
                height_ratio >=
                0.45;

            // =========================================================================
            // DOT + STEM
            // =========================================================================

            const bool previous_is_dot =
                pw <= 5 &&
                ph <= 5;

            const bool current_is_dot =
                cw <= 5 &&
                ch <= 5;

            const bool previous_is_stem =
                ph >= 6 &&
                ph >= pw * 2;

            const bool current_is_stem =
                ch >= 6 &&
                ch >= cw * 2;

            const bool dot_stem_pair =
                (
                    previous_is_dot &&
                    current_is_stem
                ) ||
                (
                    current_is_dot &&
                    previous_is_stem
                );

            if (
                dot_stem_pair
            ) {

                const int dot_width =
                    previous_is_dot
                        ? pw
                        : cw;

                const int stem_width =
                    previous_is_stem
                        ? pw
                        : cw;

                const int required_x_overlap =
                    std::max(
                        1,
                        std::min(
                            dot_width,
                            stem_width
                        ) /
                        2
                    );

                const bool aligned =
                    horizontal_overlap >=
                    required_x_overlap;

                if (
                    aligned &&
                    vertical_gap <= 6
                ) {

                    const int new_min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    const int new_min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    const int new_max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    const int new_max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    const int merged_width =
                        new_max_x -
                        new_min_x +
                        1;

                    const int merged_height =
                        new_max_y -
                        new_min_y +
                        1;

                    if (
                        merged_width <=
                            MAX_TEXT_COMPONENT_WIDTH &&
                        merged_height <=
                            MAX_TEXT_COMPONENT_HEIGHT
                    ) {

                        previous.min_x =
                            new_min_x;

                        previous.min_y =
                            new_min_y;

                        previous.max_x =
                            new_max_x;

                        previous.max_y =
                            new_max_y;

                        merged_current =
                            true;

                        break;
                    }
                }
            }

            // =========================================================================
            // VERTICAL FRAGMENT MERGE
            // =========================================================================
            //
            // Useful for:
            //
            //     i-dot
            //     split glyphs
            //     anti-aliased vertical pieces
            //
            // Do not merge tall structures into text unless height compatibility
            // is strong and the x overlap is meaningful.
            // =========================================================================

            const int min_width =
                std::min(
                    pw,
                    cw
                );

            const bool strong_x_overlap =
                horizontal_overlap >=
                std::max(
                    1,
                    min_width / 2
                );

            const int height_reference =
                std::max(
                    ph,
                    ch
                );

            const int allowed_vertical_gap =
                std::max(
                    1,
                    std::min(
                        4,
                        height_reference / 4
                    )
                );

            const bool vertical_fragment =
                strong_x_overlap &&
                vertical_gap <=
                    allowed_vertical_gap &&
                compatible_height;

            if (
                vertical_fragment
            ) {

                const bool normal_piece =
                    pw <= 24 &&
                    cw <= 24 &&
                    ph <= 20 &&
                    ch <= 20;

                if (
                    normal_piece
                ) {

                    const int new_min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    const int new_min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    const int new_max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    const int new_max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    const int merged_width =
                        new_max_x -
                        new_min_x +
                        1;

                    const int merged_height =
                        new_max_y -
                        new_min_y +
                        1;

                    if (
                        merged_width <=
                            MAX_TEXT_COMPONENT_WIDTH &&
                        merged_height <=
                            MAX_TEXT_COMPONENT_HEIGHT
                    ) {

                        previous.min_x =
                            new_min_x;

                        previous.min_y =
                            new_min_y;

                        previous.max_x =
                            new_max_x;

                        previous.max_y =
                            new_max_y;

                        merged_current =
                            true;

                        break;
                    }
                }
            }

            // =========================================================================
            // HORIZONTAL FRAGMENT MERGE
            // =========================================================================
            //
            // Merge small adjacent pieces from one glyph, but never merge
            // long chart structures.
            // =========================================================================

            const int min_height =
                std::min(
                    ph,
                    ch
                );

            const bool strong_y_overlap =
                vertical_overlap >=
                std::max(
                    1,
                    min_height / 2
                );

            const int width_reference =
                std::max(
                    pw,
                    cw
                );

            const int allowed_horizontal_gap =
                std::max(
                    1,
                    std::min(
                        3,
                        width_reference / 4
                    )
                );

            const bool horizontal_fragment =
                strong_y_overlap &&
                horizontal_gap <=
                    allowed_horizontal_gap &&
                compatible_height;

            if (
                horizontal_fragment
            ) {

                const bool small_fragments =
                    pw <= 20 &&
                    cw <= 20 &&
                    ph <= 20 &&
                    ch <= 20;

                if (
                    small_fragments
                ) {

                    const int new_min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    const int new_min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    const int new_max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    const int new_max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    const int merged_width =
                        new_max_x -
                        new_min_x +
                        1;

                    const int merged_height =
                        new_max_y -
                        new_min_y +
                        1;

                    if (
                        merged_width <=
                            MAX_TEXT_COMPONENT_WIDTH &&
                        merged_height <=
                            MAX_TEXT_COMPONENT_HEIGHT
                    ) {

                        previous.min_x =
                            new_min_x;

                        previous.min_y =
                            new_min_y;

                        previous.max_x =
                            new_max_x;

                        previous.max_y =
                            new_max_y;

                        merged_current =
                            true;

                        break;
                    }
                }
            }
        }

        if (
            !merged_current
        ) {

            merged.push_back(
                current
            );
        }
    }

    // =========================================================================
    // STAGE 4: FINAL FILTER AFTER MERGING
    // =========================================================================

    std::vector<BoundingBox> final_boxes;

    final_boxes.reserve(
        merged.size()
    );

    for (
        const BoundingBox& box :
        merged
    ) {

        const int box_width =
            box.width();

        const int box_height =
            box.height();

        if (
            !acceptable_component_geometry(
                box,
                width,
                roi_height
            )
        ) {
            continue;
        }

        if (
            box_width >=
                WIDE_COMPONENT_WIDTH &&
            box_height <=
                WIDE_COMPONENT_MAX_HEIGHT
        ) {

            if (
                component_density_proxy(
                    box
                ) >=
                WIDE_COMPONENT_MAX_DENSITY
            ) {
                continue;
            }
        }

        final_boxes.push_back(
            box
        );
    }

    // =========================================================================
    // STAGE 5: FINAL ORDER
    // =========================================================================

    std::sort(
        final_boxes.begin(),
        final_boxes.end(),
        [](
            const BoundingBox& a,
            const BoundingBox& b
        ) noexcept {

            if (
                a.min_x !=
                b.min_x
            ) {
                return
                    a.min_x <
                    b.min_x;
            }

            if (
                a.min_y !=
                b.min_y
            ) {
                return
                    a.min_y <
                    b.min_y;
            }

            if (
                a.max_x !=
                b.max_x
            ) {
                return
                    a.max_x <
                    b.max_x;
            }

            return
                a.max_y <
                b.max_y;
        }
    );

    return final_boxes;
}

} // namespace fin_ocr
