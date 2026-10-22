#ifndef FIN_VOICE_BUILDER_FFI_H
#define FIN_VOICE_BUILDER_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#error "FinTally Voice offline-builder bridge is a C-only ABI."
#endif

/*
 * Narrow, stable C ABI for the Rust offline_builder.
 *
 * The native FinVoiceDspContext and its alignment-sensitive fields remain
 * private to the bridge. Create one handle per concurrent worker; a handle is
 * mutable state and must not be used concurrently by multiple threads.
 * Allocation occurs only in create/destroy, never in frame extraction.
 */
typedef struct FinVoiceBuilderDsp FinVoiceBuilderDsp;

#define FIN_VOICE_BUILDER_FRAME_SAMPLES 320u
#define FIN_VOICE_BUILDER_FEATURE_DIMENSION 32u
#define FIN_VOICE_BUILDER_CREATE_STATUS_ALLOCATION_FAILURE (-1001)

/*
 * Create the DSP context with the canonical default configuration and AUTO
 * backend. On success *out_status is zero. On allocation failure it is
 * FIN_VOICE_BUILDER_CREATE_STATUS_ALLOCATION_FAILURE. Otherwise it contains
 * the underlying FinVoiceDspStatus returned by native initialization.
 */
FinVoiceBuilderDsp *fin_voice_builder_dsp_create(int32_t *out_status);

/* Accept NULL as a no-op. Frees only resources allocated by create(). */
void fin_voice_builder_dsp_destroy(FinVoiceBuilderDsp *dsp);

/*
 * Extract exactly one canonical feature vector from a 320-sample frame.
 * Return values are FinVoiceDspStatus integers from acoustic_dsp.h.
 * `output` is modified only on success. `output_count` must be at least 32.
 */
int32_t fin_voice_builder_dsp_extract_f32(
    FinVoiceBuilderDsp *dsp,
    const float *samples,
    size_t sample_count,
    float *output,
    size_t output_count
);

int32_t fin_voice_builder_dsp_extract_pcm16(
    FinVoiceBuilderDsp *dsp,
    const int16_t *samples,
    size_t sample_count,
    float *output,
    size_t output_count
);

uint32_t fin_voice_builder_dsp_abi_version(void);
size_t fin_voice_builder_dsp_workspace_bytes(const FinVoiceBuilderDsp *dsp);
int32_t fin_voice_builder_dsp_active_backend(const FinVoiceBuilderDsp *dsp);
const char *fin_voice_builder_dsp_backend_name(const FinVoiceBuilderDsp *dsp);

#endif /* FIN_VOICE_BUILDER_FFI_H */
