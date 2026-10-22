#ifndef FIN_VOICE_CACHE_ALIGNMENT_H
#define FIN_VOICE_CACHE_ALIGNMENT_H

/*
 * Portable alignment helpers for the C23 FinTally Voice implementation.
 *
 * 64 bytes is the project's software data-alignment target, chosen for common
 * desktop/server cache-line sizes. It is not a claim that every target has a
 * 64-byte hardware cache line.
 *
 * Use this primarily for private hot buffers/workspaces. Do not apply it
 * indiscriminately to public structs mirrored by Rust #[repr(C)]: changing a
 * public type's alignment can change its ABI and allocation requirements.
 */

#include <stddef.h>

#define FIN_VOICE_CACHE_LINE_BYTES 64U

#if defined(__cplusplus)
    #define FIN_VOICE_ALIGNAS(bytes) alignas(bytes)
#elif defined(_MSC_VER)
    /* MSVC C compiler spelling for explicit object/member alignment. */
    #define FIN_VOICE_ALIGNAS(bytes) __declspec(align(bytes))
#else
    /* Standard C11/C17/C23 spelling; valid with strict C23 mode. */
    #define FIN_VOICE_ALIGNAS(bytes) _Alignas(bytes)
#endif

#define FIN_VOICE_CACHE_ALIGN FIN_VOICE_ALIGNAS(FIN_VOICE_CACHE_LINE_BYTES)

#endif /* FIN_VOICE_CACHE_ALIGNMENT_H */
