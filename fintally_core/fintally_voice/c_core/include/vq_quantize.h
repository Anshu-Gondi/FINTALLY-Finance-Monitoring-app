#ifndef FINTALLY_VOICE_VQ_QUANTIZE_H
#define FINTALLY_VOICE_VQ_QUANTIZE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#error "Fintally Voice VQ is a C-only interface."
#endif

/*
 * Fintally Voice — FP32 nearest-centroid vector quantization.
 *
 * Centroids are row-major FP32 values:
 *     centroids[centroid_index * dimension + feature_index]
 *
 * Input vectors and centroids MUST be in the same feature space. For the
 * canonical acoustic vector, normalize features using the same statistics
 * used to train the codebook before quantizing. Do not quantize a raw mixed-
 * scale vector (MFCC/LPC/F0/formants/centroid) against normalized centroids.
 *
 * This module does not own or allocate codebook memory. Validate a codebook
 * when loading/initializing it, then call fin_vq_quantize_validated() on the
 * runtime hot path while the codebook remains immutable.
 */
#define FIN_VQ_ABI_MAJOR 1u
#define FIN_VQ_ABI_MINOR 0u
#define FIN_VQ_NO_INDEX SIZE_MAX

typedef enum FinVqStatus {
    FIN_VQ_OK = 0,
    FIN_VQ_ERROR_INVALID_ARGUMENT = 1,
    FIN_VQ_ERROR_INVALID_CODEBOOK = 2,
    FIN_VQ_ERROR_DIMENSION_MISMATCH = 3,
    FIN_VQ_ERROR_NUMERIC_FAILURE = 4,
    FIN_VQ_ERROR_SIZE_OVERFLOW = 5
} FinVqStatus;

typedef struct FinVqCodebook {
    const float *centroids;
    size_t centroid_count;
    size_t dimension;
} FinVqCodebook;

typedef struct FinVqResult {
    size_t best_index;
    size_t second_best_index;
    float best_distance_squared;
    float second_best_distance_squared;
    /* Relative distance gap in [0, 1], NOT a probability/confidence score. */
    float relative_gap;
} FinVqResult;

/* Validate shape, multiplication overflow, and every centroid element. */
FinVqStatus fin_vq_validate_codebook(const FinVqCodebook *codebook);

/* Safe convenience API: validates the codebook before searching. */
FinVqStatus fin_vq_quantize(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result
);

/*
 * Hot-path API: skips the full codebook scan. The caller MUST have called
 * fin_vq_validate_codebook() for this exact codebook and must not mutate its
 * centroid data/shape afterward. Runtime still rejects non-finite inputs and
 * detects distance overflow. Ties deterministically prefer the lower index.
 */
FinVqStatus fin_vq_quantize_validated(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result
);

/* Fixed canonical feature-vector convenience function (dimension must be 32). */
FinVqStatus fin_vq_quantize_features32(
    const float features[32],
    const FinVqCodebook *codebook,
    FinVqResult *result
);

const char *fin_vq_status_name(FinVqStatus status);
uint32_t fin_vq_abi_version(void);

#endif /* FINTALLY_VOICE_VQ_QUANTIZE_H */
