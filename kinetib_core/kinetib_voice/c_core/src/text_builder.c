#include "text_builder.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool fin_voice_text_builder_is_valid(const FinVoiceTextBuilder *builder)
{
    return builder != NULL && builder->buffer != NULL && builder->capacity > 0u &&
           builder->length < builder->capacity &&
           builder->buffer[builder->length] == '\0';
}

/*
 * Compare ranges through uintptr_t to avoid undefined relational comparisons
 * between unrelated C objects. If address arithmetic would wrap, reject the
 * ranges as overlapping rather than risk an unsafe copy.
 */
static bool fin_voice_memory_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size)
{
    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    const uintptr_t left_start = (uintptr_t)left;
    const uintptr_t right_start = (uintptr_t)right;
    if (left_size > UINTPTR_MAX - left_start ||
        right_size > UINTPTR_MAX - right_start) {
        return true;
    }

    const uintptr_t left_end = left_start + left_size;
    const uintptr_t right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

/* Return a valid code point width, or zero for malformed/incomplete UTF-8. */
static size_t fin_voice_utf8_codepoint_width(
    const unsigned char *text,
    size_t length,
    size_t offset)
{
    if (text == NULL || offset >= length) {
        return 0u;
    }

    const unsigned char lead = text[offset];
    if (lead == 0u) {
        /* The builder stores NUL-terminated strings; embedded NUL is invalid. */
        return 0u;
    }
    if (lead <= 0x7Fu) {
        return 1u;
    }

    size_t continuation_count;
    unsigned char second_min = 0x80u;
    unsigned char second_max = 0xBFu;

    if (lead >= 0xC2u && lead <= 0xDFu) {
        continuation_count = 1u;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
        continuation_count = 2u;
        if (lead == 0xE0u) {
            second_min = 0xA0u; /* Reject overlong encodings. */
        } else if (lead == 0xEDu) {
            second_max = 0x9Fu; /* Reject UTF-16 surrogate code points. */
        }
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
        continuation_count = 3u;
        if (lead == 0xF0u) {
            second_min = 0x90u; /* Reject overlong encodings. */
        } else if (lead == 0xF4u) {
            second_max = 0x8Fu; /* Unicode ends at U+10FFFF. */
        }
    } else {
        return 0u;
    }

    const size_t remaining = length - offset;
    if (remaining <= continuation_count) {
        return 0u;
    }

    const unsigned char second = text[offset + 1u];
    if (second < second_min || second > second_max) {
        return 0u;
    }

    for (size_t index = 2u; index <= continuation_count; ++index) {
        const unsigned char continuation = text[offset + index];
        if (continuation < 0x80u || continuation > 0xBFu) {
            return 0u;
        }
    }

    return continuation_count + 1u;
}

static bool fin_voice_utf8_is_valid(const char *text, size_t length)
{
    if (length > 0u && text == NULL) {
        return false;
    }

    const unsigned char *bytes = (const unsigned char *)text;
    size_t offset = 0u;
    while (offset < length) {
        const size_t width = fin_voice_utf8_codepoint_width(bytes, length, offset);
        if (width == 0u) {
            return false;
        }
        offset += width;
    }
    return true;
}

static size_t fin_voice_saturating_add_size(size_t left, size_t right)
{
    if (right > SIZE_MAX - left) {
        return SIZE_MAX;
    }
    return left + right;
}

static void fin_voice_text_builder_record_overflow(
    FinVoiceTextBuilder *builder,
    size_t dropped_bytes)
{
    builder->overflow_count = fin_voice_saturating_add_size(
        builder->overflow_count, 1u);
    builder->dropped_bytes = fin_voice_saturating_add_size(
        builder->dropped_bytes, dropped_bytes);
}

FinVoiceTextBuilderStatus fin_voice_text_builder_init(
    FinVoiceTextBuilder *builder,
    char *storage,
    size_t capacity)
{
    if (builder == NULL) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }
    if (storage == NULL || capacity == 0u) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION;
    }

    builder->buffer = storage;
    builder->capacity = capacity;
    builder->length = 0u;
    builder->overflow_count = 0u;
    builder->dropped_bytes = 0u;
    storage[0] = '\0';
    return FIN_VOICE_TEXT_BUILDER_OK;
}

FinVoiceTextBuilderStatus fin_voice_text_builder_append(
    FinVoiceTextBuilder *builder,
    const char *text,
    size_t length)
{
    if (!fin_voice_text_builder_is_valid(builder)) {
        return builder == NULL
            ? FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION;
    }
    if (length > 0u && text == NULL) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }
    if (length == 0u) {
        return FIN_VOICE_TEXT_BUILDER_OK;
    }
    if (fin_voice_memory_ranges_overlap(text, length, builder->buffer, builder->capacity)) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }

    /* Validate the entire chunk before modifying state: malformed input is atomic. */
    if (!fin_voice_utf8_is_valid(text, length)) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8;
    }

    const unsigned char *bytes = (const unsigned char *)text;
    const size_t available = builder->capacity - 1u - builder->length;
    size_t written = 0u;
    size_t offset = 0u;

    while (offset < length) {
        const size_t width = fin_voice_utf8_codepoint_width(bytes, length, offset);
        /* The full chunk was validated; zero here indicates corrupted assumptions. */
        if (width == 0u) {
            return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8;
        }
        if (width > available - written) {
            const size_t dropped = length - offset;
            fin_voice_text_builder_record_overflow(builder, dropped);
            builder->length += written;
            builder->buffer[builder->length] = '\0';
            return FIN_VOICE_TEXT_BUILDER_OVERFLOW;
        }

        memcpy(builder->buffer + builder->length + written, text + offset, width);
        written += width;
        offset += width;
    }

    builder->length += written;
    builder->buffer[builder->length] = '\0';
    return FIN_VOICE_TEXT_BUILDER_OK;
}

FinVoiceTextBuilderStatus fin_voice_text_builder_append_cstr(
    FinVoiceTextBuilder *builder,
    const char *text)
{
    if (text == NULL) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }
    return fin_voice_text_builder_append(builder, text, strlen(text));
}

FinVoiceTextBuilderStatus fin_voice_text_builder_view(
    const FinVoiceTextBuilder *builder,
    const char **out_text,
    size_t *out_length)
{
    if (out_text == NULL || out_length == NULL) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }
    if (!fin_voice_text_builder_is_valid(builder)) {
        return builder == NULL
            ? FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION;
    }

    *out_text = builder->buffer;
    *out_length = builder->length;
    return FIN_VOICE_TEXT_BUILDER_OK;
}

FinVoiceTextBuilderStatus fin_voice_text_builder_flush(
    FinVoiceTextBuilder *builder,
    char *destination,
    size_t destination_capacity,
    size_t *out_length)
{
    if (out_length == NULL) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }
    *out_length = 0u;

    if (!fin_voice_text_builder_is_valid(builder)) {
        return builder == NULL
            ? FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION;
    }
    if (destination == NULL || destination_capacity == 0u ||
        fin_voice_memory_ranges_overlap(
            destination, destination_capacity, builder->buffer, builder->capacity)) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT;
    }

    *out_length = builder->length;
    if (destination_capacity < builder->length + 1u) {
        return FIN_VOICE_TEXT_BUILDER_ERROR_OUTPUT_TOO_SMALL;
    }

    /* Caller contract: destination range does not overlap builder storage. */
    memcpy(destination, builder->buffer, builder->length + 1u);
    builder->length = 0u;
    builder->buffer[0] = '\0';
    return FIN_VOICE_TEXT_BUILDER_OK;
}

FinVoiceTextBuilderStatus fin_voice_text_builder_reset(
    FinVoiceTextBuilder *builder)
{
    if (!fin_voice_text_builder_is_valid(builder)) {
        return builder == NULL
            ? FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION;
    }

    builder->length = 0u;
    builder->overflow_count = 0u;
    builder->dropped_bytes = 0u;
    builder->buffer[0] = '\0';
    return FIN_VOICE_TEXT_BUILDER_OK;
}

size_t fin_voice_text_builder_capacity(const FinVoiceTextBuilder *builder)
{
    return fin_voice_text_builder_is_valid(builder) ? builder->capacity : 0u;
}

size_t fin_voice_text_builder_length(const FinVoiceTextBuilder *builder)
{
    return fin_voice_text_builder_is_valid(builder) ? builder->length : 0u;
}

size_t fin_voice_text_builder_overflow_count(const FinVoiceTextBuilder *builder)
{
    return fin_voice_text_builder_is_valid(builder) ? builder->overflow_count : 0u;
}

size_t fin_voice_text_builder_dropped_bytes(const FinVoiceTextBuilder *builder)
{
    return fin_voice_text_builder_is_valid(builder) ? builder->dropped_bytes : 0u;
}

const char *fin_voice_text_builder_status_name(FinVoiceTextBuilderStatus status)
{
    switch (status) {
    case FIN_VOICE_TEXT_BUILDER_OK:
        return "ok";
    case FIN_VOICE_TEXT_BUILDER_OVERFLOW:
        return "overflow";
    case FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION:
        return "invalid_configuration";
    case FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8:
        return "invalid_utf8";
    case FIN_VOICE_TEXT_BUILDER_ERROR_OUTPUT_TOO_SMALL:
        return "output_too_small";
    default:
        return "unknown_status";
    }
}

uint32_t fin_voice_text_builder_abi_version(void)
{
    return (FIN_VOICE_TEXT_BUILDER_ABI_MAJOR << 16u) |
           FIN_VOICE_TEXT_BUILDER_ABI_MINOR;
}
