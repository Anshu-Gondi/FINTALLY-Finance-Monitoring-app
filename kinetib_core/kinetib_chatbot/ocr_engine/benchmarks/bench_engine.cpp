#include <benchmark/benchmark.h>
#include "ffi_bridge.h"
#include "tensor_ops.hpp"

#include <vector>
#include <algorithm>
#include <cstdint>

// ----------------------------------------------------------------------
// 1. SIMD Vector Instruction Microbenchmarks
// ----------------------------------------------------------------------

static void BM_BinarizeSIMD(benchmark::State& state) {
    size_t width = static_cast<size_t>(state.range(0));
    size_t height = static_cast<size_t>(state.range(0));
    size_t size = width * height * 3;

    uint8_t* data = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, size));
    std::fill_n(data, size, static_cast<uint8_t>(140));

    for (auto _ : state) {
        fin::ops::binarize_simd(data, size);
        benchmark::DoNotOptimize(data);
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(size));
    state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(width * height));

    fin::ops::aligned_free(data);
}

BENCHMARK(BM_BinarizeSIMD)->Arg(512)->Arg(1080)->Arg(2160);

// Benchmark AVX2 Multi-Color Chart HSV Channel Isolation
static void BM_MultiColorHsvIsolationAVX2(benchmark::State& state) {
    size_t width = static_cast<size_t>(state.range(0));
    size_t height = static_cast<size_t>(state.range(0));
    size_t num_pixels = width * height;
    size_t data_len = num_pixels * 3;

    uint8_t* rgb_in = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, data_len));
    uint8_t* hsv_out = static_cast<uint8_t*>(fin::ops::aligned_alloc(64, data_len));
    std::fill_n(rgb_in, data_len, static_cast<uint8_t>(180));

    for (auto _ : state) {
        // Execute chart color isolation pass
        FinOcrEngineContext* engine = fin_engine_create();
        if (engine) {
            FinProcessedBuffer* buf = fin_process_document_bytes(
                engine,
                rgb_in,
                data_len,
                FIN_INPUT_FIN_CHART,
                width,
                height,
                3
            );
            benchmark::DoNotOptimize(buf);
            fin_free_processed_buffer(buf);
            fin_engine_destroy(engine);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(data_len));
    state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(num_pixels));

    fin::ops::aligned_free(rgb_in);
    fin::ops::aligned_free(hsv_out);
}

BENCHMARK(BM_MultiColorHsvIsolationAVX2)->Arg(720)->Arg(1080)->Arg(2160);

// ----------------------------------------------------------------------
// 2. End-to-End Pipeline Execution Benchmarks
// ----------------------------------------------------------------------

static void BM_ExecuteVisionPipeline(benchmark::State& state) {
    FinOcrEngineContext* engine = fin_engine_create();

    size_t width = static_cast<size_t>(state.range(0));
    size_t height = static_cast<size_t>(state.range(1));
    size_t input_len = width * height * 3;
    std::vector<uint8_t> input_bytes(input_len, 200);

    for (auto _ : state) {
        FinProcessedBuffer* buf = fin_process_document_bytes(
            engine,
            input_bytes.data(),
            input_bytes.size(),
            FIN_INPUT_PDF_PAGE,
            width,
            height,
            3
        );
        benchmark::DoNotOptimize(buf);
        fin_free_processed_buffer(buf);
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(input_len));
    state.SetItemsProcessed(int64_t(state.iterations()));
    fin_engine_destroy(engine);
}

// Benchmark standard document rendering resolutions: HD, 1080p, 4K Page Scan
BENCHMARK(BM_ExecuteVisionPipeline)
    ->Args({1280, 720})
    ->Args({1920, 1080})
    ->Args({3840, 2160})
    ->Unit(benchmark::kMicrosecond);

// ----------------------------------------------------------------------
// 3. Thermal Guard Overhead & Throttled Mode Benchmarks
// ----------------------------------------------------------------------

static void BM_ReadThermalMetrics(benchmark::State& state) {
    for (auto _ : state) {
        FinThermalMetrics metrics = fin_get_thermal_metrics();
        benchmark::DoNotOptimize(metrics);
    }
}

BENCHMARK(BM_ReadThermalMetrics)->Unit(benchmark::kNanosecond);

static void BM_ExecuteVisionPipelineThrottled(benchmark::State& state) {
    FinOcrEngineContext* engine = fin_engine_create();

    // Force system into CRITICAL_HOT fallback mode
    fin_set_thermal_thresholds(-100.0f, -50.0f);

    size_t input_len = 1920 * 1080 * 3;
    std::vector<uint8_t> input_bytes(input_len, 200);

    for (auto _ : state) {
        FinProcessedBuffer* buf = fin_process_document_bytes(
            engine,
            input_bytes.data(),
            input_bytes.size(),
            FIN_INPUT_PDF_PAGE,
            1920,
            1080,
            3
        );
        benchmark::DoNotOptimize(buf);
        fin_free_processed_buffer(buf);
    }

    state.SetItemsProcessed(int64_t(state.iterations()));

    // Restore defaults
    fin_set_thermal_thresholds(75.0f, 87.0f);
    fin_engine_destroy(engine);
}

BENCHMARK(BM_ExecuteVisionPipelineThrottled)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
