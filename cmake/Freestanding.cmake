include_guard(GLOBAL)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "InferenceOS supports only the x86-64 target architecture.")
endif()

add_library(inferenceos_freestanding INTERFACE)
add_library(InferenceOS::Freestanding ALIAS inferenceos_freestanding)

target_compile_features(inferenceos_freestanding INTERFACE c_std_17)
target_compile_options(
    inferenceos_freestanding
    INTERFACE
        $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
        $<$<COMPILE_LANGUAGE:C>:-fno-builtin>
        $<$<COMPILE_LANGUAGE:C>:-fno-stack-protector>
        $<$<COMPILE_LANGUAGE:C>:-fno-pic>
        $<$<COMPILE_LANGUAGE:C>:-fno-pie>
        $<$<COMPILE_LANGUAGE:C>:-mno-red-zone>
        $<$<COMPILE_LANGUAGE:C>:-mno-mmx>
        $<$<COMPILE_LANGUAGE:C>:-mno-sse>
        $<$<COMPILE_LANGUAGE:C>:-mno-sse2>
        $<$<COMPILE_LANGUAGE:C>:-Wall>
        $<$<COMPILE_LANGUAGE:C>:-Wextra>
        $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
        $<$<AND:$<COMPILE_LANGUAGE:C>,$<BOOL:${INFERENCEOS_WARNINGS_AS_ERRORS}>>:-Werror>
)

if(INFERENCEOS_DIRECT_LLD_LINK)
    target_link_options(
        inferenceos_freestanding
        INTERFACE
            -static
            --no-pie
            --build-id=none
    )
else()
    target_link_options(
        inferenceos_freestanding
        INTERFACE
            -nostdlib
            -static
            -no-pie
            LINKER:--build-id=none
    )
endif()

function(inferenceos_configure_freestanding_target target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot configure unknown freestanding target '${target_name}'.")
    endif()

    target_link_libraries("${target_name}" PRIVATE InferenceOS::Freestanding)
    set_target_properties(
        "${target_name}"
        PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO
    )
endfunction()
