#ifndef FIN_VOICE_EVENT_QUEUE_H
#define FIN_VOICE_EVENT_QUEUE_H

/*
 * FinTally Voice — bounded, allocation-free event transport.
 *
 * This is an in-process C ABI, not a wire/disk serialization format. Event
 * timestamps are supplied by the producer in monotonic nanoseconds when such
 * a clock is available; sample_position is measured in the producer's audio
 * stream. The queue does not read clocks or synthesize either field.
 *
 * Ownership is intentionally SPSC: one logical producer and one logical
 * consumer may call push/pop concurrently. Do NOT publish to the same queue
 * from the audio, feature, quantization, and error threads simultaneously.
 * Give each independent producer its own queue or design an explicitly
 * multi-producer transport in a future phase.
 *
 * Storage is caller-owned. There is no dynamic allocation. The queue wraps the
 * existing FinSpscRing primitive because the ownership contract is identical.
 * Reset is only valid after both producer and consumer are quiescent.
 */

#include "spsc_ring.h"

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
#error "FinTally Voice event queue is a C-only API; include it from C sources."
#endif

#define FIN_VOICE_EVENT_ABI_MAJOR 1u
#define FIN_VOICE_EVENT_ABI_MINOR 0u

#define FIN_VOICE_EVENT_FEATURE_DIMENSION 32u
#define FIN_VOICE_EVENT_TEXT_CAPACITY 96u /* Includes the terminating NUL. */
#define FIN_VOICE_EVENT_NO_CENTROID UINT32_MAX

typedef enum FinVoiceEventType {
    FIN_VOICE_EVENT_SPEECH_STARTED = 1,
    FIN_VOICE_EVENT_SPEECH_ENDED = 2,
    FIN_VOICE_EVENT_FEATURES_READY = 3,
    FIN_VOICE_EVENT_QUANTIZATION_RESULT = 4,
    FIN_VOICE_EVENT_TEXT_READY = 5,
    FIN_VOICE_EVENT_ERROR = 6,
    FIN_VOICE_EVENT_STATUS = 7,
    FIN_VOICE_EVENT_SHUTDOWN = 8,
    FIN_VOICE_EVENT_RESET = 9
} FinVoiceEventType;

typedef enum FinVoiceEventLanguage {
    FIN_VOICE_EVENT_LANGUAGE_UNKNOWN = 0,
    FIN_VOICE_EVENT_LANGUAGE_EN = 1,
    FIN_VOICE_EVENT_LANGUAGE_FR = 2,
    FIN_VOICE_EVENT_LANGUAGE_HI = 3,
    FIN_VOICE_EVENT_LANGUAGE_ZH = 4
} FinVoiceEventLanguage;

enum {
    FIN_VOICE_EVENT_FLAG_NONE = 0u,
    FIN_VOICE_EVENT_FLAG_FINAL = 1u << 0,
    FIN_VOICE_EVENT_FLAG_DISCONTINUITY = 1u << 1,
    FIN_VOICE_EVENT_FLAG_PAYLOAD_TRUNCATED = 1u << 2
};

#define FIN_VOICE_EVENT_KNOWN_FLAGS \
    (FIN_VOICE_EVENT_FLAG_FINAL | FIN_VOICE_EVENT_FLAG_DISCONTINUITY | \
     FIN_VOICE_EVENT_FLAG_PAYLOAD_TRUNCATED)

/*
 * Construct events from a zero-initialized value (for example,
 * `FinVoiceEvent event = {0};`) and then fill the common fields and the active
 * payload. Only the payload selected by `type` is interpreted by ABI v1.
 */
typedef struct FinVoiceEvent {
    uint64_t timestamp_ns;
    uint64_t sample_position;
    uint32_t sequence_id; /* Assigned by the producer; not changed by queue. */
    uint16_t type;        /* FinVoiceEventType; fixed-width for ABI stability. */
    uint16_t flags;

    union {
        struct {
            uint16_t dimension; /* Must be FIN_VOICE_EVENT_FEATURE_DIMENSION. */
            uint16_t reserved;  /* Must be zero for ABI v1. */
            float values[FIN_VOICE_EVENT_FEATURE_DIMENSION];
        } features;

        struct {
            uint32_t best_index;
            uint32_t second_best_index; /* NO_CENTROID if no second result. */
            float best_distance_squared;
            float second_distance_squared; /* +infinity when second is absent. */
            float relative_gap; /* Finite and in [0, 1]. */
            uint8_t language;   /* FinVoiceEventLanguage. */
            uint8_t reserved[3]; /* Must be zero for ABI v1. */
        } quantization;

        struct {
            uint16_t length; /* UTF-8 bytes, excluding terminating NUL. */
            uint16_t reserved; /* Must be zero for ABI v1. */
            char utf8[FIN_VOICE_EVENT_TEXT_CAPACITY];
        } text;

        struct {
            uint32_t code; /* Module-defined stable status/error code. */
            int32_t detail; /* Optional compact detail; meaning is code-specific. */
        } status;
    } payload;
} FinVoiceEvent;

typedef enum FinVoiceEventQueueStatus {
    FIN_VOICE_EVENT_QUEUE_OK = 0,
    FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT = 1,
    FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_CONFIGURATION = 2,
    FIN_VOICE_EVENT_QUEUE_ERROR_SIZE_OVERFLOW = 3,
    FIN_VOICE_EVENT_QUEUE_ERROR_FULL = 4,
    FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY = 5,
    FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT = 6,
    FIN_VOICE_EVENT_QUEUE_ERROR_INTERNAL = 7
} FinVoiceEventQueueStatus;

typedef struct FinVoiceEventQueue {
    FinSpscRing ring;
    atomic_size_t dropped_event_count;
} FinVoiceEventQueue;

/* Validate the tagged event and the payload selected by its type. */
FinVoiceEventQueueStatus fin_voice_event_validate(const FinVoiceEvent *event);

/* Initialize over capacity FinVoiceEvent slots owned by the caller. */
FinVoiceEventQueueStatus fin_voice_event_queue_init(
    FinVoiceEventQueue *queue,
    FinVoiceEvent *storage,
    size_t capacity);

/* Discard queued events and clear the drop counter; both sides must be stopped. */
FinVoiceEventQueueStatus fin_voice_event_queue_reset(FinVoiceEventQueue *queue);

/* Non-blocking SPSC operations. Invalid events are rejected before enqueue. */
FinVoiceEventQueueStatus fin_voice_event_queue_push(
    FinVoiceEventQueue *queue,
    const FinVoiceEvent *event);
FinVoiceEventQueueStatus fin_voice_event_queue_pop(
    FinVoiceEventQueue *queue,
    FinVoiceEvent *out_event);

size_t fin_voice_event_queue_capacity(const FinVoiceEventQueue *queue);
/* Saturating count of valid events rejected solely because the queue was full. */
size_t fin_voice_event_queue_dropped_count(const FinVoiceEventQueue *queue);
const char *fin_voice_event_queue_status_name(FinVoiceEventQueueStatus status);
uint32_t fin_voice_event_abi_version(void);

#endif /* FIN_VOICE_EVENT_QUEUE_H */
