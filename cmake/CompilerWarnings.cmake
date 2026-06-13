# Centralizes the warning configuration so every target opts into the same
# strict diagnostics. Include this module, then call monitor_switch_set_warnings
# on each target.

# Treat warnings as errors when requested. Off by default so a clean configure
# and build succeed on compilers that emit extra, harmless diagnostics; CI and
# development can opt in with -DMONITOR_SWITCH_WARNINGS_AS_ERRORS=ON.
option(MONITOR_SWITCH_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors" OFF)

function(monitor_switch_set_warnings target)
    set(warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow)

    if(MONITOR_SWITCH_WARNINGS_AS_ERRORS)
        list(APPEND warnings -Werror)
    endif()

    # PRIVATE: warnings apply to this target's own sources, not to consumers.
    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
