
#include "fin_voice_ffi_shim.h"

#include <stddef.h>

#include "acoustic_dsp.h"
#include "vq_quantize.h"

int32_t fin_voice_ffi_get_abi_info(FinVoiceFfiAbiInfo *out_info)
{
    if (out_info == NULL) {
        return -1;
    }

    out_info->struct_size = (uint32_t)sizeof(*out_info);
    out_info->dsp_abi_version = fin_voice_dsp_abi_version();
    out_info->vq_abi_version = fin_vq_abi_version();
    out_info->feature_dimension = FIN_VOICE_FEATURE_DIM;
    out_info->sample_rate_hz = FIN_VOICE_SAMPLE_RATE_HZ;
    out_info->frame_samples = FIN_VOICE_FRAME_SAMPLES;
    out_info->hop_samples = FIN_VOICE_HOP_SAMPLES;

    return 0;
}
