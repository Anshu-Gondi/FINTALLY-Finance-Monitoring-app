#define _POSIX_C_SOURCE 200809L

#include "voice_pipeline.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FIN_VOICE_BENCH_DEFAULT_ITERATIONS
#define FIN_VOICE_BENCH_DEFAULT_ITERATIONS 100000u
#endif

#define FIN_VOICE_BENCH_QUEUE_CAPACITY 8u
#define FIN_VOICE_BENCH_TEXT_CAPACITY 128u
#define FIN_VOICE_BENCH_WARMUP_ITERATIONS 2000u
#define FIN_VOICE_BENCH_REALTIME_FRAME_NS UINT64_C(20000000)
#define FIN_VOICE_BENCH_TAU_F 6.28318530717958647692f

typedef struct FinVoiceBenchFixture {
    FinVoiceDspContext dsp;
    FinVoiceLanguageFsm language_fsm;
    FinVoiceEventQueue event_queue;
    FinVoiceTextBuilder text_builder;
    FinVoiceEvent events[FIN_VOICE_BENCH_QUEUE_CAPACITY];
    char text_storage[FIN_VOICE_BENCH_TEXT_CAPACITY];
    unsigned char *dsp_memory;
    unsigned char *pipeline_memory;
    size_t dsp_memory_size;
    size_t pipeline_memory_size;
    FinVoicePipeline pipeline;
} FinVoiceBenchFixture;

static volatile uint64_t g_result_sink;

static uint64_t monotonic_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        (void)fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                      strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ((uint64_t)value.tv_sec * UINT64_C(1000000000)) +
           (uint64_t)value.tv_nsec;
}

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t percentile_sorted(const uint64_t *values, size_t count,
                                  unsigned percentile)
{
    size_t index;
    if (count == 0u) {
        return 0u;
    }
    index = ((count - 1u) * (size_t)percentile + 99u) / 100u;
    if (index >= count) {
        index = count - 1u;
    }
    return values[index];
}

static void make_benchmark_frame(int16_t *pcm, size_t count)
{
    /* A deterministic, mixed-tone frame. This is a microbenchmark signal,
       not a substitute for benchmarking recorded speech. */
    for (size_t index = 0u; index < count; ++index) {
        const float t = (float)index / (float)FIN_VOICE_SAMPLE_RATE_HZ;
        const float sample = 0.23f * sinf(FIN_VOICE_BENCH_TAU_F * 220.0f * t) +
                             0.11f * sinf(FIN_VOICE_BENCH_TAU_F * 713.0f * t);
        pcm[index] = (int16_t)lrintf(sample * 32767.0f);
    }
}

static int fixture_init(FinVoiceBenchFixture *fixture)
{
    FinVoiceDspConfig dsp_config;
    FinVoiceLanguageFsmConfig fsm_config;
    const size_t pipeline_required = fin_voice_pipeline_workspace_size();

    memset(fixture, 0, sizeof(*fixture));
    fin_voice_dsp_default_config(&dsp_config);
    /* Benchmark the runtime-selected backend by default. Set the backend in a
       separate build/configuration if comparing scalar vs SIMD. */
    fixture->dsp_memory_size = fin_voice_dsp_workspace_size(&dsp_config);
    if (fixture->dsp_memory_size == 0u || pipeline_required == 0u) {
        (void)fprintf(stderr, "invalid DSP/pipeline workspace requirement\n");
        return 0;
    }

    fixture->dsp_memory = (unsigned char *)malloc(fixture->dsp_memory_size + 1u);
    fixture->pipeline_memory = (unsigned char *)malloc(pipeline_required + 1u);
    if (fixture->dsp_memory == NULL || fixture->pipeline_memory == NULL) {
        (void)fprintf(stderr, "failed to allocate benchmark startup workspace\n");
        return 0;
    }

    if (fin_voice_dsp_init(&fixture->dsp, &dsp_config,
            fixture->dsp_memory + 1u, fixture->dsp_memory_size) != FIN_DSP_OK) {
        (void)fprintf(stderr, "fin_voice_dsp_init failed\n");
        return 0;
    }
    fin_voice_language_fsm_config_default(&fsm_config);
    if (fin_voice_language_fsm_init(&fixture->language_fsm, &fsm_config) !=
            FIN_VOICE_LANGUAGE_FSM_OK ||
        fin_voice_event_queue_init(&fixture->event_queue, fixture->events,
            FIN_VOICE_BENCH_QUEUE_CAPACITY) != FIN_VOICE_EVENT_QUEUE_OK ||
        fin_voice_text_builder_init(&fixture->text_builder, fixture->text_storage,
            sizeof(fixture->text_storage)) != FIN_VOICE_TEXT_BUILDER_OK ||
        fin_voice_pipeline_init(&fixture->pipeline, fixture->pipeline_memory + 1u,
            pipeline_required, &fixture->dsp, &fixture->language_fsm,
            &fixture->event_queue, &fixture->text_builder) != FIN_VOICE_PIPELINE_OK) {
        (void)fprintf(stderr, "component or pipeline initialization failed\n");
        return 0;
    }
    if (fin_voice_pipeline_select_language_tag(&fixture->pipeline, "en") !=
            FIN_VOICE_PIPELINE_OK) {
        (void)fprintf(stderr, "failed to select generated English codebook\n");
        return 0;
    }
    return 1;
}

static void fixture_destroy(FinVoiceBenchFixture *fixture)
{
    free(fixture->pipeline_memory);
    free(fixture->dsp_memory);
    fixture->pipeline_memory = NULL;
    fixture->dsp_memory = NULL;
}

static int run_one_frame(FinVoiceBenchFixture *fixture, const int16_t *pcm,
                         uint64_t sequence, uint64_t *out_digest)
{
    FinVoicePipelineFrameResult result = {0};
    FinVoiceEvent event = {0};
    const FinVoicePipelineStatus pipeline_status =
        fin_voice_pipeline_process_pcm16_frame(&fixture->pipeline, pcm,
            FIN_VOICE_FRAME_SAMPLES, &result);
    if (pipeline_status != FIN_VOICE_PIPELINE_OK) {
        (void)fprintf(stderr, "process frame failed: %s\n",
                      fin_voice_pipeline_status_name(pipeline_status));
        return 0;
    }

    if (fin_voice_pipeline_publish_frame_events(&fixture->pipeline, &result,
            sequence * FIN_VOICE_BENCH_REALTIME_FRAME_NS,
            sequence * FIN_VOICE_FRAME_SAMPLES) != FIN_VOICE_PIPELINE_OK) {
        (void)fprintf(stderr, "publishing frame events failed\n");
        return 0;
    }
    for (size_t event_index = 0u; event_index < 2u; ++event_index) {
        if (fin_voice_event_queue_pop(&fixture->event_queue, &event) !=
                FIN_VOICE_EVENT_QUEUE_OK) {
            (void)fprintf(stderr, "event queue pop failed\n");
            return 0;
        }
        *out_digest ^= ((uint64_t)event.type << ((event_index + 1u) * 8u));
        *out_digest += event.sequence_id + (uint64_t)event.sample_position;
    }
    *out_digest ^= (uint64_t)result.quantization.best_index;
    *out_digest += (uint64_t)(result.quantization.best_distance_squared * 1000.0f);
    return 1;
}

static int parse_iterations(const char *argument, size_t *out_iterations)
{
    char *end = NULL;
    unsigned long long parsed;
    errno = 0;
    parsed = strtoull(argument, &end, 10);
    if (errno != 0 || end == argument || *end != '\0' || parsed == 0u ||
        parsed > 10000000ull || parsed > (unsigned long long)(SIZE_MAX / sizeof(uint64_t))) {
        return 0;
    }
    *out_iterations = (size_t)parsed;
    return 1;
}

int main(int argc, char **argv)
{
    FinVoiceBenchFixture fixture;
    int16_t pcm[FIN_VOICE_FRAME_SAMPLES];
    size_t iterations = (size_t)FIN_VOICE_BENCH_DEFAULT_ITERATIONS;
    uint64_t *latencies;
    uint64_t digest = UINT64_C(0x9e3779b97f4a7c15);
    uint64_t total_start;
    uint64_t total_end;
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0u;
    long double sum_ns = 0.0L;

    if (argc > 2 || (argc == 2 && !parse_iterations(argv[1], &iterations))) {
        (void)fprintf(stderr, "usage: %s [iterations: 1..10000000]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!fixture_init(&fixture)) {
        fixture_destroy(&fixture);
        return EXIT_FAILURE;
    }
    make_benchmark_frame(pcm, FIN_VOICE_FRAME_SAMPLES);
    latencies = (uint64_t *)malloc(iterations * sizeof(*latencies));
    if (latencies == NULL) {
        (void)fprintf(stderr, "failed to allocate latency samples\n");
        fixture_destroy(&fixture);
        return EXIT_FAILURE;
    }

    /* Warm caches and DSP state before collecting samples. */
    for (size_t index = 0u; index < FIN_VOICE_BENCH_WARMUP_ITERATIONS; ++index) {
        if (!run_one_frame(&fixture, pcm, (uint64_t)index + 1u, &digest)) {
            free(latencies);
            fixture_destroy(&fixture);
            return EXIT_FAILURE;
        }
    }

    total_start = monotonic_ns();
    for (size_t index = 0u; index < iterations; ++index) {
        const uint64_t start = monotonic_ns();
        if (!run_one_frame(&fixture, pcm,
                (uint64_t)FIN_VOICE_BENCH_WARMUP_ITERATIONS + (uint64_t)index + 1u,
                &digest)) {
            free(latencies);
            fixture_destroy(&fixture);
            return EXIT_FAILURE;
        }
        const uint64_t elapsed = monotonic_ns() - start;
        latencies[index] = elapsed;
        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        sum_ns += (long double)elapsed;
    }
    total_end = monotonic_ns();
    qsort(latencies, iterations, sizeof(*latencies), compare_u64);
    g_result_sink = digest;

    {
        const long double mean_ns = sum_ns / (long double)iterations;
        const long double elapsed_total_ns = (long double)(total_end - total_start);
        const long double frames_per_second =
            elapsed_total_ns > 0.0L ? (long double)iterations * 1.0e9L / elapsed_total_ns : 0.0L;
        const long double realtime_factor = mean_ns > 0.0L
            ? (long double)FIN_VOICE_BENCH_REALTIME_FRAME_NS / mean_ns : 0.0L;
        const size_t reported_workspace = fixture.dsp_memory_size +
            fin_voice_pipeline_workspace_size();

        (void)printf("FinTally Voice pipeline benchmark\n");
        (void)printf("mode=single-threaded, language=en, frame_samples=%u, frame_ms=20\n",
                     (unsigned)FIN_VOICE_FRAME_SAMPLES);
        (void)printf("iterations=%zu, warmup=%u\n", iterations,
                     (unsigned)FIN_VOICE_BENCH_WARMUP_ITERATIONS);
        (void)printf("latency_ns min=%" PRIu64 " mean=%.0Lf p50=%" PRIu64
                     " p90=%" PRIu64 " p95=%" PRIu64 " p99=%" PRIu64
                     " max=%" PRIu64 "\n",
                     min_ns, mean_ns,
                     percentile_sorted(latencies, iterations, 50u),
                     percentile_sorted(latencies, iterations, 90u),
                     percentile_sorted(latencies, iterations, 95u),
                     percentile_sorted(latencies, iterations, 99u), max_ns);
        (void)printf("throughput_frames_per_second=%.2Lf\n", frames_per_second);
        (void)printf("mean_realtime_factor=%.3Lf (audio-seconds processed per wall-second)\n",
                     realtime_factor);
        (void)printf("20ms_frame_budget_mean=%s p99=%s\n",
                     mean_ns <= FIN_VOICE_BENCH_REALTIME_FRAME_NS ? "PASS" : "MISS",
                     percentile_sorted(latencies, iterations, 99u) <= FIN_VOICE_BENCH_REALTIME_FRAME_NS
                         ? "PASS" : "MISS");
        (void)printf("known_workspace_bytes=%zu (DSP workspace + pipeline private workspace; excludes queue/text buffers, stack, allocator overhead, codebooks, and process runtime)\n",
                     reported_workspace);
        (void)printf("event_size_bytes=%zu, event_queue_capacity=%u\n",
                     sizeof(FinVoiceEvent), (unsigned)FIN_VOICE_BENCH_QUEUE_CAPACITY);
        (void)printf("result_sink=%" PRIu64 "\n", (uint64_t)g_result_sink);
        (void)printf("cache_misses=not measured internally; use the documented Cachegrind command\n");
    }

    free(latencies);
    fixture_destroy(&fixture);
    return EXIT_SUCCESS;
}
