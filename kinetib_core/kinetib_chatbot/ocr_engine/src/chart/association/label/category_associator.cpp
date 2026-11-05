#include "fin_ocr/chart/association/label/category_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace fin_ocr::chart::association::label::category {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double MAX_AXIS_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_AXIS_DISTANCE;

constexpr std::size_t MAX_CATEGORIES =
    config::CHART_MAX_CATEGORIES;

} // namespace

// =============================================================================
// AXIS / CATEGORY ASSOCIATION
// =============================================================================
//
// Converts recognized X-axis labels into ordered ChartCategory records.
//
// Ordering is deterministic:
//
//     primary key   -> label center X
//     secondary key -> label center Y
//
// Numeric values are not inferred here.
//
// =============================================================================

void associate_axis_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels,
    std::vector<
        ::fin_ocr::chart::association::ChartCategory
    >& categories
)
{
    // =========================================================================
    // RESET RESULT
    // =========================================================================

    categories.clear();

    // =========================================================================
    // REQUIRE HORIZONTAL X AXIS
    // =========================================================================

    if (
        !coordinates.x_axis.horizontal
    ) {
        return;
    }

    // =========================================================================
    // INTERNAL CANDIDATE
    // =========================================================================
    //
    // Candidate is deliberately private to this translation unit.
    //
    // =========================================================================

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

    // =========================================================================
    // COLLECT X-AXIS LABEL CANDIDATES
    // =========================================================================

    for (
        std::size_t i = 0;
        i < labels.size();
        ++i
    ) {
        const ::fin_ocr::chart::association::ChartLabel& label =
            labels[i];

        if (
            label.kind !=
            ::fin_ocr::chart::association::
                ChartLabelKind::X_AXIS_LABEL
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Label center.
        // ---------------------------------------------------------------------

        const auto [cx, cy] =
            ::fin_ocr::chart::association::geometry::
                rect_center(
                    label.min_x,
                    label.min_y,
                    label.max_x,
                    label.max_y
                );

        // ---------------------------------------------------------------------
        // Distance from the axis row to the label rectangle.
        // ---------------------------------------------------------------------

        const double distance =
            ::fin_ocr::chart::association::geometry::
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

    // =========================================================================
    // DETERMINISTIC CATEGORY ORDERING
    // =========================================================================

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

    // =========================================================================
    // LIMIT CATEGORY COUNT
    // =========================================================================

    const std::size_t count =
        std::min(
            candidates.size(),
            MAX_CATEGORIES
        );

    categories.reserve(
        count
    );

    // =========================================================================
    // MATERIALIZE CATEGORIES
    // =========================================================================

    for (
        std::size_t i = 0;
        i < count;
        ++i
    ) {
        const Candidate& candidate =
            candidates[i];

        ::fin_ocr::chart::association::ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    candidate.label_index
                )
            ];

        ::fin_ocr::chart::association::ChartCategory category{};

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

        // ---------------------------------------------------------------------
        // Distance-based confidence.
        // ---------------------------------------------------------------------

        const double proximity =
            ::fin_ocr::chart::association::geometry::
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

        // ---------------------------------------------------------------------
        // Write category index back to the recognized label.
        // ---------------------------------------------------------------------

        label.category_index =
            category.category_index;

        categories.push_back(
            std::move(
                category
            )
        );
    }
}

} // namespace fin_ocr::chart::association::label::category
