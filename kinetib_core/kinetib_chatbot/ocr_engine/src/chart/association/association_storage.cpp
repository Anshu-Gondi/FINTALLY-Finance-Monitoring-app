#include "fin_ocr/chart/association/association_storage.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::association::storage {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_ASSOCIATIONS =
    config::CHART_MAX_ASSOCIATIONS;

constexpr double MIN_ASSOCIATION_CONFIDENCE =
    static_cast<double>(
        config::CHART_LABEL_MIN_ASSOCIATION_CONFIDENCE
    );

} // namespace

// =============================================================================
// ASSOCIATION EXISTENCE
// =============================================================================
//
// Linear lookup is intentionally preserved from the original implementation.
//
// The association limit is bounded by MAX_ASSOCIATIONS, so this remains a
// simple deterministic storage primitive. A hash/index structure can be
// introduced later after profiling if duplicate checks become measurable.
//
// =============================================================================

bool association_exists(
    const std::vector<ChartAssociation>& associations,
    int label_index,
    AssociatedObjectKind object_kind,
    int object_index
) noexcept
{
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

// =============================================================================
// ASSOCIATION INSERTION
// =============================================================================
//
// Central insertion policy:
//
//     1. enforce maximum capacity
//     2. reject invalid label/object indexes
//     3. reject low-confidence associations
//     4. reject duplicates
//     5. append accepted association
//
// =============================================================================

void append_association(
    std::vector<ChartAssociation>& associations,
    const ChartAssociation& association
)
{
    // =========================================================================
    // CAPACITY
    // =========================================================================

    if (
        associations.size() >=
        MAX_ASSOCIATIONS
    ) {
        return;
    }

    // =========================================================================
    // BASIC VALIDITY
    // =========================================================================

    if (
        association.label_index < 0 ||
        association.object_index < 0
    ) {
        return;
    }

    // =========================================================================
    // CONFIDENCE
    // =========================================================================

    if (
        association.confidence <
        MIN_ASSOCIATION_CONFIDENCE
    ) {
        return;
    }

    // =========================================================================
    // DUPLICATE SUPPRESSION
    // =========================================================================

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

    // =========================================================================
    // COMMIT
    // =========================================================================

    associations.push_back(
        association
    );
}

} // namespace fin_ocr::chart::association::storage
