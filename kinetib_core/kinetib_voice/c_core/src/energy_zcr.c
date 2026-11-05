#include "energy_zcr.h"
#include "macros/cache_alignment.h"

#include <math.h>
#include <stddef.h>

/*
 * Why these tiles are intentionally small:
 *
 * The canonical audio frame is only 320 floats (1.25 KiB), and the canonical
 * 512-point FFT power spectrum is 257 floats (just over 1 KiB). This module
 * doesn't own or copy either array, so aligning or padding its public result
 * structures would add ABI cost without reducing memory traffic. The loops
 * use bounded chunks to keep the execution pattern explicit and to provide a
 * stable unit for later measured optimization. The accumulators remain in the
 * same ascending input order to avoid needless changes to FP32 rounding.
 *
 * The 100 KiB budget is a software target, not a hardware-cache guarantee.
 * A cache line's alignment does not force data to stay in L1 or avoid L2.
 *
 * Private result staging is explicitly aligned, but public result structs stay
 * naturally aligned for Rust/C ABI compatibility. The combined scratch object
 * is a single cache-line-sized-or-smaller record, avoiding multiple separately
 * aligned locals and unnecessary stack padding. Input arrays are never copied.
 */
typedef struct FinEnergyZcrCombinedScratch {
    FinEnergyZcrTimeFeatures time;
    FinEnergyZcrSpectralFeatures spectral;
    FinEnergyZcrFeatures combined;
} FinEnergyZcrCombinedScratch;

_Static_assert(sizeof(FinEnergyZcrCombinedScratch) <= FIN_VOICE_CACHE_LINE_BYTES,
    "Combined Energy/ZCR staging must fit within one cache line.");

_Static_assert(
    (FIN_ENERGY_ZCR_TIME_TILE_SAMPLES %
        (FIN_VOICE_CACHE_LINE_BYTES / sizeof(float))) == 0u,
    "Time-domain tile should be a multiple of 64 bytes.");

_Static_assert(
    (FIN_ENERGY_ZCR_SPECTRUM_TILE_BINS %
        (FIN_VOICE_CACHE_LINE_BYTES / sizeof(float))) == 0u,
    "Spectral tile should be a multiple of 64 bytes.");

_Static_assert(
    (sizeof(float) * (size_t)FIN_ENERGY_ZCR_TIME_TILE_SAMPLES) +
        (sizeof(float) * (size_t)FIN_ENERGY_ZCR_SPECTRUM_TILE_BINS) <=
        (size_t)FIN_ENERGY_ZCR_WORKING_SET_BUDGET_BYTES,
    "Energy/ZCR processing tiles must fit within the software working-set budget."
);

static int fin_energy_zcr_is_finite(float value)
{
    return isfinite(value) ? 1 : 0;
}

static int fin_energy_zcr_valid_epsilon(float epsilon)
{
    return fin_energy_zcr_is_finite(epsilon) && epsilon > 0.0f;
}

FinEnergyZcrStatus fin_energy_zcr_compute_time_domain(
    const float *frame,
    size_t frame_samples,
    float epsilon,
    FinEnergyZcrTimeFeatures *output
)
{
    FIN_VOICE_CACHE_ALIGN FinEnergyZcrTimeFeatures result = {0.0f, 0.0f};
    float energy_sum = 0.0f;
    float previous_sample = 0.0f;
    size_t crossings = 0u;
    size_t tile_start = 0u;

    if (frame == NULL || output == NULL) {
        return FIN_ENERGY_ZCR_ERROR_INVALID_ARGUMENT;
    }
    if (frame_samples == 0u || !fin_energy_zcr_valid_epsilon(epsilon)) {
        return FIN_ENERGY_ZCR_ERROR_INVALID_CONFIG;
    }

    /*
     * Process tiles without copying samples into temporary storage. Carry
     * previous_sample across tile boundaries so a sign change between the
     * final sample of one tile and the first of the next is not lost.
     */
    while (tile_start < frame_samples) {
        const size_t remaining = frame_samples - tile_start;
        const size_t tile_count =
            remaining < (size_t)FIN_ENERGY_ZCR_TIME_TILE_SAMPLES
                ? remaining
                : (size_t)FIN_ENERGY_ZCR_TIME_TILE_SAMPLES;
        const size_t tile_end = tile_start + tile_count;
        size_t i;

        for (i = tile_start; i < tile_end; ++i) {
            const float sample = frame[i];

            if (!fin_energy_zcr_is_finite(sample)) {
                return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
            }

            energy_sum += sample * sample;
            if (!fin_energy_zcr_is_finite(energy_sum)) {
                return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
            }

            if (i > 0u) {
                const int crossed =
                    (previous_sample < 0.0f && sample >= 0.0f) ||
                    (previous_sample >= 0.0f && sample < 0.0f);
                crossings += (size_t)crossed;
            }

            previous_sample = sample;
        }

        tile_start = tile_end;
    }

    {
        const float mean_square = energy_sum / (float)frame_samples;
        result.log_energy = logf(fmaxf(mean_square, epsilon));
        result.zero_crossing_rate = frame_samples > 1u
            ? (float)crossings / (float)(frame_samples - 1u)
            : 0.0f;
    }

    if (!fin_energy_zcr_is_finite(result.log_energy) ||
        !fin_energy_zcr_is_finite(result.zero_crossing_rate)) {
        return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
    }

    *output = result;
    return FIN_ENERGY_ZCR_OK;
}

FinEnergyZcrStatus fin_energy_zcr_compute_spectral(
    const float *power_spectrum,
    size_t spectrum_bins,
    size_t fft_size,
    uint32_t sample_rate_hz,
    float epsilon,
    FinEnergyZcrSpectralFeatures *output
)
{
    FIN_VOICE_CACHE_ALIGN FinEnergyZcrSpectralFeatures result = {0.0f, 0.0f};
    float total_power = 0.0f;
    float weighted_frequency = 0.0f;
    size_t tile_start = 0u;

    if (power_spectrum == NULL || output == NULL) {
        return FIN_ENERGY_ZCR_ERROR_INVALID_ARGUMENT;
    }
    if (fft_size < 2u || spectrum_bins != fft_size / 2u + 1u ||
        sample_rate_hz == 0u || !fin_energy_zcr_valid_epsilon(epsilon)) {
        return FIN_ENERGY_ZCR_ERROR_INVALID_CONFIG;
    }

    /*
     * Iterate spectrum tiles in ascending bin order. No temporary spectrum
     * copy is created, and both accumulators retain their original summation
     * order. The centroid is computed in Hz using the original FFT size, not
     * the number of bins in the final (possibly partial) tile.
     */
    while (tile_start < spectrum_bins) {
        const size_t remaining = spectrum_bins - tile_start;
        const size_t tile_count =
            remaining < (size_t)FIN_ENERGY_ZCR_SPECTRUM_TILE_BINS
                ? remaining
                : (size_t)FIN_ENERGY_ZCR_SPECTRUM_TILE_BINS;
        const size_t tile_end = tile_start + tile_count;
        size_t k;

        for (k = tile_start; k < tile_end; ++k) {
            const float power = power_spectrum[k];
            float frequency_hz;

            if (!fin_energy_zcr_is_finite(power) || power < 0.0f) {
                return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
            }

            frequency_hz =
                (float)k * (float)sample_rate_hz / (float)fft_size;
            total_power += power;
            weighted_frequency += power * frequency_hz;

            if (!fin_energy_zcr_is_finite(total_power) ||
                !fin_energy_zcr_is_finite(weighted_frequency)) {
                return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
            }
        }

        tile_start = tile_end;
    }

    result.spectral_energy = total_power / (float)spectrum_bins;
    result.spectral_centroid_hz =
        weighted_frequency / fmaxf(total_power, epsilon);

    if (!fin_energy_zcr_is_finite(result.spectral_energy) ||
        !fin_energy_zcr_is_finite(result.spectral_centroid_hz)) {
        return FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE;
    }

    *output = result;
    return FIN_ENERGY_ZCR_OK;
}

FinEnergyZcrStatus fin_energy_zcr_compute(
    const float *frame,
    size_t frame_samples,
    const float *power_spectrum,
    size_t spectrum_bins,
    size_t fft_size,
    uint32_t sample_rate_hz,
    float epsilon,
    FinEnergyZcrFeatures *output
)
{
    /*
     * Align one private staging bundle rather than each small result separately.
     * The bundle fits in one cache line, does not alter public ABI types, and
     * lets us commit all four output values only after both calculations succeed.
     */
    FIN_VOICE_CACHE_ALIGN FinEnergyZcrCombinedScratch scratch;
    FinEnergyZcrStatus status;

    if (output == NULL) {
        return FIN_ENERGY_ZCR_ERROR_INVALID_ARGUMENT;
    }

    status = fin_energy_zcr_compute_time_domain(
        frame, frame_samples, epsilon, &scratch.time
    );
    if (status != FIN_ENERGY_ZCR_OK) {
        return status;
    }

    status = fin_energy_zcr_compute_spectral(
        power_spectrum,
        spectrum_bins,
        fft_size,
        sample_rate_hz,
        epsilon,
        &scratch.spectral
    );
    if (status != FIN_ENERGY_ZCR_OK) {
        return status;
    }

    /* Commit atomically at the API level: never leave partially updated output. */
    scratch.combined.log_energy = scratch.time.log_energy;
    scratch.combined.zero_crossing_rate = scratch.time.zero_crossing_rate;
    scratch.combined.spectral_energy = scratch.spectral.spectral_energy;
    scratch.combined.spectral_centroid_hz =
        scratch.spectral.spectral_centroid_hz;
    *output = scratch.combined;

    return FIN_ENERGY_ZCR_OK;
}
