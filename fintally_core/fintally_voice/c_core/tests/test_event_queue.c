#include "event_queue.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

static size_t checks;
static size_t failures;

#define CHECK(expression)                                                       \
    do {                                                                        \
        ++checks;                                                               \
        if (!(expression)) {                                                    \
            ++failures;                                                         \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                          #expression);                                        \
        }                                                                       \
    } while (0)

static FinVoiceEvent make_simple_event(
    FinVoiceEventType type,
    uint32_t sequence_id,
    uint64_t timestamp_ns,
    uint64_t sample_position)
{
    FinVoiceEvent event = {0};
    event.type = (uint16_t)type;
    event.sequence_id = sequence_id;
    event.timestamp_ns = timestamp_ns;
    event.sample_position = sample_position;
    return event;
}

static FinVoiceEvent make_feature_event(uint32_t sequence_id)
{
    FinVoiceEvent event = make_simple_event(
        FIN_VOICE_EVENT_FEATURES_READY, sequence_id, 123456u, 320u);
    event.payload.features.dimension = FIN_VOICE_EVENT_FEATURE_DIMENSION;
    for (size_t index = 0u; index < FIN_VOICE_EVENT_FEATURE_DIMENSION; ++index) {
        event.payload.features.values[index] = (float)index / 32.0f;
    }
    return event;
}

static FinVoiceEvent make_quantization_event(uint32_t sequence_id)
{
    FinVoiceEvent event = make_simple_event(
        FIN_VOICE_EVENT_QUANTIZATION_RESULT, sequence_id, 123457u, 480u);
    event.payload.quantization.best_index = 4u;
    event.payload.quantization.second_best_index = 9u;
    event.payload.quantization.best_distance_squared = 1.25f;
    event.payload.quantization.second_distance_squared = 2.5f;
    event.payload.quantization.relative_gap = 0.5f;
    event.payload.quantization.language = FIN_VOICE_EVENT_LANGUAGE_EN;
    return event;
}

static FinVoiceEvent make_text_event(uint32_t sequence_id, const char *text)
{
    FinVoiceEvent event = make_simple_event(
        FIN_VOICE_EVENT_TEXT_READY, sequence_id, 123458u, 640u);
    const size_t length = strlen(text);
    if (length < FIN_VOICE_EVENT_TEXT_CAPACITY) {
        event.payload.text.length = (uint16_t)length;
        memcpy(event.payload.text.utf8, text, length + 1u);
    }
    return event;
}

static void test_init_and_basic_contract(void)
{
    FinVoiceEventQueue queue;
    FinVoiceEvent storage[2];

    CHECK(fin_voice_event_queue_init(NULL, storage, 2u) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_event_queue_init(&queue, NULL, 2u) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_event_queue_init(&queue, storage, 0u) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_CONFIGURATION);
    CHECK(fin_voice_event_queue_init(
              &queue, storage, SIZE_MAX / sizeof(FinVoiceEvent) + 1u) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_SIZE_OVERFLOW);

    CHECK(fin_voice_event_queue_init(&queue, storage, 2u) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_capacity(&queue) == 2u);
    CHECK(fin_voice_event_queue_dropped_count(&queue) == 0u);
    CHECK(fin_voice_event_queue_capacity(NULL) == 0u);
    CHECK(fin_voice_event_queue_dropped_count(NULL) == 0u);
    CHECK(fin_voice_event_abi_version() ==
          ((FIN_VOICE_EVENT_ABI_MAJOR << 16u) | FIN_VOICE_EVENT_ABI_MINOR));
    CHECK(strcmp(fin_voice_event_queue_status_name(FIN_VOICE_EVENT_QUEUE_OK),
                 "ok") == 0);
    CHECK(strcmp(fin_voice_event_queue_status_name(
                     FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT),
                 "invalid_event") == 0);
    CHECK(strcmp(fin_voice_event_queue_status_name(
                     FIN_VOICE_EVENT_QUEUE_ERROR_FULL),
                 "full") == 0);
    CHECK(strcmp(fin_voice_event_queue_status_name(
                     (FinVoiceEventQueueStatus)999),
                 "unknown_status") == 0);
}

static void test_event_types_and_payload_validation(void)
{
    FinVoiceEvent event = make_simple_event(
        FIN_VOICE_EVENT_SPEECH_STARTED, 1u, 90u, 320u);
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.type = FIN_VOICE_EVENT_SPEECH_ENDED;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.type = FIN_VOICE_EVENT_ERROR;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.type = FIN_VOICE_EVENT_STATUS;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.type = FIN_VOICE_EVENT_SHUTDOWN;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.type = FIN_VOICE_EVENT_RESET;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_validate(NULL) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);

    event.type = UINT16_MAX;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_simple_event(FIN_VOICE_EVENT_STATUS, 2u, 0u, 0u);
    event.flags = UINT16_C(0x8000);
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event.flags = FIN_VOICE_EVENT_FLAG_FINAL;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);

    event = make_feature_event(3u);
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.payload.features.dimension = 31u;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_feature_event(4u);
    event.payload.features.values[7] = NAN;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_feature_event(5u);
    event.payload.features.values[7] = INFINITY;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_feature_event(6u);
    event.payload.features.reserved = 1u;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);

    event = make_quantization_event(7u);
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.payload.quantization.best_distance_squared = -1.0f;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_quantization_event(8u);
    event.payload.quantization.second_distance_squared = 0.5f;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_quantization_event(9u);
    event.payload.quantization.relative_gap = 1.1f;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_quantization_event(10u);
    event.payload.quantization.language = UINT8_MAX;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_quantization_event(11u);
    event.payload.quantization.second_best_index = FIN_VOICE_EVENT_NO_CENTROID;
    event.payload.quantization.second_distance_squared = INFINITY;
    event.payload.quantization.relative_gap = 0.0f;
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event.payload.quantization.second_distance_squared = 0.0f;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);

    event = make_text_event(12u, "recognized words");
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event = make_text_event(13u, "h\xC3\xA9llo");
    CHECK(fin_voice_event_validate(&event) == FIN_VOICE_EVENT_QUEUE_OK);
    event = make_text_event(14u, "x");
    event.payload.text.utf8[event.payload.text.length] = 'x';
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_text_event(15u, "x");
    event.payload.text.utf8[0] = '\0'; /* NUL inside the declared byte range. */
    event.payload.text.length = 2u;
    event.payload.text.utf8[2] = '\0';
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_text_event(16u, "x");
    event.payload.text.utf8[0] = (char)0xC0; /* Illegal UTF-8 lead byte. */
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_text_event(17u, "x");
    event.payload.text.length = FIN_VOICE_EVENT_TEXT_CAPACITY;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_text_event(18u, "x");
    event.payload.text.reserved = 1u;
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    event = make_simple_event(FIN_VOICE_EVENT_TEXT_READY, 19u, 0u, 0u);
    CHECK(fin_voice_event_validate(&event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
}

static void test_order_capacity_full_and_drop_count(void)
{
    FinVoiceEventQueue queue;
    FinVoiceEvent storage[3];
    FinVoiceEvent output;
    const FinVoiceEvent first = make_simple_event(
        FIN_VOICE_EVENT_SPEECH_STARTED, 100u, 1000u, 0u);
    const FinVoiceEvent second = make_feature_event(101u);
    const FinVoiceEvent third = make_quantization_event(102u);
    const FinVoiceEvent overflow = make_simple_event(
        FIN_VOICE_EVENT_STATUS, 103u, 1003u, 960u);

    CHECK(fin_voice_event_queue_init(&queue, storage, 3u) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_pop(&queue, &output) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY);
    CHECK(fin_voice_event_queue_push(&queue, &first) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_push(&queue, &second) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_push(&queue, &third) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_push(&queue, &overflow) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_FULL);
    CHECK(fin_voice_event_queue_dropped_count(&queue) == 1u);

    FinVoiceEvent invalid = make_feature_event(104u);
    invalid.payload.features.values[0] = NAN;
    CHECK(fin_voice_event_queue_push(&queue, &invalid) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT);
    CHECK(fin_voice_event_queue_dropped_count(&queue) == 1u);

    CHECK(fin_voice_event_queue_pop(&queue, &output) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(output.type == FIN_VOICE_EVENT_SPEECH_STARTED);
    CHECK(output.sequence_id == 100u);
    CHECK(output.timestamp_ns == 1000u);
    CHECK(output.sample_position == 0u);

    CHECK(fin_voice_event_queue_pop(&queue, &output) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(output.type == FIN_VOICE_EVENT_FEATURES_READY);
    CHECK(output.sequence_id == 101u);
    CHECK(output.payload.features.dimension == FIN_VOICE_EVENT_FEATURE_DIMENSION);
    CHECK(output.payload.features.values[31] == 31.0f / 32.0f);

    CHECK(fin_voice_event_queue_pop(&queue, &output) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(output.type == FIN_VOICE_EVENT_QUANTIZATION_RESULT);
    CHECK(output.sequence_id == 102u);
    CHECK(output.payload.quantization.best_index == 4u);
    CHECK(fin_voice_event_queue_pop(&queue, &output) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY);
    CHECK(fin_voice_event_queue_push(NULL, &first) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_event_queue_push(&queue, NULL) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_event_queue_pop(NULL, &output) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_event_queue_pop(&queue, NULL) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
}

static void test_reset_discards_events_and_resets_counter(void)
{
    FinVoiceEventQueue queue;
    FinVoiceEvent storage[1];
    FinVoiceEvent output;
    const FinVoiceEvent event = make_simple_event(
        FIN_VOICE_EVENT_STATUS, 200u, 5000u, 1600u);

    CHECK(fin_voice_event_queue_init(&queue, storage, 1u) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_push(&queue, &event) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_push(&queue, &event) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_FULL);
    CHECK(fin_voice_event_queue_dropped_count(&queue) == 1u);
    CHECK(fin_voice_event_queue_reset(&queue) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_dropped_count(&queue) == 0u);
    CHECK(fin_voice_event_queue_pop(&queue, &output) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY);

    FinVoiceEvent after_reset = make_simple_event(
        FIN_VOICE_EVENT_SHUTDOWN, 201u, 6000u, 1920u);
    CHECK(fin_voice_event_queue_push(&queue, &after_reset) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_event_queue_pop(&queue, &output) == FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(output.type == FIN_VOICE_EVENT_SHUTDOWN);
    CHECK(output.sequence_id == 201u);
    CHECK(fin_voice_event_queue_reset(NULL) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT);
}

#define CONCURRENT_EVENT_COUNT ((uint32_t)100000u)

typedef struct ConcurrentContext {
    FinVoiceEventQueue *queue;
    atomic_bool failed;
} ConcurrentContext;

static int event_producer_thread(void *argument)
{
    ConcurrentContext *context = (ConcurrentContext *)argument;

    for (uint32_t index = 0u; index < CONCURRENT_EVENT_COUNT; ++index) {
        FinVoiceEvent event = make_simple_event(
            FIN_VOICE_EVENT_STATUS, index, UINT64_C(5000000) + index,
            (uint64_t)index * 160u);
        event.payload.status.code = index;
        event.payload.status.detail = -(int32_t)(index & UINT32_C(0x7FFF));

        for (;;) {
            const FinVoiceEventQueueStatus status =
                fin_voice_event_queue_push(context->queue, &event);
            if (status == FIN_VOICE_EVENT_QUEUE_OK) {
                break;
            }
            if (status != FIN_VOICE_EVENT_QUEUE_ERROR_FULL) {
                atomic_store_explicit(&context->failed, true, memory_order_release);
                return thrd_error;
            }
            thrd_yield();
        }
    }
    return thrd_success;
}

static void test_concurrent_single_producer_single_consumer_order(void)
{
    FinVoiceEventQueue queue;
    FinVoiceEvent storage[32];
    ConcurrentContext context;
    thrd_t producer;
    int thread_result = thrd_error;

    CHECK(fin_voice_event_queue_init(&queue, storage, 32u) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    context.queue = &queue;
    atomic_init(&context.failed, false);
    const int create_status = thrd_create(&producer, event_producer_thread, &context);
    CHECK(create_status == thrd_success);
    if (create_status != thrd_success) {
        return;
    }

    for (uint32_t expected = 0u; expected < CONCURRENT_EVENT_COUNT; ++expected) {
        FinVoiceEvent output;
        FinVoiceEventQueueStatus status;
        do {
            status = fin_voice_event_queue_pop(&queue, &output);
            if (status == FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY) {
                if (atomic_load_explicit(&context.failed, memory_order_acquire)) {
                    break;
                }
                thrd_yield();
            }
        } while (status == FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY);

        if (status != FIN_VOICE_EVENT_QUEUE_OK &&
            atomic_load_explicit(&context.failed, memory_order_acquire)) {
            break;
        }
        CHECK(status == FIN_VOICE_EVENT_QUEUE_OK);
        if (status != FIN_VOICE_EVENT_QUEUE_OK) {
            atomic_store_explicit(&context.failed, true, memory_order_release);
            break;
        }
        CHECK(output.type == FIN_VOICE_EVENT_STATUS);
        CHECK(output.sequence_id == expected);
        CHECK(output.timestamp_ns == UINT64_C(5000000) + expected);
        CHECK(output.sample_position == (uint64_t)expected * 160u);
        CHECK(output.payload.status.code == expected);
        CHECK(output.payload.status.detail == -(int32_t)(expected & UINT32_C(0x7FFF)));
    }

    CHECK(thrd_join(producer, &thread_result) == thrd_success);
    CHECK(thread_result == thrd_success);
    CHECK(!atomic_load_explicit(&context.failed, memory_order_acquire));
    /* Overflow/drop behavior is tested deterministically by the single-threaded case. */
    CHECK(fin_voice_event_queue_pop(&queue, &(FinVoiceEvent){0}) ==
          FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY);
}

int main(void)
{
    test_init_and_basic_contract();
    test_event_types_and_payload_validation();
    test_order_capacity_full_and_drop_count();
    test_reset_discards_events_and_resets_counter();
    test_concurrent_single_producer_single_consumer_order();

    if (failures != 0u) {
        (void)fprintf(stderr, "FinTally Voice event queue: %zu/%zu checks failed\n",
                      failures, checks);
        return 1;
    }
    (void)printf("FinTally Voice event queue: PASS (%zu checks; 100000 concurrent events)\n",
                  checks);
    return 0;
}
