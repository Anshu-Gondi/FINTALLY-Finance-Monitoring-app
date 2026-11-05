#ifndef FIN_VOICE_LANGUAGE_FSM_H
#define FIN_VOICE_LANGUAGE_FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIN_VOICE_LANGUAGE_FSM_ABI_MAJOR 1u
#define FIN_VOICE_LANGUAGE_FSM_ABI_MINOR 0u

/*
 * Languages supported by the current trained/exported codebook bundle.
 * Chinese is intentionally absent from this enum until its training/export
 * contract produces validated codebook data.
 */
typedef enum FinVoiceLanguage {
    FIN_VOICE_LANGUAGE_UNKNOWN = 0,
    FIN_VOICE_LANGUAGE_EN = 1,
    FIN_VOICE_LANGUAGE_FR = 2,
    FIN_VOICE_LANGUAGE_HI = 3
} FinVoiceLanguage;

typedef enum FinVoiceLanguageFsmState {
    FIN_VOICE_LANGUAGE_FSM_UNKNOWN = 0,
    FIN_VOICE_LANGUAGE_FSM_CANDIDATE = 1,
    FIN_VOICE_LANGUAGE_FSM_CONFIRMED = 2,
    FIN_VOICE_LANGUAGE_FSM_UNCERTAIN = 3
} FinVoiceLanguageFsmState;

typedef enum FinVoiceLanguageFsmStatus {
    FIN_VOICE_LANGUAGE_FSM_OK = 0,
    FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT = 1,
    FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG = 2,
    FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE = 3,
    FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE = 4,
    FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG = 5
} FinVoiceLanguageFsmStatus;

/*
 * Comparative confidence values from an upstream language-identification
 * component. Each value must be finite and within [0, 1]. Values must be
 * comparable across en/fr/hi and calibrated/evaluated on labelled examples.
 * These are NOT nearest-centroid distances, and this module does not infer
 * language from a single vector-quantization result.
 */
typedef struct FinVoiceLanguageEvidence {
    float en_confidence;
    float fr_confidence;
    float hi_confidence;
    uint64_t sample_position;
} FinVoiceLanguageEvidence;

typedef struct FinVoiceLanguageFsmConfig {
    float minimum_confidence;
    float minimum_margin;
    uint32_t consecutive_confirmations;
} FinVoiceLanguageFsmConfig;

/*
 * Caller-owned, allocation-free state. A single thread must own and mutate
 * an instance. This structure is intentionally not atomic/thread-safe.
 */
typedef struct FinVoiceLanguageFsm {
    FinVoiceLanguageFsmConfig config;
    FinVoiceLanguageFsmState state;
    FinVoiceLanguage language;
    FinVoiceLanguage candidate_language;
    uint32_t candidate_streak;
    uint64_t accepted_evidence_count;
    uint64_t transition_count;
    uint64_t last_sample_position;
    float last_best_confidence;
    float last_margin;
} FinVoiceLanguageFsm;

typedef struct FinVoiceLanguageFsmSnapshot {
    FinVoiceLanguageFsmState state;
    /* UNKNOWN unless state == CONFIRMED. */
    FinVoiceLanguage language;
    /* UNKNOWN unless state == CANDIDATE. */
    FinVoiceLanguage candidate_language;
    uint32_t candidate_streak;
    uint32_t confirmations_required;
    uint64_t accepted_evidence_count;
    uint64_t transition_count;
    uint64_t last_sample_position;
    float last_best_confidence;
    float last_margin;
} FinVoiceLanguageFsmSnapshot;

/* Reference defaults only; tune via labelled validation before deployment. */
void fin_voice_language_fsm_config_default(FinVoiceLanguageFsmConfig *config);

/* A NULL config selects the documented reference defaults. */
FinVoiceLanguageFsmStatus fin_voice_language_fsm_init(
    FinVoiceLanguageFsm *fsm,
    const FinVoiceLanguageFsmConfig *config);

/*
 * Process one independent evidence frame. Low confidence or insufficient
 * best-vs-runner-up margin moves to UNCERTAIN and clears the selected
 * language. A language switch is not published until it is confirmed again.
 * Invalid evidence returns an error without modifying the FSM.
 */
FinVoiceLanguageFsmStatus fin_voice_language_fsm_update(
    FinVoiceLanguageFsm *fsm,
    const FinVoiceLanguageEvidence *evidence,
    FinVoiceLanguageFsmSnapshot *out_snapshot);

FinVoiceLanguageFsmStatus fin_voice_language_fsm_snapshot(
    const FinVoiceLanguageFsm *fsm,
    FinVoiceLanguageFsmSnapshot *out_snapshot);

/* Clears all stream state/counters but preserves the current configuration. */
FinVoiceLanguageFsmStatus fin_voice_language_fsm_reset(
    FinVoiceLanguageFsm *fsm);

/* Parse en/fr/hi or locale tags such as EN-us. zh and other languages fail closed. */
FinVoiceLanguageFsmStatus fin_voice_language_parse_tag(
    const char *language_tag,
    FinVoiceLanguage *out_language);

const char *fin_voice_language_tag(FinVoiceLanguage language);
const char *fin_voice_language_fsm_state_name(FinVoiceLanguageFsmState state);
const char *fin_voice_language_fsm_status_name(FinVoiceLanguageFsmStatus status);
uint32_t fin_voice_language_fsm_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_LANGUAGE_FSM_H */
