#define _POSIX_C_SOURCE 200809L

/*
 * FinTally Voice sequential multi-context cache-pressure benchmark.
 *
 * Each context owns an independent DSP state/workspace. Calls rotate through
 * contexts and four deterministic, different PCM frames per context. All
 * allocation, initialization, and input generation happen before timing.
 *
 * This is a single-threaded working-set/cache-pressure test, not a parallel
 * throughput test. Run several context counts with the same iteration count
 * and compare native latency plus Cachegrind simulated cache events.
 */

#include "acoustic_dsp.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_DEFAULT_CONTEXTS ((size_t)1u)
#define BENCH_MAX_CONTEXTS ((size_t)128u)
#define BENCH_DEFAULT_ITERATIONS ((size_t)20000u)
#define BENCH_MIN_ITERATIONS ((size_t)100u)
#define BENCH_MAX_ITERATIONS ((size_t)5000000u)
#define BENCH_WARMUP_MIN ((size_t)512u)
#define BENCH_INPUT_VARIANTS ((size_t)4u)
#define BENCH_MAX_LATENCY_SAMPLES ((size_t)2000u)
#define BENCH_NS_PER_SECOND UINT64_C(1000000000)
#define BENCH_FRAME_NS UINT64_C(20000000)
#define BENCH_TWO_PI_F 6.28318530717958647692f

typedef struct BenchContextSlot {
    FinVoiceDspContext context;
    void *workspace_memory;
    FinVoiceFeatureVector features;
    int16_t frames[BENCH_INPUT_VARIANTS][FIN_VOICE_FRAME_SAMPLES];
} BenchContextSlot;

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
    if (seconds > (UINT64_MAX - nanoseconds) / BENCH_NS_PER_SECOND) {
        return false;
    }
    *out_ns = seconds * BENCH_NS_PER_SECOND + nanoseconds;
    return true;
}

static bool bench_parse_size(
    const char *text,
    size_t minimum,
    size_t maximum,
    size_t *out_value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || out_value == NULL || *text == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0' ||
        parsed > (unsigned long long)SIZE_MAX ||
        parsed < (unsigned long long)minimum ||
        parsed > (unsigned long long)maximum) {
        return false;
    }
    *out_value = (size_t)parsed;
    return true;
}

static int bench_compare_u64(const void *lhs, const void *rhs)
{
    const uint64_t a = *(const uint64_t *)lhs;
    const uint64_t b = *(const uint64_t *)rhs;
    return (a > b) - (a < b);
}

static size_t bench_percentile_index(size_t count, size_t percentile)
{
    const size_t rank = (count * percentile + 99u) / 100u;
    return rank == 0u ? 0u : rank - 1u;
}

static void bench_fill_signal(
    int16_t output[FIN_VOICE_FRAME_SAMPLES],
    size_t context_index,
    size_t variant)
{
    const float sample_rate = (float)FIN_VOICE_SAMPLE_RATE_HZ;
    const float frequency = 90.0f +
        (float)((context_index * 29u + variant * 43u) % 300u);
    const float harmonic = frequency * 1.73f;
    const float phase = (float)(context_index + variant * 7u) * 0.137f;
    size_t i;

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        const float t = (float)i / sample_rate;
        const float fundamental = sinf(BENCH_TWO_PI_F * frequency * t + phase);
        const float overtone = sinf(BENCH_TWO_PI_F * harmonic * t + phase * 0.5f);
        const float modulator = 0.85f + 0.15f * cosf(
            BENCH_TWO_PI_F * (30.0f + (float)variant * 7.0f) * t);
        const float sample = (0.42f * fundamental + 0.11f * overtone) * modulator;
        output[i] = (int16_t)(sample * 32767.0f);
    }
}

static void bench_consume_features(
    const FinVoiceFeatureVector *features,
    long double *checksum)
{
    *checksum += (long double)features->values[FIN_FEATURE_MFCC_0];
    *checksum += (long double)features->values[FIN_FEATURE_F0] * 0.001L;
    *checksum += (long double)features->values[FIN_FEATURE_LOG_ENERGY] * 0.0001L;
    *checksum += (long double)features->values[FIN_FEATURE_SPECTRAL_CENTROID] * 0.000001L;
}

static void bench_free_slots(BenchContextSlot *slots, size_t context_count)
{
    size_t i;
    if (slots == NULL) {
        return;
    }
    for (i = 0u; i < context_count; ++i) {
        free(slots[i].workspace_memory);
        slots[i].workspace_memory = NULL;
    }
    free(slots);
}

static bool bench_process_one(
    BenchContextSlot *slots,
    size_t context_count,
    size_t iteration,
    long double *checksum)
{
    const size_t slot_index = iteration % context_count;
    const size_t variant = ((iteration / context_count) + slot_index) %
        BENCH_INPUT_VARIANTS;
    BenchContextSlot *slot = &slots[slot_index];
    const FinVoiceDspStatus status = fin_voice_dsp_extract_features(
        &slot->context,
        slot->frames[variant],
        FIN_VOICE_FRAME_SAMPLES,
        &slot->features);

    if (status != FIN_DSP_OK) {
        (void)fprintf(stderr,
            "DSP call failed: iteration=%zu context=%zu variant=%zu status=%d\n",
            iteration, slot_index, variant, (int)status);
        return false;
    }
    bench_consume_features(&slot->features, checksum);
    return true;
}

int main(int argc, char **argv)
{
    FinVoiceDspConfig config;
    BenchContextSlot *slots = NULL;
    uint64_t *latencies_ns = NULL;
    size_t context_count = BENCH_DEFAULT_CONTEXTS;
    size_t iterations = BENCH_DEFAULT_ITERATIONS;
    size_t workspace_size;
    size_t aggregate_workspace_bytes;
    size_t warmup_iterations;
    size_t latency_samples;
    size_t i;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t elapsed_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    long double latency_sum_ns = 0.0L;
    long double checksum = 0.0L;
    double average_frame_ns;
    double frames_per_second;
    double real_time_factor;
    FinVoiceDspStatus status;
    int exit_status = EXIT_FAILURE;

    if (argc > 3 ||
        (argc >= 2 && !bench_parse_size(argv[1], 1u, BENCH_MAX_CONTEXTS,
                                        &context_count)) ||
        (argc >= 3 && !bench_parse_size(argv[2], BENCH_MIN_ITERATIONS,
                                        BENCH_MAX_ITERATIONS, &iterations))) {
        (void)fprintf(stderr,
            "Usage: %s [contexts: 1..%zu] [iterations: %zu..%zu]\n",
            argc > 0 ? argv[0] : "fin_voice_bench_multicontext",
            BENCH_MAX_CONTEXTS, BENCH_MIN_ITERATIONS, BENCH_MAX_ITERATIONS);
        return EXIT_FAILURE;
    }

    fin_voice_dsp_default_config(&config);
    workspace_size = fin_voice_dsp_workspace_size(&config);
    if (workspace_size == 0u || context_count > SIZE_MAX / workspace_size) {
        (void)fprintf(stderr, "Invalid or overflowing DSP workspace size.\n");
        return EXIT_FAILURE;
    }
    aggregate_workspace_bytes = workspace_size * context_count;

    slots = calloc(context_count, sizeof(*slots));
    if (slots == NULL) {
        (void)fprintf(stderr, "Could not allocate %zu context slots.\n", context_count);
        goto cleanup;
    }

    /* Initialize every independent context and prepare varied input before timing. */
    for (i = 0u; i < context_count; ++i) {
        size_t variant;
        slots[i].workspace_memory = malloc(workspace_size);
        if (slots[i].workspace_memory == NULL) {
            (void)fprintf(stderr, "Workspace allocation failed for context %zu.\n", i);
            goto cleanup;
        }
        status = fin_voice_dsp_init(
            &slots[i].context, &config, slots[i].workspace_memory, workspace_size);
        if (status != FIN_DSP_OK) {
            (void)fprintf(stderr, "DSP initialization failed for context %zu: %d\n",
                i, (int)status);
            goto cleanup;
        }
        for (variant = 0u; variant < BENCH_INPUT_VARIANTS; ++variant) {
            bench_fill_signal(slots[i].frames[variant], i, variant);
        }
    }

    warmup_iterations = context_count > BENCH_WARMUP_MIN / BENCH_INPUT_VARIANTS
        ? context_count * BENCH_INPUT_VARIANTS
        : BENCH_WARMUP_MIN;
    if (warmup_iterations < context_count * BENCH_INPUT_VARIANTS) {
        warmup_iterations = context_count * BENCH_INPUT_VARIANTS;
    }
    for (i = 0u; i < warmup_iterations; ++i) {
        if (!bench_process_one(slots, context_count, i, &checksum)) {
            goto cleanup;
        }
    }

    latency_samples = iterations < BENCH_MAX_LATENCY_SAMPLES
        ? iterations : BENCH_MAX_LATENCY_SAMPLES;
    if (latency_samples > SIZE_MAX / sizeof(*latencies_ns)) {
        (void)fprintf(stderr, "Latency sample allocation overflow.\n");
        goto cleanup;
    }
    latencies_ns = malloc(latency_samples * sizeof(*latencies_ns));
    if (latencies_ns == NULL) {
        (void)fprintf(stderr, "Could not allocate latency sample buffer.\n");
        goto cleanup;
    }

    if (!bench_monotonic_now_ns(&start_ns)) {
        (void)fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed.\n");
        goto cleanup;
    }
    for (i = 0u; i < iterations; ++i) {
        if (!bench_process_one(slots, context_count, i, &checksum)) {
            goto cleanup;
        }
    }
    if (!bench_monotonic_now_ns(&end_ns) || end_ns < start_ns) {
        (void)fprintf(stderr, "Invalid elapsed-time measurement.\n");
        goto cleanup;
    }
    elapsed_ns = end_ns - start_ns;
    if (elapsed_ns == 0u) {
        (void)fprintf(stderr, "Measured duration was zero.\n");
        goto cleanup;
    }

    /* Separate sampled pass: do not mix per-call timer overhead into throughput. */
    for (i = 0u; i < latency_samples; ++i) {
        uint64_t call_start_ns;
        uint64_t call_end_ns;
        if (!bench_monotonic_now_ns(&call_start_ns) ||
            !bench_process_one(slots, context_count, i, &checksum) ||
            !bench_monotonic_now_ns(&call_end_ns) || call_end_ns < call_start_ns) {
            (void)fprintf(stderr, "Latency sampling failed at sample %zu.\n", i);
            goto cleanup;
        }
        latencies_ns[i] = call_end_ns - call_start_ns;
        latency_sum_ns += (long double)latencies_ns[i];
    }

    qsort(latencies_ns, latency_samples, sizeof(*latencies_ns), bench_compare_u64);
    min_latency_ns = latencies_ns[0];
    max_latency_ns = latencies_ns[latency_samples - 1u];
    average_frame_ns = (double)elapsed_ns / (double)iterations;
    frames_per_second = (double)iterations * 1.0e9 / (double)elapsed_ns;
    real_time_factor = average_frame_ns / (double)BENCH_FRAME_NS;

    (void)printf("FinTally Voice sequential multi-context cache-pressure benchmark\n");
#if defined(__clang__)
    (void)printf("Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    (void)printf("Compiler: GCC-compatible %s\n", __VERSION__);
#else
    (void)printf("Compiler: unknown\n");
#endif
    (void)printf("Contexts: %zu (single-threaded round-robin; not a concurrency test)\n",
        context_count);
    (void)printf("Frames per context: 4 deterministic synthetic variants\n");
    (void)printf("Frame: %u samples at %u Hz (20.00 ms)\n",
        (unsigned)FIN_VOICE_FRAME_SAMPLES, (unsigned)FIN_VOICE_SAMPLE_RATE_HZ);
    (void)printf("Selected backend (quantized operations): %s\n",
        fin_voice_dsp_backend_name(fin_voice_dsp_active_backend(&slots[0].context)));
    (void)printf("FP32 acoustic feature path: scalar\n");
    (void)printf("Workspace/context: %zu bytes\n", workspace_size);
    (void)printf("Aggregate workspace: %zu bytes (%.2f KiB)\n",
        aggregate_workspace_bytes, (double)aggregate_workspace_bytes / 1024.0);
    (void)printf("Context-slot array: %zu bytes\n", context_count * sizeof(*slots));
    (void)printf("Warm-up calls: %zu\n", warmup_iterations);
    (void)printf("Throughput iterations: %zu\n", iterations);
    (void)printf("Throughput-pass duration: %.3f ms\n", (double)elapsed_ns / 1.0e6);
    (void)printf("Average latency: %.2f us/frame\n", average_frame_ns / 1.0e3);
    (void)printf("Throughput: %.2f frames/s\n", frames_per_second);
    (void)printf("Real-time factor: %.5f (below 1.0 means faster than real time)\n",
        real_time_factor);
    (void)printf("Latency samples: %zu\n", latency_samples);
    (void)printf("Latency min / p50 / p95 / p99 / max: %.2f / %.2f / %.2f / %.2f / %.2f us\n",
        (double)min_latency_ns / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 50u)] / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 95u)] / 1.0e3,
        (double)latencies_ns[bench_percentile_index(latency_samples, 99u)] / 1.0e3,
        (double)max_latency_ns / 1.0e3);
    (void)printf("Latency-sample mean: %.2Lf us\n",
        (latency_sum_ns / (long double)latency_samples) / 1000.0L);
    (void)printf("Feature checksum: %.12Lg\n", checksum);
    (void)printf("Input policy: context-specific frequencies, phases, and four variants\n");
    exit_status = EXIT_SUCCESS;

cleanup:
    free(latencies_ns);
    bench_free_slots(slots, context_count);
    return exit_status;
}
