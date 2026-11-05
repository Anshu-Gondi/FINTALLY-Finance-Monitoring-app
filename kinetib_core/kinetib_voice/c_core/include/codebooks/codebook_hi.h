#ifndef FIN_VOICE_CODEBOOK_HI_H
#define FIN_VOICE_CODEBOOK_HI_H

#include "codebooks/fin_voice_codebook_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the generated Hindi codebook view, or NULL when it is unavailable. */
const FinVoiceCodebookView *fin_voice_codebook_hi_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_CODEBOOK_HI_H */
