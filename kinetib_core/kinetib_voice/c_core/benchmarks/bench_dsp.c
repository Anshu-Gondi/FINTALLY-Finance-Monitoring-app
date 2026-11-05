#define _POSIX_C_SOURCE 200809L

/*
 * FinTally Voice acoustic DSP microbenchmark.
 *
 * This benchmark deliberately allocates and initializes its workspace before
 * timing begins. The measured hot path is one canonical 20 ms audio frame per
 * call; it does not include setup, heap allocation, or table initialization.
 *
 * The same PCM frame is reused to establish a hot-cache baseline. That is
 * useful for frame latency, but it is not a cache-miss benchmark and does not
 * represent a large streaming working set. Use hardware counters or a separate
 * varied-input benchmark for cache-miss measurements.
 */

#include "acoustic_dsp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_DEFAULT_ITERATIONS ((size_t)20000u)
#define BENCH_MIN_ITERATIONS     ((size_t)100u)
#define BENCH_MAX_ITERATIONS     ((size_t)5000000u)
#define BENCH_WARMUP_ITERATIONS  ((size_t)512u)
#define BENCH_MAX_LATENCY_SAMPLES ((size_t)2000u)
#define NANOSECONDS_PER_SECOND   UINT64_C(1000000000)
#define CANONICAL_FRAME_NS       UINT64_C(20000000)

static bool bench_monotonic_now_ns(uint64_t *out_ns)
{
    struct timespec now;
    uint64_t seconds;
    uint64_t nanoseconds;

    if (out_ns == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return false;
    }

    if (now.tv_sec < (time_t)0 || now.tv_nsec < 0L ||
        now.tv_nsec >= 1000000000L) {
        return false;
    }

    seconds = (uint64_t)now.tv_sec;
    nanoseconds = (uint64_t)now.tv_nsec;

    if (seconds > (UINT64_MAX - nanoseconds) / NANOSECONDS_PER_SECOND) {
        return false;
    }

    *out_ns = seconds * NANOSECONDS_PER_SECOND + nanoseconds;
    return true;
}

static int bench_parse_iterations(int argc, char **argv, size_t *out_iterations)
{
    char *end = NULL;
    unsigned long long parsed;

    if (out_iterations == NULL || argc > 2) {
        return 0;
    }

    if (argc == 1) {
        *out_iterations = BENCH_DEFAULT_ITERATIONS;
        return 1;
    }

    errno = 0;
    parsed = strtoull(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || end == NULL || *end != '\0' ||
        parsed > (unsigned long long)SIZE_MAX ||
        parsed < (unsigned long long)BENCH_MIN_ITERATIONS ||
        parsed > (unsigned long long)BENCH_MAX_ITERATIONS) {
        return 0;
    }

    *out_iterations = (size_t)parsed;
    return 1;
}

static int bench_compare_u64(const void *lhs, const void *rhs)
{
    const uint64_t a = *(const uint64_t *)lhs;
    const uint64_t b = *(const uint64_t *)rhs;

    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

/* Nearest-rank percentile; percentile must be in [1, 100]. */
static size_t bench_percentile_index(size_t count, size_t percentile)
{
    const size_t rank = (count * percentile + 99u) / 100u;
    return rank == 0u ? 0u : rank - 1u;
}

static void bench_fill_signal(int16_t samples[FIN_VOICE_FRAME_SAMPLES])
{
    const float sample_rate = (float)FIN_VOICE_SAMPLE_RATE_HZ;
    const float two_pi = 6.28318530717958647692f;
    size_t i;

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        const float time_seconds = (float)i / sample_rate;
        const float fundamental = sinf(two_pi * 220.0f * time_seconds);
        const float harmonic = sinf(two_pi * 440.0f * time_seconds);
        const float sample = 0.45f * fundamental + 0.15f * harmonic;

        /* The generated signal is within [-1, 1], so this cast is in range. */
        samples[i] = (int16_t)(sample * 32767.0f);
    }
}

static void bench_consume_features(
    const FinVoiceFeatureVector *features,
    double *checksum)
{
    /* A visible result prevents the benchmark from becoming a dead-code loop. */
    *checksum += (double)features->values[FIN_FEATURE_MFCC_0];
    *checksum += (double)features->values[FIN_FEATURE_F0] * 0.001;
    *checksum += (double)features->values[FIN_FEATURE_LOG_ENERGY] * 0.0001;
}

int main(int argc, char **argv)
{
    FinVoiceDspConfig config;
    FinVoiceDspContext context = {0};
    FinVoiceFeatureVector features = {0};
    int16_t pcm16[FIN_VOICE_FRAME_SAMPLES];
    void *workspace_memory = NULL;
    uint64_t *latencies_ns = NULL;
    size_t workspace_size;
    size_t iterations;
    size_t latency_samples;
    size_t i;
    uint64_t run_start_ns;
    uint64_t run_end_ns;
    uint64_t total_elapsed_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    long double latency_sum_ns = 0.0L;
    double checksum = 0.0;
    double average_throughput_frame_ns;
    double frames_per_second;
    double real_time_factor;
    FinVoiceDspStatus status;

    if (!bench_parse_iterations(argc, argv, &iterations)) {
        (void)fprintf(stderr,
            "Usage: %s [iterations: %zu..%zu]\n",
            argc > 0 ? argv[0] : "bench_dsp",
            BENCH_MIN_ITERATIONS,
            BENCH_MAX_ITERATIONS);
        return EXIT_FAILURE;
    }

    fin_voice_dsp_default_config(&config);
    workspace_size = fin_voice_dsp_workspace_size(&config);
    if (workspace_size == 0u) {
        (void)fprintf(stderr, "DSP returned an invalid workspace size.\n");
        return EXIT_FAILURE;
    }

    workspace_memory = malloc(workspace_size);
    if (workspace_memory == NULL) {
        (void)fprintf(stderr, "Could not allocate %zu-byte DSP workspace.\n",
            workspace_size);
        return EXIT_FAILURE;
    }

    status = fin_voice_dsp_init(
        &context, &config, workspace_memory, workspace_size);
    if (status != FIN_DSP_OK) {
        (void)fprintf(stderr, "DSP initialization failed with status %d.\n",
            (int)status);
        free(workspace_memory);
        return EXIT_FAILURE;
    }

    bench_fill_signal(pcm16);

    /* Warm up code and data before collecting steady-state hot-path results. */
    for (i = 0u; i < BENCH_WARMUP_ITERATIONS; ++i) {
        status = fin_voice_dsp_extract_features(
            &context, pcm16, FIN_VOICE_FRAME_SAMPLES, &features);
        if (status != FIN_DSP_OK) {
            (void)fprintf(stderr,
                "DSP warm-up failed at iteration %zu with status %d.\n",
                i, (int)status);
            free(workspace_memory);
            return EXIT_FAILURE;
        }
        bench_consume_features(&features, &checksum);
    }

    /* Allocate only the small sample array; never allocate in the timed loop. */
    latency_samples = iterations < BENCH_MAX_LATENCY_SAMPLES
        ? iterations
        : BENCH_MAX_LATENCY_SAMPLES;
    if (latency_samples > SIZE_MAX / sizeof(*latencies_ns)) {
        (void)fprintf(stderr, "Latency sample allocation size overflow.\n");
        free(workspace_memory);
        return EXIT_FAILURE;
    }

    latencies_ns = malloc(latency_samples * sizeof(*latencies_ns));
    if (latencies_ns == NULL) {
        (void)fprintf(stderr, "Could not allocate latency sample buffer.\n");
        free(workspace_memory);
        return EXIT_FAILURE;
    }

    /* Throughput pass: one timer pair for the entire loop to reduce overhead. */
    if (!bench_monotonic_now_ns(&run_start_ns)) {
        (void)fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed.\n");
        free(latencies_ns);
        free(workspace_memory);
        return EXIT_FAILURE;
    }

    for (i = 0u; i < iterations; ++i) {
        status = fin_voice_dsp_extract_features(
            &context, pcm16, FIN_VOICE_FRAME_SAMPLES, &features);
        if (status != FIN_DSP_OK) {
            (void)fprintf(stderr,
                "DSP throughput run failed at iteration %zu with status %d.\n",
                i, (int)status);
            free(latencies_ns);
            free(workspace_memory);
            return EXIT_FAILURE;
        }
        bench_consume_features(&features, &checksum);
    }

    if (!bench_monotonic_now_ns(&run_end_ns) || run_end_ns < run_start_ns) {
        (void)fprintf(stderr, "Could not measure elapsed benchmark time.\n");
        free(latencies_ns);
        free(workspace_memory);
        return EXIT_FAILURE;
    }
    total_elapsed_ns = run_end_ns - run_start_ns;
    if (total_elapsed_ns == 0u) {
        (void)fprintf(stderr, "Measured benchmark duration was zero.\n");
        free(latencies_ns);
        free(workspace_memory);
        return EXIT_FAILURE;
    }

    /* Latency pass: each call is timed separately for percentile statistics. */
    for (i = 0u; i < latency_samples; ++i) {
        uint64_t call_start_ns;
        uint64_t call_end_ns;

        if (!bench_monotonic_now_ns(&call_start_ns)) {
            (void)fprintf(stderr, "Clock read failed during latency sampling.\n");
            free(latencies_ns);
            free(workspace_memory);
            return EXIT_FAILURE;
        }

        status = fin_voice_dsp_extract_features(
            &context, pcm16, FIN_VOICE_FRAME_SAMPLES, &features);
        if (status != FIN_DSP_OK) {
            (void)fprintf(stderr,
                "DSP latency run failed at sample %zu with status %d.\n",
                i, (int)status);
            free(latencies_ns);
            free(workspace_memory);
            return EXIT_FAILURE;
        }

        if (!bench_monotonic_now_ns(&call_end_ns) || call_end_ns < call_start_ns) {
            (void)fprintf(stderr, "Invalid clock delta during latency sampling.\n");
            free(latencies_ns);
            free(workspace_memory);
            return EXIT_FAILURE;
        }

        latencies_ns[i] = call_end_ns - call_start_ns;
        latency_sum_ns += (long double)latencies_ns[i];
        bench_consume_features(&features, &checksum);
    }

    qsort(latencies_ns, latency_samples, sizeof(*latencies_ns), bench_compare_u64);
    min_latency_ns = latencies_ns[0];
    max_latency_ns = latencies_ns[latency_samples - 1u];
    average_throughput_frame_ns = (double)total_elapsed_ns / (double)iterations;
    frames_per_second = (double)iterations * 1.0e9 / (double)total_elapsed_ns;
    real_time_factor = average_throughput_frame_ns / (double)CANONICAL_FRAME_NS;

    (void)printf("FinTally Voice acoustic DSP benchmark\n");
#if defined(__clang__)
    (void)printf("Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    (void)printf("Compiler: GCC-compatible %s\n", __VERSION__);
#else
    (void)printf("Compiler: unknown\n");
#endif
    (void)printf("Frame: %u samples at %u Hz (%.2f ms)\n",
        (unsigned)FIN_VOICE_FRAME_SAMPLES,
        (unsigned)FIN_VOICE_SAMPLE_RATE_HZ,
        (double)CANONICAL_FRAME_NS / 1.0e6);
    (void)printf("Selected backend for quantized operations: %s\n",
        fin_voice_dsp_backend_name(fin_voice_dsp_active_backend(&context)));
    (void)printf("Note: the canonical FP32 acoustic feature path remains scalar.\n");
    (void)printf("Caller workspace bytes: %zu\n", workspace_size);
    (void)printf("Warm-up frames: %zu\n", BENCH_WARMUP_ITERATIONS);
    (void)printf("Throughput iterations: %zu\n", iterations);
    (void)printf("Total throughput-pass time: %.3f ms\n",
        (double)total_elapsed_ns / 1.0e6);
    (void)printf("Average throughput-pass latency: %.2f us/frame\n",
        average_throughput_frame_ns / 1.0e3);
    (void)printf("Throughput: %.2f frames/s\n", frames_per_second);
    (void)printf("Real-time factor: %.5f (below 1.0 means faster than real time)\n",
        real_time_factor);
    (void)printf("Per-call latency sample count: %zu\n", latency_samples);
    (void)printf("Latency min / p50 / p95 / p99 / max: %.2f / %.2f / %.2f / %.2f / %.2f us\n",
        (double)min_latency_ns / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 50u)] / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 95u)] / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 99u)] / 1.0e3,
        (double)max_latency_ns / 1.0e3);
    (void)printf("Latency-sample mean: %.2Lf us\n",
        (latency_sum_ns / (long double)latency_samples) / 1000.0L);
    (void)printf("Feature checksum: %.9g\n", checksum);
    (void)printf("Input pattern: repeated synthetic 220 Hz + 440 Hz tone (hot-cache baseline)\n");

    free(latencies_ns);
    free(workspace_memory);
    return EXIT_SUCCESS;
}
