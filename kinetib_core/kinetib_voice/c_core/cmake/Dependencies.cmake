include_guard(GLOBAL)

add_library(kinetib_voice_dependencies INTERFACE)

add_library(
    KinetibVoice::Dependencies
    ALIAS kinetib_voice_dependencies
)

# Acoustic DSP uses libm on common Unix/Linux toolchains.
if(UNIX AND NOT APPLE)
    target_link_libraries(
        kinetib_voice_dependencies
        INTERFACE
            m
    )
endif()

# Threading remains optional. Enable this only when a C-core component
# actually needs a platform threading library.
if(FIN_VOICE_ENABLE_THREADS)
    find_package(Threads REQUIRED)

    target_link_libraries(
        kinetib_voice_dependencies
        INTERFACE
            Threads::Threads
    )
endif()
