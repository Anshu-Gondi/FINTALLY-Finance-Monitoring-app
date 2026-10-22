#include "event_queue.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool fin_voice_event_type_is_known(uint16_t type)
{
    switch (type) {
    case FIN_VOICE_EVENT_SPEECH_STARTED:
    case FIN_VOICE_EVENT_SPEECH_ENDED:
    case FIN_VOICE_EVENT_FEATURES_READY:
    case FIN_VOICE_EVENT_QUANTIZATION_RESULT:
    case FIN_VOICE_EVENT_TEXT_READY:
    case FIN_VOICE_EVENT_ERROR:
    case FIN_VOICE_EVENT_STATUS:
    case FIN_VOICE_EVENT_SHUTDOWN:
    case FIN_VOICE_EVENT_RESET:
        return true;
    default:
        return false;
    }
}

static bool fin_voice_event_language_is_known(uint8_t language)
{
    return language <= FIN_VOICE_EVENT_LANGUAGE_ZH;
}

/* Validate well-formed UTF-8 without allocating or relying on locale APIs. */
static bool fin_voice_event_utf8_is_valid(const unsigned char *text, size_t length)
{
    size_t index = 0u;

    while (index < length) {
        const unsigned char lead = text[index];
        size_t continuation_count;
        unsigned char second_min = 0x80u;
        unsigned char second_max = 0xBFu;

        if (lead <= 0x7Fu) {
            ++index;
            continue;
        }
        if (lead >= 0xC2u && lead <= 0xDFu) {
            continuation_count = 1u;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            continuation_count = 2u;
            if (lead == 0xE0u) {
                second_min = 0xA0u; /* Prevent overlong encoding. */
            } else if (lead == 0xEDu) {
                second_max = 0x9Fu; /* Exclude UTF-16 surrogate range. */
            }
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            continuation_count = 3u;
            if (lead == 0xF0u) {
                second_min = 0x90u; /* Prevent overlong encoding. */
            } else if (lead == 0xF4u) {
                second_max = 0x8Fu; /* Unicode ends at U+10FFFF. */
            }
        } else {
            return false;
        }

        if (continuation_count > length - index - 1u) {
            return false;
        }
        const unsigned char second = text[index + 1u];
        if (second < second_min || second > second_max) {
            return false;
        }
        for (size_t offset = 2u; offset <= continuation_count; ++offset) {
            const unsigned char byte = text[index + offset];
            if (byte < 0x80u || byte > 0xBFu) {
                return false;
            }
        }
        index += continuation_count + 1u;
    }

    return true;
}

static bool fin_voice_event_validate_features(const FinVoiceEvent *event)
{
    if (event->payload.features.dimension != FIN_VOICE_EVENT_FEATURE_DIMENSION ||
        event->payload.features.reserved != 0u) {
        return false;
    }

    for (size_t index = 0u; index < FIN_VOICE_EVENT_FEATURE_DIMENSION; ++index) {
        if (!isfinite(event->payload.features.values[index])) {
            return false;
        }
    }
    return true;
}

static bool fin_voice_event_validate_quantization(const FinVoiceEvent *event)
{
    const uint32_t best_index = event->payload.quantization.best_index;
    const uint32_t second_index = event->payload.quantization.second_best_index;
    const float best_distance = event->payload.quantization.best_distance_squared;
    const float second_distance = event->payload.quantization.second_distance_squared;
    const float relative_gap = event->payload.quantization.relative_gap;
    const uint8_t language = event->payload.quantization.language;

    if (best_index == FIN_VOICE_EVENT_NO_CENTROID || !isfinite(best_distance) ||
        best_distance < 0.0f || !isfinite(relative_gap) || relative_gap < 0.0f ||
        relative_gap > 1.0f || !fin_voice_event_language_is_known(language)) {
        return false;
    }
    if (event->payload.quantization.reserved[0] != 0u ||
        event->payload.quantization.reserved[1] != 0u ||
        event->payload.quantization.reserved[2] != 0u) {
        return false;
    }

    if (second_index == FIN_VOICE_EVENT_NO_CENTROID) {
        return isinf(second_distance) && second_distance > 0.0f &&
               relative_gap == 0.0f;
    }

    return second_index != best_index && isfinite(second_distance) &&
           second_distance >= best_distance;
}

static bool fin_voice_event_validate_text(const FinVoiceEvent *event)
{
    const size_t length = event->payload.text.length;

    if (event->payload.text.reserved != 0u || length == 0u ||
        length >= FIN_VOICE_EVENT_TEXT_CAPACITY) {
        return false;
    }
    if (event->payload.text.utf8[length] != '\0') {
        return false;
    }
    if (memchr(event->payload.text.utf8, '\0', length) != NULL) {
        return false;
    }
    return fin_voice_event_utf8_is_valid(
        (const unsigned char *)event->payload.text.utf8, length);
}

FinVoiceEventQueueStatus fin_voice_event_validate(const FinVoiceEvent *event)
{
    if (event == NULL) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    }
    if (!fin_voice_event_type_is_known(event->type) ||
        (event->flags & (uint16_t)~FIN_VOICE_EVENT_KNOWN_FLAGS) != 0u) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT;
    }

    switch ((FinVoiceEventType)event->type) {
    case FIN_VOICE_EVENT_FEATURES_READY:
        return fin_voice_event_validate_features(event)
                   ? FIN_VOICE_EVENT_QUEUE_OK
                   : FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT;
    case FIN_VOICE_EVENT_QUANTIZATION_RESULT:
        return fin_voice_event_validate_quantization(event)
                   ? FIN_VOICE_EVENT_QUEUE_OK
                   : FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT;
    case FIN_VOICE_EVENT_TEXT_READY:
        return fin_voice_event_validate_text(event)
                   ? FIN_VOICE_EVENT_QUEUE_OK
                   : FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT;
    case FIN_VOICE_EVENT_SPEECH_STARTED:
    case FIN_VOICE_EVENT_SPEECH_ENDED:
    case FIN_VOICE_EVENT_ERROR:
    case FIN_VOICE_EVENT_STATUS:
    case FIN_VOICE_EVENT_SHUTDOWN:
    case FIN_VOICE_EVENT_RESET:
        return FIN_VOICE_EVENT_QUEUE_OK;
    default:
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT;
    }
}

static FinVoiceEventQueueStatus fin_voice_event_map_ring_status(FinSpscRingStatus status)
{
    switch (status) {
    case FIN_SPSC_RING_OK:
        return FIN_VOICE_EVENT_QUEUE_OK;
    case FIN_SPSC_RING_ERROR_INVALID_ARGUMENT:
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    case FIN_SPSC_RING_ERROR_INVALID_CONFIGURATION:
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_CONFIGURATION;
    case FIN_SPSC_RING_ERROR_SIZE_OVERFLOW:
        return FIN_VOICE_EVENT_QUEUE_ERROR_SIZE_OVERFLOW;
    case FIN_SPSC_RING_ERROR_FULL:
        return FIN_VOICE_EVENT_QUEUE_ERROR_FULL;
    case FIN_SPSC_RING_ERROR_EMPTY:
        return FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY;
    default:
        return FIN_VOICE_EVENT_QUEUE_ERROR_INTERNAL;
    }
}

FinVoiceEventQueueStatus fin_voice_event_queue_init(
    FinVoiceEventQueue *queue,
    FinVoiceEvent *storage,
    size_t capacity)
{
    if (queue == NULL || storage == NULL) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    }
    if (capacity == 0u) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_CONFIGURATION;
    }

    const FinSpscRingStatus status = fin_spsc_ring_init(
        &queue->ring, storage, capacity, sizeof(FinVoiceEvent));
    if (status != FIN_SPSC_RING_OK) {
        return fin_voice_event_map_ring_status(status);
    }
    atomic_init(&queue->dropped_event_count, 0u);
    return FIN_VOICE_EVENT_QUEUE_OK;
}

FinVoiceEventQueueStatus fin_voice_event_queue_reset(FinVoiceEventQueue *queue)
{
    if (queue == NULL) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    }

    const FinSpscRingStatus status = fin_spsc_ring_reset(&queue->ring);
    if (status != FIN_SPSC_RING_OK) {
        return fin_voice_event_map_ring_status(status);
    }
    atomic_store_explicit(
        &queue->dropped_event_count, 0u, memory_order_relaxed);
    return FIN_VOICE_EVENT_QUEUE_OK;
}

static void fin_voice_event_count_drop(FinVoiceEventQueue *queue)
{
    size_t observed = atomic_load_explicit(
        &queue->dropped_event_count, memory_order_relaxed);

    /* Saturate rather than wrapping the diagnostic counter back to zero. */
    while (observed != SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &queue->dropped_event_count,
                &observed,
                observed + 1u,
                memory_order_relaxed,
                memory_order_relaxed)) {
            return;
        }
    }
}

FinVoiceEventQueueStatus fin_voice_event_queue_push(
    FinVoiceEventQueue *queue,
    const FinVoiceEvent *event)
{
    if (queue == NULL || event == NULL) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    }

    const FinVoiceEventQueueStatus validation = fin_voice_event_validate(event);
    if (validation != FIN_VOICE_EVENT_QUEUE_OK) {
        return validation;
    }

    const FinVoiceEventQueueStatus status = fin_voice_event_map_ring_status(
        fin_spsc_ring_push(&queue->ring, event));
    if (status == FIN_VOICE_EVENT_QUEUE_ERROR_FULL) {
        fin_voice_event_count_drop(queue);
    }
    return status;
}

FinVoiceEventQueueStatus fin_voice_event_queue_pop(
    FinVoiceEventQueue *queue,
    FinVoiceEvent *out_event)
{
    if (queue == NULL || out_event == NULL) {
        return FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT;
    }
    return fin_voice_event_map_ring_status(
        fin_spsc_ring_pop(&queue->ring, out_event));
}

size_t fin_voice_event_queue_capacity(const FinVoiceEventQueue *queue)
{
    return queue != NULL ? fin_spsc_ring_capacity(&queue->ring) : 0u;
}

size_t fin_voice_event_queue_dropped_count(const FinVoiceEventQueue *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    return atomic_load_explicit(
        &queue->dropped_event_count, memory_order_relaxed);
}

const char *fin_voice_event_queue_status_name(FinVoiceEventQueueStatus status)
{
    switch (status) {
    case FIN_VOICE_EVENT_QUEUE_OK:
        return "ok";
    case FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_CONFIGURATION:
        return "invalid_configuration";
    case FIN_VOICE_EVENT_QUEUE_ERROR_SIZE_OVERFLOW:
        return "size_overflow";
    case FIN_VOICE_EVENT_QUEUE_ERROR_FULL:
        return "full";
    case FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY:
        return "empty";
    case FIN_VOICE_EVENT_QUEUE_ERROR_INVALID_EVENT:
        return "invalid_event";
    case FIN_VOICE_EVENT_QUEUE_ERROR_INTERNAL:
        return "internal_error";
    default:
        return "unknown_status";
    }
}

uint32_t fin_voice_event_abi_version(void)
{
    return (FIN_VOICE_EVENT_ABI_MAJOR << 16u) | FIN_VOICE_EVENT_ABI_MINOR;
}
