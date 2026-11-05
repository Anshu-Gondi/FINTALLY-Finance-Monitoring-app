#include "text_builder.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t g_checks = 0u;
static size_t g_failures = 0u;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_STATUS(actual, expected) CHECK((actual) == (expected))

static void test_initialization_and_arguments(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[8];

    CHECK_STATUS(fin_voice_text_builder_init(NULL, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_init(&builder, NULL, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION);
    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, 0u),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_CONFIGURATION);

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(storage[0] == '\0');
    CHECK(fin_voice_text_builder_capacity(&builder) == sizeof(storage));
    CHECK(fin_voice_text_builder_length(&builder) == 0u);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 0u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 0u);

    CHECK_STATUS(fin_voice_text_builder_append(&builder, NULL, 0u),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append(&builder, NULL, 1u),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, NULL),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_view(&builder, NULL, &(size_t){0}),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_view(&builder, &(const char *){0}, NULL),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
}

static void test_chunked_unicode_and_view(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[64];
    const char *view = NULL;
    size_t length = 0u;

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "Hello "),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "\xE4\xB8\x96\xE7\x95\x8C"),
        FIN_VOICE_TEXT_BUILDER_OK); /* 世界 */
    CHECK_STATUS(fin_voice_text_builder_append(&builder, "!", 1u),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, " \xF0\x9F\x8C\x8D"),
        FIN_VOICE_TEXT_BUILDER_OK); /* 🌍 */

    CHECK_STATUS(fin_voice_text_builder_view(&builder, &view, &length),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(view == storage);
    CHECK(length == strlen("Hello \xE4\xB8\x96\xE7\x95\x8C! \xF0\x9F\x8C\x8D"));
    CHECK(strcmp(view, "Hello \xE4\xB8\x96\xE7\x95\x8C! \xF0\x9F\x8C\x8D") == 0);
    CHECK(storage[length] == '\0');
    CHECK(fin_voice_text_builder_length(&builder) == length);
}

static void test_capacity_and_utf8_safe_overflow(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[5]; /* Four payload bytes plus NUL. */

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append(&builder, "A", 1u),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append(&builder, "\xE2\x82\xAC" "B", 4u),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW); /* € fits; B does not. */
    CHECK(strcmp(storage, "A\xE2\x82\xAC") == 0);
    CHECK(fin_voice_text_builder_length(&builder) == 4u);
    CHECK(storage[4] == '\0');
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 1u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 1u);

    CHECK_STATUS(fin_voice_text_builder_append(&builder, "Z", 1u),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(strcmp(storage, "A\xE2\x82\xAC") == 0);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 2u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 2u);

    /* If the next code point cannot fit, none of its bytes are written. */
    char tiny[4];
    CHECK_STATUS(fin_voice_text_builder_init(&builder, tiny, sizeof(tiny)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "ab"),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append(&builder, "\xE2\x82\xAC", 3u),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(strcmp(tiny, "ab") == 0);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 3u);

    /* One-byte storage is a valid empty builder but cannot store a character. */
    char empty_only[1] = {'x'};
    CHECK_STATUS(fin_voice_text_builder_init(&builder, empty_only, sizeof(empty_only)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(empty_only[0] == '\0');
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "x"),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(empty_only[0] == '\0');
}

static void test_invalid_utf8_is_atomic(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[64];
    const char *invalid_inputs[] = {
        "\xC0\xAF",             /* Overlong encoding. */
        "\xE2\x82",             /* Incomplete three-byte sequence. */
        "\x80",                 /* Stray continuation byte. */
        "\xED\xA0\x80",         /* UTF-16 surrogate. */
        "\xF4\x90\x80\x80",     /* Above U+10FFFF. */
        "\xE2X\xAC",             /* Invalid continuation. */
        "\xF0\x9F\x8C",         /* Incomplete four-byte sequence. */
    };
    const size_t invalid_lengths[] = {2u, 2u, 1u, 3u, 4u, 3u, 3u};
    const char embedded_nul[] = {'a', '\0', 'b'};

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "stable"),
        FIN_VOICE_TEXT_BUILDER_OK);

    for (size_t index = 0u;
         index < sizeof(invalid_inputs) / sizeof(invalid_inputs[0]);
         ++index) {
        CHECK_STATUS(fin_voice_text_builder_append(
            &builder, invalid_inputs[index], invalid_lengths[index]),
            FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8);
        CHECK(strcmp(storage, "stable") == 0);
        CHECK(fin_voice_text_builder_overflow_count(&builder) == 0u);
        CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 0u);
    }

    CHECK_STATUS(fin_voice_text_builder_append(&builder, embedded_nul, sizeof(embedded_nul)),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8);
    CHECK(strcmp(storage, "stable") == 0);

    /* Valid one-, two-, three-, and four-byte code points. */
    CHECK_STATUS(fin_voice_text_builder_reset(&builder), FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(
        &builder, "A\xC2\xA2\xE2\x82\xAC\xF0\x9F\x8C\x8D"),
        FIN_VOICE_TEXT_BUILDER_OK);
}

static void test_flush_and_reset(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[32];
    char output[32];
    size_t output_length = 999u;

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "flush me"),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, storage),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);

    char too_small[3] = {'x', 'x', 'x'};
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, too_small, sizeof(too_small), &output_length),
        FIN_VOICE_TEXT_BUILDER_ERROR_OUTPUT_TOO_SMALL);
    CHECK(output_length == strlen("flush me"));
    CHECK(strcmp(storage, "flush me") == 0);
    CHECK(too_small[0] == 'x');

    output_length = 0u;
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, output, sizeof(output), &output_length), FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(strcmp(output, "flush me") == 0);
    CHECK(output_length == strlen("flush me"));
    CHECK(storage[0] == '\0');
    CHECK(fin_voice_text_builder_length(&builder) == 0u);

    /* Flush preserves overflow telemetry; explicit reset clears telemetry too. */
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder,
        "this text is deliberately longer than the buffer"),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 1u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) > 0u);
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, output, sizeof(output), &output_length), FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 1u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) > 0u);

    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "again"),
        FIN_VOICE_TEXT_BUILDER_OK);
    CHECK_STATUS(fin_voice_text_builder_reset(&builder), FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(storage[0] == '\0');
    CHECK(fin_voice_text_builder_length(&builder) == 0u);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == 0u);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == 0u);

    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, output, sizeof(output), &output_length), FIN_VOICE_TEXT_BUILDER_OK);
    CHECK(output_length == 0u);
    CHECK(output[0] == '\0');
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, storage, sizeof(storage), &output_length),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, storage + 1, sizeof(storage) - 1u, &output_length),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(fin_voice_text_builder_flush(
        &builder, output, sizeof(output), NULL),
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_ARGUMENT);
}

static void test_saturating_telemetry_and_status_abi(void)
{
    FinVoiceTextBuilder builder = {0};
    char storage[2];

    CHECK_STATUS(fin_voice_text_builder_init(&builder, storage, sizeof(storage)),
        FIN_VOICE_TEXT_BUILDER_OK);
    /* Saturation is tested by placing counters at the documented boundary. */
    builder.overflow_count = SIZE_MAX - 1u;
    builder.dropped_bytes = SIZE_MAX - 1u;
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "abc"),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == SIZE_MAX);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == SIZE_MAX);
    CHECK_STATUS(fin_voice_text_builder_append_cstr(&builder, "z"),
        FIN_VOICE_TEXT_BUILDER_OVERFLOW);
    CHECK(fin_voice_text_builder_overflow_count(&builder) == SIZE_MAX);
    CHECK(fin_voice_text_builder_dropped_bytes(&builder) == SIZE_MAX);

    CHECK(strcmp(fin_voice_text_builder_status_name(FIN_VOICE_TEXT_BUILDER_OK), "ok") == 0);
    CHECK(strcmp(fin_voice_text_builder_status_name(FIN_VOICE_TEXT_BUILDER_OVERFLOW), "overflow") == 0);
    CHECK(strcmp(fin_voice_text_builder_status_name(
        FIN_VOICE_TEXT_BUILDER_ERROR_INVALID_UTF8), "invalid_utf8") == 0);
    CHECK(strcmp(fin_voice_text_builder_status_name((FinVoiceTextBuilderStatus)99),
        "unknown_status") == 0);
    CHECK(fin_voice_text_builder_abi_version() == 0x00010000u);
    CHECK(fin_voice_text_builder_capacity(NULL) == 0u);
    CHECK(fin_voice_text_builder_length(NULL) == 0u);
    CHECK(fin_voice_text_builder_overflow_count(NULL) == 0u);
    CHECK(fin_voice_text_builder_dropped_bytes(NULL) == 0u);
}

int main(void)
{
    (void)puts("FinTally Voice bounded text-builder tests");

    test_initialization_and_arguments();
    test_chunked_unicode_and_view();
    test_capacity_and_utf8_safe_overflow();
    test_invalid_utf8_is_atomic();
    test_flush_and_reset();
    test_saturating_telemetry_and_status_abi();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "FAIL: %zu of %zu checks failed\n", g_failures, g_checks);
        return 1;
    }

    (void)printf("PASS: %zu checks\n", g_checks);
    return 0;
}
