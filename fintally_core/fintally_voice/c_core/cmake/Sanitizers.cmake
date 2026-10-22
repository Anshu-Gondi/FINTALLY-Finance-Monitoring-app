include_guard(GLOBAL)

# Normalize the current public options to the legacy switches consumed below.
# This prevents a modern ASan/UBSan or TSan build from silently remaining
# uninstrumented when only the FIN_VOICE_ENABLE_* option is provided.
if(FIN_VOICE_ENABLE_ASAN_UBSAN)
    set(FIN_VOICE_SANITIZE_ADDRESS ON)
    set(FIN_VOICE_SANITIZE_UNDEFINED ON)
endif()

if(FIN_VOICE_ENABLE_TSAN)
    set(FIN_VOICE_SANITIZE_THREAD ON)
endif()

if(FIN_VOICE_SANITIZE_ADDRESS AND FIN_VOICE_SANITIZE_THREAD)
    message(FATAL_ERROR
        "AddressSanitizer and ThreadSanitizer cannot be enabled together."
    )
endif()

if(
    (FIN_VOICE_SANITIZE_ADDRESS OR
     FIN_VOICE_SANITIZE_UNDEFINED OR
     FIN_VOICE_SANITIZE_THREAD)
    AND NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang"
)
    message(FATAL_ERROR
        "The selected sanitizer configuration requires a supported "
        "GCC- or Clang-family compiler."
    )
endif()

function(fin_voice_apply_sanitizers target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "fin_voice_apply_sanitizers: unknown target '${target}'."
        )
    endif()

    set(compile_flags)
    set(link_flags)

    if(FIN_VOICE_SANITIZE_ADDRESS)
        list(APPEND compile_flags
            -fsanitize=address
            -fno-omit-frame-pointer
        )
        list(APPEND link_flags
            -fsanitize=address
        )
    endif()

    if(FIN_VOICE_SANITIZE_UNDEFINED)
        list(APPEND compile_flags
            -fsanitize=undefined
        )
        list(APPEND link_flags
            -fsanitize=undefined
        )
    endif()

    if(FIN_VOICE_SANITIZE_THREAD)
        list(APPEND compile_flags
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        list(APPEND link_flags
            -fsanitize=thread
        )
    endif()

    if(compile_flags)
        target_compile_options(
            "${target}"
            PRIVATE
                ${compile_flags}
        )
    endif()

    if(link_flags)
        get_target_property(target_type "${target}" TYPE)

        if(target_type STREQUAL "STATIC_LIBRARY")
            # A static archive has no final linker step. Propagate the
            # required sanitizer link flags to CMake consumers.
            target_link_options(
                "${target}"
                INTERFACE
                    ${link_flags}
            )

        elseif(NOT target_type STREQUAL "OBJECT_LIBRARY")
            target_link_options(
                "${target}"
                PRIVATE
                    ${link_flags}
            )
        endif()
    endif()
endfunction()
