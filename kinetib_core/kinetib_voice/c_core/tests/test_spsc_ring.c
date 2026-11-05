#include "spsc_ring.h"

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
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                          #expression);                                        \
        }                                                                       \
    } while (0)

static void test_invalid_arguments(void)
{
    FinSpscRing ring;
    unsigned char storage[16];
    uint32_t input = 7u;
    uint32_t output = 0u;

    CHECK(fin_spsc_ring_init(NULL, storage, 2u, sizeof(input)) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
    CHECK(fin_spsc_ring_init(&ring, NULL, 2u, sizeof(input)) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
    CHECK(fin_spsc_ring_init(&ring, storage, 0u, sizeof(input)) ==
          FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION);
    CHECK(fin_spsc_ring_init(&ring, storage, 2u, 0u) ==
          FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION);
    CHECK(fin_spsc_ring_init(&ring, storage, SIZE_MAX / 2u + 1u, 2u) ==
          FIN_SPSC_RING_ERROR_SIZE_OVERFLOW);

    CHECK(fin_spsc_ring_init(&ring, storage, 2u, sizeof(input)) ==
          FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(NULL, &input) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
    CHECK(fin_spsc_ring_push(&ring, NULL) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
    CHECK(fin_spsc_ring_pop(NULL, &output) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
    CHECK(fin_spsc_ring_pop(&ring, NULL) ==
          FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);

    CHECK(fin_spsc_ring_capacity(NULL) == 0u);
    CHECK(fin_spsc_ring_element_size(NULL) == 0u);
}

static void test_capacity_and_empty_full_transitions(void)
{
    FinSpscRing ring;
    uint32_t storage[3] = {0u, 0u, 0u};
    uint32_t output = 0u;
    const uint32_t first = 101u;
    const uint32_t second = 202u;
    const uint32_t third = 303u;
    const uint32_t fourth = 404u;

    CHECK(fin_spsc_ring_init(&ring, storage, 3u, sizeof(uint32_t)) ==
          FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_capacity(&ring) == 3u);
    CHECK(fin_spsc_ring_element_size(&ring) == sizeof(uint32_t));
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);

    CHECK(fin_spsc_ring_push(&ring, &first) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &second) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &third) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &fourth) == FIN_SPSC_RING_ERROR_FULL);

    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == first);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == second);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == third);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);
}

static void test_physical_wraparound_preserves_fifo_order(void)
{
    FinSpscRing ring;
    uint8_t storage[3] = {0u, 0u, 0u};
    uint8_t output = 0u;
    const uint8_t one = 1u;
    const uint8_t two = 2u;
    const uint8_t three = 3u;
    const uint8_t four = 4u;
    const uint8_t five = 5u;

    CHECK(fin_spsc_ring_init(&ring, storage, 3u, sizeof(uint8_t)) ==
          FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &one) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &two) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == one);

    CHECK(fin_spsc_ring_push(&ring, &three) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &four) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &five) == FIN_SPSC_RING_ERROR_FULL);

    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == two);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == three);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == four);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);
}

static void test_single_slot_capacity(void)
{
    FinSpscRing ring;
    uint64_t storage = 0u;
    uint64_t output = 0u;
    const uint64_t value = UINT64_C(0xD00DFEED12345678);
    const uint64_t another = 99u;

    CHECK(fin_spsc_ring_init(&ring, &storage, 1u, sizeof(storage)) ==
          FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &value) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &another) == FIN_SPSC_RING_ERROR_FULL);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == value);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);
    CHECK(fin_spsc_ring_push(&ring, &another) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == another);
}

static void test_reset_discards_queued_elements(void)
{
    FinSpscRing ring;
    uint32_t storage[4] = {0u, 0u, 0u, 0u};
    uint32_t output = 0u;
    const uint32_t first = 11u;
    const uint32_t second = 22u;
    const uint32_t after_reset = 33u;

    CHECK(fin_spsc_ring_init(&ring, storage, 4u, sizeof(uint32_t)) ==
          FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &first) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &second) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_reset(&ring) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);
    CHECK(fin_spsc_ring_push(&ring, &after_reset) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
    CHECK(output == after_reset);
    CHECK(fin_spsc_ring_reset(NULL) == FIN_SPSC_RING_ERROR_INVALID_ARGUMENT);
}

static void test_sequence_counter_wraparound(void)
{
    FinSpscRing ring;
    uint8_t storage[3] = {0u, 0u, 0u};
    uint8_t output = 0u;
    const uint8_t values[] = {17u, 29u, 41u};

    CHECK(fin_spsc_ring_init(&ring, storage, 3u, sizeof(uint8_t)) ==
          FIN_SPSC_RING_OK);

    /*
     * White-box boundary test: counters are seeded near SIZE_MAX while the
     * ring is quiescent. Physical slots are reset to match an empty queue.
     * Applications must never manipulate these implementation fields.
     */
    ring.producer_slot = 0u;
    ring.consumer_slot = 0u;
    atomic_store_explicit(
        &ring.published_write_count, SIZE_MAX - 1u, memory_order_relaxed);
    atomic_store_explicit(
        &ring.published_read_count, SIZE_MAX - 1u, memory_order_relaxed);

    CHECK(fin_spsc_ring_push(&ring, &values[0]) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &values[1]) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &values[2]) == FIN_SPSC_RING_OK);
    CHECK(fin_spsc_ring_push(&ring, &values[0]) == FIN_SPSC_RING_ERROR_FULL);

    for (size_t index = 0u; index < sizeof(values) / sizeof(values[0]); ++index) {
        CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_OK);
        CHECK(output == values[index]);
    }
    CHECK(fin_spsc_ring_pop(&ring, &output) == FIN_SPSC_RING_ERROR_EMPTY);
}

static void test_status_names(void)
{
    CHECK(strcmp(fin_spsc_ring_status_name(FIN_SPSC_RING_OK), "ok") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name(
                     FIN_SPSC_RING_ERROR_INVALID_ARGUMENT),
                 "invalid_argument") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name(
                     FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION),
                 "invalid_configuration") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name(
                     FIN_SPSC_RING_ERROR_SIZE_OVERFLOW),
                 "size_overflow") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name(FIN_SPSC_RING_ERROR_FULL),
                 "full") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name(FIN_SPSC_RING_ERROR_EMPTY),
                 "empty") == 0);
    CHECK(strcmp(fin_spsc_ring_status_name((FinSpscRingStatus)999),
                 "unknown_status") == 0);
}

typedef struct SpscConcurrentMessage {
    uint64_t sequence;
    uint64_t inverse_sequence;
} SpscConcurrentMessage;

typedef struct SpscConcurrentContext {
    FinSpscRing ring;
    SpscConcurrentMessage storage[64];
    size_t item_count;
    size_t consumed_count;
    atomic_bool failed;
} SpscConcurrentContext;

#define SPSC_CONCURRENT_ITEM_COUNT ((size_t)250000u)
#define SPSC_CONCURRENT_RETRY_LIMIT ((size_t)10000000u)

static int spsc_concurrent_fail(SpscConcurrentContext *context)
{
    atomic_store_explicit(&context->failed, true, memory_order_release);
    return thrd_error;
}

static int spsc_producer_thread(void *argument)
{
    SpscConcurrentContext *context = (SpscConcurrentContext *)argument;

    for (size_t index = 0u; index < context->item_count; ++index) {
        if (atomic_load_explicit(&context->failed, memory_order_acquire)) {
            return thrd_error;
        }

        const uint64_t sequence = (uint64_t)index;
        const SpscConcurrentMessage message = {
            .sequence = sequence,
            .inverse_sequence = ~sequence
        };
        size_t attempts = 0u;
        for (;;) {
            if (atomic_load_explicit(&context->failed, memory_order_acquire)) {
                return thrd_error;
            }
            const FinSpscRingStatus status =
                fin_spsc_ring_push(&context->ring, &message);
            if (status == FIN_SPSC_RING_OK) {
                break;
            }
            if (status != FIN_SPSC_RING_ERROR_FULL) {
                return spsc_concurrent_fail(context);
            }
            if (++attempts >= SPSC_CONCURRENT_RETRY_LIMIT) {
                return spsc_concurrent_fail(context);
            }
            thrd_yield();
        }
    }

    return thrd_success;
}

static int spsc_consumer_thread(void *argument)
{
    SpscConcurrentContext *context = (SpscConcurrentContext *)argument;

    for (size_t index = 0u; index < context->item_count; ++index) {
        SpscConcurrentMessage message = {0u, 0u};
        size_t attempts = 0u;
        for (;;) {
            if (atomic_load_explicit(&context->failed, memory_order_acquire)) {
                return thrd_error;
            }
            const FinSpscRingStatus status =
                fin_spsc_ring_pop(&context->ring, &message);
            if (status == FIN_SPSC_RING_OK) {
                break;
            }
            if (status != FIN_SPSC_RING_ERROR_EMPTY) {
                return spsc_concurrent_fail(context);
            }
            if (++attempts >= SPSC_CONCURRENT_RETRY_LIMIT) {
                return spsc_concurrent_fail(context);
            }
            thrd_yield();
        }

        const uint64_t expected = (uint64_t)index;
        if (message.sequence != expected ||
            message.inverse_sequence != ~expected) {
            return spsc_concurrent_fail(context);
        }
        context->consumed_count = index + 1u;
    }

    return thrd_success;
}

static void test_one_producer_one_consumer_concurrently(void)
{
    SpscConcurrentContext context;
    thrd_t producer;
    thrd_t consumer;
    int producer_result = thrd_error;
    int consumer_result = thrd_error;

    memset(&context, 0, sizeof(context));
    context.item_count = SPSC_CONCURRENT_ITEM_COUNT;
    atomic_init(&context.failed, false);
    CHECK(fin_spsc_ring_init(
              &context.ring,
              context.storage,
              sizeof(context.storage) / sizeof(context.storage[0]),
              sizeof(context.storage[0])) == FIN_SPSC_RING_OK);

    /* Start consumer first; shared failure flag prevents a stranded waiter. */
    if (thrd_create(&consumer, spsc_consumer_thread, &context) != thrd_success) {
        CHECK(false);
        return;
    }
    if (thrd_create(&producer, spsc_producer_thread, &context) != thrd_success) {
        atomic_store_explicit(&context.failed, true, memory_order_release);
        (void)thrd_join(consumer, &consumer_result);
        CHECK(false);
        return;
    }

    CHECK(thrd_join(producer, &producer_result) == thrd_success);
    CHECK(thrd_join(consumer, &consumer_result) == thrd_success);
    CHECK(producer_result == thrd_success);
    CHECK(consumer_result == thrd_success);
    CHECK(!atomic_load_explicit(&context.failed, memory_order_acquire));
    CHECK(context.consumed_count == context.item_count);
}

int main(void)
{
    test_invalid_arguments();
    test_capacity_and_empty_full_transitions();
    test_physical_wraparound_preserves_fifo_order();
    test_single_slot_capacity();
    test_reset_discards_queued_elements();
    test_sequence_counter_wraparound();
    test_status_names();
    test_one_producer_one_consumer_concurrently();

    if (failures != 0u) {
        (void)fprintf(stderr, "FAIL: %zu of %zu checks failed\n", failures,
                      checks);
        return 1;
    }

    (void)printf("PASS: %zu checks; SPSC capacity, ordering, reset, counter wrap, "
                 "and 1P/1C concurrency verified\n",
                 checks);
    return 0;
}
