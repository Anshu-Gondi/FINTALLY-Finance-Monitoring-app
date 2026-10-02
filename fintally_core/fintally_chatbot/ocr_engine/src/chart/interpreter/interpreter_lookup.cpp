#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>

namespace fin_ocr::chart::interpreter::lookup {

// =============================================================================
// LABEL LOOKUP
// =============================================================================

const ::fin_ocr::chart::association::ChartLabel*
find_label(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int label_index
) noexcept
{
    if (
        label_index < 0 ||
        static_cast<std::size_t>(label_index) >=
            associations.labels.size()
    ) {
        return nullptr;
    }

    return
        &associations.labels[
            static_cast<std::size_t>(
                label_index
            )
        ];
}

// =============================================================================
// CATEGORY LOOKUP
// =============================================================================

const ::fin_ocr::chart::association::ChartCategory*
find_category(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int category_index
) noexcept
{
    if (
        category_index < 0 ||
        static_cast<std::size_t>(category_index) >=
            associations.categories.size()
    ) {
        return nullptr;
    }

    return
        &associations.categories[
            static_cast<std::size_t>(
                category_index
            )
        ];
}

// =============================================================================
// SERIES LOOKUP
// =============================================================================

const ::fin_ocr::chart::association::ChartSeries*
find_series(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int series_index
) noexcept
{
    if (
        series_index < 0 ||
        static_cast<std::size_t>(series_index) >=
            associations.series.size()
    ) {
        return nullptr;
    }

    return
        &associations.series[
            static_cast<std::size_t>(
                series_index
            )
        ];
}

// =============================================================================
// ASSOCIATION LOOKUP
// =============================================================================

const ::fin_ocr::chart::association::ChartAssociation*
find_object_association(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    ::fin_ocr::chart::association::AssociatedObjectKind object_kind,
    int object_index
) noexcept
{
    const ::fin_ocr::chart::association::ChartAssociation* best =
        nullptr;

    for (
        const ::fin_ocr::chart::association::ChartAssociation& association :
        associations.associations
    ) {

        if (
            association.object_kind !=
            object_kind
        ) {
            continue;
        }

        if (
            association.object_index !=
            object_index
        ) {
            continue;
        }

        if (
            best == nullptr ||
            association.confidence >
                best->confidence
        ) {
            best =
                &association;
        }
    }

    return best;
}

// =============================================================================
// CATEGORY NAME
// =============================================================================

std::string category_name(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int category_index
)
{
    const ::fin_ocr::chart::association::ChartCategory* category =
        find_category(
            associations,
            category_index
        );

    if (
        category == nullptr
    ) {
        return {};
    }

    return category->name;
}

// =============================================================================
// SERIES NAME
// =============================================================================

std::string series_name(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int series_index
)
{
    const ::fin_ocr::chart::association::ChartSeries* series =
        find_series(
            associations,
            series_index
        );

    if (
        series == nullptr
    ) {
        return {};
    }

    return series->name;
}

} // namespace fin_ocr::chart::interpreter::lookup
