#ifndef FIN_VOICE_CODEBOOK_EN_H
#define FIN_VOICE_CODEBOOK_EN_H

#include "codebooks/fin_voice_codebook_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return the generated English codebook view, or NULL if the generated bundle
 * does not contain English data. The returned view and centroid memory are
 * immutable, statically allocated, and owned by generated_codebook_data.c.
 */
const FinVoiceCodebookView *fin_voice_codebook_en_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_CODEBOOK_EN_H */
