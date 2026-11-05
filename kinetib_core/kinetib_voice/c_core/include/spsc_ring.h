#ifndef FIN_VOICE_SPSC_RING_H
#define FIN_VOICE_SPSC_RING_H

/*
 * FinTally Voice — fixed-capacity single-producer/single-consumer ring.
 *
 * Storage and element memory are caller-owned. This module never allocates or
 * copies the backing buffer. Push/pop copy exactly one fixed
 * size element using memcpy. Push/pop never wait for space: they return FULL or
 * EMPTY immediately, although the C implementation may use non-lock-free atomics
 * on targets that do not provide lock-free atomic_size_t operations.
 *
 * Concurrency contract:
 *   - At most one logical producer may call fin_spsc_ring_push at a time.
 *   - At most one logical consumer may call fin_spsc_ring_pop at a time.
 *   - One producer and one consumer may operate concurrently.
 *   - This is NOT MPSC, SPMC, MPMC, or generally thread-safe.
 *   - Init and reset must not race with push/pop. Reset only after both sides
 *     have stopped and the caller has established any required thread join or
 *     synchronization.
 *   - Item input/output memory must not overlap the ring's backing storage.
 *
 * Capacity is the number of usable elements (no sentinel slot is sacrificed).
 * The caller must provide at least capacity * element_size bytes of storage.
 * The ring object and backing storage must not overlap. Treat the struct fields
 * as implementation details: do not mutate them after successful init.
 *
 * The release-store of the published write count makes a producer's element
 * write visible to a consumer that observes it with an acquire-load. Likewise,
 * the consumer's release-store of the read count ensures that the producer
 * does not reuse a slot before the prior consumer read has completed.
 * C atomics do not guarantee lock-free implementation on every target; verify
 * target toolchain/architecture requirements if strict lock-free behavior is
 * a deployment requirement.
 */

#include <stdatomic.h>
#include <stddef.h>

#ifdef __cplusplus
#error "FinTally Voice SPSC ring is a C-only API; include it from C sources."
#endif

typedef enum FinSpscRingStatus {
    FIN_SPSC_RING_OK = 0,
    FIN_SPSC_RING_ERROR_INVALID_ARGUMENT = 1,
    FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION = 2,
    FIN_SPSC_RING_ERROR_SIZE_OVERFLOW = 3,
    FIN_SPSC_RING_ERROR_FULL = 4,
    FIN_SPSC_RING_ERROR_EMPTY = 5
} FinSpscRingStatus;

typedef struct FinSpscRing {
    unsigned char *storage;
    size_t capacity;
    size_t element_size;

    /* Producer-only and consumer-only physical slots, explicitly wrapped. */
    size_t producer_slot;
    size_t consumer_slot;

    /* Modulo size_t publication counters; occupancy never exceeds capacity. */
    atomic_size_t published_write_count;
    atomic_size_t published_read_count;
} FinSpscRing;

/*
 * Initialize an unshared ring over caller-owned storage.
 *
 * Returns INVALID_ARGUMENT for null ring/storage; INVALID_CONFIGURATION for
 * zero capacity or element size; SIZE_OVERFLOW if capacity * element_size
 * cannot be represented by size_t. The caller remains responsible for ensuring
 * the storage allocation is actually at least that many bytes.
 */
FinSpscRingStatus fin_spsc_ring_init(
    FinSpscRing *ring,
    void *storage,
    size_t capacity,
    size_t element_size);

/*
 * Reset logical state and discard queued elements. The backing bytes are not
 * cleared. Must only be called when producer and consumer are both quiescent.
 */
FinSpscRingStatus fin_spsc_ring_reset(FinSpscRing *ring);

/* Non-blocking operation: FULL means no element was copied or published. */
FinSpscRingStatus fin_spsc_ring_push(
    FinSpscRing *ring,
    const void *element);

/* Non-blocking operation: EMPTY means no output element was written. */
FinSpscRingStatus fin_spsc_ring_pop(
    FinSpscRing *ring,
    void *out_element);

/* Return immutable initialized configuration; zero for a null ring. */
size_t fin_spsc_ring_capacity(const FinSpscRing *ring);
size_t fin_spsc_ring_element_size(const FinSpscRing *ring);

/* Stable, allocation-free diagnostic names. */
const char *fin_spsc_ring_status_name(FinSpscRingStatus status);

#endif /* FIN_VOICE_SPSC_RING_H */
