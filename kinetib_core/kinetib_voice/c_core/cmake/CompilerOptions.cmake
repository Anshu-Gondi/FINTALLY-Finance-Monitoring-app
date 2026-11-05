include_guard(GLOBAL)

function(fin_voice_configure_c_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "fin_voice_configure_c_target: unknown target '${target}'."
        )
    endif()

    set_target_properties("${target}" PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
    )

    target_compile_features("${target}" PRIVATE c_std_23)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options("${target}" PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wformat=2
            -Wundef
            -Werror=implicit-function-declaration
            -Werror=return-type
        )

        if(FIN_VOICE_PEDANTIC_C23)
            target_compile_options("${target}" PRIVATE
                -pedantic-errors
            )
        endif()

        if(FIN_VOICE_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE
                -Werror
            )
        endif()

    elseif(MSVC)
        target_compile_options("${target}" PRIVATE
            /W4
        )

        if(FIN_VOICE_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE
                /WX
            )
        endif()

        if(FIN_VOICE_PEDANTIC_C23)
            message(FATAL_ERROR
                "The configured MSVC toolchain has not been validated "
                "for this project's strict C23 requirement. Use a "
                "verified C23-capable compiler."
            )
        endif()

    else()
        message(FATAL_ERROR
            "Unsupported C compiler: ${CMAKE_C_COMPILER_ID}. "
            "Configure a compiler with the required C23 support."
        )
    endif()
endfunction()
