#ifndef FIN_VOICE_PIPELINE_H
#define FIN_VOICE_PIPELINE_H

/*
 * FinTally Voice — C orchestration façade.
 *
 * This module connects the existing DSP, codebook registry, FP32 VQ, language
 * FSM, event queue, and bounded text builder without merging their ownership or
 * implementation into acoustic_dsp.c. It is intended to be a convenient,
 * stable C ABI surface for Rust FFI.
 *
 * This is a C-only API because its component ABI includes C atomics. Do not
 * include it from C++ translation units. Rust callers use extern "C" bindings.
 *
 * The pipeline does NOT infer language from centroid indices/distances. A
 * language must be explicitly selected by the caller or confirmed by the FSM
 * from independently produced and validated comparative language evidence.
 * VQ returns acoustic cluster indices, not characters, phonemes, words, or
 * semantic tokens. Transcript APIs accept recognizer-produced UTF-8 only.
 *
 * The caller owns every component context, its backing storage, and this
 * pipeline's private workspace. No allocation, thread creation, logging, or
 * clock access occurs here. Mutating calls must be serialized on one logical
 * producer/owner thread. The event queue may have one concurrent consumer.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "acoustic_dsp.h"
#include "codebooks/codebook_registry.h"
#include "event_queue.h"
#include "language_fsm.h"
#include "text_builder.h"
#include "vq_quantize.h"

#ifdef __cplusplus
#error "FinTally Voice pipeline is a C-only API; call it through Rust extern \"C\" bindings."
#endif

#define FIN_VOICE_PIPELINE_ABI_MAJOR 1u
#define FIN_VOICE_PIPELINE_ABI_MINOR 0u

typedef enum FinVoicePipelineStatus {
    FIN_VOICE_PIPELINE_OK = 0,
    FIN_VOICE_PIPELINE_OVERFLOW = 1,
    FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT = 2,
    FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED = 3,
    FIN_VOICE_PIPELINE_ERROR_WORKSPACE_TOO_SMALL = 4,
    FIN_VOICE_PIPELINE_ERROR_COMPONENT_NOT_READY = 5,
    FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE = 6,
    FIN_VOICE_PIPELINE_ERROR_LANGUAGE_UNCONFIRMED = 7,
    FIN_VOICE_PIPELINE_ERROR_DSP = 8,
    FIN_VOICE_PIPELINE_ERROR_VQ = 9,
    FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM = 10,
    FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE = 11,
    FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE_FULL = 12,
    FIN_VOICE_PIPELINE_ERROR_TEXT_BUILDER = 13,
    FIN_VOICE_PIPELINE_ERROR_TEXT_OUTPUT_TOO_SMALL = 14,
    FIN_VOICE_PIPELINE_ERROR_TEXT_EVENT_TOO_LARGE = 15
} FinVoicePipelineStatus;

/* Public FFI handle. Private pipeline state is stored in caller-provided memory. */
typedef struct FinVoicePipeline {
    void *workspace_memory; /* Aligned interior pointer, owned by the caller. */
    size_t workspace_size;  /* Bytes occupied by private state after alignment. */
    bool initialized;
} FinVoicePipeline;

/*
 * Result of one frame's DSP -> normalization -> FP32 VQ path. Both feature
 * vectors are retained for validation/diagnostics; quantization uses the
 * normalized vector and the selected language's immutable trained codebook.
 */
typedef struct FinVoicePipelineFrameResult {
    FinVoiceLanguage language;
    FinVoiceFeatureVector raw_features;
    FinVoiceFeatureVector normalized_features;
    FinVqResult quantization;
} FinVoicePipelineFrameResult;

/* Required caller-owned pipeline workspace, including alignment padding. */
size_t fin_voice_pipeline_workspace_size(void);

/*
 * Bind already initialized components. This function does not initialize or
 * take ownership of them. Requirements:
 *   - dsp is initialized;
 *   - language_fsm, event_queue, and text_builder were initialized separately;
 *   - queue/text backing storage remains alive for the pipeline's lifetime;
 *   - workspace is distinct from every component object and backing buffer.
 *
 * `workspace` may be unaligned; the implementation aligns private state inside
 * it. A failed initialization leaves pipeline->initialized false.
 */
FinVoicePipelineStatus fin_voice_pipeline_init(
    FinVoicePipeline *pipeline,
    void *workspace,
    size_t workspace_size,
    FinVoiceDspContext *dsp,
    FinVoiceLanguageFsm *language_fsm,
    FinVoiceEventQueue *event_queue,
    FinVoiceTextBuilder *text_builder);

/*
 * Select a user/configuration-specified language explicitly. Only en/fr/hi are
 * supported by the current generated codebooks. This does not claim that the
 * selected language was inferred from audio.
 */
FinVoicePipelineStatus fin_voice_pipeline_select_language_tag(
    FinVoicePipeline *pipeline,
    const char *language_tag);

/*
 * Feed independent language evidence to the language FSM. Evidence must come
 * from a calibrated language-identification component and labelled evaluation,
 * never from treating a VQ centroid index/distance gap as language confidence.
 * A CONFIRMED FSM result binds its codebook; CANDIDATE/UNCERTAIN/UNKNOWN clears
 * the active codebook to prevent unconfirmed language use.
 */
FinVoicePipelineStatus fin_voice_pipeline_update_language_evidence(
    FinVoicePipeline *pipeline,
    const FinVoiceLanguageEvidence *evidence,
    FinVoiceLanguageFsmSnapshot *out_snapshot);

/* UNKNOWN when no selected/confirmed supported language is active. */
FinVoiceLanguage fin_voice_pipeline_active_language(
    const FinVoicePipeline *pipeline);

/* One fixed 20 ms / 320-sample PCM16 frame, using the current selected language. */
FinVoicePipelineStatus fin_voice_pipeline_process_pcm16_frame(
    FinVoicePipeline *pipeline,
    const int16_t *pcm16,
    size_t sample_count,
    FinVoicePipelineFrameResult *out_result);

/*
 * Publish a frame's normalized 32-D FP32 feature event followed by its VQ event.
 * Timestamp and sample position are supplied by the producer; this function
 * never reads a clock. The event queue remains SPSC, with this entire pipeline
 * running on its single producer thread. If the second push fails because the
 * queue fills between pushes, the feature event may already be visible.
 */
FinVoicePipelineStatus fin_voice_pipeline_publish_frame_events(
    FinVoicePipeline *pipeline,
    const FinVoicePipelineFrameResult *result,
    uint64_t timestamp_ns,
    uint64_t sample_position);

/* Append UTF-8 text from a real recognizer; no ASR or centroid decoding occurs. */
FinVoicePipelineStatus fin_voice_pipeline_append_recognized_text(
    FinVoicePipeline *pipeline,
    const char *utf8,
    size_t byte_length);

/* Flush buffered recognizer text into caller storage; source is consumed on success. */
FinVoicePipelineStatus fin_voice_pipeline_flush_recognized_text(
    FinVoicePipeline *pipeline,
    char *destination,
    size_t destination_capacity,
    size_t *out_byte_length);

/* Publish one recognizer-provided UTF-8 text chunk (at most 95 bytes). */
FinVoicePipelineStatus fin_voice_pipeline_publish_text_event(
    FinVoicePipeline *pipeline,
    const char *utf8,
    size_t byte_length,
    uint64_t timestamp_ns,
    uint64_t sample_position,
    bool is_final);

/*
 * Reset all bound components and discard queued events. Caller MUST stop and
 * synchronize both queue producer and consumer before reset. Component memory
 * is retained; language selection and event sequence counter are cleared.
 */
FinVoicePipelineStatus fin_voice_pipeline_reset(FinVoicePipeline *pipeline);

const char *fin_voice_pipeline_status_name(FinVoicePipelineStatus status);
uint32_t fin_voice_pipeline_abi_version(void);

#endif /* FIN_VOICE_PIPELINE_H */
