#include "fin_ocr/chart/label/label_merger.hpp"

#include "fin_ocr/chart/label/label_geometry.hpp"

namespace fin_ocr::chart::label::merger {

// =============================================================================
// DUPLICATE LABEL DETECTION
// =============================================================================
//
// Two detected labels are treated as duplicates when:
//
//     1. Their recognized text is identical.
//     2. Their vertical spans overlap.
//     3. Their horizontal spans overlap.
//
// The first matching label is sufficient to reject the candidate.
//
// =============================================================================

[[nodiscard]]
bool duplicate_label(
    const DetectedLabel& candidate,
    const std::vector<DetectedLabel>& existing
) noexcept
{
    for (
        const DetectedLabel& current :
        existing
    ) {

        if (
            current.label.text !=
            candidate.label.text
        ) {
            continue;
        }

        if (
            !geometry::vertical_overlap(
                current.label.min_y,
                current.label.max_y,
                candidate.label.min_y,
                candidate.label.max_y
            )
        ) {
            continue;
        }

        if (
            geometry::horizontal_overlap(
                current.label.min_x,
                current.label.max_x,
                candidate.label.min_x,
                candidate.label.max_x
            )
        ) {
            return true;
        }
    }

    return false;
}

} // namespace fin_ocr::chart::label::merger
