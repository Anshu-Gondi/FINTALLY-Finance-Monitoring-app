#include "fin_ocr/segmentation/connected_components.hpp"

#include "fin_ocr/core/pixel_access.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr {

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
        min_y >= max_y ||
        channels <= 0
    ) {
        return boxes;
    }

    const int roi_height =
        max_y -
        min_y;

    // =========================================================================
    // VISITED STATE
    // =========================================================================

    std::vector<uint8_t> visited(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(roi_height),
        0
    );

    // =========================================================================
    // BFS QUEUE
    // =========================================================================

    std::vector<int> queue;

    queue.reserve(256);

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

            if (
                !is_foreground_for_channels(
                    ocr_pixel(
                        image,
                        width,
                        channels,
                        x,
                        y
                    ),
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

            // =================================================================
            // COMPONENT DIMENSIONS
            // =================================================================

            const int component_w =
                box.width();

            const int component_h =
                box.height();

            const int component_area =
                box.area();

            if (
                component_w < 1 ||
                component_h < 2 ||
                component_area < 2
            ) {
                continue;
            }

            // =================================================================
            // HORIZONTAL RULE
            // =================================================================

            const bool horizontal_rule =
                component_w >= 20 &&
                component_h <= 3 &&
                component_w >=
                    component_h * 8;

            if (
                horizontal_rule
            ) {
                continue;
            }

            // =================================================================
            // VERTICAL RULE
            // =================================================================

            const bool vertical_rule =
                component_h >= 20 &&
                component_w <= 3 &&
                component_h >=
                    component_w * 8;

            if (
                vertical_rule
            ) {
                continue;
            }

            // =================================================================
            // HUGE IMAGE BLOB
            // =================================================================

            const bool image_width_blob =
                component_w >=
                std::max(
                    128,
                    width * 3 / 4
                );

            const bool image_height_blob =
                component_h >=
                std::max(
                    128,
                    roi_height * 3 / 4
                );

            const bool huge_blob =
                image_width_blob &&
                image_height_blob;

            if (
                huge_blob
            ) {
                continue;
            }

            // =================================================================
            // THICK HORIZONTAL STRUCTURE
            // =================================================================

            const bool thick_horizontal_structure =
                component_w >=
                    std::max(
                        64,
                        width / 3
                    ) &&

                component_h <= 6 &&

                component_w >=
                    component_h * 12;

            if (
                thick_horizontal_structure
            ) {
                continue;
            }

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

            return
                a.min_y <
                b.min_y;
        }
    );

    // =========================================================================
    // STAGE 3: MERGE FRAGMENTS
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
            merged.size() > 4
                ? merged.size() - 4
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

                    : previous.min_x -
                      current.max_x -
                      1;

            const int vertical_gap =
                current.min_y >
                    previous.max_y

                    ? current.min_y -
                      previous.max_y -
                      1

                    : previous.min_y -
                      current.max_y -
                      1;

            // =================================================================
            // DOT + STEM
            // =================================================================

            const bool previous_is_dot =
                pw <= 5 &&
                ph <= 5;

            const bool current_is_dot =
                cw <= 5 &&
                ch <= 5;

            const bool previous_is_stem =
                ph >= 6 &&
                ph >=
                    pw * 2;

            const bool current_is_stem =
                ch >= 6 &&
                ch >=
                    cw * 2;

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
                        ) / 2
                    );

                const bool good_x_alignment =
                    horizontal_overlap >=
                    required_x_overlap;

                const bool good_vertical_gap =
                    vertical_gap <=
                    6;

                if (
                    good_x_alignment &&
                    good_vertical_gap
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
                }
            }

            // =================================================================
            // GENERAL VERTICAL FRAGMENT
            // =================================================================

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
                    2,
                    std::min(
                        5,
                        height_reference / 4
                    )
                );

            const bool vertical_fragment =
                strong_x_overlap &&
                vertical_gap <=
                    allowed_vertical_gap;

            if (
                vertical_fragment
            ) {

                const bool both_normal_sized =
                    ph >= 6 &&
                    ch >= 6 &&
                    pw >= 3 &&
                    cw >= 3;

                if (
                    !both_normal_sized
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
                }
            }

            // =================================================================
            // HORIZONTAL FRAGMENT
            // =================================================================

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
                    allowed_horizontal_gap;

            if (
                horizontal_fragment
            ) {

                const bool tiny_fragment =
                    pw <= 5 ||
                    cw <= 5;

                if (
                    tiny_fragment
                ) {

                    previous.min_x =
                        std::min(
                            previous.min_x,
                            current.min_x
                        );

                    previous.min_y =
                        std::min(
                            previous.min_y,
                            current.min_y
                        );

                    previous.max_x =
                        std::max(
                            previous.max_x,
                            current.max_x
                        );

                    previous.max_y =
                        std::max(
                            previous.max_y,
                            current.max_y
                        );

                    merged_current =
                        true;

                    break;
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
    // STAGE 4: FINAL ORDER
    // =========================================================================

    std::sort(
        merged.begin(),
        merged.end(),
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

            return
                a.min_y <
                b.min_y;
        }
    );

    return merged;
}

} // namespace fin_ocr
