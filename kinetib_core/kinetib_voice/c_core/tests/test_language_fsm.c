#include "language_fsm.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t checks;
static size_t failures;

#define CHECK(expression)                                                       \
    do {                                                                        \
        ++checks;                                                               \
        if (!(expression)) {                                                    \
            ++failures;                                                         \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                          #expression);                                         \
        }                                                                       \
    } while (0)

static FinVoiceLanguageEvidence evidence(
    float en,
    float fr,
    float hi,
    uint64_t sample_position)
{
    FinVoiceLanguageEvidence item = {
        .en_confidence = en,
        .fr_confidence = fr,
        .hi_confidence = hi,
        .sample_position = sample_position
    };
    return item;
}

static FinVoiceLanguageFsmConfig config_for(uint32_t required)
{
    FinVoiceLanguageFsmConfig config = {
        .minimum_confidence = 0.75f,
        .minimum_margin = 0.15f,
        .consecutive_confirmations = required
    };
    return config;
}

static FinVoiceLanguageFsmStatus update_scores(
    FinVoiceLanguageFsm *fsm,
    float en,
    float fr,
    float hi,
    uint64_t sample_position,
    FinVoiceLanguageFsmSnapshot *snapshot)
{
    const FinVoiceLanguageEvidence item = evidence(en, fr, hi, sample_position);
    return fin_voice_language_fsm_update(fsm, &item, snapshot);
}

static void test_defaults_and_initial_state(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot snapshot;
    FinVoiceLanguageFsmConfig defaults;

    fin_voice_language_fsm_config_default(&defaults);
    CHECK(defaults.minimum_confidence == 0.75f);
    CHECK(defaults.minimum_margin == 0.15f);
    CHECK(defaults.consecutive_confirmations == 3u);
    fin_voice_language_fsm_config_default(NULL);

    CHECK(fin_voice_language_fsm_init(&fsm, NULL) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(fin_voice_language_fsm_snapshot(&fsm, &snapshot) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNKNOWN);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_streak == 0u);
    CHECK(snapshot.confirmations_required == 3u);
    CHECK(snapshot.accepted_evidence_count == 0u);
    CHECK(snapshot.transition_count == 0u);
    CHECK(fin_voice_language_fsm_abi_version() == 0x00010000u);
}

static void test_tag_parsing_and_supported_languages(void)
{
    FinVoiceLanguage language = FIN_VOICE_LANGUAGE_HI;

    CHECK(fin_voice_language_parse_tag("en", &language) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(language == FIN_VOICE_LANGUAGE_EN);
    CHECK(fin_voice_language_parse_tag("EN-us", &language) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(language == FIN_VOICE_LANGUAGE_EN);
    CHECK(fin_voice_language_parse_tag("fr_FR", &language) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(language == FIN_VOICE_LANGUAGE_FR);
    CHECK(fin_voice_language_parse_tag("hi-IN", &language) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(language == FIN_VOICE_LANGUAGE_HI);

    language = FIN_VOICE_LANGUAGE_HI;
    CHECK(fin_voice_language_parse_tag("zh-CN", &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE);
    CHECK(language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_language_parse_tag("es", &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE);
    CHECK(language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_language_parse_tag("en--US", &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG);
    CHECK(fin_voice_language_parse_tag("e", &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG);
    CHECK(fin_voice_language_parse_tag("", &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_LANGUAGE_TAG);
    CHECK(fin_voice_language_parse_tag(NULL, &language) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_language_parse_tag("en", NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(strcmp(fin_voice_language_tag(FIN_VOICE_LANGUAGE_EN), "en") == 0);
    CHECK(strcmp(fin_voice_language_tag(FIN_VOICE_LANGUAGE_FR), "fr") == 0);
    CHECK(strcmp(fin_voice_language_tag(FIN_VOICE_LANGUAGE_HI), "hi") == 0);
    CHECK(fin_voice_language_tag(FIN_VOICE_LANGUAGE_UNKNOWN) == NULL);
}

static void test_requires_consecutive_clear_evidence(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot snapshot;
    const FinVoiceLanguageFsmConfig config = config_for(3u);

    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.92f, 0.04f, 0.04f, 160u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_EN);
    CHECK(snapshot.candidate_streak == 1u);
    CHECK(snapshot.last_sample_position == 160u);

    CHECK(update_scores(&fsm, 0.90f, 0.06f, 0.04f, 320u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
    CHECK(snapshot.candidate_streak == 2u);

    CHECK(update_scores(&fsm, 0.89f, 0.07f, 0.04f, 480u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_EN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_streak == 0u);
    CHECK(snapshot.accepted_evidence_count == 3u);
    CHECK(snapshot.last_best_confidence == 0.89f);
    CHECK(snapshot.last_margin > 0.8f);
}

static void test_low_confidence_margin_and_ties_go_uncertain(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot snapshot;
    const FinVoiceLanguageFsmConfig config = config_for(2u);

    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.55f, 0.25f, 0.20f, 10u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNCERTAIN);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_streak == 0u);

    CHECK(update_scores(&fsm, 0.84f, 0.82f, 0.05f, 20u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNCERTAIN);
    CHECK(snapshot.last_best_confidence == 0.84f);
    CHECK(snapshot.last_margin < config.minimum_margin);

    CHECK(update_scores(&fsm, 0.90f, 0.90f, 0.10f, 30u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNCERTAIN);
    CHECK(snapshot.last_margin == 0.0f);

    /* A clear decision after uncertainty must build a fresh streak. */
    CHECK(update_scores(&fsm, 0.91f, 0.04f, 0.05f, 40u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
    CHECK(snapshot.candidate_streak == 1u);
}

static void test_language_switch_is_reconfirmed(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot snapshot;
    const FinVoiceLanguageFsmConfig config = config_for(3u);

    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.95f, 0.03f, 0.02f, 100u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.94f, 0.04f, 0.02f, 200u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.93f, 0.05f, 0.02f, 300u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_EN);

    CHECK(update_scores(&fsm, 0.04f, 0.92f, 0.04f, 400u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_FR);
    CHECK(snapshot.candidate_streak == 1u);

    CHECK(update_scores(&fsm, 0.03f, 0.93f, 0.04f, 500u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
    CHECK(snapshot.candidate_streak == 2u);

    CHECK(update_scores(&fsm, 0.02f, 0.94f, 0.04f, 600u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_FR);
}

static void test_invalid_evidence_does_not_mutate_state(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot before;
    FinVoiceLanguageFsmSnapshot after;
    FinVoiceLanguageEvidence invalid;
    const FinVoiceLanguageFsmConfig config = config_for(2u);

    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.92f, 0.04f, 0.04f, 10u, &before) ==
          FIN_VOICE_LANGUAGE_FSM_OK);

    invalid = evidence(NAN, 0.1f, 0.1f, 20u);
    CHECK(fin_voice_language_fsm_update(&fsm, &invalid, NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE);
    invalid = evidence(0.9f, INFINITY, 0.0f, 20u);
    CHECK(fin_voice_language_fsm_update(&fsm, &invalid, NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE);
    invalid = evidence(0.9f, -0.01f, 0.0f, 20u);
    CHECK(fin_voice_language_fsm_update(&fsm, &invalid, NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE);
    invalid = evidence(0.9f, 0.1f, 1.01f, 20u);
    CHECK(fin_voice_language_fsm_update(&fsm, &invalid, NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_EVIDENCE);

    CHECK(fin_voice_language_fsm_snapshot(&fsm, &after) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(after.state == before.state);
    CHECK(after.language == before.language);
    CHECK(after.candidate_language == before.candidate_language);
    CHECK(after.candidate_streak == before.candidate_streak);
    CHECK(after.accepted_evidence_count == before.accepted_evidence_count);
    CHECK(after.transition_count == before.transition_count);
    CHECK(after.last_sample_position == before.last_sample_position);
}

static void test_reset_clears_stream_but_preserves_config(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmSnapshot snapshot;
    const FinVoiceLanguageFsmConfig config = config_for(1u);

    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(update_scores(&fsm, 0.92f, 0.04f, 0.04f, 1234u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_EN);
    CHECK(snapshot.accepted_evidence_count == 1u);

    CHECK(fin_voice_language_fsm_reset(&fsm) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(fin_voice_language_fsm_snapshot(&fsm, &snapshot) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNKNOWN);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(snapshot.candidate_streak == 0u);
    CHECK(snapshot.accepted_evidence_count == 0u);
    CHECK(snapshot.transition_count == 0u);
    CHECK(snapshot.last_sample_position == 0u);
    CHECK(snapshot.confirmations_required == 1u);

    CHECK(update_scores(&fsm, 0.02f, 0.02f, 0.96f, 55u, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_HI);
}

static void test_config_and_argument_errors(void)
{
    FinVoiceLanguageFsm fsm;
    FinVoiceLanguageFsmConfig config = config_for(1u);
    FinVoiceLanguageFsmSnapshot snapshot;
    FinVoiceLanguageEvidence item = evidence(0.9f, 0.05f, 0.05f, 1u);

    CHECK(fin_voice_language_fsm_init(NULL, &config) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    config.minimum_confidence = NAN;
    CHECK(fin_voice_language_fsm_init(&fsm, &config) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG);
    config = config_for(1u);
    config.minimum_confidence = 0.0f;
    CHECK(fin_voice_language_fsm_init(&fsm, &config) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG);
    config = config_for(1u);
    config.minimum_margin = 0.0f;
    CHECK(fin_voice_language_fsm_init(&fsm, &config) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG);
    config = config_for(0u);
    CHECK(fin_voice_language_fsm_init(&fsm, &config) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_CONFIG);

    config = config_for(1u);
    CHECK(fin_voice_language_fsm_init(&fsm, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(fin_voice_language_fsm_update(NULL, &item, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_language_fsm_update(&fsm, NULL, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_language_fsm_snapshot(NULL, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_language_fsm_snapshot(&fsm, NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_language_fsm_reset(NULL) ==
          FIN_VOICE_LANGUAGE_FSM_ERROR_INVALID_ARGUMENT);
    CHECK(strcmp(fin_voice_language_fsm_state_name(FIN_VOICE_LANGUAGE_FSM_UNKNOWN),
                 "unknown") == 0);
    CHECK(strcmp(fin_voice_language_fsm_state_name(FIN_VOICE_LANGUAGE_FSM_CANDIDATE),
                 "candidate") == 0);
    CHECK(strcmp(fin_voice_language_fsm_state_name(FIN_VOICE_LANGUAGE_FSM_CONFIRMED),
                 "confirmed") == 0);
    CHECK(strcmp(fin_voice_language_fsm_state_name(FIN_VOICE_LANGUAGE_FSM_UNCERTAIN),
                 "uncertain") == 0);
    CHECK(strcmp(fin_voice_language_fsm_state_name((FinVoiceLanguageFsmState)99),
                 "invalid_state") == 0);
    CHECK(strcmp(fin_voice_language_fsm_status_name(FIN_VOICE_LANGUAGE_FSM_OK),
                 "ok") == 0);
    CHECK(strcmp(fin_voice_language_fsm_status_name(
                     FIN_VOICE_LANGUAGE_FSM_ERROR_UNSUPPORTED_LANGUAGE),
                 "unsupported_language") == 0);
}

static void test_identical_streams_are_deterministic(void)
{
    FinVoiceLanguageFsm left;
    FinVoiceLanguageFsm right;
    FinVoiceLanguageFsmSnapshot left_snapshot;
    FinVoiceLanguageFsmSnapshot right_snapshot;
    const FinVoiceLanguageFsmConfig config = config_for(2u);
    const FinVoiceLanguageEvidence items[] = {
        {0.91f, 0.05f, 0.04f, 100u},
        {0.90f, 0.06f, 0.04f, 200u},
        {0.50f, 0.45f, 0.05f, 300u},
        {0.04f, 0.91f, 0.05f, 400u},
        {0.03f, 0.92f, 0.05f, 500u},
        {0.04f, 0.91f, 0.05f, 600u}
    };

    CHECK(fin_voice_language_fsm_init(&left, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(fin_voice_language_fsm_init(&right, &config) == FIN_VOICE_LANGUAGE_FSM_OK);
    for (size_t i = 0u; i < sizeof(items) / sizeof(items[0]); ++i) {
        CHECK(fin_voice_language_fsm_update(&left, &items[i], &left_snapshot) ==
              FIN_VOICE_LANGUAGE_FSM_OK);
        CHECK(fin_voice_language_fsm_update(&right, &items[i], &right_snapshot) ==
              FIN_VOICE_LANGUAGE_FSM_OK);
        CHECK(left_snapshot.state == right_snapshot.state);
        CHECK(left_snapshot.language == right_snapshot.language);
        CHECK(left_snapshot.candidate_language == right_snapshot.candidate_language);
        CHECK(left_snapshot.candidate_streak == right_snapshot.candidate_streak);
        CHECK(left_snapshot.transition_count == right_snapshot.transition_count);
        CHECK(left_snapshot.last_sample_position == right_snapshot.last_sample_position);
        CHECK(left_snapshot.last_best_confidence == right_snapshot.last_best_confidence);
        CHECK(left_snapshot.last_margin == right_snapshot.last_margin);
    }
}

int main(void)
{
    test_defaults_and_initial_state();
    test_tag_parsing_and_supported_languages();
    test_requires_consecutive_clear_evidence();
    test_low_confidence_margin_and_ties_go_uncertain();
    test_language_switch_is_reconfirmed();
    test_invalid_evidence_does_not_mutate_state();
    test_reset_clears_stream_but_preserves_config();
    test_config_and_argument_errors();
    test_identical_streams_are_deterministic();

    if (failures != 0u) {
        (void)fprintf(stderr, "Language FSM tests: %zu/%zu checks failed\n",
                      failures, checks);
        return 1;
    }

    (void)printf("Language FSM tests: PASS (%zu checks)\n", checks);
    return 0;
}
