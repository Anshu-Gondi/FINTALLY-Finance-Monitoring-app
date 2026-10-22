#include "voice_pipeline.h"

#include <math.h>
#include <string.h>

#define FIN_VOICE_PIPELINE_INVALID_LANGUAGE FIN_VOICE_LANGUAGE_UNKNOWN

typedef struct FinVoicePipelineInternal {
    FinVoiceDspContext *dsp;
    FinVoiceLanguageFsm *language_fsm;
    FinVoiceEventQueue *event_queue;
    FinVoiceTextBuilder *text_builder;
    FinVoiceCodebookRegistryEntry registry_entry;
    FinVqCodebook vq_codebook;
    FinVoiceFeatureNormalization normalization;
    FinVoiceLanguage active_language;
    uint32_t next_sequence_id;
} FinVoicePipelineInternal;

static bool fin_voice_pipeline_memory_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size)
{
    if (left == NULL || right == NULL || left_size == 0u || right_size == 0u) {
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

static FinVoicePipelineInternal *fin_voice_pipeline_state(
    const FinVoicePipeline *pipeline)
{
    if (pipeline == NULL || !pipeline->initialized ||
        pipeline->workspace_memory == NULL ||
        pipeline->workspace_size < sizeof(FinVoicePipelineInternal)) {
        return NULL;
    }
    return (FinVoicePipelineInternal *)pipeline->workspace_memory;
}

size_t fin_voice_pipeline_workspace_size(void)
{
    const size_t alignment = _Alignof(FinVoicePipelineInternal);
    if (alignment == 0u || alignment - 1u > SIZE_MAX - sizeof(FinVoicePipelineInternal)) {
        return 0u;
    }
    return sizeof(FinVoicePipelineInternal) + alignment - 1u;
}

FinVoicePipelineStatus fin_voice_pipeline_init(
    FinVoicePipeline *pipeline,
    void *workspace,
    size_t workspace_size,
    FinVoiceDspContext *dsp,
    FinVoiceLanguageFsm *language_fsm,
    FinVoiceEventQueue *event_queue,
    FinVoiceTextBuilder *text_builder)
{
    const size_t alignment = _Alignof(FinVoicePipelineInternal);
    const size_t required_size = fin_voice_pipeline_workspace_size();
    uintptr_t raw_address;
    uintptr_t remainder;
    uintptr_t padding;
    uintptr_t aligned_address;
    size_t alignment_loss;
    FinVoiceLanguageFsmSnapshot snapshot = {0};
    FinVoicePipelineInternal *state;

    if (pipeline == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    pipeline->workspace_memory = NULL;
    pipeline->workspace_size = 0u;
    pipeline->initialized = false;

    if (workspace == NULL || dsp == NULL || language_fsm == NULL ||
        event_queue == NULL || text_builder == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    if (required_size == 0u || workspace_size < required_size || alignment == 0u) {
        return FIN_VOICE_PIPELINE_ERROR_WORKSPACE_TOO_SMALL;
    }
    if (!dsp->initialized || fin_voice_event_queue_capacity(event_queue) == 0u ||
        fin_voice_text_builder_capacity(text_builder) == 0u ||
        fin_voice_language_fsm_snapshot(language_fsm, &snapshot) !=
            FIN_VOICE_LANGUAGE_FSM_OK ||
        language_fsm->config.consecutive_confirmations == 0u) {
        return FIN_VOICE_PIPELINE_ERROR_COMPONENT_NOT_READY;
    }

    if (fin_voice_pipeline_memory_overlap(workspace, workspace_size,
            dsp, sizeof(*dsp)) ||
        fin_voice_pipeline_memory_overlap(workspace, workspace_size,
            language_fsm, sizeof(*language_fsm)) ||
        fin_voice_pipeline_memory_overlap(workspace, workspace_size,
            event_queue, sizeof(*event_queue)) ||
        fin_voice_pipeline_memory_overlap(workspace, workspace_size,
            text_builder, sizeof(*text_builder))) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }

    raw_address = (uintptr_t)workspace;
    remainder = raw_address % (uintptr_t)alignment;
    padding = remainder == 0u ? 0u : (uintptr_t)alignment - remainder;
    if (raw_address > UINTPTR_MAX - padding) {
        return FIN_VOICE_PIPELINE_ERROR_WORKSPACE_TOO_SMALL;
    }
    aligned_address = raw_address + padding;
    alignment_loss = (size_t)padding;
    if (alignment_loss > workspace_size ||
        workspace_size - alignment_loss < sizeof(FinVoicePipelineInternal)) {
        return FIN_VOICE_PIPELINE_ERROR_WORKSPACE_TOO_SMALL;
    }

    memset((void *)aligned_address, 0, sizeof(FinVoicePipelineInternal));
    state = (FinVoicePipelineInternal *)aligned_address;
    state->dsp = dsp;
    state->language_fsm = language_fsm;
    state->event_queue = event_queue;
    state->text_builder = text_builder;
    state->active_language = FIN_VOICE_PIPELINE_INVALID_LANGUAGE;
    state->next_sequence_id = 1u;

    pipeline->workspace_memory = state;
    pipeline->workspace_size = sizeof(*state);
    pipeline->initialized = true;
    return FIN_VOICE_PIPELINE_OK;
}

static void fin_voice_pipeline_clear_codebook(FinVoicePipelineInternal *state)
{
    state->registry_entry.codebook = NULL;
    state->registry_entry.normalization_mean = NULL;
    state->registry_entry.normalization_inv_std = NULL;
    state->vq_codebook.centroids = NULL;
    state->vq_codebook.centroid_count = 0u;
    state->vq_codebook.dimension = 0u;
    memset(&state->normalization, 0, sizeof(state->normalization));
    state->active_language = FIN_VOICE_PIPELINE_INVALID_LANGUAGE;
}

static FinVoicePipelineStatus fin_voice_pipeline_select_language_internal(
    FinVoicePipelineInternal *state,
    const char *language_tag)
{
    FinVoiceLanguage parsed_language = FIN_VOICE_LANGUAGE_UNKNOWN;
    FinVoiceCodebookRegistryEntry entry = {0};
    FinVqCodebook codebook = {0};
    FinVoiceLanguageFsmStatus language_status;
    FinVoiceCodebookRegistryStatus registry_status;
    FinVqStatus vq_status;

    language_status = fin_voice_language_parse_tag(language_tag, &parsed_language);
    if (language_status == FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE) {
        fin_voice_pipeline_clear_codebook(state);
        return FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE;
    }
    if (language_status != FIN_VOICE_LANGUAGE_FSM_OK) {
        return FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM;
    }

    registry_status = fin_voice_codebook_registry_lookup(language_tag, &entry);
    if (registry_status == FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE) {
        fin_voice_pipeline_clear_codebook(state);
        return FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE;
    }
    if (registry_status != FIN_VOICE_CODEBOOK_REGISTRY_OK ||
        entry.codebook == NULL || entry.normalization_mean == NULL ||
        entry.normalization_inv_std == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_COMPONENT_NOT_READY;
    }

    codebook.centroids = entry.codebook->centroids;
    codebook.centroid_count = entry.codebook->centroid_count;
    codebook.dimension = entry.codebook->dimensions;
    vq_status = fin_vq_validate_codebook(&codebook);
    if (vq_status != FIN_VQ_OK) {
        return FIN_VOICE_PIPELINE_ERROR_VQ;
    }

    state->registry_entry = entry;
    state->vq_codebook = codebook;
    for (size_t index = 0u; index < FIN_VOICE_FEATURE_DIM; ++index) {
        state->normalization.mean[index] = entry.normalization_mean[index];
        state->normalization.inv_std[index] = entry.normalization_inv_std[index];
        if (!isfinite(state->normalization.mean[index]) ||
            !isfinite(state->normalization.inv_std[index]) ||
            state->normalization.inv_std[index] <= 0.0f) {
            fin_voice_pipeline_clear_codebook(state);
            return FIN_VOICE_PIPELINE_ERROR_COMPONENT_NOT_READY;
        }
    }
    state->active_language = parsed_language;
    return FIN_VOICE_PIPELINE_OK;
}

FinVoicePipelineStatus fin_voice_pipeline_select_language_tag(
    FinVoicePipeline *pipeline,
    const char *language_tag)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    if (language_tag == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    return fin_voice_pipeline_select_language_internal(state, language_tag);
}

FinVoicePipelineStatus fin_voice_pipeline_update_language_evidence(
    FinVoicePipeline *pipeline,
    const FinVoiceLanguageEvidence *evidence,
    FinVoiceLanguageFsmSnapshot *out_snapshot)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceLanguageFsmSnapshot local_snapshot = {0};
    FinVoiceLanguageFsmStatus status;

    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    if (evidence == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }

    status = fin_voice_language_fsm_update(
        state->language_fsm, evidence,
        out_snapshot != NULL ? out_snapshot : &local_snapshot);
    if (status != FIN_VOICE_LANGUAGE_FSM_OK) {
        return FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM;
    }
    if (out_snapshot != NULL) {
        local_snapshot = *out_snapshot;
    }

    if (local_snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED) {
        const char *tag = fin_voice_language_tag(local_snapshot.language);
        if (tag == NULL) {
            fin_voice_pipeline_clear_codebook(state);
            return FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE;
        }
        return fin_voice_pipeline_select_language_internal(state, tag);
    }

    fin_voice_pipeline_clear_codebook(state);
    return FIN_VOICE_PIPELINE_OK;
}

FinVoiceLanguage fin_voice_pipeline_active_language(
    const FinVoicePipeline *pipeline)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    return state == NULL ? FIN_VOICE_LANGUAGE_UNKNOWN : state->active_language;
}

FinVoicePipelineStatus fin_voice_pipeline_process_pcm16_frame(
    FinVoicePipeline *pipeline,
    const int16_t *pcm16,
    size_t sample_count,
    FinVoicePipelineFrameResult *out_result)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceDspStatus dsp_status;
    FinVqStatus vq_status;
    FinVoicePipelineFrameResult result = {0};

    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    if (pcm16 == NULL || out_result == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    if (state->active_language == FIN_VOICE_LANGUAGE_UNKNOWN ||
        state->vq_codebook.centroids == NULL) {
        return FIN_VOICE_PIPELINE_ERROR_LANGUAGE_UNCONFIRMED;
    }
    if (sample_count != FIN_VOICE_FRAME_SAMPLES) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }

    dsp_status = fin_voice_dsp_extract_features(
        state->dsp, pcm16, sample_count, &result.raw_features);
    if (dsp_status != FIN_DSP_OK) {
        return FIN_VOICE_PIPELINE_ERROR_DSP;
    }
    dsp_status = fin_voice_dsp_normalize_features(
        &result.raw_features, &state->normalization, &result.normalized_features);
    if (dsp_status != FIN_DSP_OK) {
        return FIN_VOICE_PIPELINE_ERROR_DSP;
    }

    vq_status = fin_vq_quantize_validated(
        result.normalized_features.values,
        FIN_VOICE_FEATURE_DIM,
        &state->vq_codebook,
        &result.quantization);
    if (vq_status != FIN_VQ_OK) {
        return FIN_VOICE_PIPELINE_ERROR_VQ;
    }

    result.language = state->active_language;
    *out_result = result;
    return FIN_VOICE_PIPELINE_OK;
}

static FinVoiceEventLanguage fin_voice_pipeline_event_language(
    FinVoiceLanguage language)
{
    switch (language) {
    case FIN_VOICE_LANGUAGE_EN:
        return FIN_VOICE_EVENT_LANGUAGE_EN;
    case FIN_VOICE_LANGUAGE_FR:
        return FIN_VOICE_EVENT_LANGUAGE_FR;
    case FIN_VOICE_LANGUAGE_HI:
        return FIN_VOICE_EVENT_LANGUAGE_HI;
    default:
        return FIN_VOICE_EVENT_LANGUAGE_UNKNOWN;
    }
}

static uint32_t fin_voice_pipeline_next_sequence(FinVoicePipelineInternal *state)
{
    const uint32_t sequence = state->next_sequence_id;
    ++state->next_sequence_id; /* Wrap is defined for uint32_t and is ABI-safe. */
    return sequence;
}

static FinVoicePipelineStatus fin_voice_pipeline_push_event(
    FinVoicePipelineInternal *state,
    const FinVoiceEvent *event)
{
    const FinVoiceEventQueueStatus status =
        fin_voice_event_queue_push(state->event_queue, event);
    if (status == FIN_VOICE_EVENT_QUEUE_OK) {
        return FIN_VOICE_PIPELINE_OK;
    }
    if (status == FIN_VOICE_EVENT_QUEUE_ERROR_FULL) {
        return FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE_FULL;
    }
    return FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE;
}

FinVoicePipelineStatus fin_voice_pipeline_publish_frame_events(
    FinVoicePipeline *pipeline,
    const FinVoicePipelineFrameResult *result,
    uint64_t timestamp_ns,
    uint64_t sample_position)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceEvent event = {0};
    FinVoicePipelineStatus status;

    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    if (result == NULL || result->language == FIN_VOICE_LANGUAGE_UNKNOWN ||
        result->language > FIN_VOICE_LANGUAGE_HI ||
        result->quantization.best_index > UINT32_MAX ||
        !isfinite(result->quantization.best_distance_squared) ||
        result->quantization.best_distance_squared < 0.0f ||
        !isfinite(result->quantization.relative_gap) ||
        result->quantization.relative_gap < 0.0f ||
        result->quantization.relative_gap > 1.0f ||
        fin_voice_dsp_validate_features(&result->normalized_features) != FIN_DSP_OK) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }

    event.timestamp_ns = timestamp_ns;
    event.sample_position = sample_position;
    event.sequence_id = fin_voice_pipeline_next_sequence(state);
    event.type = (uint16_t)FIN_VOICE_EVENT_FEATURES_READY;
    event.payload.features.dimension = FIN_VOICE_EVENT_FEATURE_DIMENSION;
    memcpy(event.payload.features.values, result->normalized_features.values,
           sizeof(event.payload.features.values));
    status = fin_voice_pipeline_push_event(state, &event);
    if (status != FIN_VOICE_PIPELINE_OK) {
        return status;
    }

    memset(&event, 0, sizeof(event));
    event.timestamp_ns = timestamp_ns;
    event.sample_position = sample_position;
    event.sequence_id = fin_voice_pipeline_next_sequence(state);
    event.type = (uint16_t)FIN_VOICE_EVENT_QUANTIZATION_RESULT;
    event.payload.quantization.best_index = (uint32_t)result->quantization.best_index;
    event.payload.quantization.second_best_index =
        result->quantization.second_best_index == FIN_VQ_NO_INDEX ||
        result->quantization.second_best_index > UINT32_MAX
            ? FIN_VOICE_EVENT_NO_CENTROID
            : (uint32_t)result->quantization.second_best_index;
    event.payload.quantization.best_distance_squared =
        result->quantization.best_distance_squared;
    event.payload.quantization.second_distance_squared =
        result->quantization.second_best_distance_squared;
    event.payload.quantization.relative_gap = result->quantization.relative_gap;
    event.payload.quantization.language =
        (uint8_t)fin_voice_pipeline_event_language(result->language);
    status = fin_voice_pipeline_push_event(state, &event);
    return status;
}

FinVoicePipelineStatus fin_voice_pipeline_append_recognized_text(
    FinVoicePipeline *pipeline,
    const char *utf8,
    size_t byte_length)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceTextBuilderStatus status;
    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    status = fin_voice_text_builder_append(state->text_builder, utf8, byte_length);
    if (status == FIN_VOICE_TEXT_BUILDER_OK) {
        return FIN_VOICE_PIPELINE_OK;
    }
    if (status == FIN_VOICE_TEXT_BUILDER_OVERFLOW) {
        return FIN_VOICE_PIPELINE_OVERFLOW;
    }
    return FIN_VOICE_PIPELINE_ERROR_TEXT_BUILDER;
}

FinVoicePipelineStatus fin_voice_pipeline_flush_recognized_text(
    FinVoicePipeline *pipeline,
    char *destination,
    size_t destination_capacity,
    size_t *out_byte_length)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceTextBuilderStatus status;
    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    status = fin_voice_text_builder_flush(
        state->text_builder, destination, destination_capacity, out_byte_length);
    if (status == FIN_VOICE_TEXT_BUILDER_OK) {
        return FIN_VOICE_PIPELINE_OK;
    }
    if (status == FIN_VOICE_TEXT_BUILDER_ERROR_OUTPUT_TOO_SMALL) {
        return FIN_VOICE_PIPELINE_ERROR_TEXT_OUTPUT_TOO_SMALL;
    }
    return FIN_VOICE_PIPELINE_ERROR_TEXT_BUILDER;
}

FinVoicePipelineStatus fin_voice_pipeline_publish_text_event(
    FinVoicePipeline *pipeline,
    const char *utf8,
    size_t byte_length,
    uint64_t timestamp_ns,
    uint64_t sample_position,
    bool is_final)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceEvent event = {0};

    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }
    if (utf8 == NULL || byte_length == 0u) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    if (byte_length >= (size_t)FIN_VOICE_EVENT_TEXT_CAPACITY ||
        byte_length > UINT16_MAX) {
        return FIN_VOICE_PIPELINE_ERROR_TEXT_EVENT_TOO_LARGE;
    }

    event.timestamp_ns = timestamp_ns;
    event.sample_position = sample_position;
    event.sequence_id = fin_voice_pipeline_next_sequence(state);
    event.type = (uint16_t)FIN_VOICE_EVENT_TEXT_READY;
    event.flags = is_final ? FIN_VOICE_EVENT_FLAG_FINAL : FIN_VOICE_EVENT_FLAG_NONE;
    event.payload.text.length = (uint16_t)byte_length;
    memcpy(event.payload.text.utf8, utf8, byte_length);
    event.payload.text.utf8[byte_length] = '\0';

    if (fin_voice_event_validate(&event) != FIN_VOICE_EVENT_QUEUE_OK) {
        return FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT;
    }
    return fin_voice_pipeline_push_event(state, &event);
}

FinVoicePipelineStatus fin_voice_pipeline_reset(FinVoicePipeline *pipeline)
{
    FinVoicePipelineInternal *state = fin_voice_pipeline_state(pipeline);
    FinVoiceEventQueueStatus queue_status;
    FinVoiceTextBuilderStatus text_status;
    FinVoiceLanguageFsmStatus language_status;
    FinVoiceDspStatus dsp_status;

    if (state == NULL) {
        return pipeline == NULL
            ? FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT
            : FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED;
    }

    /* Contract: producer and consumer are quiescent before any reset begins. */
    queue_status = fin_voice_event_queue_reset(state->event_queue);
    if (queue_status != FIN_VOICE_EVENT_QUEUE_OK) {
        return FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE;
    }
    text_status = fin_voice_text_builder_reset(state->text_builder);
    if (text_status != FIN_VOICE_TEXT_BUILDER_OK) {
        return FIN_VOICE_PIPELINE_ERROR_TEXT_BUILDER;
    }
    language_status = fin_voice_language_fsm_reset(state->language_fsm);
    if (language_status != FIN_VOICE_LANGUAGE_FSM_OK) {
        return FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM;
    }
    dsp_status = fin_voice_dsp_reset(state->dsp);
    if (dsp_status != FIN_DSP_OK) {
        return FIN_VOICE_PIPELINE_ERROR_DSP;
    }

    fin_voice_pipeline_clear_codebook(state);
    state->next_sequence_id = 1u;
    return FIN_VOICE_PIPELINE_OK;
}

const char *fin_voice_pipeline_status_name(FinVoicePipelineStatus status)
{
    switch (status) {
    case FIN_VOICE_PIPELINE_OK: return "ok";
    case FIN_VOICE_PIPELINE_OVERFLOW: return "overflow";
    case FIN_VOICE_PIPELINE_ERROR_INVALID_ARGUMENT: return "invalid_argument";
    case FIN_VOICE_PIPELINE_ERROR_NOT_INITIALIZED: return "not_initialized";
    case FIN_VOICE_PIPELINE_ERROR_WORKSPACE_TOO_SMALL: return "workspace_too_small";
    case FIN_VOICE_PIPELINE_ERROR_COMPONENT_NOT_READY: return "component_not_ready";
    case FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE: return "unsupported_language";
    case FIN_VOICE_PIPELINE_ERROR_LANGUAGE_UNCONFIRMED: return "language_unconfirmed";
    case FIN_VOICE_PIPELINE_ERROR_DSP: return "dsp_error";
    case FIN_VOICE_PIPELINE_ERROR_VQ: return "vq_error";
    case FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM: return "language_fsm_error";
    case FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE: return "event_queue_error";
    case FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE_FULL: return "event_queue_full";
    case FIN_VOICE_PIPELINE_ERROR_TEXT_BUILDER: return "text_builder_error";
    case FIN_VOICE_PIPELINE_ERROR_TEXT_OUTPUT_TOO_SMALL: return "text_output_too_small";
    case FIN_VOICE_PIPELINE_ERROR_TEXT_EVENT_TOO_LARGE: return "text_event_too_large";
    default: return "unknown_status";
    }
}

uint32_t fin_voice_pipeline_abi_version(void)
{
    return (FIN_VOICE_PIPELINE_ABI_MAJOR << 16u) | FIN_VOICE_PIPELINE_ABI_MINOR;
}
