#include "language_fsm.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int fin_voice_is_finite_unit_interval(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

static int fin_voice_config_valid(const FinVoiceLanguageFsmConfig *config)
{
    return config != NULL &&
           isfinite(config->minimum_confidence) &&
           config->minimum_confidence > 0.0f &&
           config->minimum_confidence <= 1.0f &&
           isfinite(config->minimum_margin) &&
           config->minimum_margin > 0.0f &&
           config->minimum_margin <= 1.0f &&
           config->consecutive_confirmations > 0u;
}

static uint64_t fin_voice_saturating_increment_u64(uint64_t value)
{
    return value == UINT64_MAX ? UINT64_MAX : value + 1u;
}

static void fin_voice_clear_dynamic_state(FinVoiceLanguageFsm *fsm)
{
    fsm->state = FIN_VOICE_LANGUAGE_FSM_UNKNOWN;
    fsm->language = FIN_VOICE_LANGUAGE_UNKNOWN;
    fsm->candidate_language = FIN_VOICE_LANGUAGE_UNKNOWN;
    fsm->candidate_streak = 0u;
    fsm->accepted_evidence_count = 0u;
    fsm->transition_count = 0u;
    fsm->last_sample_position = 0u;
    fsm->last_best_confidence = 0.0f;
    fsm->last_margin = 0.0f;
}

void fin_voice_language_fsm_config_default(FinVoiceLanguageFsmConfig *config)
{
    if (config == NULL) {
        return;
    }

    config->minimum_confidence = 0.75f;
    config->minimum_margin = 0.15f;
    config->consecutive_confirmations = 3u;
}

FinVoiceLanguageFsmStatus fin_voice_language_fsm_init(
    FinVoiceLanguageFsm *fsm,
    const FinVoiceLanguageFsmConfig *config)
{
    if (fsm == NULL) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT;
    }

    FinVoiceLanguageFsmConfig selected_config;
    if (config == NULL) {
        fin_voice_language_fsm_config_default(&selected_config);
        config = &selected_config;
    }
    if (!fin_voice_config_valid(config)) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG;
    }

    (void)memset(fsm, 0, sizeof(*fsm));
    fsm->config = *config;
    fin_voice_clear_dynamic_state(fsm);
    return FIN_VOICE_LANGUAGE_FSM_OK;
}

static void fin_voice_snapshot_from_fsm(
    const FinVoiceLanguageFsm *fsm,
    FinVoiceLanguageFsmSnapshot *snapshot)
{
    snapshot->state = fsm->state;
    snapshot->language = fsm->language;
    snapshot->candidate_language = fsm->candidate_language;
    snapshot->candidate_streak = fsm->candidate_streak;
    snapshot->confirmations_required = fsm->config.consecutive_confirmations;
    snapshot->accepted_evidence_count = fsm->accepted_evidence_count;
    snapshot->transition_count = fsm->transition_count;
    snapshot->last_sample_position = fsm->last_sample_position;
    snapshot->last_best_confidence = fsm->last_best_confidence;
    snapshot->last_margin = fsm->last_margin;
}

FinVoiceLanguageFsmStatus fin_voice_language_fsm_snapshot(
    const FinVoiceLanguageFsm *fsm,
    FinVoiceLanguageFsmSnapshot *out_snapshot)
{
    if (fsm == NULL || out_snapshot == NULL) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT;
    }

    fin_voice_snapshot_from_fsm(fsm, out_snapshot);
    return FIN_VOICE_LANGUAGE_FSM_OK;
}

FinVoiceLanguageFsmStatus fin_voice_language_fsm_reset(
    FinVoiceLanguageFsm *fsm)
{
    if (fsm == NULL) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT;
    }

    /* The state/config are caller-owned; reset must be serialized by caller. */
    const FinVoiceLanguageFsmConfig config = fsm->config;
    if (!fin_voice_config_valid(&config)) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG;
    }

    fin_voice_clear_dynamic_state(fsm);
    fsm->config = config;
    return FIN_VOICE_LANGUAGE_FSM_OK;
}

typedef struct FinVoiceBestLanguage {
    FinVoiceLanguage language;
    float best;
    float second;
} FinVoiceBestLanguage;

static FinVoiceBestLanguage fin_voice_find_best_language(
    const FinVoiceLanguageEvidence *evidence)
{
    FinVoiceBestLanguage result = {
        .language = FIN_VOICE_LANGUAGE_EN,
        .best = evidence->en_confidence,
        .second = evidence->fr_confidence
    };

    if (result.second > result.best) {
        const float old_best = result.best;
        result.best = result.second;
        result.second = old_best;
        result.language = FIN_VOICE_LANGUAGE_FR;
    }

    if (evidence->hi_confidence > result.best) {
        result.second = result.best;
        result.best = evidence->hi_confidence;
        result.language = FIN_VOICE_LANGUAGE_HI;
    } else if (evidence->hi_confidence > result.second) {
        result.second = evidence->hi_confidence;
    }

    /* Stable tie preference is en, then fr, then hi; a positive margin
       requirement normally rejects exact ties as uncertain evidence. */
    if (evidence->en_confidence == result.best) {
        result.language = FIN_VOICE_LANGUAGE_EN;
    } else if (evidence->fr_confidence == result.best) {
        result.language = FIN_VOICE_LANGUAGE_FR;
    }

    return result;
}

static void fin_voice_enter_uncertain(FinVoiceLanguageFsm *fsm)
{
    fsm->state = FIN_VOICE_LANGUAGE_FSM_UNCERTAIN;
    fsm->language = FIN_VOICE_LANGUAGE_UNKNOWN;
    fsm->candidate_language = FIN_VOICE_LANGUAGE_UNKNOWN;
    fsm->candidate_streak = 0u;
}

static void fin_voice_begin_candidate(
    FinVoiceLanguageFsm *fsm,
    FinVoiceLanguage candidate)
{
    fsm->state = FIN_VOICE_LANGUAGE_FSM_CANDIDATE;
    fsm->language = FIN_VOICE_LANGUAGE_UNKNOWN;
    fsm->candidate_language = candidate;
    fsm->candidate_streak = 1u;

    if (fsm->config.consecutive_confirmations <= 1u) {
        fsm->state = FIN_VOICE_LANGUAGE_FSM_CONFIRMED;
        fsm->language = candidate;
        fsm->candidate_language = FIN_VOICE_LANGUAGE_UNKNOWN;
        fsm->candidate_streak = 0u;
    }
}

static void fin_voice_accept_candidate_frame(FinVoiceLanguageFsm *fsm)
{
    if (fsm->candidate_streak < UINT32_MAX) {
        ++fsm->candidate_streak;
    }

    if (fsm->candidate_streak >= fsm->config.consecutive_confirmations) {
        fsm->state = FIN_VOICE_LANGUAGE_FSM_CONFIRMED;
        fsm->language = fsm->candidate_language;
        fsm->candidate_language = FIN_VOICE_LANGUAGE_UNKNOWN;
        fsm->candidate_streak = 0u;
    }
}

static int fin_voice_decision_fields_changed(
    FinVoiceLanguageFsmState old_state,
    FinVoiceLanguage old_language,
    FinVoiceLanguage old_candidate,
    const FinVoiceLanguageFsm *fsm)
{
    return old_state != fsm->state || old_language != fsm->language ||
           old_candidate != fsm->candidate_language;
}

FinVoiceLanguageFsmStatus fin_voice_language_fsm_update(
    FinVoiceLanguageFsm *fsm,
    const FinVoiceLanguageEvidence *evidence,
    FinVoiceLanguageFsmSnapshot *out_snapshot)
{
    if (fsm == NULL || evidence == NULL) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT;
    }
    if (!fin_voice_config_valid(&fsm->config)) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG;
    }
    if (!fin_voice_is_finite_unit_interval(evidence->en_confidence) ||
        !fin_voice_is_finite_unit_interval(evidence->fr_confidence) ||
        !fin_voice_is_finite_unit_interval(evidence->hi_confidence)) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE;
    }

    const FinVoiceLanguageFsmState old_state = fsm->state;
    const FinVoiceLanguage old_language = fsm->language;
    const FinVoiceLanguage old_candidate = fsm->candidate_language;
    const FinVoiceBestLanguage best = fin_voice_find_best_language(evidence);
    const float margin = best.best - best.second;

    fsm->accepted_evidence_count =
        fin_voice_saturating_increment_u64(fsm->accepted_evidence_count);
    fsm->last_sample_position = evidence->sample_position;
    fsm->last_best_confidence = best.best;
    fsm->last_margin = margin;

    if (best.best < fsm->config.minimum_confidence ||
        margin < fsm->config.minimum_margin) {
        fin_voice_enter_uncertain(fsm);
    } else if (fsm->state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED &&
               fsm->language == best.language) {
        /* Reliable evidence agrees with the current selection. */
        fsm->candidate_language = FIN_VOICE_LANGUAGE_UNKNOWN;
        fsm->candidate_streak = 0u;
    } else if (fsm->state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE &&
               fsm->candidate_language == best.language) {
        fin_voice_accept_candidate_frame(fsm);
    } else {
        /* Initial selection, recovery from uncertainty, or language switch. */
        fin_voice_begin_candidate(fsm, best.language);
    }

    if (fin_voice_decision_fields_changed(
            old_state, old_language, old_candidate, fsm)) {
        fsm->transition_count =
            fin_voice_saturating_increment_u64(fsm->transition_count);
    }

    if (out_snapshot != NULL) {
        fin_voice_snapshot_from_fsm(fsm, out_snapshot);
    }
    return FIN_VOICE_LANGUAGE_FSM_OK;
}

static char fin_voice_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static int fin_voice_ascii_alpha(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static int fin_voice_ascii_digit(char value)
{
    return value >= '0' && value <= '9';
}

static int fin_voice_tag_syntax_valid(const char *tag)
{
    if (tag == NULL || !fin_voice_ascii_alpha(tag[0]) ||
        !fin_voice_ascii_alpha(tag[1])) {
        return 0;
    }
    if (tag[2] == '\0') {
        return 1;
    }
    if (tag[2] != '-' && tag[2] != '_') {
        return 0;
    }

    int expecting_alphanumeric = 1;
    for (size_t i = 3u; tag[i] != '\0'; ++i) {
        if (tag[i] == '-' || tag[i] == '_') {
            if (expecting_alphanumeric) {
                return 0;
            }
            expecting_alphanumeric = 1;
        } else if (fin_voice_ascii_alpha(tag[i]) || fin_voice_ascii_digit(tag[i])) {
            expecting_alphanumeric = 0;
        } else {
            return 0;
        }
    }
    return !expecting_alphanumeric;
}

FinVoiceLanguageFsmStatus fin_voice_language_parse_tag(
    const char *language_tag,
    FinVoiceLanguage *out_language)
{
    if (out_language != NULL) {
        *out_language = FIN_VOICE_LANGUAGE_UNKNOWN;
    }
    if (language_tag == NULL || out_language == NULL) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT;
    }
    if (!fin_voice_tag_syntax_valid(language_tag)) {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG;
    }

    const char first = fin_voice_ascii_lower(language_tag[0]);
    const char second = fin_voice_ascii_lower(language_tag[1]);
    if (first == 'e' && second == 'n') {
        *out_language = FIN_VOICE_LANGUAGE_EN;
    } else if (first == 'f' && second == 'r') {
        *out_language = FIN_VOICE_LANGUAGE_FR;
    } else if (first == 'h' && second == 'i') {
        *out_language = FIN_VOICE_LANGUAGE_HI;
    } else {
        return FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE;
    }

    return FIN_VOICE_LANGUAGE_FSM_OK;
}

const char *fin_voice_language_tag(FinVoiceLanguage language)
{
    switch (language) {
    case FIN_VOICE_LANGUAGE_EN:
        return "en";
    case FIN_VOICE_LANGUAGE_FR:
        return "fr";
    case FIN_VOICE_LANGUAGE_HI:
        return "hi";
    case FIN_VOICE_LANGUAGE_UNKNOWN:
    default:
        return NULL;
    }
}

const char *fin_voice_language_fsm_state_name(FinVoiceLanguageFsmState state)
{
    switch (state) {
    case FIN_VOICE_LANGUAGE_FSM_UNKNOWN:
        return "unknown";
    case FIN_VOICE_LANGUAGE_FSM_CANDIDATE:
        return "candidate";
    case FIN_VOICE_LANGUAGE_FSM_CONFIRMED:
        return "confirmed";
    case FIN_VOICE_LANGUAGE_FSM_UNCERTAIN:
        return "uncertain";
    default:
        return "invalid_state";
    }
}

const char *fin_voice_language_fsm_status_name(FinVoiceLanguageFsmStatus status)
{
    switch (status) {
    case FIN_VOICE_LANGUAGE_FSM_OK:
        return "ok";
    case FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG:
        return "invalid_config";
    case FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE:
        return "invalid_evidence";
    case FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE:
        return "unsupported_language";
    case FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG:
        return "invalid_language_tag";
    default:
        return "unknown_status";
    }
}

uint32_t fin_voice_language_fsm_abi_version(void)
{
    return (FIN_VOICE_LANGUAGE_FSM_ABI_MAJOR << 16u) |
           FIN_VOICE_LANGUAGE_FSM_ABI_MINOR;
}
