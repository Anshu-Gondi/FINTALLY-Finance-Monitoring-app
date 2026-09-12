# ==============================================================================
# Compiler / CPU configuration
# ==============================================================================

function(fin_apply_cpu_optimizations TARGET)

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "[FinOCR] Cannot configure compiler options: "
            "target '${TARGET}' does not exist."
        )

    endif()

    # ==========================================================================
    # MSVC
    # ==========================================================================

    if(MSVC)

        target_compile_options(
            "${TARGET}"
            PRIVATE
                /O2
                /fp:fast
                /W4
                /permissive-
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /WX
            )

        endif()

        # ----------------------------------------------------------------------
        # MSVC x86/x64 vector ISA selection
        # ----------------------------------------------------------------------

        if(FIN_ENABLE_AVX512)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /arch:AVX512
            )

        elseif(FIN_ENABLE_AVX2)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /arch:AVX2
            )

        endif()

    # ==========================================================================
    # GCC / Clang
    # ==========================================================================

    else()

        target_compile_options(
            "${TARGET}"
            PRIVATE
                -O3
                -ffast-math
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -Werror
            )

        endif()

        # ----------------------------------------------------------------------
        # CPU ISA selection
        #
        # Priority:
        #
        #   FIN_ENABLE_NATIVE
        #       >
        #   FIN_ENABLE_AVX512
        #       >
        #   FIN_ENABLE_AVX2
        #       >
        #   FIN_ENABLE_SSE42
        #       >
        #   compiler baseline
        #
        # IMPORTANT:
        #
        # tensor_ops.hpp contains direct SSE intrinsics, including:
        #
        #     _mm_min_epu16()
        #
        # which requires SSE4.1 support.
        #
        # SSE4.2 is therefore used as the project's explicit portable SIMD
        # baseline. It also guarantees that AVX/FMA/AVX512 are not accidentally
        # emitted in the baseline configuration.
        # ----------------------------------------------------------------------

        if(FIN_ENABLE_NATIVE)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -march=native
            )

        elseif(FIN_ENABLE_AVX512)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -mavx512f
                    -mavx512bw
                    -mavx2
                    -msse4.2
            )

        elseif(FIN_ENABLE_AVX2)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -mavx2
                    -msse4.2
            )

        elseif(FIN_ENABLE_SSE42)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -msse4.2

                    # ----------------------------------------------------------
                    # Explicitly prevent newer ISA instructions in the baseline
                    # build.
                    # ----------------------------------------------------------

                    -mno-avx
                    -mno-avx2
                    -mno-fma

                    -mno-avx512f
                    -mno-avx512bw
                    -mno-avx512vl
                    -mno-avx512dq
                    -mno-avx512cd
                    -mno-avx512ifma
                    -mno-avx512vbmi
                    -mno-avx512vbmi2
                    -mno-avx512vnni
                    -mno-avx512bitalg
                    -mno-avx512vpopcntdq
            )

        endif()

    endif()

endfunction()


# ==============================================================================
# General warning configuration
# ==============================================================================

function(fin_apply_common_compile_options TARGET)

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "[FinOCR] Cannot configure common compiler options: "
            "target '${TARGET}' does not exist."
        )

    endif()

    if(MSVC)

        target_compile_options(
            "${TARGET}"
            PRIVATE
                /W4
                /permissive-
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /WX
            )

        endif()

    else()

        target_compile_options(
            "${TARGET}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -Werror
            )

        endif()

    endif()

endfunction()


# ==============================================================================
# Project-wide target configuration
# ==============================================================================

function(fin_configure_native_target TARGET)

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "[FinOCR] Native target '${TARGET}' does not exist."
        )

    endif()

    set_target_properties(
        "${TARGET}"
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN OFF
    )

    fin_apply_cpu_optimizations(
        "${TARGET}"
    )

endfunction()
