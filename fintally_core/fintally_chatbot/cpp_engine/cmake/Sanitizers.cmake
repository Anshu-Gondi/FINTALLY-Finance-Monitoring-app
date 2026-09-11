# ==============================================================================
# Sanitizer support
# ==============================================================================

option(
    FIN_SANITIZER_ADDRESS
    "Enable AddressSanitizer"
    OFF
)

option(
    FIN_SANITIZER_UNDEFINED
    "Enable UndefinedBehaviorSanitizer"
    OFF
)

option(
    FIN_SANITIZER_LEAK
    "Enable LeakSanitizer"
    OFF
)


function(fin_apply_sanitizers TARGET)

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "[FinOCR] Cannot apply sanitizers: "
            "target '${TARGET}' does not exist."
        )

    endif()

    # ==========================================================================
    # MSVC
    # ==========================================================================

    if(MSVC)

        if(
            FIN_SANITIZER_ADDRESS AND
            NOT FIN_SANITIZER_UNDEFINED AND
            NOT FIN_SANITIZER_LEAK
        )

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /fsanitize=address
            )

            message(
                STATUS
                "[FinOCR] AddressSanitizer enabled for ${TARGET}"
            )

        elseif(
            FIN_SANITIZER_ADDRESS OR
            FIN_SANITIZER_UNDEFINED OR
            FIN_SANITIZER_LEAK
        )

            message(
                WARNING
                "[FinOCR] MSVC sanitizer support is limited. "
                "Only AddressSanitizer is configured."
            )

        endif()

        return()

    endif()

    # ==========================================================================
    # GCC / Clang
    # ==========================================================================

    set(SANITIZER_FLAGS "")

    if(FIN_SANITIZER_ADDRESS)

        string(
            APPEND
            SANITIZER_FLAGS
            "address,"
        )

    endif()

    if(FIN_SANITIZER_UNDEFINED)

        string(
            APPEND
            SANITIZER_FLAGS
            "undefined,"
        )

    endif()

    if(FIN_SANITIZER_LEAK)

        string(
            APPEND
            SANITIZER_FLAGS
            "leak,"
        )

    endif()

    if(SANITIZER_FLAGS STREQUAL "")

        return()

    endif()

    string(
        REGEX REPLACE
        ",$"
        ""
        SANITIZER_FLAGS
        "${SANITIZER_FLAGS}"
    )

    target_compile_options(
        "${TARGET}"
        PRIVATE
            "-fsanitize=${SANITIZER_FLAGS}"
            -fno-omit-frame-pointer
            -g
    )

    target_link_options(
        "${TARGET}"
        PRIVATE
            "-fsanitize=${SANITIZER_FLAGS}"
    )

    message(
        STATUS
        "[FinOCR] Sanitizers enabled for ${TARGET}: "
        "${SANITIZER_FLAGS}"
    )

endfunction()
