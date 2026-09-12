# ==============================================================================
# FinOCR Benchmarks
# ==============================================================================
#
# Each benchmark source is an independent executable because each source owns
# its own main() entry point / benchmark registration.
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Dataset benchmark
# ------------------------------------------------------------------------------

add_executable(
    bench_dataset
    bench_dataset.cpp
)

target_link_libraries(
    bench_dataset
    PRIVATE
        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_dataset
)

# ------------------------------------------------------------------------------
# Dataset diagnostic benchmark
# ------------------------------------------------------------------------------

add_executable(
    bench_dataset_diagnostic
    bench_dataset_diagnostic.cpp
)

target_link_libraries(
    bench_dataset_diagnostic
    PRIVATE
        fin_ocr_native
)

fin_apply_cpu_optimizations(
    bench_dataset_diagnostic
)

# ------------------------------------------------------------------------------
# Engine benchmark
# ------------------------------------------------------------------------------

add_executable(
    bench_runner
    bench_engine.cpp
)

target_link_libraries(
    bench_runner
    PRIVATE
        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_runner
)

# ------------------------------------------------------------------------------
# MatrixMatcher benchmark
# ------------------------------------------------------------------------------

add_executable(
    bench_matrix_matcher
    bench_matrix_matcher.cpp
)

target_link_libraries(
    bench_matrix_matcher
    PRIVATE
        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_matrix_matcher
)

# ------------------------------------------------------------------------------
# TensorOps benchmark
# ------------------------------------------------------------------------------

add_executable(
    bench_tensor_ops
    bench_tensor_ops.cpp
)

target_link_libraries(
    bench_tensor_ops
    PRIVATE
        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_tensor_ops
)
