# Standalone microbenchmarks. Build optimized for performance measurements.
add_executable(fin_voice_bench_dsp bench_dsp.c)
target_link_libraries(fin_voice_bench_dsp PRIVATE FintallyVoice::core)
fin_voice_configure_c_target(fin_voice_bench_dsp)
fin_voice_apply_sanitizers(fin_voice_bench_dsp)

add_executable(fin_voice_bench_multicontext bench_dsp_multicontext.c)
target_link_libraries(fin_voice_bench_multicontext PRIVATE FintallyVoice::core)
fin_voice_configure_c_target(fin_voice_bench_multicontext)
fin_voice_apply_sanitizers(fin_voice_bench_multicontext)

# End-to-end C pipeline microbenchmark: DSP -> normalization/VQ -> event push/pop.
# It intentionally excludes ASR (none is integrated into this C pipeline).
add_executable(fin_voice_bench_pipeline bench_voice_pipeline.c)
target_link_libraries(fin_voice_bench_pipeline PRIVATE FintallyVoice::core)
fin_voice_configure_c_target(fin_voice_bench_pipeline)
fin_voice_apply_sanitizers(fin_voice_bench_pipeline)
