#include <benchmark/benchmark.h>
#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include <vector>
#include <algorithm>
#include <cstdint>

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

static void BM_ExecuteVisionPipeline(benchmark::State& state) {
    FinOcrEngineContext* engine = fin_engine_create();

    size_t input_len = 1920 * 1080;
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
    fin_engine_destroy(engine);
}

BENCHMARK(BM_ExecuteVisionPipeline)->Unit(benchmark::kMicrosecond);

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

    size_t input_len = 1920 * 1080;
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
