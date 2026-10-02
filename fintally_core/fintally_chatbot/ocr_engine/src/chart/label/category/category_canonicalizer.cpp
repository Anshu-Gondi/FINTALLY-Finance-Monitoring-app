#include "fin_ocr/chart/label/category/category_canonicalizer.hpp"

#include <string>

namespace fin_ocr::chart::label::category {

// =============================================================================
// CATEGORY LABEL CANONICALIZATION
// =============================================================================
//
// Financial x-axis labels frequently contain short month names.
//
// OCR errors on three-character month abbreviations are predictable:
//
//     Apt -> Apr
//     Seo -> Sep
//     Now -> Nov
//     Bec -> Dec
//     Mav -> May
//
// These corrections are deliberately constrained to the known month
// vocabulary.
//
// No general-purpose fuzzy correction is performed.
//
// =============================================================================

[[nodiscard]]
const char* canonical_month_label(
    const std::string& text
) noexcept
{
    if (
        text == "Jan"
    ) {
        return "Jan";
    }

    if (
        text == "Feb"
    ) {
        return "Feb";
    }

    if (
        text == "Mar"
    ) {
        return "Mar";
    }

    if (
        text == "Apt"
    ) {
        return "Apr";
    }

    if (
        text == "Apr"
    ) {
        return "Apr";
    }

    if (
        text == "May"
    ) {
        return "May";
    }

    if (
        text == "Mav"
    ) {
        return "May";
    }

    if (
        text == "Jun"
    ) {
        return "Jun";
    }

    if (
        text == "Jul"
    ) {
        return "Jul";
    }

    if (
        text == "Seo"
    ) {
        return "Sep";
    }

    if (
        text == "Sep"
    ) {
        return "Sep";
    }

    if (
        text == "Oct"
    ) {
        return "Oct";
    }

    if (
        text == "Now"
    ) {
        return "Nov";
    }

    if (
        text == "Nov"
    ) {
        return "Nov";
    }

    if (
        text == "Bec"
    ) {
        return "Dec";
    }

    if (
        text == "Dec"
    ) {
        return "Dec";
    }

    return nullptr;
}

// =============================================================================
// CANONICALIZE CATEGORY LABEL
// =============================================================================
//
// Returns true when the OCR text belongs to the constrained month vocabulary
// and was actually changed.
//
// =============================================================================

[[nodiscard]]
bool canonicalize_category_label(
    std::string& text
) noexcept
{
    const char* canonical =
        canonical_month_label(
            text
        );

    if (
        canonical == nullptr
    ) {
        return false;
    }

    const bool changed =
        text != canonical;

    if (
        changed
    ) {
        text =
            canonical;
    }

    return changed;
}

} // namespace fin_ocr::chart::label::category
