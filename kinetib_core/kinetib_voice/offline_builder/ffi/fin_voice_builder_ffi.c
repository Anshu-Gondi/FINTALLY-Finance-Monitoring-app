#include "fin_voice_builder_ffi.h"

#include "acoustic_dsp.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(
    FIN_VOICE_BUILDER_FRAME_SAMPLES == FIN_VOICE_FRAME_SAMPLES,
    "Rust bridge frame size must match the canonical acoustic DSP ABI"
);
_Static_assert(
    FIN_VOICE_BUILDER_FEATURE_DIMENSION == FIN_VOICE_FEATURE_DIM,
    "Rust bridge feature dimension must match the canonical acoustic DSP ABI"
);

struct FinVoiceBuilderDsp {
    FinVoiceDspContext context;
    void *workspace;
    size_t workspace_size;
};

FinVoiceBuilderDsp *fin_voice_builder_dsp_create(int32_t *out_status)
{
    FinVoiceBuilderDsp *dsp;
    FinVoiceDspConfig config;
    FinVoiceDspStatus status;
    size_t workspace_size;

    if (out_status == NULL) {
        return NULL;
    }
    *out_status = (int32_t)FIN_DSP_ERROR_INVALID_ARGUMENT;

    dsp = (FinVoiceBuilderDsp *)calloc(1u, sizeof(*dsp));
    if (dsp == NULL) {
        *out_status = FIN_VOICE_BUILDER_CREATE_STATUS_ALLOCATION_FAILURE;
        return NULL;
    }

    fin_voice_dsp_default_config(&config);
    config.simd_backend = FIN_SIMD_AUTO;

    workspace_size = fin_voice_dsp_workspace_size(&config);
    if (workspace_size == 0u) {
        free(dsp);
        *out_status = (int32_t)FIN_DSP_ERROR_WORKSPACE_TOO_SMALL;
        return NULL;
    }

    dsp->workspace = malloc(workspace_size);
    if (dsp->workspace == NULL) {
        free(dsp);
        *out_status = FIN_VOICE_BUILDER_CREATE_STATUS_ALLOCATION_FAILURE;
        return NULL;
    }
    dsp->workspace_size = workspace_size;

    status = fin_voice_dsp_init(
        &dsp->context,
        &config,
        dsp->workspace,
        dsp->workspace_size
    );
    if (status != FIN_DSP_OK) {
        free(dsp->workspace);
        free(dsp);
        *out_status = (int32_t)status;
        return NULL;
    }

    *out_status = (int32_t)FIN_DSP_OK;
    return dsp;
}

void fin_voice_builder_dsp_destroy(FinVoiceBuilderDsp *dsp)
{
    if (dsp == NULL) {
        return;
    }

    free(dsp->workspace);
    dsp->workspace = NULL;
    dsp->workspace_size = 0u;
    memset(&dsp->context, 0, sizeof(dsp->context));
    free(dsp);
}

int32_t fin_voice_builder_dsp_extract_f32(
    FinVoiceBuilderDsp *dsp,
    const float *samples,
    size_t sample_count,
    float *output,
    size_t output_count
)
{
    FinVoiceFeatureVector features = {{0.0f}};
    FinVoiceDspStatus status;

    if (dsp == NULL || samples == NULL || output == NULL) {
        return (int32_t)FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (sample_count != FIN_VOICE_FRAME_SAMPLES ||
        output_count < FIN_VOICE_FEATURE_DIM) {
        return (int32_t)FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    status = fin_voice_dsp_extract_features_f32(
        &dsp->context,
        samples,
        sample_count,
        &features
    );
    if (status != FIN_DSP_OK) {
        return (int32_t)status;
    }

    /* Commit only after the C extractor succeeds; failed calls leave output intact. */
    memcpy(output, features.values, sizeof(features.values));
    return (int32_t)FIN_DSP_OK;
}

int32_t fin_voice_builder_dsp_extract_pcm16(
    FinVoiceBuilderDsp *dsp,
    const int16_t *samples,
    size_t sample_count,
    float *output,
    size_t output_count
)
{
    FinVoiceFeatureVector features = {{0.0f}};
    FinVoiceDspStatus status;

    if (dsp == NULL || samples == NULL || output == NULL) {
        return (int32_t)FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (sample_count != FIN_VOICE_FRAME_SAMPLES ||
        output_count < FIN_VOICE_FEATURE_DIM) {
        return (int32_t)FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    status = fin_voice_dsp_extract_features(
        &dsp->context,
        samples,
        sample_count,
        &features
    );
    if (status != FIN_DSP_OK) {
        return (int32_t)status;
    }

    memcpy(output, features.values, sizeof(features.values));
    return (int32_t)FIN_DSP_OK;
}

uint32_t fin_voice_builder_dsp_abi_version(void)
{
    return fin_voice_dsp_abi_version();
}

size_t fin_voice_builder_dsp_workspace_bytes(const FinVoiceBuilderDsp *dsp)
{
    return dsp == NULL ? 0u : dsp->workspace_size;
}

int32_t fin_voice_builder_dsp_active_backend(const FinVoiceBuilderDsp *dsp)
{
    if (dsp == NULL) {
        return -1;
    }
    return (int32_t)fin_voice_dsp_active_backend(&dsp->context);
}

const char *fin_voice_builder_dsp_backend_name(const FinVoiceBuilderDsp *dsp)
{
    if (dsp == NULL) {
        return "unknown";
    }
    return fin_voice_dsp_backend_name(
        fin_voice_dsp_active_backend(&dsp->context)
    );
}
