#ifndef FIN_VOICE_CODEBOOK_REGISTRY_H
#define FIN_VOICE_CODEBOOK_REGISTRY_H

#include <stddef.h>

#include "codebooks/fin_voice_codebook_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FinVoiceCodebookRegistryStatus {
    FIN_VOICE_CODEBOOK_REGISTRY_OK = 0,
    FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_ARGUMENT = 1,
    FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_LANGUAGE_TAG = 2,
    FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE = 3,
    FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_CODEBOOK = 4,
    FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_NORMALIZATION = 5
} FinVoiceCodebookRegistryStatus;

/* A non-owning view over immutable exporter-generated memory. */
typedef struct FinVoiceCodebookRegistryEntry {
    const FinVoiceCodebookView *codebook;
    const float *normalization_mean;
    const float *normalization_inv_std;
} FinVoiceCodebookRegistryEntry;

/*
 * Resolve a two-letter language code or locale tag such as en, EN-us, fr_FR,
 * hi-IN, or zh-CN. Matching uses ASCII only and does not allocate memory.
 *
 * Normalization must occur before VQ, using:
 *   normalized[i] = (feature[i] - normalization_mean[i])
 *                  * normalization_inv_std[i]
 * The features and statistics must match the training feature schema.
 *
 * A syntactically valid language with no exported codebook returns
 * FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE. On failure, a
 * non-NULL out_entry is cleared to three NULL pointers.
 */
FinVoiceCodebookRegistryStatus fin_voice_codebook_registry_lookup(
    const char *language_tag,
    FinVoiceCodebookRegistryEntry *out_entry);

/* Counts only languages for which real generated data is present. */
size_t fin_voice_codebook_registry_supported_count(void);

const char *fin_voice_codebook_registry_status_name(
    FinVoiceCodebookRegistryStatus status);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_CODEBOOK_REGISTRY_H */
