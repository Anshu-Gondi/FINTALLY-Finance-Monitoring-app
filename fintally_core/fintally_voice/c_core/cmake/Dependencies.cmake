include_guard(GLOBAL)

add_library(fintally_voice_dependencies INTERFACE)

add_library(
    FintallyVoice::Dependencies
    ALIAS fintally_voice_dependencies
)

# Acoustic DSP uses libm on common Unix/Linux toolchains.
if(UNIX AND NOT APPLE)
    target_link_libraries(
        fintally_voice_dependencies
        INTERFACE
            m
    )
endif()

# Threading remains optional. Enable this only when a C-core component
# actually needs a platform threading library.
if(FIN_VOICE_ENABLE_THREADS)
    find_package(Threads REQUIRED)

    target_link_libraries(
        fintally_voice_dependencies
        INTERFACE
            Threads::Threads
    )
endif()
