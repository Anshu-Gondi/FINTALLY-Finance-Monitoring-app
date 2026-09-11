#include "fin_ocr/core/safe_arithmetric.hpp"

#include <cstddef>
#include <limits>

namespace fin_ocr::core {

// =============================================================================
// SAFE MULTIPLICATION
//
// Returns:
//
//     true  -> multiplication succeeded, `out` contains a * b
//     false -> overflow would occur, `out` is left unchanged
//
// GNU/Clang:
//     use compiler overflow intrinsic.
//
// Other compilers:
//     use the standard division-based overflow check.
// =============================================================================

bool safe_mul(
    std::size_t a,
    std::size_t b,
    std::size_t& out
) noexcept {

#if defined(__GNUC__) || defined(__clang__)

    return !__builtin_mul_overflow(
        a,
        b,
        &out
    );

#else

    if (
        a != 0 &&
        b >
            std::numeric_limits<
                std::size_t
            >::max() / a
    ) {

        return false;
    }

    out =
        a * b;

    return true;

#endif
}

// =============================================================================
// SAFE ADDITION
//
// Returns:
//
//     true  -> addition succeeded, `out` contains a + b
//     false -> overflow would occur, `out` is left unchanged
// =============================================================================

bool safe_add(
    std::size_t a,
    std::size_t b,
    std::size_t& out
) noexcept {

    if (
        b >
        std::numeric_limits<
            std::size_t
        >::max() - a
    ) {

        return false;
    }

    out =
        a + b;

    return true;
}

} // namespace fin_ocr::core
