#ifndef FIN_VOICE_CODEBOOK_ZH_H
#define FIN_VOICE_CODEBOOK_ZH_H

#include "codebooks/fin_voice_codebook_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return the generated Chinese codebook view, or NULL until the exporter
 * contains a Chinese-language entry. No substitute codebook is provided.
 */
const FinVoiceCodebookView *fin_voice_codebook_zh_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_CODEBOOK_ZH_H */
