#include "spsc_ring.h"

#include <stdint.h>
#include <string.h>

static size_t fin_spsc_ring_advance_slot(size_t slot, size_t capacity)
{
    ++slot;
    return slot == capacity ? 0u : slot;
}

FinSpscRingStatus fin_spsc_ring_init(
    FinSpscRing *ring,
    void *storage,
    size_t capacity,
    size_t element_size)
{
    if (ring == NULL || storage == NULL) {
        return FIN_SPSC_RING_ERROR_INVALID_ARGUMENT;
    }
    if (capacity == 0u || element_size == 0u) {
        return FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION;
    }
    if (capacity > SIZE_MAX / element_size) {
        return FIN_SPSC_RING_ERROR_SIZE_OVERFLOW;
    }

    ring->storage = (unsigned char *)storage;
    ring->capacity = capacity;
    ring->element_size = element_size;
    ring->producer_slot = 0u;
    ring->consumer_slot = 0u;
    atomic_init(&ring->published_write_count, 0u);
    atomic_init(&ring->published_read_count, 0u);
    return FIN_SPSC_RING_OK;
}

FinSpscRingStatus fin_spsc_ring_reset(FinSpscRing *ring)
{
    if (ring == NULL || ring->storage == NULL || ring->capacity == 0u ||
        ring->element_size == 0u) {
        return FIN_SPSC_RING_ERROR_INVALID_ARGUMENT;
    }

    /* Caller guarantees quiescence; no cross-thread reset protocol exists. */
    ring->producer_slot = 0u;
    ring->consumer_slot = 0u;
    atomic_store_explicit(
        &ring->published_write_count, 0u, memory_order_relaxed);
    atomic_store_explicit(
        &ring->published_read_count, 0u, memory_order_relaxed);
    return FIN_SPSC_RING_OK;
}

FinSpscRingStatus fin_spsc_ring_push(FinSpscRing *ring, const void *element)
{
    if (ring == NULL || element == NULL || ring->storage == NULL) {
        return FIN_SPSC_RING_ERROR_INVALID_ARGUMENT;
    }
    if (ring->capacity == 0u || ring->element_size == 0u ||
        ring->producer_slot >= ring->capacity) {
        return FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION;
    }

    /* This side exclusively writes published_write_count. */
    const size_t write_count = atomic_load_explicit(
        &ring->published_write_count, memory_order_relaxed);

    /* Acquire the consumer's release to avoid reusing a slot prematurely. */
    const size_t read_count = atomic_load_explicit(
        &ring->published_read_count, memory_order_acquire);

    /* Unsigned subtraction intentionally handles sequence-counter wrap. */
    if (write_count - read_count >= ring->capacity) {
        return FIN_SPSC_RING_ERROR_FULL;
    }

    const size_t byte_offset = ring->producer_slot * ring->element_size;
    memcpy(ring->storage + byte_offset, element, ring->element_size);
    ring->producer_slot = fin_spsc_ring_advance_slot(
        ring->producer_slot, ring->capacity);

    /* Publish only after the payload has been fully copied into its slot. */
    atomic_store_explicit(
        &ring->published_write_count, write_count + 1u, memory_order_release);
    return FIN_SPSC_RING_OK;
}

FinSpscRingStatus fin_spsc_ring_pop(FinSpscRing *ring, void *out_element)
{
    if (ring == NULL || out_element == NULL || ring->storage == NULL) {
        return FIN_SPSC_RING_ERROR_INVALID_ARGUMENT;
    }
    if (ring->capacity == 0u || ring->element_size == 0u ||
        ring->consumer_slot >= ring->capacity) {
        return FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION;
    }

    /* This side exclusively writes published_read_count. */
    const size_t read_count = atomic_load_explicit(
        &ring->published_read_count, memory_order_relaxed);

    /* Acquire pairs with producer's release publication of the element. */
    const size_t write_count = atomic_load_explicit(
        &ring->published_write_count, memory_order_acquire);

    if (read_count == write_count) {
        return FIN_SPSC_RING_ERROR_EMPTY;
    }

    const size_t byte_offset = ring->consumer_slot * ring->element_size;
    memcpy(out_element, ring->storage + byte_offset, ring->element_size);
    ring->consumer_slot = fin_spsc_ring_advance_slot(
        ring->consumer_slot, ring->capacity);

    /* Allow the producer to reuse this slot only after the copy has finished. */
    atomic_store_explicit(
        &ring->published_read_count, read_count + 1u, memory_order_release);
    return FIN_SPSC_RING_OK;
}

size_t fin_spsc_ring_capacity(const FinSpscRing *ring)
{
    return ring != NULL ? ring->capacity : 0u;
}

size_t fin_spsc_ring_element_size(const FinSpscRing *ring)
{
    return ring != NULL ? ring->element_size : 0u;
}

const char *fin_spsc_ring_status_name(FinSpscRingStatus status)
{
    switch (status) {
    case FIN_SPSC_RING_OK:
        return "ok";
    case FIN_SPSC_RING_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION:
        return "invalid_configuration";
    case FIN_SPSC_RING_ERROR_SIZE_OVERFLOW:
        return "size_overflow";
    case FIN_SPSC_RING_ERROR_FULL:
        return "full";
    case FIN_SPSC_RING_ERROR_EMPTY:
        return "empty";
    default:
        return "unknown_status";
    }
}
