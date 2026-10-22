#include "voice_pipeline.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TAU_F 6.28318530717958647692f

static unsigned long g_checks;
static unsigned long g_failures;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

typedef struct TestFixture {
    FinVoiceDspContext dsp;
    FinVoiceLanguageFsm language_fsm;
    FinVoiceEventQueue event_queue;
    FinVoiceTextBuilder text_builder;
    FinVoiceEvent events[8];
    char text_storage[128];
    unsigned char *dsp_allocation;
    unsigned char *pipeline_allocation;
    size_t dsp_workspace_size;
    size_t pipeline_workspace_size;
    FinVoicePipeline pipeline;
} TestFixture;

static void fill_sine_pcm16(int16_t *frame, size_t count, float frequency_hz)
{
    for (size_t index = 0u; index < count; ++index) {
        const float phase = TEST_TAU_F * frequency_hz * (float)index /
                            (float)FIN_VOICE_SAMPLE_RATE_HZ;
        frame[index] = (int16_t)lrintf(0.35f * sinf(phase) * 32767.0f);
    }
}

static int fixture_init(TestFixture *fixture)
{
    FinVoiceDspConfig dsp_config;
    FinVoiceLanguageFsmConfig fsm_config;
    size_t needed;

    memset(fixture, 0, sizeof(*fixture));

    fin_voice_dsp_default_config(&dsp_config);
    dsp_config.simd_backend = FIN_SIMD_SCALAR;
    fixture->dsp_workspace_size = fin_voice_dsp_workspace_size(&dsp_config);
    CHECK(fixture->dsp_workspace_size > 0u);
    if (fixture->dsp_workspace_size == 0u) {
        return 0;
    }
    fixture->dsp_allocation = (unsigned char *)malloc(fixture->dsp_workspace_size + 1u);
    CHECK(fixture->dsp_allocation != NULL);
    if (fixture->dsp_allocation == NULL) {
        return 0;
    }
    CHECK(fin_voice_dsp_init(&fixture->dsp, &dsp_config,
        fixture->dsp_allocation + 1u, fixture->dsp_workspace_size) == FIN_DSP_OK);

    fin_voice_language_fsm_config_default(&fsm_config);
    CHECK(fin_voice_language_fsm_init(&fixture->language_fsm, &fsm_config) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(fin_voice_event_queue_init(&fixture->event_queue, fixture->events,
          sizeof(fixture->events) / sizeof(fixture->events[0])) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(fin_voice_text_builder_init(&fixture->text_builder,
          fixture->text_storage, sizeof(fixture->text_storage)) ==
          FIN_VOICE_TEXT_BUILDER_OK);

    fixture->pipeline_workspace_size = fin_voice_pipeline_workspace_size();
    CHECK(fixture->pipeline_workspace_size > 0u);
    needed = fixture->pipeline_workspace_size;
    fixture->pipeline_allocation = (unsigned char *)malloc(needed + 1u);
    CHECK(fixture->pipeline_allocation != NULL);
    if (fixture->pipeline_allocation == NULL) {
        return 0;
    }
    CHECK(fin_voice_pipeline_init(&fixture->pipeline,
          fixture->pipeline_allocation + 1u, needed,
          &fixture->dsp, &fixture->language_fsm,
          &fixture->event_queue, &fixture->text_builder) == FIN_VOICE_PIPELINE_OK);
    return fixture->pipeline.initialized;
}

static void fixture_destroy(TestFixture *fixture)
{
    free(fixture->pipeline_allocation);
    free(fixture->dsp_allocation);
    fixture->pipeline_allocation = NULL;
    fixture->dsp_allocation = NULL;
}

static void test_language_select_and_frame(TestFixture *fixture)
{
    int16_t pcm[FIN_VOICE_FRAME_SAMPLES];
    FinVoicePipelineFrameResult result = {0};
    FinVoiceEvent event = {0};

    fill_sine_pcm16(pcm, FIN_VOICE_FRAME_SAMPLES, 220.0f);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) ==
          FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_pipeline_process_pcm16_frame(&fixture->pipeline, pcm,
          FIN_VOICE_FRAME_SAMPLES, &result) ==
          FIN_VOICE_PIPELINE_ERROR_LANGUAGE_UNCONFIRMED);

    CHECK(fin_voice_pipeline_select_language_tag(&fixture->pipeline, "EN-us") ==
          FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) == FIN_VOICE_LANGUAGE_EN);
    CHECK(fin_voice_pipeline_process_pcm16_frame(&fixture->pipeline, pcm,
          FIN_VOICE_FRAME_SAMPLES, &result) == FIN_VOICE_PIPELINE_OK);
    CHECK(result.language == FIN_VOICE_LANGUAGE_EN);
    CHECK(fin_voice_dsp_validate_features(&result.raw_features) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_validate_features(&result.normalized_features) == FIN_DSP_OK);
    CHECK(result.quantization.best_index < 128u);
    CHECK(isfinite(result.quantization.best_distance_squared));
    CHECK(result.quantization.best_distance_squared >= 0.0f);
    CHECK(result.quantization.relative_gap >= 0.0f &&
          result.quantization.relative_gap <= 1.0f);

    CHECK(fin_voice_pipeline_publish_frame_events(&fixture->pipeline, &result,
          UINT64_C(123456789), UINT64_C(320)) == FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_event_queue_pop(&fixture->event_queue, &event) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(event.type == FIN_VOICE_EVENT_FEATURES_READY);
    CHECK(event.timestamp_ns == UINT64_C(123456789));
    CHECK(event.sample_position == UINT64_C(320));
    CHECK(event.payload.features.dimension == FIN_VOICE_EVENT_FEATURE_DIMENSION);
    CHECK(event.payload.features.values[0] == result.normalized_features.values[0]);
    CHECK(fin_voice_event_queue_pop(&fixture->event_queue, &event) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(event.type == FIN_VOICE_EVENT_QUANTIZATION_RESULT);
    CHECK(event.payload.quantization.language == FIN_VOICE_EVENT_LANGUAGE_EN);
    CHECK(event.payload.quantization.best_index == result.quantization.best_index);
    CHECK(event.payload.quantization.best_distance_squared ==
          result.quantization.best_distance_squared);

    CHECK(fin_voice_pipeline_select_language_tag(&fixture->pipeline, "zh-CN") ==
          FIN_VOICE_PIPELINE_ERROR_UNSUPPORTED_LANGUAGE);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) ==
          FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_pipeline_process_pcm16_frame(&fixture->pipeline, pcm,
          FIN_VOICE_FRAME_SAMPLES, &result) ==
          FIN_VOICE_PIPELINE_ERROR_LANGUAGE_UNCONFIRMED);
    CHECK(fin_voice_pipeline_select_language_tag(&fixture->pipeline, "fr") ==
          FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_pipeline_select_language_tag(&fixture->pipeline, "invalid!") ==
          FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM);
}

static void test_evidence_drives_fsm_not_vq(TestFixture *fixture)
{
    FinVoiceLanguageEvidence evidence = {
        .en_confidence = 0.04f,
        .fr_confidence = 0.92f,
        .hi_confidence = 0.04f,
        .sample_position = 320u
    };
    FinVoiceLanguageFsmSnapshot snapshot = {0};

    for (uint32_t index = 0u; index < 2u; ++index) {
        evidence.sample_position += 160u;
        CHECK(fin_voice_pipeline_update_language_evidence(&fixture->pipeline,
              &evidence, &snapshot) == FIN_VOICE_PIPELINE_OK);
        CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CANDIDATE);
        CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) ==
              FIN_VOICE_LANGUAGE_UNKNOWN);
    }

    evidence.sample_position += 160u;
    CHECK(fin_voice_pipeline_update_language_evidence(&fixture->pipeline,
          &evidence, &snapshot) == FIN_VOICE_PIPELINE_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_CONFIRMED);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_FR);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) == FIN_VOICE_LANGUAGE_FR);

    evidence.en_confidence = 0.34f;
    evidence.fr_confidence = 0.33f;
    evidence.hi_confidence = 0.33f;
    evidence.sample_position += 160u;
    CHECK(fin_voice_pipeline_update_language_evidence(&fixture->pipeline,
          &evidence, &snapshot) == FIN_VOICE_PIPELINE_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNCERTAIN);
    CHECK(snapshot.language == FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) ==
          FIN_VOICE_LANGUAGE_UNKNOWN);

    evidence.en_confidence = NAN;
    CHECK(fin_voice_pipeline_update_language_evidence(&fixture->pipeline,
          &evidence, &snapshot) == FIN_VOICE_PIPELINE_ERROR_LANGUAGE_FSM);
}

static void test_text_and_queue_overflow(TestFixture *fixture)
{
    char output[128];
    size_t output_length = 0u;
    FinVoiceEvent event = {0};

    CHECK(fin_voice_pipeline_append_recognized_text(&fixture->pipeline,
          "hello ", 6u) == FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_pipeline_append_recognized_text(&fixture->pipeline,
          "caf\xC3\xA9", 5u) == FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_pipeline_flush_recognized_text(&fixture->pipeline,
          output, sizeof(output), &output_length) == FIN_VOICE_PIPELINE_OK);
    CHECK(output_length == 11u);
    CHECK(strcmp(output, "hello caf\xC3\xA9") == 0);

    CHECK(fin_voice_pipeline_publish_text_event(&fixture->pipeline,
          "bonjour", 7u, 555u, 960u, true) == FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_event_queue_pop(&fixture->event_queue, &event) ==
          FIN_VOICE_EVENT_QUEUE_OK);
    CHECK(event.type == FIN_VOICE_EVENT_TEXT_READY);
    CHECK((event.flags & FIN_VOICE_EVENT_FLAG_FINAL) != 0u);
    CHECK(strcmp(event.payload.text.utf8, "bonjour") == 0);
    CHECK(fin_voice_pipeline_publish_text_event(&fixture->pipeline,
          "text too large for one event", 28u, 555u, 960u, false) ==
          FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_event_queue_pop(&fixture->event_queue, &event) ==
          FIN_VOICE_EVENT_QUEUE_OK);

    {
        char oversized_text[FIN_VOICE_EVENT_TEXT_CAPACITY];
        memset(oversized_text, 'x', sizeof(oversized_text));
        CHECK(fin_voice_pipeline_publish_text_event(&fixture->pipeline,
              oversized_text, sizeof(oversized_text), 555u, 960u, false) ==
              FIN_VOICE_PIPELINE_ERROR_TEXT_EVENT_TOO_LARGE);
    }

    event = (FinVoiceEvent){0};
    event.type = (uint16_t)FIN_VOICE_EVENT_STATUS;
    for (size_t index = 0u; index < 8u; ++index) {
        event.sequence_id = (uint32_t)index;
        CHECK(fin_voice_event_queue_push(&fixture->event_queue, &event) ==
              FIN_VOICE_EVENT_QUEUE_OK);
    }
    CHECK(fin_voice_pipeline_publish_text_event(&fixture->pipeline,
          "full", 4u, 999u, 1500u, false) ==
          FIN_VOICE_PIPELINE_ERROR_EVENT_QUEUE_FULL);
    CHECK(fin_voice_event_queue_dropped_count(&fixture->event_queue) == 1u);
}

static void test_reset(TestFixture *fixture)
{
    FinVoiceLanguageFsmSnapshot snapshot = {0};
    CHECK(fin_voice_pipeline_reset(&fixture->pipeline) == FIN_VOICE_PIPELINE_OK);
    CHECK(fin_voice_pipeline_active_language(&fixture->pipeline) ==
          FIN_VOICE_LANGUAGE_UNKNOWN);
    CHECK(fin_voice_event_queue_dropped_count(&fixture->event_queue) == 0u);
    CHECK(fin_voice_text_builder_length(&fixture->text_builder) == 0u);
    CHECK(fin_voice_language_fsm_snapshot(&fixture->language_fsm, &snapshot) ==
          FIN_VOICE_LANGUAGE_FSM_OK);
    CHECK(snapshot.state == FIN_VOICE_LANGUAGE_FSM_UNKNOWN);
    CHECK(fin_voice_pipeline_abi_version() ==
          ((FIN_VOICE_PIPELINE_ABI_MAJOR << 16u) | FIN_VOICE_PIPELINE_ABI_MINOR));
    CHECK(strcmp(fin_voice_pipeline_status_name(FIN_VOICE_PIPELINE_OK), "ok") == 0);
}

int main(void)
{
    TestFixture fixture;
    if (!fixture_init(&fixture)) {
        fixture_destroy(&fixture);
        return EXIT_FAILURE;
    }

    test_language_select_and_frame(&fixture);
    /* Restore a clean supported selection before testing evidence transitions. */
    CHECK(fin_voice_pipeline_select_language_tag(&fixture.pipeline, "en") ==
          FIN_VOICE_PIPELINE_OK);
    test_evidence_drives_fsm_not_vq(&fixture);
    test_text_and_queue_overflow(&fixture);
    test_reset(&fixture);
    fixture_destroy(&fixture);

    if (g_failures != 0u) {
        (void)fprintf(stderr, "FinTally Voice pipeline: %lu/%lu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }
    (void)printf("FinTally Voice pipeline: PASS (%lu checks)\n", g_checks);
    return EXIT_SUCCESS;
}
