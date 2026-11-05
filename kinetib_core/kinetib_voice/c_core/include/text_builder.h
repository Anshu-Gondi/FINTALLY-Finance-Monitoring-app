#ifndef FIN_VOICE_TEXT_BUILDER_H
#define FIN_VOICE_TEXT_BUILDER_H

/*
 * FinTally Voice — bounded assembly of text supplied by a real recognizer.
 *
 * This module is NOT speech recognition, language modeling, or decoding of
 * vector-quantizer IDs. It accepts already-decoded UTF-8 text only. A codebook
 * centroid index has no character/word meaning and must never be passed here
 * as if it were a transcript token.
 *
 * Caller owns both builder and storage. No heap allocation or thread
 * synchronization is performed. One serialized owner must mutate a builder.
 * Capacity includes the final NUL byte. Each append's input must be complete,
 * strictly valid UTF-8 and must not contain an embedded NUL.
 *
 * Overflow policy: append the largest PREFIX ending at a complete Unicode code
 * point that fits, drop the rest of that append, record saturated telemetry,
 * and return FIN_VOICE_TEXT_BUILDER_OVERFLOW. Already-buffered text is kept.
 * Later appends may still add data if capacity remains. No partial UTF-8
 * code point is ever written.
 *
 * Flush copies the current text plus NUL to a distinct caller-provided output
 * buffer. If output capacity is insufficient, it returns OUTPUT_TOO_SMALL
 * and preserves the builder contents. On success, it clears buffered text but
 * preserves cumulative overflow telemetry; reset clears both content and
 * telemetry. Flush source and destination ranges must not overlap.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#error "FinTally Voice text builder is a C-only API; include it from C sources."
#endif

#define FIN_VOICE_TEXT_BUILDER_ABI_MAJOR 1u
#define FIN_VOICE_TEXT_BUILDER_ABI_MINOR 0u
#define FIN_VOICE_TEXT_BUILDER_MAX_UTF8_BYTES_PER_CODEPOINT 4u

typedef enum FinVoiceTextBuilderStatus {
    FIN_VOICE_TEXT_BUILDER_OK = 0,
    FIN_VOICE_TEXT_BUILDER_OVERFLOW = 1,
    FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT = 2,
    FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION = 3,
    FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8 = 4,
    FIN_VOICE_TEXT_BUILDER_ERROR_OUTPUT_TOO_SMALL = 5
} FinVoiceTextBuilderStatus;

typedef struct FinVoiceTextBuilder {
    char *buffer;              /* Caller-owned storage. */
    size_t capacity;           /* Total bytes, including trailing NUL. */
    size_t length;             /* Current text length in UTF-8 bytes. */
    size_t overflow_count;     /* Saturating count of append calls truncated. */
    size_t dropped_bytes;      /* Saturating count of UTF-8 input bytes dropped. */
} FinVoiceTextBuilder;

/* Initialize an empty builder. capacity must be at least one byte. */
FinVoiceTextBuilderStatus fin_voice_text_builder_init(
    FinVoiceTextBuilder *builder,
    char *storage,
    size_t capacity);

/*
 * Append a byte-counted, complete UTF-8 chunk. A truncated append can return
 * OVERFLOW while committing a valid prefix; invalid UTF-8 commits nothing.
 * Passing text == NULL is allowed only when length == 0. The input byte range
 * must not overlap the builder's entire storage range.
 */
FinVoiceTextBuilderStatus fin_voice_text_builder_append(
    FinVoiceTextBuilder *builder,
    const char *text,
    size_t length);

/* Convenience wrapper for a NUL-terminated UTF-8 string. */
FinVoiceTextBuilderStatus fin_voice_text_builder_append_cstr(
    FinVoiceTextBuilder *builder,
    const char *text);

/* Borrow the current text without changing the builder. */
FinVoiceTextBuilderStatus fin_voice_text_builder_view(
    const FinVoiceTextBuilder *builder,
    const char **out_text,
    size_t *out_length);

/*
 * Copy current text to a distinct output buffer and consume the buffered text.
 * out_length receives payload bytes excluding NUL. On OUTPUT_TOO_SMALL, the
 * required payload length is still reported and builder contents are retained.
 * Output capacity must include the terminating NUL.
 */
FinVoiceTextBuilderStatus fin_voice_text_builder_flush(
    FinVoiceTextBuilder *builder,
    char *destination,
    size_t destination_capacity,
    size_t *out_length);

/* Clear buffered content and cumulative overflow telemetry. */
FinVoiceTextBuilderStatus fin_voice_text_builder_reset(
    FinVoiceTextBuilder *builder);

size_t fin_voice_text_builder_capacity(const FinVoiceTextBuilder *builder);
size_t fin_voice_text_builder_length(const FinVoiceTextBuilder *builder);
size_t fin_voice_text_builder_overflow_count(const FinVoiceTextBuilder *builder);
size_t fin_voice_text_builder_dropped_bytes(const FinVoiceTextBuilder *builder);

const char *fin_voice_text_builder_status_name(FinVoiceTextBuilderStatus status);
uint32_t fin_voice_text_builder_abi_version(void);

#endif /* FIN_VOICE_TEXT_BUILDER_H */
