#ifndef FINTALLY_VOICE_FFI_SHIM_H
#define FINTALLY_VOICE_FFI_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable, small metadata record used by Rust to smoke-test the linked native
 * archive and check that the C header ABI matches the compiled implementation.
 */
typedef struct FinVoiceFfiAbiInfo {
    uint32_t struct_size;
    uint32_t dsp_abi_version;
    uint32_t vq_abi_version;
    uint32_t feature_dimension;
    uint32_t sample_rate_hz;
    uint32_t frame_samples;
    uint32_t hop_samples;
} FinVoiceFfiAbiInfo;

/* Returns 0 on success, -1 when out_info is NULL. */
int32_t fin_voice_ffi_get_abi_info(FinVoiceFfiAbiInfo *out_info);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FINTALLY_VOICE_FFI_SHIM_H */
