#include "tensor_ops.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

// ============================================================================
// Scalar reference implementations
// ============================================================================

void scalar_binarize(
    std::uint8_t* data,
    std::size_t length
) noexcept {
    for (std::size_t i = 0; i < length; ++i) {
        data[i] = (data[i] > 128u) ? 255u : 0u;
    }
}

void scalar_contrast(
    std::uint8_t* data,
    std::size_t length
) noexcept {
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(data[i]) * 294u;

        const std::uint32_t result = value >> 8u;

        data[i] = static_cast<std::uint8_t>(
            result > 255u ? 255u : result
        );
    }
}

// ============================================================================
// Deterministic input generation
// ============================================================================

std::vector<std::uint8_t> make_input(std::size_t size) {
    std::vector<std::uint8_t> data(size);

    std::mt19937 rng(0xBADC0DEu);
    std::uniform_int_distribution<unsigned> dist(0, 255);

    for (auto& x : data) {
        x = static_cast<std::uint8_t>(dist(rng));
    }

    return data;
}

// ============================================================================
// Generic benchmark harness
// ============================================================================

template <typename Fn>
void benchmark_kernel(
    benchmark::State& state,
    Fn&& fn
) {
    const std::size_t size =
        static_cast<std::size_t>(state.range(0));

    // ------------------------------------------------------------
    // Prepare deterministic input outside the timed region.
    // ------------------------------------------------------------

    const auto input = make_input(size);

    std::vector<std::uint8_t> buffer(size);

    // ------------------------------------------------------------
    // Benchmark loop
    // ------------------------------------------------------------

    for (auto _ : state) {

        // Copying input is not part of the kernel measurement.
        state.PauseTiming();

        std::copy(
            input.begin(),
            input.end(),
            buffer.begin()
        );

        state.ResumeTiming();

        benchmark::DoNotOptimize(buffer.data());

        fn(buffer.data(), buffer.size());

        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }

    // ------------------------------------------------------------
    // Throughput
    // ------------------------------------------------------------

    state.SetBytesProcessed(
        static_cast<std::int64_t>(state.iterations()) *
        static_cast<std::int64_t>(size)
    );
}

// ============================================================================
// Binarization benchmarks
// ============================================================================

void BM_Binarize_SIMD(benchmark::State& state) {
    benchmark_kernel(
        state,
        [](std::uint8_t* data, std::size_t size) {
            fin::ops::binarize_simd(data, size);
        }
    );
}

void BM_Binarize_Scalar(benchmark::State& state) {
    benchmark_kernel(
        state,
        [](std::uint8_t* data, std::size_t size) {
            scalar_binarize(data, size);
        }
    );
}

// ============================================================================
// Contrast benchmarks
// ============================================================================

void BM_Contrast_SIMD(benchmark::State& state) {
    benchmark_kernel(
        state,
        [](std::uint8_t* data, std::size_t size) {
            fin::ops::contrast_boost_simd(data, size);
        }
    );
}

void BM_Contrast_Scalar(benchmark::State& state) {
    benchmark_kernel(
        state,
        [](std::uint8_t* data, std::size_t size) {
            scalar_contrast(data, size);
        }
    );
}

// ============================================================================
// Benchmark registration
// ============================================================================

BENCHMARK(BM_Binarize_SIMD)
    ->Arg(1 << 10)       // 1 KiB
    ->Arg(1 << 15)       // 32 KiB
    ->Arg(1 << 20)       // 1 MiB
    ->Arg(4 << 20)       // 4 MiB
    ->Arg(16 << 20)      // 16 MiB
    ->Arg(64 << 20)      // 64 MiB
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Binarize_Scalar)
    ->Arg(1 << 10)
    ->Arg(1 << 15)
    ->Arg(1 << 20)
    ->Arg(4 << 20)
    ->Arg(16 << 20)
    ->Arg(64 << 20)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Contrast_SIMD)
    ->Arg(1 << 10)
    ->Arg(1 << 15)
    ->Arg(1 << 20)
    ->Arg(4 << 20)
    ->Arg(16 << 20)
    ->Arg(64 << 20)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Contrast_Scalar)
    ->Arg(1 << 10)
    ->Arg(1 << 15)
    ->Arg(1 << 20)
    ->Arg(4 << 20)
    ->Arg(16 << 20)
    ->Arg(64 << 20)
    ->Unit(benchmark::kMillisecond);

} // namespace

// ============================================================================
// Google Benchmark entry point
//
// IMPORTANT:
// This MUST remain outside the anonymous namespace.
// ============================================================================

BENCHMARK_MAIN();
